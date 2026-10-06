#!/usr/bin/env python3
"""Archive completed CoinRender campaigns and recompute report statistics.

No benchmark, build or GPU command is run. Runner JSON is copied verbatim.
PPM images stay in their original location; hashes, digests and RGB metrics are
archived instead. All aggregate frame counts/maxima come directly from CSV.
"""

import argparse
import csv
from datetime import datetime, timezone
import hashlib
import json
import math
from pathlib import Path
import re
import shutil
import statistics


LABELS = {"coingl": "CoinGL", "bgfx-vulkan": "BGFX / Vulkan",
          "bgfx-opengl": "BGFX / OpenGL", "wgpu-vulkan": "wgpu / Vulkan"}
BUDGET_60 = 1000 / 60
BUDGET_30 = 1000 / 30
ARCHIVE_SUFFIXES = {".json", ".csv", ".log", ".txt"}


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


def read_json(path):
    return json.loads(path.read_text(encoding="utf-8"))


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def require(condition, message):
    if not condition:
        raise ValueError(message)


def inside(directory, relative):
    path = (directory / relative).resolve()
    require(path.is_relative_to(directory.resolve()), f"Path outside campaign: {relative}")
    require(path.is_file(), f"Missing campaign file: {path}")
    return path


def finite_number(value, context):
    number = float(value)
    require(math.isfinite(number) and number >= 0, f"Invalid time/size {context}: {value}")
    return number


def statistics_for(values):
    require(bool(values), "No measured samples")
    ordered = sorted(values)
    count = len(ordered)
    return {"median_ms": statistics.median(ordered),
            "p95_ms": ordered[math.ceil(count * .95) - 1],
            "p99_ms": ordered[math.ceil(count * .99) - 1],
            "max_ms": ordered[-1], "over_16_67": sum(v > BUDGET_60 for v in values),
            "over_33_33": sum(v > BUDGET_30 for v in values)}


def boolean(value):
    require(value.lower() in {"0", "1", "true", "false"}, f"Invalid warmup flag: {value}")
    return value.lower() in {"1", "true"}


def parse_metadata(log):
    selected = [line for line in log.splitlines()
                if line.startswith("animation") or "selection_digest=" in line]
    fields = {}
    for line in selected:
        fields.update(dict(re.findall(r"(\w+)=([^\s]+)", line)))
    final_state = re.findall(r"^.*_animation_final .*state_fnv64=(\S+)", log, re.M)
    gpu_lines = [line for line in log.splitlines() if
                 re.search(r"(^adapter=|GL_VENDOR|GL_RENDERER|GL_VERSION|"
                           r"gl_vendor|gl_renderer|gl_version|driver|vendor_id=|device_id=)", line)]
    return {"lines": selected, "fields": fields,
            "final_state_fnv64": final_state[-1] if final_state else fields.get("final_state_digest"),
            "gpu_identity_lines": gpu_lines}


