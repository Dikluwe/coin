"""Run interleaved CoinGL/BGFX/wgpu animation measurements on one GPU.

The benchmark executables emit buffered per-frame CSV. This runner preserves
those samples, complete logs, commands, environment, binary hashes and summaries.
Image sampling is a separate invocation, never part of a timing campaign.
"""

import argparse
import csv
import hashlib
import json
import math
import os
from pathlib import Path
import re
import statistics
import subprocess


VARIANTS = {
    "coingl": ("coingl", "gl", "coin-gl", None),
    "bgfx-vulkan": ("bgfx", "bgfx", "bgfx-vulkan", "vulkan"),
    "bgfx-opengl": ("bgfx", "bgfx", "bgfx-opengl", "opengl"),
    "wgpu-vulkan": ("wgpu", "wgpu", "wgpu-vulkan", "vulkan"),
    "wgpu-opengl": ("wgpu", "wgpu", None, "gl"),
}
DEFAULT_CASES = "static,camera,transforms-1,transforms-10,transforms-100,materials-10,geometry-10"


def write_json(path, data):
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def sha256(path):
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def case_options(case):
    if case in ("static", "camera"):
        return case, 10
    mode, percent = case.rsplit("-", 1)
    if mode not in ("transforms", "materials", "geometry") or not 1 <= int(percent) <= 100:
        raise ValueError(f"Invalid animation case: {case}")
    return mode, int(percent)


def environment(build, variant, gpu):
    env = os.environ.copy()
    for key in list(env):
        if (key.startswith("COIN_BGFX_DISABLE_") or key.startswith("COIN_WGPU_DISABLE_")
                or key in {"COIN_RENDER_TRACE_PHASES", "COIN_WGPU_TRACE_PHASES", "COIN_DEBUG_GLGLUE",
                           "COIN_WGPU_GPU_TIMESTAMPS", "COIN_RENDER_DISABLE_CAPTURE_RESERVE",
                           "COIN_RENDER_DISABLE_CAPTURE_CAMERA_BASIS_REUSE",
                           "COIN_RENDER_DISABLE_COMPOSITION_BORROW",
                           "COIN_RENDER_DISABLE_GEOMETRY_INTERVAL_VALIDATION",
                           "COIN_RENDER_DISABLE_CAMERA_OVERLAY",
                           "COIN_RENDER_DISABLE_TRANSLATION_OVERLAY",
                           "COIN_RENDER_DISABLE_MATERIAL_OVERLAY",
                           "COIN_RENDER_DISABLE_CUBE_OVERLAY",
                           "COIN_RENDER_DISABLE_MATERIAL_INTERNING",
                           "COIN_RENDER_DISABLE_CUBE_TEMPLATE_CACHE",
                           "COIN_RENDER_DISABLE_OBJECT_PROOF_MEMOIZATION",
                           "COIN_RENDER_DISABLE_COMPOSITION_RANGE_MEMOIZATION",
                           "COIN_RENDER_DISABLE_COMBINE_VALIDATION_MEMO",
                           "COIN_BGFX_READBACK_PIPELINE_DEPTH", "COIN_BGFX_RENDERER",
                           "WGPU_BACKEND", "COIN_GLXGLUE_NO_PBUFFERS",
                           "COIN_GLXGLUE_NO_GLX13_PBUFFERS", "COIN_GLX_PIXMAP_DIRECT_RENDERING",
                           "__NV_PRIME_RENDER_OFFLOAD", "__GLX_VENDOR_LIBRARY_NAME",
                           "__EGL_VENDOR_LIBRARY_FILENAMES", "COIN_RENDER_RTT_GPU_DIRECT"}):
            env.pop(key, None)
    env["LD_LIBRARY_PATH"] = str(build / "lib")
    if gpu == "nvidia":
        env.update(__NV_PRIME_RENDER_OFFLOAD="1", __GLX_VENDOR_LIBRARY_NAME="nvidia",
                   VK_ICD_FILENAMES="/usr/share/vulkan/icd.d/nvidia_icd.json")
    else:
        env["VK_ICD_FILENAMES"] = "/usr/share/vulkan/icd.d/radeon_icd.json"
    if variant == "coingl":
        env["COIN_GLX_PIXMAP_DIRECT_RENDERING"] = "1"
        if gpu == "amd":
            env["COIN_GLXGLUE_NO_PBUFFERS"] = "1"
    else:
        env["COIN_BGFX_RENDERER" if variant.startswith("bgfx") else "WGPU_BACKEND"] = VARIANTS[variant][3]
    return env


