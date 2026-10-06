#!/usr/bin/env python3
"""Focused same-build BGFX/Vulkan geometry-10 probe, without phase tracing.

Only main() launches benchmarks. Three alternating on/off pairs use five
warmups and fifteen measured frames: six processes, ninety measured frames,
thirty warmups. CSV warmup flags select measured rows. This supplemental probe
preserves the principal before/after campaign, including its observed increase.
"""

import argparse
import csv
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import re
import statistics
import subprocess
import sys

sys.dont_write_bytecode = True
SOURCE = "96e5ed80fa3ab3c9016cf10e6bbfc9b638335a33"
OPTOUT = "COIN_RENDER_DISABLE_GEOMETRY_INTERVAL_VALIDATION"
EXPECTED = {"processes": 6, "measured_frames": 90, "warmup_frames": 30}
METRICS = ("update_ms", "render_ms", "publication_ms", "total_ms")


def sha(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def sample_statistics(values):
    ordered = sorted(values)
    return {"median_ms": statistics.median(ordered), "min_ms": ordered[0], "max_ms": ordered[-1],
            "p95_ms": ordered[math.ceil(len(ordered) * .95) - 1],
            "p99_ms": ordered[math.ceil(len(ordered) * .99) - 1]}


def samples(path):
    with path.open(newline="") as stream:
        rows = list(csv.DictReader(stream))
    measured, warmup = [], []
    for index, row in enumerate(rows):
        flag = row.get("warmup", "").strip().lower()
        if flag in ("0", "false"):
            measured.append(index)
        elif flag in ("1", "true"):
            warmup.append(index)
        else:
            raise ValueError("Unknown CSV warmup flag in row " + str(index))
    if measured != list(range(5, 20)) or warmup != list(range(5)) or [row.get("frame_index") for row in rows] != [str(index) for index in range(-5, 15)]:
        raise ValueError("CSV is not exactly five warmups and fifteen measured frames")
    stats = {}
    for metric in METRICS:
        values = [float(rows[index][metric]) for index in measured]
        if not all(math.isfinite(value) and value >= 0 for value in values):
            raise ValueError("Nonfinite/negative measured CSV metric: " + metric)
        stats[metric] = sample_statistics(values)
    return {"rows": rows, "measured_row_indices": measured, "warmup_row_indices": warmup,
            "measured_frames": len(measured), "warmup_frames": len(warmup), "stats": stats,
            "csv_input": {"path": str(path.resolve()), "bytes": path.stat().st_size, "sha256": sha(path)}}


def difference(off, on):
    return {"off": off, "on": on, "delta_ms": on - off,
            "change_percent": (on / off - 1) * 100 if off else None}


def summarize(records):
    successful = [record for record in records if record.get("exit_code") == 0 and not record.get("timed_out") and "samples" in record]
    pairs = []
    for round_number in range(1, 4):
        pair = {record["mode"]: record for record in successful if record["round"] == round_number}
        if set(pair) != {"on", "off"}:
            continue
        on, off = pair["on"], pair["off"]
        normalized = []
        for record in (on, off):
            command = list(record["command"])
            command[command.index("--samples-output") + 1] = "<samples>"
            environment = {key: value for key, value in record["environment"].items() if key != OPTOUT}
            normalized.append((command, environment, record["source_content_revision"]))
        comparable = normalized[0] == normalized[1] and on["trace_environment_absent"] and off["trace_environment_absent"] and not on["phase_trace_seen"] and not off["phase_trace_seen"]
        pairs.append({"round": round_number, "on": on["stem"], "off": off["stem"], "complete_and_comparable": comparable,
                      "stats": {metric: {statistic: difference(off["samples"]["stats"][metric][statistic], on["samples"]["stats"][metric][statistic])
                                            for statistic in on["samples"]["stats"][metric]} for metric in METRICS}})
    groups = {}
    for mode in ("off", "on"):
        selected = sorted((record for record in successful if record["mode"] == mode), key=lambda record: record["round"])
        if not selected:
            continue
        groups[mode] = {"processes": len(selected), "stems": [record["stem"] for record in selected],
                        "rounds": [record["round"] for record in selected], "stats": {}, "process_median_values_ms": {}, "process_median_range_ms": {}}
        for metric in METRICS:
            values = [record["samples"]["stats"][metric]["median_ms"] for record in selected]
            groups[mode]["stats"][metric] = {statistic: statistics.median(record["samples"]["stats"][metric][statistic] for record in selected) for statistic in selected[0]["samples"]["stats"][metric]}
            groups[mode]["process_median_values_ms"][metric] = values
            groups[mode]["process_median_range_ms"][metric] = {"minimum": min(values), "maximum": max(values)}
    comparisons = {metric: difference(groups["off"]["stats"][metric]["median_ms"], groups["on"]["stats"][metric]["median_ms"])
                   for metric in METRICS} if set(groups) == {"off", "on"} else {}
    return {"pairs": pairs, "groups": groups, "comparisons": comparisons,
            "complete_and_comparable": len(successful) == 6 and len(pairs) == 3 and all(pair["complete_and_comparable"] for pair in pairs),
            "summary_method": "median of three per-process measured medians; never pooled frames",
            "difference": "on minus off; positive values mean slower",
            "limitation": "Supplemental same-build probe without trace; does not replace or remove the principal matched increase or identify its cause"}


def plan(args):
    commands = []
    for round_number in range(1, 4):
        for mode in (("on", "off") if round_number % 2 else ("off", "on")):
            stem = f"bgfx-vulkan-geometry-10-{mode}-{round_number}"
            commands.append({"stem": stem, "variant": "bgfx-vulkan", "case": "geometry-10", "mode": mode,
                             "round": round_number, "source_content_revision": args.source_content_revision,
                             "command": [str(args.build / "bin/coin_render_gl_benchmark"), "--backend", "bgfx", "--scene", str(args.scene),
                                 "--animation", "geometry", "--animated-percent", "10", "--transparency", "object", "--size", "1024",
                                 "--warmup", "5", "--frames", "15", "--samples-output", str(args.output / (stem + ".csv"))]})
    return commands


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repository", type=Path, default=Path("/tmp/coin-render-first-frame"))
    parser.add_argument("--build", type=Path, default=Path("/tmp/coin-render-first-frame-bgfx"))
    parser.add_argument("--scene", type=Path, default=Path("/tmp/coin-render-city-40000.iv"))
    parser.add_argument("--output", type=Path, default=Path("/tmp/coin-render-geometry-overlay-regression-probe"))
    parser.add_argument("--source-content-revision", default=SOURCE)
    parser.add_argument("--timeout", type=float, default=180)
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    if re.fullmatch("[0-9a-f]{40}", args.source_content_revision) is None or args.timeout <= 0:
        parser.error("Source must be a full SHA and timeout positive")
    for name in ("repository", "build", "scene", "output"):
        setattr(args, name, getattr(args, name).resolve())
    commands = plan(args)
    if args.dry_run:
        print(json.dumps({"expected_unique_counts": EXPECTED, "commands": commands,
                          "optout": OPTOUT, "trace": False, "ordering": "on/off; off/on; on/off"}, indent=2))
        return
    args.output.mkdir(parents=True, exist_ok=False)
    helper_path = args.repository / "scripts/coinrender/run_animation_benchmark.py"
    spec = importlib.util.spec_from_file_location("geometry_probe_environment", helper_path)
    runner = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(runner)
    hashes = {str(args.build / path): sha(args.build / path) for path in ("bin/coin_render_gl_benchmark", "lib/libCoinRender.so", "lib/libCoin.so.80")}
    metadata = {"source_content_revision": args.source_content_revision, "scene_sha256": sha(args.scene), "binary_hashes": hashes,
                "commands": [], "expected_unique_counts": EXPECTED,
                "parameters": {"variant": "bgfx-vulkan", "case": "geometry-10", "rounds": 3, "warmup": 5, "frames": 15, "size": 1024,
                    "gpu": "nvidia", "transparency": "object", "optout": OPTOUT, "trace": False},
                "invocation": sys.argv, "runner_script": str(Path(__file__).resolve()), "runner_sha256": sha(Path(__file__)),
                "parser_sha256": sha(Path(__file__)), "environment_helper": str(helper_path), "environment_helper_sha256": sha(helper_path)}
    def save():
        metadata["summary"] = summarize(metadata["commands"])
        (args.output / "commands.json").write_text(json.dumps(metadata, indent=2, allow_nan=False) + "\n")
    save()
    for record in commands:
        environment = runner.environment(args.build, "bgfx-vulkan", "nvidia")
        for key in (OPTOUT, "COIN_RENDER_TRACE_PHASES", "COIN_WGPU_TRACE_PHASES"):
            environment.pop(key, None)
        if record["mode"] == "off":
            environment[OPTOUT] = "1"
        record.update(environment={key: value for key, value in environment.items() if key.startswith(("COIN_", "WGPU_", "__NV", "__GLX", "VK_", "LD_LIBRARY"))},
                      trace_environment_absent=all(key not in environment for key in ("COIN_RENDER_TRACE_PHASES", "COIN_WGPU_TRACE_PHASES")),
                      timeout_seconds=args.timeout, timed_command=["/usr/bin/time", "-f", "benchmark_peak_rss_kib=%M", *record["command"]])
        metadata["commands"].append(record)
        save()
        print("START", record["stem"], flush=True)
        try:
            result = subprocess.run(record["timed_command"], cwd=args.repository, env=environment, capture_output=True, text=True, timeout=args.timeout)
            stdout, stderr = result.stdout, result.stderr
            record.update(exit_code=result.returncode, timed_out=False)
        except subprocess.TimeoutExpired as error:
            def text(value):
                return value.decode("utf-8", errors="replace") if isinstance(value, bytes) else (value or "")
            stdout, stderr = text(error.stdout), text(error.stderr)
            record.update(exit_code=None, timed_out=True)
        log = stdout + stderr
        for suffix, value in ((".stdout", stdout), (".stderr", stderr), (".log", log)):
            (args.output / (record["stem"] + suffix)).write_text(value)
        record.update(adapter_verified_nvidia="NVIDIA" in log, phase_trace_seen="COIN_RENDER_PHASE " in log,
                      log_input={"path": str(args.output / (record["stem"] + ".log")), "sha256": sha(args.output / (record["stem"] + ".log"))})
        save()
        if record["exit_code"] != 0 or record["timed_out"] or not record["adapter_verified_nvidia"] or record["phase_trace_seen"]:
            raise RuntimeError(record["stem"] + " failed/timed out/unverified adapter/unexpected trace; raw outputs preserved")
        record["samples"] = samples(args.output / (record["stem"] + ".csv"))
        save()
        print("END", record["stem"], "total_median_ms=" + str(record["samples"]["stats"]["total_ms"]["median_ms"]), flush=True)
    metadata["binary_hashes_post_campaign"] = {path: sha(Path(path)) for path in hashes}
    metadata["binaries_unchanged"] = metadata["binary_hashes_post_campaign"] == hashes
    metadata["completed_unique_counts"] = {"processes": len(metadata["commands"]), "measured_frames": sum(record["samples"]["measured_frames"] for record in metadata["commands"]),
                                           "warmup_frames": sum(record["samples"]["warmup_frames"] for record in metadata["commands"])}
    metadata["complete_and_comparable"] = metadata["binaries_unchanged"] and metadata["completed_unique_counts"] == EXPECTED and summarize(metadata["commands"])["complete_and_comparable"]
    save()
    (args.output / "summary.json").write_text(json.dumps(metadata["summary"], indent=2, allow_nan=False) + "\n")
    print(json.dumps(metadata["summary"]["comparisons"], indent=2), flush=True)
    if not metadata["complete_and_comparable"]:
        raise RuntimeError("Probe incomplete/not comparable or binary hashes changed")


if __name__ == "__main__":
    main()