def load_process(directory, row, parameters):
    sample_path = inside(directory, row["samples"])
    log_path = inside(directory, row["log"])
    log = log_path.read_text(encoding="utf-8")
    metadata = parse_metadata(log)
    require(metadata["final_state_fnv64"] is not None, f"Missing final scene-state digest: {log_path}")
    with sample_path.open(newline="", encoding="utf-8") as stream:
        reader = csv.DictReader(stream)
        columns = set(reader.fieldnames or [])
        rows = list(reader)
    required = {"frame_index", "logical_frame", "warmup", "update_ms", "total_ms"}
    require(required <= columns, f"Missing sample columns: {sample_path}: {required - columns}")
    scope = row.get("scope", parameters["scope"])
    render_column = "render_present_ms" if scope == "window" else "render_ms"
    require(render_column in columns, f"Missing {render_column}: {sample_path}")
    mode = parameters.get("mode", "measure")
    frames = 7 if mode == "verify" else int(parameters["frames"])
    warmup = 0 if mode == "verify" else int(parameters["warmup"])
    step = int(metadata["fields"].get("frame_step", metadata["fields"].get("animation_step", 100 if mode == "verify" else 1)))
    require(len(rows) == frames + warmup, f"Incomplete CSV: {sample_path}: {len(rows)}")
    require([int(x["frame_index"]) for x in rows] == list(range(-warmup, frames)),
            f"Disordered frame indices: {sample_path}")
    require([int(x["logical_frame"]) for x in rows] == [i * step for i in range(-warmup, frames)],
            f"Disordered logical frame sequence: {sample_path}")
    require([boolean(x["warmup"]) for x in rows] == [True] * warmup + [False] * frames,
            f"Warmup/measurement flags disagree: {sample_path}")
    metric_columns = {"update_ms": "update_ms", "render_ms": render_column, "total_ms": "total_ms"}
    if scope == "offscreen":
        require("publication_ms" in columns, f"Missing publication_ms: {sample_path}")
        metric_columns["publication_ms"] = "publication_ms"
    if "event_ms" in columns:
        metric_columns["event_ms"] = "event_ms"
    metrics = {key: [] for key in metric_columns}
    for index, sample in enumerate(rows):
        times = {key: finite_number(sample[column], f"{sample_path}:{index + 2}/{column}")
                 for key, column in metric_columns.items()}
        components = times["update_ms"] + times["render_ms"] + times.get("publication_ms", 0)
        require(math.isclose(times["total_ms"], components, rel_tol=1e-8, abs_tol=2e-5),
                f"Timing sum disagrees at {sample_path}:{index + 2}")
        if "t_seconds" in columns:
            require(math.isclose(float(sample["t_seconds"]), int(sample["logical_frame"]) / 60,
                                 abs_tol=1e-8), f"Non-deterministic time at {sample_path}:{index + 2}")
        if not boolean(sample["warmup"]):
            for key in metrics:
                metrics[key].append(times[key])
    stats = {key: statistics_for(values) for key, values in metrics.items()}
    for key, values in stats.items():
        if key not in row.get("stats", {}):
            continue
        for name, value in values.items():
            recorded = row["stats"][key].get(name)
            if recorded is not None:
                require(math.isclose(value, float(recorded), rel_tol=1e-12, abs_tol=1e-9),
                        f"Runner statistic disagrees: {sample_path}/{key}/{name}")
    rss_matches = re.findall(r"^benchmark_peak_rss_kib=(\d+)$", log, re.M)
    require(bool(rss_matches), f"Missing process RSS: {log_path}")
    rss = int(rss_matches[-1])
    require(rss == row["peak_rss_kib"], f"RSS result/log mismatch: {log_path}")
    first = rows[0]
    result = {"case": row["case"], "variant": row["variant"], "round": int(row["round"]),
              "scope": scope, "samples": row["samples"], "log": row["log"],
              "measured_frames": frames, "warmup_frames": warmup, "stats": stats,
              "peak_rss_kib": rss, "first_total_ms_from_csv": float(first["total_ms"]),
              "first_render_ms_from_csv": float(first[render_column]), "metadata": metadata}
    for key in ("first_ms", "first_total_ms", "result_since_main_ms", "wall_total_ms",
                "throughput_fps", "final_gpu_drain_ms", "throughput_scope"):
        if key in row:
            result[key] = row[key]
    if "first_total_ms" in row:
        require(math.isclose(result["first_total_ms_from_csv"], row["first_total_ms"], abs_tol=.002),
                f"First frame CSV/detail mismatch: {log_path}")
    return result, metrics