def sample_stats(values):
    ordered = sorted(values)
    return {"median_ms": statistics.median(ordered),
            "p95_ms": ordered[math.ceil(len(ordered) * 0.95) - 1],
            "p99_ms": ordered[math.ceil(len(ordered) * 0.99) - 1],
            "max_ms": ordered[-1],
            "over_16_67": sum(v > 1000 / 60 for v in values),
            "over_33_33": sum(v > 1000 / 30 for v in values)}


def csv_stats(path, frames):
    with path.open(newline="") as stream:
        samples = [row for row in csv.DictReader(stream) if row["warmup"] in ("0", "false")]
    assert len(samples) == frames, (path, len(samples), frames)
    return {key: sample_stats([float(row.get(key, row.get("render_present_ms", "0"))
                                     if key == "render_ms" else row.get(key, "0")) for row in samples])
            for key in ("update_ms", "render_ms", "publication_ms", "total_ms")}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bgfx-build", type=Path, required=True)
    parser.add_argument("--coingl-build", type=Path,
                        help="Separate Coin/OpenGL control; defaults to --bgfx-build")
    parser.add_argument("--wgpu-build", type=Path, required=True)
    parser.add_argument("--scene", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--scope", choices=("offscreen", "window"), default="offscreen")
    parser.add_argument("--mode", choices=("measure", "verify"), default="measure")
    parser.add_argument("--gpu", choices=("nvidia", "amd"), default="nvidia")
    parser.add_argument("--variants", default="coingl,bgfx-vulkan,bgfx-opengl,wgpu-vulkan")
    parser.add_argument("--cases", default=DEFAULT_CASES)
    parser.add_argument("--warmup", type=int, default=60)
    parser.add_argument("--frames", type=int, default=600)
    parser.add_argument("--rounds", type=int, default=3)
    parser.add_argument("--size", type=int, default=1024)
    parser.add_argument("--timeout", type=int, default=1800)
    parser.add_argument("--resume", action="store_true")
    args = parser.parse_args()
    variants = args.variants.split(",")
    cases = args.cases.split(",")
    for variant in variants:
        if variant not in VARIANTS or (args.scope == "window" and not VARIANTS[variant][2]):
            parser.error(f"Unsupported variant/scope: {variant}/{args.scope}")
    for case in cases:
        case_options(case)
    if args.mode == "verify" and args.scope != "offscreen":
        parser.error("Image validation uses the offscreen RGB contract")
    if not args.scene.is_file() or args.frames < 1 or args.warmup < 0 or args.rounds < 1:
        parser.error("Missing scene or invalid sample counts")
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    for subdir in ("logs", "samples", "images"):
        (out / subdir).mkdir(exist_ok=True)
    builds = {"bgfx": args.bgfx_build.resolve(), "wgpu": args.wgpu_build.resolve(),
              "coingl": (args.coingl_build or args.bgfx_build).resolve()}
    binary_name = "coin_render_gl_benchmark" if args.scope == "offscreen" else "coin_render_window_benchmark"
    manifest = {"parameters": {k: str(v) if isinstance(v, Path) else v for k, v in vars(args).items()},
                "scene_sha256": sha256(args.scene), "binary_hashes": {}, "commands": []}
    for variant in variants:
        build = builds[VARIANTS[variant][0]]
        for path in (build / "bin" / binary_name, build / "lib/libCoinRender.so", build / "lib/libCoin.so.80"):
            manifest["binary_hashes"][str(path)] = sha256(path)
    root = Path(__file__).resolve().parents[2]
    revision = subprocess.run(["git", "rev-parse", "HEAD"], cwd=root, capture_output=True, text=True)
    manifest["source_revision"] = revision.stdout.strip()
    result_path = out / "results.json"
    if not args.resume and result_path.exists():
        parser.error("Output already has results; use a new directory or --resume")
    rows = json.loads(result_path.read_text()) if args.resume and result_path.exists() else []
    if args.resume and (out / "manifest.json").exists():
        old = json.loads((out / "manifest.json").read_text())
        assert old["binary_hashes"] == manifest["binary_hashes"], "Cannot mix different binaries"
        assert old["scene_sha256"] == manifest["scene_sha256"], "Cannot mix different scenes"
        identity_keys = set(manifest["parameters"]) - {"resume", "timeout"}
        mismatches = [key for key in identity_keys
                      if old["parameters"].get(key) != manifest["parameters"][key]]
        assert not mismatches, f"Cannot resume a different campaign: {mismatches}"
        manifest["commands"] = old["commands"]
    elif rows:
        parser.error("Cannot resume results without their original manifest")
    rounds = 1 if args.mode == "verify" else args.rounds
    for round_index in range(rounds):
        case_order = cases[round_index % len(cases):] + cases[:round_index % len(cases)]
        if round_index % 2:
            case_order.reverse()
        for case_index, case in enumerate(case_order):
            offset = (round_index + case_index) % len(variants)
            variant_order = variants[offset:] + variants[:offset]
            if round_index % 2:
                variant_order.reverse()
            for variant in variant_order:
                if any(r["case"] == case and r["variant"] == variant and r["round"] == round_index + 1 for r in rows):
                    continue
                build_key, offscreen_backend, window_backend, _ = VARIANTS[variant]
                build = builds[build_key]
                animation, percent = case_options(case)
                stem = f"{case}-{variant}-{round_index + 1}"
                sample_path = out / "samples" / f"{stem}.csv"
                command = [str(build / "bin" / binary_name), "--backend",
                           offscreen_backend if args.scope == "offscreen" else window_backend,
                           "--scene", str(args.scene.resolve()), "--animation", animation,
                           "--animated-percent", str(percent), "--transparency", "object",
                           "--warmup", str(0 if args.mode == "verify" else args.warmup),
                           "--frames", str(7 if args.mode == "verify" else args.frames),
                           "--samples-output", str(sample_path)]
                command += ["--size", str(args.size)] if args.scope == "offscreen" else ["--width", str(args.size), "--height", str(args.size)]
                if args.mode == "verify":
                    command += ["--animation-step", "100", "--capture-frames", "0,1,2,3,4,5,6",
                                "--capture-prefix", str(out / "images" / stem)]
                env = environment(build, variant, args.gpu)
                saved_env = {key: env[key] for key in ("LD_LIBRARY_PATH", "VK_ICD_FILENAMES",
                            "__NV_PRIME_RENDER_OFFLOAD", "__GLX_VENDOR_LIBRARY_NAME",
                            "COIN_BGFX_RENDERER", "WGPU_BACKEND", "COIN_GLX_PIXMAP_DIRECT_RENDERING",
                            "COIN_GLXGLUE_NO_PBUFFERS") if key in env}
                manifest["commands"].append({"stem": stem, "command": command, "environment": saved_env})
                write_json(out / "manifest.json", manifest)
                print(f"START {args.scope}/{stem}", flush=True)
                process = subprocess.run(["/usr/bin/time", "-f", "benchmark_peak_rss_kib=%M", *command],
                                         env=env, capture_output=True, text=True, timeout=args.timeout)
                log = process.stdout + process.stderr
                (out / "logs" / f"{stem}.log").write_text(log, encoding="utf-8")
                if process.returncode:
                    raise RuntimeError(f"{stem}: return={process.returncode}; {log[-2000:]}")
                if variant != "coingl" and args.gpu == "nvidia":
                    assert "NVIDIA" in log, f"Unexpected adapter for {stem}"
                stats = csv_stats(sample_path, 7 if args.mode == "verify" else args.frames)
                row = {"case": case, "variant": variant, "round": round_index + 1,
                       "scope": args.scope, "gpu": args.gpu, "stats": stats,
                       "peak_rss_kib": int(re.search(r"benchmark_peak_rss_kib=(\d+)", log)[1]),
                       "log": str(Path("logs") / f"{stem}.log"),
                       "samples": str(Path("samples") / f"{stem}.csv")}
                first = re.search(r"^\S+_first_frame_ms=([\d.eE+-]+)", log, re.M)
                if first:
                    row["first_ms"] = float(first[1])
                detail = re.search(r"^(?:\S+_first_detail|window_first_frame_detail) (.+)$", log, re.M)
                if detail:
                    fields = dict(re.findall(r"(\w+)=([\d.eE+-]+)(?:\s|$)", detail[1]))
                    if args.scope == "window":
                        row["first_ms"] = float(fields["render_present_ms"])
                    total_key = "total_ms" if args.scope == "window" else "total_with_update_ms"
                    row["first_total_ms"] = float(fields[total_key])
                    row["result_since_main_ms"] = float(fields["result_since_main_ms"])
                checksum = re.search(r"^(?:gl_)?rgba_fnv64=(\S+)", log, re.M)
                if checksum:
                    row["final_rgba_checksum"] = checksum[1]
                throughput = re.search(r"^\S+_throughput frames=\d+ total_ms=([\d.eE+-]+) fps=([\d.eE+-]+)", log, re.M)
                if throughput:
                    row["wall_total_ms"], row["throughput_fps"] = map(float, throughput.groups())
                elif args.scope == "window":
                    report = next(line for line in log.splitlines() if line.startswith("window_benchmark "))
                    fields = dict(re.findall(r"(\w+)=([^\s]+)", report))
                    row["wall_total_ms"] = float(fields["total_ms"])
                    row["throughput_fps"] = float(fields["throughput_fps"])
                    row["throughput_scope"] = fields["throughput_scope"]
                    row["final_gpu_drain_ms"] = float(fields["final_gpu_drain_ms"])
                row["animation_metadata"] = [line for line in log.splitlines() if line.startswith("animation") or "selection_digest=" in line]
                rows.append(row)
                write_json(result_path, rows)
                total = stats["total_ms"]
                print(f"DONE {stem}: total median={total['median_ms']:.3f} p95={total['p95_ms']:.3f} p99={total['p99_ms']:.3f} update={stats['update_ms']['median_ms']:.3f} ms", flush=True)
    summary = []
    for case in cases:
        for variant in variants:
            group = [r for r in rows if r["case"] == case and r["variant"] == variant]
            assert len(group) == rounds
            item = {"case": case, "variant": variant, "processes": len(group),
                    "scope": args.scope, "gpu": args.gpu, "stats": {}}
            for metric in ("update_ms", "render_ms", "publication_ms", "total_ms"):
                item["stats"][metric] = {key: statistics.median(r["stats"][metric][key] for r in group)
                                        for key in group[0]["stats"][metric]}
            item["peak_rss_kib"] = statistics.median(r["peak_rss_kib"] for r in group)
            for key in ("first_ms", "first_total_ms", "result_since_main_ms", "wall_total_ms", "throughput_fps", "final_gpu_drain_ms"):
                if all(key in r for r in group):
                    item[key] = statistics.median(r[key] for r in group)
            if all("throughput_scope" in r for r in group):
                assert len({r["throughput_scope"] for r in group}) == 1
                item["throughput_scope"] = group[0]["throughput_scope"]
            summary.append(item)
    write_json(out / "medians.json", summary)
    print(f"Completed {len(rows)} new processes; raw CSV and logs preserved", flush=True)


if __name__ == "__main__":
    main()
