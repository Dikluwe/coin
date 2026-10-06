#!/usr/bin/env python3
"""Validate and archive before/after CoinRender animation campaigns.

Only reads benchmark evidence; never builds or runs a GPU benchmark. CSV values
are recomputed through the already validated camera campaign summarizer. The
archive includes that helper and this CLI. PPM files are hashed/read, not copied.

Example:
  python3 /tmp/coin-render-wgpu-motion-summary.py \
    --before /tmp/motion-before --after /tmp/motion-after \
    --verify-before /tmp/verify-before --verify-after /tmp/verify-after \
    --control /tmp/frozen-binaries.json --output /tmp/motion-evidence
"""

import argparse
from datetime import datetime, timezone
import importlib.util
import json
from pathlib import Path
import re
import shutil
import sys

sys.dont_write_bytecode = True


def module_at(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def protocol_differences(before, after):
    a, b = before["manifest"], after["manifest"]
    result = {key: [a.get("effective_parameters", a["parameters"]).get(key), b.get("effective_parameters", b["parameters"]).get(key)]
              for key in ("scope", "gpu", "size", "warmup", "frames", "rounds", "mode")
              if a.get("effective_parameters", a["parameters"]).get(key) != b.get("effective_parameters", b["parameters"]).get(key)}
    if a["scene_sha256"] != b["scene_sha256"]:
        result["scene_sha256"] = [a["scene_sha256"], b["scene_sha256"]]
    return result


def metric_pair(before, after):
    if before is None or after is None:
        return {"before": before, "after": after, "change_percent": None}
    return {"before": before, "after": after,
            "change_percent": (after / before - 1) * 100 if before else None,
            "ratio_before_to_after": before / after if after else None}


def compare_timing(core, before, after):
    differences = protocol_differences(before, after)
    core.require(not any(key in differences for key in ("scope", "gpu", "size", "scene_sha256")),
                 f"Cannot compare different scope/GPU/resolution/scene: {differences}")
    old = {(g["case"], g["variant"]): g for g in before["groups"]}
    new = {(g["case"], g["variant"]): g for g in after["groups"]}
    shared = sorted(old.keys() & new.keys())
    core.require(bool(shared), "No matching case/variant between timing campaigns")
    result = []
    for case, variant in shared:
        a, b = old[(case, variant)], new[(case, variant)]
        metrics = {}
        for name in ("median_ms", "p95_ms", "p99_ms", "max_global_ms"):
            metrics["total_" + name] = metric_pair(a["stats"]["total_ms"][name],
                                                  b["stats"]["total_ms"][name])
        for metric in ("update_ms", "render_ms", "publication_ms"):
            if metric in a["stats"] and metric in b["stats"]:
                metrics[metric + "_median"] = metric_pair(a["stats"][metric]["median_ms"],
                                                            b["stats"][metric]["median_ms"])
        for key in ("first_total_ms_from_csv_median", "first_render_ms_from_csv_median",
                    "result_since_main_ms_median", "peak_rss_median_mib"):
            metrics[key] = metric_pair(a.get(key), b.get(key))
        state_a, state_b = before["state_digests"].get(case), after["state_digests"].get(case)
        result.append({"case": case, "variant": variant, "metrics": metrics,
                       "same_recorded_protocol": not differences,
                       "protocol_differences": differences,
                       "final_scene_state_digests": {"before": state_a, "after": state_b,
                                                     "equal": state_a == state_b},
                       "limit": "Observed CPU wall times; matched protocol does not establish equal driver/display/thermal state"})
    return {"comparisons": result,
            "before_only": [list(key) for key in sorted(old.keys() - new.keys())],
            "after_only": [list(key) for key in sorted(new.keys() - old.keys())]}


def compare_rgb(core, analyzer_path, before, after):
    """Reuse capture-log parsing, comparing the same renderer across revisions."""
    import numpy as np
    from PIL import Image
    analyzer = module_at("motion_image_analyzer", analyzer_path)
    differences = protocol_differences(before, after)
    core.require(not differences, f"Verification protocols differ: {differences}")
    old = {(r["case"], r["variant"], r["round"]): r for r in before["processes"]}
    new = {(r["case"], r["variant"], r["round"]): r for r in after["processes"]}
    shared = sorted(old.keys() & new.keys())
    core.require(bool(shared), "No matching case/variant/round between verification campaigns")
    results = []
    for case, variant, run in shared:
        a = analyzer.captures(Path(before["original_directory"]), old[(case, variant, run)])
        b = analyzer.captures(Path(after["original_directory"]), new[(case, variant, run)])
        core.require(a.keys() == b.keys(), f"Captured logical frames differ: {case}/{variant}/{run}")
        for frame in sorted(a):
            core.require(a[frame]["state"] == b[frame]["state"],
                         f"Scene state differs: {case}/{variant}/{run}/{frame}")
            pixels = []
            for item in (a[frame], b[frame]):
                with Image.open(item["image"]) as image:
                    core.require(image.mode == "RGB", f"Expected RGB PPM: {item['image']}")
                    pixels.append(np.asarray(image, dtype=np.int16))
            core.require(pixels[0].shape == pixels[1].shape,
                         f"Image dimensions differ: {case}/{variant}/{frame}")
            delta = np.abs(pixels[1] - pixels[0])
            results.append({"case": case, "variant": variant, "round": run,
                            "logical_frame": frame, "state_fnv64": a[frame]["state"],
                            "rgb_mae": float(delta.mean()), "max_channel_error": int(delta.max()),
                            "pixels_different": int(np.any(delta != 0, axis=2).sum()),
                            "pixels_over3": int(np.any(delta > 3, axis=2).sum()),
                            "before": {"original_image": str(a[frame]["image"]),
                                       "ppm_sha256": core.sha256(a[frame]["image"]),
                                       "rgb_fnv64": a[frame]["rgb"], "rgba_fnv64": a[frame]["rgba"]},
                            "after": {"original_image": str(b[frame]["image"]),
                                      "ppm_sha256": core.sha256(b[frame]["image"]),
                                      "rgb_fnv64": b[frame]["rgb"], "rgba_fnv64": b[frame]["rgba"]}})
    return {"rgb_metrics_recomputed_this_invocation": True, "ppm_files_copied": False,
            "comparison": "Same variant, case, round and logical frame; scene-state digest equality required",
            "comparison_count": len(results), "all_rgb_identical": all(r["pixels_different"] == 0 for r in results),
            "results": results,
            "before_only": [list(k) for k in sorted(old.keys() - new.keys())],
            "after_only": [list(k) for k in sorted(new.keys() - old.keys())]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repository", type=Path, default=Path("/tmp/coin-render-first-frame"))
    parser.add_argument("--before", type=Path, required=True)
    parser.add_argument("--after", type=Path, required=True)
    parser.add_argument("--verify-before", type=Path)
    parser.add_argument("--verify-after", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--control", type=Path, action="append", default=[])
    parser.add_argument("--binary-source", action="append", default=[], metavar="CAMPAIGN=COMMIT")
    parser.add_argument("--recorded-rgb", type=Path,
                        help="Reuse archived RGB evidence when original PPMs are unavailable; explicitly marked as not recomputed")
    parser.add_argument("--require-identical-rgb", action="store_true")
    parser.add_argument("--validate-only", action="store_true", help="Read/recompute; do not create files")
    args = parser.parse_args()
    repository = args.repository.resolve()
    sidecar = Path(__file__).with_name("campaign_core.py")
    core_path = sidecar if sidecar.is_file() else repository / "docs/validation/camera-reuse-linux/summarize.py"
    core = module_at("motion_campaign_core", core_path)
    analyzer_sidecar = Path(__file__).with_name("analyze_animation_images.py")
    analyzer_path = analyzer_sidecar if analyzer_sidecar.is_file() else repository / "scripts/coinrender/analyze_animation_images.py"
    core.require(bool(args.verify_before) == bool(args.verify_after), "Pass both verification campaigns")
    core.require(not (args.recorded_rgb and args.verify_before), "Choose original verification campaigns or recorded RGB")
    inputs = {"before": args.before, "after": args.after}
    if args.verify_before:
        inputs.update({"verify-before": args.verify_before, "verify-after": args.verify_after})
    output = (args.output or repository / "docs/validation/wgpu-motion-linux").resolve()
    if not args.validate_only:
        for source in inputs.values():
            core.require(not output.is_relative_to(source.resolve()) and not source.resolve().is_relative_to(output),
                         f"Output overlaps input campaign: {source}")
    datasets = {name: core.summarize_campaign(name, path) for name, path in inputs.items()}
    for name in ("before", "after"):
        core.require(datasets[name]["manifest"]["parameters"].get("mode", "measure") == "measure",
                     f"Timing campaign has verification mode: {name}")
    controls = [path.resolve() for path in args.control]
    core.require(all(path.exists() for path in controls), "Missing control evidence")
    sources = {}
    for assignment in args.binary_source:
        name, separator, revision = assignment.partition("=")
        core.require(separator and name in datasets and re.fullmatch(r"[0-9a-fA-F]{7,40}", revision),
                     f"Invalid compiled source assignment: {assignment}")
        core.require(name not in sources, f"Duplicate compiled source assignment: {name}")
        sources[name] = revision
    core.provenance(datasets, controls, sources)
    rgb = None
    if args.verify_before:
        for name in ("verify-before", "verify-after"):
            core.require(datasets[name]["manifest"]["parameters"].get("mode") == "verify",
                         f"Verification campaign has timing mode: {name}")
        rgb = compare_rgb(core, analyzer_path, datasets["verify-before"], datasets["verify-after"])
    elif args.recorded_rgb:
        rgb = core.read_json(args.recorded_rgb)
        rgb["rgb_metrics_recomputed_this_invocation"] = False
        rgb["reproduction_limit"] = "Stored metrics reused; original PPMs intentionally excluded from archive"
    if args.require_identical_rgb:
        core.require(rgb is not None and rgb["all_rgb_identical"], "RGB before/after equality was not established")
    summary = {"generated_at_utc": datetime.now(timezone.utc).isoformat(),
               "aggregation": "Median of independently recomputed per-process median/p95/p99; global maxima and summed threshold counts from CSV",
               "first_frame": "First CSV row, including warmup; render excludes update, total includes update",
               "rss": "Process peak RSS from time(1), not GPU memory",
               "campaigns": datasets, "timing": compare_timing(core, datasets["before"], datasets["after"]),
               "rgb_before_after": rgb}
    if not args.validate_only:
        output.mkdir(parents=True, exist_ok=True)
        copied = [item for data in datasets.values() for item in core.copy_campaign(data, output)]
        copied.extend(core.copy_controls(controls, output))
        helper_files = [(Path(__file__).resolve(), "summarize.py"), (core_path, "campaign_core.py")]
        if analyzer_path.is_file():
            helper_files.append((analyzer_path, "analyze_animation_images.py"))
        for source, filename in helper_files:
            target = output / filename
            if source.resolve() != target.resolve():
                core.require(not target.exists() or core.sha256(source) == core.sha256(target),
                             f"Conflicting archived helper: {target}")
                shutil.copy2(source, target)
            copied.append({"original_path": str(source.resolve()), "archive_path": filename,
                           "sha256": core.sha256(target), "bytes": target.stat().st_size})
        core.write_json(output / "report-summary.json", summary)
        if rgb is not None:
            core.write_json(output / "rgb-before-after.json", rgb)
        core.write_json(output / "archive-manifest.json", {"ppm_files_copied": False,
                        "runner_json_preserved_verbatim": True, "copied_files": copied})
    print(json.dumps({"validated_campaigns": list(datasets), "archive": None if args.validate_only else str(output),
                      "timing_comparisons": len(summary["timing"]["comparisons"]),
                      "rgb_comparisons": rgb["comparison_count"] if rgb else 0}))


if __name__ == "__main__":
    main()