def summarize_campaign(name, directory):
    directory = directory.resolve()
    manifest = read_json(inside(directory, "manifest.json"))
    rows = read_json(inside(directory, "results.json"))
    inside(directory, "medians.json")  # Completion artifact, preserved verbatim.
    params = manifest["parameters"]
    rounds = 1 if params.get("mode") == "verify" else int(params["rounds"])
    expected = {(case, variant, run) for case in params["cases"].split(",")
                for variant in params["variants"].split(",") for run in range(1, rounds + 1)}
    actual = [(row["case"], row["variant"], int(row["round"])) for row in rows]
    require(len(actual) == len(set(actual)) and set(actual) == expected,
            f"Incomplete/duplicate campaign processes: {name}: missing={expected - set(actual)}")
    processes, groups = [], {}
    for row in rows:
        process, metrics = load_process(directory, row, params)
        processes.append(process)
        group = groups.setdefault((row["case"], row["variant"]), {"processes": [], "values": {}})
        group["processes"].append(process)
        for metric, values in metrics.items():
            group["values"].setdefault(metric, []).extend(values)
    aggregates = []
    for (case, variant), group in sorted(groups.items()):
        members = sorted(group["processes"], key=lambda item: item["round"])
        item = {"campaign": name, "scope": params["scope"], "case": case, "variant": variant,
                "processes": len(members), "measured_frames": sum(p["measured_frames"] for p in members),
                "warmup_frames": sum(p["warmup_frames"] for p in members), "stats": {},
                "peak_rss_median_mib": statistics.median(p["peak_rss_kib"] for p in members) / 1024,
                "peak_rss_global_max_mib": max(p["peak_rss_kib"] for p in members) / 1024}
        for metric, values in group["values"].items():
            direct = statistics_for(values)
            item["stats"][metric] = {
                **{key: statistics.median(p["stats"][metric][key] for p in members)
                   for key in ("median_ms", "p95_ms", "p99_ms")},
                "max_global_ms": direct["max_ms"],
                "frames_over_16_67_sum": direct["over_16_67"],
                "frames_over_33_33_sum": direct["over_33_33"]}
        for key in ("first_total_ms_from_csv", "first_render_ms_from_csv", "first_ms",
                    "first_total_ms", "result_since_main_ms", "wall_total_ms", "throughput_fps",
                    "final_gpu_drain_ms"):
            if all(key in p for p in members):
                item[key + "_median"] = (None if key == "final_gpu_drain_ms" and
                                           any(p[key] < 0 for p in members) else
                                           statistics.median(p[key] for p in members))
        scopes = {p["throughput_scope"] for p in members if "throughput_scope" in p}
        require(len(scopes) <= 1, f"Mixed throughput contracts: {name}/{case}/{variant}")
        if scopes:
            item["throughput_scope"] = scopes.pop()
        aggregates.append(item)
    digest_groups = {}
    for process in processes:
        state = process["metadata"]["final_state_fnv64"]
        if state:
            digest_groups.setdefault(process["case"], set()).add(state)
    require(all(len(states) == 1 for states in digest_groups.values()),
            f"Final scene-state digests differ between processes/backends: {name}")
    return {"name": name, "original_directory": str(directory), "manifest": manifest,
            "processes": processes, "groups": aggregates,
            "state_digests": {case: sorted(values) for case, values in digest_groups.items()}}


def image_records(directory, processes, reference):
    """Hash original images and compute RGB without modifying/copying pixels."""
    captures = {}
    inventory = []
    for process in processes:
        log = inside(directory, process["log"]).read_text(encoding="utf-8")
        frames = {}
        for line in log.splitlines():
            if not line.startswith("capture "):
                continue
            fields = dict(re.findall(r"(\w+)=(\S+)", line))
            frame = int(fields["logical_frame"])
            require(frame not in frames, f"Duplicate image frame: {process['log']}/{frame}")
            original = Path(fields["image"])
            actual = original if original.is_file() else directory / "images" / original.name
            require(actual.is_file(), f"Missing original image: {actual}")
            frames[frame] = {"path": actual, "state": fields["state_fnv64"]}
            inventory.append({"case": process["case"], "variant": process["variant"],
                              "round": process["round"], "logical_frame": frame,
                              "original_path_recorded": str(original), "original_path_read": str(actual.resolve()),
                              "sha256": sha256(actual), "bytes": actual.stat().st_size,
                              "state_fnv64": fields["state_fnv64"], "rgb_fnv64": fields["rgb_fnv64"],
                              "rgba_fnv64": fields["rgba_fnv64"], "copied": False})
        if frames:
            require(len(frames) == process["measured_frames"], f"Incomplete image set: {process['log']}")
            captures[(process["case"], process["variant"], process["round"])] = frames
    if not captures:
        return {"images": [], "comparisons": [], "motion": []}
    import numpy as np
    from PIL import Image
    comparisons, motion = [], []
    for (case, variant, run), frames in sorted(captures.items()):
        key = (case, reference, run)
        require(key in captures, f"Missing image reference: {key}")
        reference_frames = captures[key]
        require(frames.keys() == reference_frames.keys(), f"Mismatched image frame set: {case}/{variant}")
        states, hashes = set(), set()
        for frame, sample in frames.items():
            ref = reference_frames[frame]
            require(sample["state"] == ref["state"], f"Scene-state mismatch: {case}/{variant}/{frame}")
            with Image.open(sample["path"]) as image:
                require(image.mode == "RGB", f"Expected RGB image: {sample['path']}")
                pixels = np.asarray(image, dtype=np.int16)
            with Image.open(ref["path"]) as image:
                require(image.mode == "RGB", f"Expected RGB reference: {ref['path']}")
                reference_pixels = np.asarray(image, dtype=np.int16)
            require(pixels.shape == reference_pixels.shape, f"Image size mismatch: {case}/{variant}/{frame}")
            difference = np.abs(pixels - reference_pixels)
            comparisons.append({"case": case, "variant": variant, "round": run, "reference": reference,
                                "logical_frame": frame, "state_fnv64": sample["state"],
                                "rgb_mae": float(difference.mean()), "max_channel_error": int(difference.max()),
                                "pixels_different": int(np.any(difference != 0, axis=2).sum()),
                                "pixels_over3": int(np.any(difference > 3, axis=2).sum()),
                                "pixels": int(pixels.shape[0] * pixels.shape[1])})
            states.add(sample["state"])
            hashes.add(hashlib.sha256(pixels.tobytes()).hexdigest())
        require((len(states) == 1 and len(hashes) == 1) if case == "static" else
                (len(states) > 1 and len(hashes) > 1), f"Unexpected image/state motion: {case}/{variant}")
        motion.append({"case": case, "variant": variant, "round": run,
                       "distinct_states": len(states), "distinct_rgb": len(hashes)})
    return {"images": inventory, "comparisons": comparisons, "motion": motion,
            "comparison_count": len(comparisons),
            "self_controls": sum(x["variant"] == x["reference"] for x in comparisons),
            "max_rgb_mae": max(x["rgb_mae"] for x in comparisons),
            "max_channel_error": max(x["max_channel_error"] for x in comparisons),
            "max_pixels_over3": max(x["pixels_over3"] for x in comparisons)}


def copy_campaign(dataset, output):
    source = Path(dataset["original_directory"])
    destination = output / dataset["name"]
    inventory = []
    for original in sorted(source.rglob("*")):
        if not original.is_file() or original.suffix.lower() not in ARCHIVE_SUFFIXES:
            continue
        relative = original.relative_to(source)
        target = destination / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        digest = sha256(original)
        require(not target.exists() or sha256(target) == digest,
                f"Refusing to overwrite different archived evidence: {target}")
        shutil.copy2(original, target)
        require(sha256(target) == digest, f"Archive copy mismatch: {target}")
        inventory.append({"original_path": str(original.resolve()),
                          "archive_path": str(target.relative_to(output)),
                          "sha256": digest, "bytes": original.stat().st_size})
    return inventory


def provenance(datasets, controls, supplied_sources):
    evidence = []
    control_files = [original for control in controls
                     for original in ([control] if control.is_file() else sorted(control.rglob("*.json")))]
    for control in control_files:
        if control.is_file() and control.suffix == ".json":
            data = read_json(control)
            if isinstance(data, dict) and "source_content_revision" in data and "hashes" in data:
                evidence.append((control, data))
    for name, dataset in datasets.items():
        manifest = dataset["manifest"]
        identity = {"runner_recorded_source_revision": manifest.get("source_revision"),
                    "source_identity_limit": "Runner source_revision identifies its checkout; it does not by itself prove the compiled binary source",
                    "verified_binary_source_revision_supplied": supplied_sources.get(name),
                    "frozen_binary_matches": []}
        hashes = manifest.get("binary_hashes", {})
        for control, data in evidence:
            frozen_values = set(data["hashes"].values())
            matches = {path: digest for path, digest in hashes.items() if digest in frozen_values}
            if not matches:
                continue
            complete = bool(hashes) and len(matches) == len(hashes)
            identity["frozen_binary_matches"].append({
                "control_original_path": str(control.resolve()),
                "source_content_revision": data["source_content_revision"],
                "source_head_at_snapshot": data.get("source_head_at_snapshot"),
                "all_campaign_binary_hashes_match": complete, "matched_campaign_hashes": matches,
                "match_contract": "Content SHA256 match; original filename/symlink spelling may differ"})
        dataset["source_provenance"] = identity


def copy_controls(controls, output):
    inventory = []
    for source in controls:
        originals = [source] if source.is_file() else sorted(p for p in source.rglob("*") if p.is_file())
        for original in originals:
            if original.suffix.lower() not in ARCHIVE_SUFFIXES:
                continue
            relative = Path(source.name) if source.is_file() else Path(source.name) / original.relative_to(source)
            target = output / "controls" / relative
            digest = sha256(original)
            require(not target.exists() or sha256(target) == digest, f"Conflicting control evidence: {target}")
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(original, target)
            require(sha256(target) == digest, f"Control copy mismatch: {target}")
            inventory.append({"original_path": str(original.resolve()),
                              "archive_path": str(target.relative_to(output)),
                              "sha256": digest, "bytes": original.stat().st_size})
    return inventory


def comparisons(datasets):
    lookup = {(name, group["case"], group["variant"]): group
              for name, data in datasets.items() for group in data["groups"]}
    output = []
    pairs = [("baseline-offscreen", "offscreen", "camera"),
             ("baseline-window", "window", "camera"),
             ("transform-before", "transform-after", "transforms-10")]
    for before, after, case in pairs:
        if before not in datasets or after not in datasets:
            continue
        a = datasets[before]["manifest"]; b = datasets[after]["manifest"]
        differences = {key: [a["parameters"].get(key), b["parameters"].get(key)]
                       for key in ("size", "warmup", "frames", "rounds", "gpu", "scope")
                       if a["parameters"].get(key) != b["parameters"].get(key)}
        if a["scene_sha256"] != b["scene_sha256"]:
            differences["scene_sha256"] = [a["scene_sha256"], b["scene_sha256"]]
        for group in datasets[after]["groups"]:
            if group["case"] != case:
                continue
            old = lookup.get((before, case, group["variant"]))
            reference = lookup.get((after, case, "coingl"))
            median = group["stats"]["total_ms"]["median_ms"]
            require(median > 0, f"Zero comparison median: {after}/{case}/{group['variant']}")
            item = {"before_campaign": before, "after_campaign": after, "case": case,
                    "variant": group["variant"], "after_median_ms": median,
                    "protocol_differences": differences, "same_recorded_protocol": not differences,
                    "comparison_limit": "CPU wall-time comparison; matched parameters alone do not establish equal driver/display/thermal state"}
            if old:
                previous = old["stats"]["total_ms"]["median_ms"]
                item.update(before_median_ms=previous, speedup_vs_baseline=previous / median,
                            reduction_percent_vs_baseline=(1 - median / previous) * 100)
            if reference:
                coingl = reference["stats"]["total_ms"]["median_ms"]
                item.update(coingl_median_ms=coingl, speedup_vs_coingl=coingl / median,
                            ratio_to_coingl=median / coingl)
            output.append(item)
    return output


def plot_camera(datasets, path):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    scopes = [scope for scope in ("offscreen", "window") if scope in datasets]
    require(bool(scopes), "Camera figure requires an offscreen or window campaign")
    fig, axes = plt.subplots(1, len(scopes), figsize=(5.7 * len(scopes), 4.9), squeeze=False,
                             layout="constrained")
    historical = False
    for index, scope in enumerate(scopes):
        ax = axes[0][index]
        after = {g["variant"]: g for g in datasets[scope]["groups"] if g["case"] == "camera"}
        baseline = datasets.get("baseline-" + scope)
        before = {g["variant"]: g for g in baseline["groups"] if g["case"] == "camera"} if baseline else {}
        variants = [variant for variant in LABELS if variant in after]
        colors = {"coingl": "#58616e", "bgfx-vulkan": "#2874ad",
                  "bgfx-opengl": "#bb7026", "wgpu-vulkan": "#7358a6"}
        minimum, maximum = [], []
        for y, variant in enumerate(variants):
            color = colors[variant]
            for data, offset, filled in ((before, -.13, False), (after, .13, True)):
                if variant not in data:
                    continue
                stats = data[variant]["stats"]["total_ms"]
                median, p99 = stats["median_ms"], stats["p99_ms"]
                ax.hlines(y + offset, median, p99, color=color, linewidth=1.4, alpha=.7)
                ax.scatter(median, y + offset, facecolors=color if filled else "white",
                           edgecolors=color, s=52, zorder=3)
                ax.scatter(p99, y + offset, marker="|", color=color, s=40, zorder=3)
                ax.annotate(f"{median:.2f}", (median, y + offset), xytext=(0, 7 if not filled else -14),
                            textcoords="offset points", fontsize=8.5, ha="center", color=color)
                minimum.append(median); maximum.append(p99)
        require(bool(minimum), f"No camera measurements in {scope}")
        ax.set_yticks(range(len(variants)), [LABELS[v] for v in variants])
        ax.set_ylim(len(variants) - .45, -.55)
        ax.set_xscale("log"); ax.set_xlim(max(.1, min(minimum) * .65), max(maximum) * 1.4)
        ax.axvline(BUDGET_60, color="#909090", linestyle="--", linewidth=1)
        ax.axvline(BUDGET_30, color="#b0b0b0", linestyle=":", linewidth=1)
        ax.grid(axis="x", alpha=.18); ax.spines[["top", "right"]].set_visible(False)
        ax.set_xlabel("Tempo total por quadro (ms; escala log)")
        ax.set_title("OFFSCREEN" if scope == "offscreen" else "JANELA", loc="left")
        if baseline:
            historical |= any(baseline["manifest"]["parameters"].get(key) !=
                              datasets[scope]["manifest"]["parameters"].get(key)
                              for key in ("frames", "warmup", "rounds", "size"))
    fig.suptitle("CoinRender — câmera móvel\nMediana dos processos; segmento até p99 · CoinGL como referência", fontsize=13)
    caption = "Vazio: antes · Cheio: após overlay de câmera · Linhas: 60 Hz e 30 Hz"
    caption += "\nJanela mede chamada CPU/parede, não latência até a tela."
    caption += "\nBaselines, GPU e protocolos identificados no resumo."
    if historical:
        caption += "\nBaseline histórica com protocolo diferente; diferenças registradas no resumo."
    fig.supxlabel(caption, fontsize=9)
    fig.savefig(path, dpi=150)
    plt.close(fig)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repository", type=Path, default=Path.cwd())
    parser.add_argument("--output", type=Path)
    for name in ("offscreen", "window", "transform-before", "transform-after", "verify",
                 "baseline-offscreen", "baseline-window"):
        parser.add_argument("--" + name, type=Path)
    parser.add_argument("--image-reference", default="coingl")
    parser.add_argument("--control", type=Path, action="append", default=[],
                        help="Original JSON/CSV/log/txt control file or directory; copied under controls")
    parser.add_argument("--binary-source", action="append", default=[], metavar="CAMPAIGN=COMMIT",
                        help="Explicit verified compiled source, separate from runner checkout revision")
    parser.add_argument("--no-default-baselines", action="store_true")
    parser.add_argument("--no-plot", action="store_true")
    parser.add_argument("--recorded-images", action="store_true",
                        help="Use archived RGB metrics for reproduction when original PPM files are not available")
    parser.add_argument("--validate-only", action="store_true",
                        help="Read/recompute inputs; do not create an archive or figure")
    args = parser.parse_args()
    repository = args.repository.resolve()
    inputs = {name: getattr(args, name.replace("-", "_"))
              for name in ("offscreen", "window", "transform-before", "transform-after", "verify",
                           "baseline-offscreen", "baseline-window")}
    if not args.no_default_baselines:
        for scope in ("offscreen", "window"):
            if inputs[scope] and not inputs["baseline-" + scope]:
                candidate = repository / "docs/validation/animation-linux" / scope
                if candidate.is_dir():
                    inputs["baseline-" + scope] = candidate
    require(any(inputs[name] for name in ("offscreen", "window", "transform-before", "transform-after", "verify")),
            "Pass at least one final campaign directory")
    output = (args.output or repository / "docs/validation/camera-reuse-linux").resolve()
    if not args.validate_only:
        for source in filter(None, inputs.values()):
            require(not output.is_relative_to(source.resolve()) and not source.resolve().is_relative_to(output),
                    f"Output overlaps an input campaign: {source}")
    datasets = {name: summarize_campaign(name, directory) for name, directory in inputs.items() if directory}
    controls = [path.resolve() for path in args.control]
    require(all(path.exists() for path in controls), "A requested control file/directory is missing")
    binary_sources = {}
    for assignment in args.binary_source:
        name, separator, revision = assignment.partition("=")
        require(separator and name in datasets and re.fullmatch(r"[0-9a-fA-F]{7,40}", revision),
                f"Invalid compiled source assignment: {assignment}")
        require(name not in binary_sources, f"Duplicate compiled source assignment: {name}")
        binary_sources[name] = revision
    provenance(datasets, controls, binary_sources)
    images = {}
    for name, data in datasets.items():
        if data["manifest"]["parameters"].get("mode") != "verify":
            continue
        directory = Path(data["original_directory"])
        if args.recorded_images:
            image_evidence = read_json(inside(directory, "report-image-evidence.json"))
            image_evidence["rgb_metrics_recomputed_this_invocation"] = False
            image_evidence["reproduction_limit"] = "Archived metrics reused; original PPM intentionally not stored in Git"
        else:
            image_evidence = image_records(directory, data["processes"], args.image_reference)
            image_evidence["rgb_metrics_recomputed_this_invocation"] = True
        images[name] = image_evidence
    timing = [data for name, data in datasets.items() if not name.startswith("baseline-") and
              data["manifest"]["parameters"].get("mode", "measure") == "measure"]
    verification = [data for name, data in datasets.items() if not name.startswith("baseline-") and
                    data["manifest"]["parameters"].get("mode") == "verify"]
    baselines = [data for name, data in datasets.items() if name.startswith("baseline-")]
    totals = {"timing_processes": sum(len(data["processes"]) for data in timing),
              "timing_measured_frames": sum(p["measured_frames"] for data in timing for p in data["processes"]),
              "timing_warmup_frames": sum(p["warmup_frames"] for data in timing for p in data["processes"]),
              "verify_processes": sum(len(data["processes"]) for data in verification),
              "verify_frames": sum(p["measured_frames"] for data in verification for p in data["processes"]),
              "baseline_reference_processes": sum(len(data["processes"]) for data in baselines),
              "baseline_raw_copied": False}
    summary = {"generated_at_utc": datetime.now(timezone.utc).isoformat(),
               "totals": totals,
               "aggregation": {"median_p95_p99": "Median of independently recalculated per-process statistics",
                               "percentile": "Nearest rank: ceil(n*p)-1 in sorted measured samples",
                               "maximum": "Maximum of all measured CSV samples across processes",
                               "threshold_counts": "Sum across measured CSV samples; strict >1000/60 and >1000/30",
                               "threshold_60_ms": BUDGET_60, "threshold_30_ms": BUDGET_30,
                               "runner_medians": "Preserved verbatim; runner maxima/counts are medians of process maxima/counts",
                               "publication_window": "Not applicable; absent from recomputed statistics",
                               "native_window_final_gpu_drain": "Not exposed by the public API; negative runner sentinel becomes null in summary",
                               "first_frame": "First CSV row, including warmup, independently of measured frame zero"},
               "campaigns": datasets, "comparisons": comparisons(datasets), "image_validation": images}
    if args.validate_only:
        print(json.dumps({"validated_campaigns": list(datasets),
                          "totals": totals,
                          "image_comparisons": sum(data.get("comparison_count", 0) for data in images.values())}))
        return
    output.mkdir(parents=True, exist_ok=True)
    copied, references = [], []
    for name, data in datasets.items():
        if name.startswith("baseline-"):
            source = Path(data["original_directory"])
            for original in sorted(source.rglob("*")):
                if original.is_file() and original.suffix.lower() in ARCHIVE_SUFFIXES:
                    references.append({"campaign": name, "original_path": str(original.resolve()),
                                       "repository_path": str(original.relative_to(repository))
                                       if original.is_relative_to(repository) else None,
                                       "sha256": sha256(original), "bytes": original.stat().st_size,
                                       "copied": False})
        else:
            copied.extend(copy_campaign(data, output))
    copied.extend(copy_controls(controls, output))
    for name, data in images.items():
        write_json(output / name / "report-image-evidence.json", data)
    write_json(output / "report-summary.json", summary)
    write_json(output / "archive-manifest.json", {"original_paths_preserved_in_runner_json": True,
                                                "ppm_files_copied": False, "copied_files": copied,
                                                "baseline_references": references})
    helper_target = output / "summarize.py"
    if Path(__file__).resolve() != helper_target:
        shutil.copy2(Path(__file__), helper_target)
    if not args.no_plot and ("offscreen" in datasets or "window" in datasets):
        plot_camera(datasets, output / "camera-before-after.png")
    print(json.dumps({"archive": str(output), "copied_files": len(copied),
                      "totals": totals,
                      "image_comparisons": sum(data.get("comparison_count", 0) for data in images.values())}))


if __name__ == "__main__":
    main()
