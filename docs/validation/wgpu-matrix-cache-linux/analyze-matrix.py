#!/usr/bin/env python3
"""Reproduce matched wgpu/CoinGL summaries and matrix-cache ablation traces.

Reads archived files only. No benchmark, GPU command, build or Git command is
invoked. Each process is summarized from CSV rows whose warmup flag is false.
Campaign summaries are medians of process summaries, never pooled frames.
The shared CoinGL control is counted once only after CSV and log SHA256 match.
Diagnostic phase/counter series are delegated to the cardinality-guarded raw
trace parser; short/long series are retained without inferred frame statistics.

Example:
  python3 /tmp/coin-render-matrix-analysis.py \
    --pair offscreen /tmp/matrix-before /tmp/matrix-after \
    --pair stress /tmp/matrix-stress-before /tmp/matrix-stress-after \
    --diagnostic /tmp/matrix-ablation --output /tmp/matrix-analysis.json
"""

import argparse
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import statistics
import sys

sys.dont_write_bytecode = True

METRICS = ("update_ms", "render_ms", "publication_ms", "total_ms")
RECORDED_FIELDS = ("first_ms", "first_total_ms", "result_since_main_ms", "peak_rss_kib", "throughput_fps")
MATRIX_OPTOUT = "COIN_WGPU_DISABLE_INSTANCE_MATRIX_CACHE"


def load_helper(path):
    spec = importlib.util.spec_from_file_location("coin_validation_diagnostics", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def sample_statistics(values):
    ordered = sorted(values)
    return {"median_ms": statistics.median(ordered),
            "p95_ms": ordered[math.ceil(len(ordered) * .95) - 1],
            "p99_ms": ordered[math.ceil(len(ordered) * .99) - 1],
            "min_ms": ordered[0], "max_ms": ordered[-1],
            "over_16_67": sum(value > 1000 / 60 for value in values),
            "over_33_33": sum(value > 1000 / 30 for value in values)}


def input_bytes(helper, path, root, files):
    data, metadata = helper.input_file(path, root)
    files[metadata["relative_path"]] = metadata
    return data, metadata


def read_json(helper, path, root, files):
    data, metadata = input_bytes(helper, path, root, files)
    return json.loads(data), metadata


def identity(row):
    return row["case"], row["variant"], row["round"]


def key_name(key):
    return f"{key[0]}|{key[1]}|round={key[2]}"


def expected_integer(parameters, name):
    value = parameters.get(name)
    try:
        return int(value) if value is not None else None
    except (TypeError, ValueError):
        return None


def read_campaign(helper, directory):
    root = directory.resolve()
    files = {}
    manifest, manifest_input = read_json(helper, root / "manifest.json", root, files)
    rows, results_input = read_json(helper, root / "results.json", root, files)
    if not isinstance(rows, list):
        raise ValueError(f"Expected result array in {root}/results.json")
    parameters = manifest.get("parameters", {})
    expected_frames = expected_integer(parameters, "frames")
    expected_warmup = expected_integer(parameters, "warmup")
    processes = {}
    for row in rows:
        key = identity(row)
        label = key_name(key)
        if label in processes:
            raise ValueError(f"Duplicate case/variant/round {label} in {root}")
        limitations = []
        csv_data, csv_input = input_bytes(helper, root / row["samples"], root, files)
        log_data, log_input = input_bytes(helper, root / row["log"], root, files)
        samples = helper.read_samples(csv_data, limitations)
        if not samples["selection_valid"] or not samples["measured_row_indices"]:
            limitations.append("No valid measured-frame selection; process summaries omitted")
        measured_count = len(samples["measured_row_indices"] or [])
        warmup_count = len(samples["warmup_row_indices"] or [])
        if expected_frames is not None and measured_count != expected_frames:
            limitations.append(f"Measured rows {measured_count} differ from manifest frames {expected_frames}")
        if expected_warmup is not None and warmup_count != expected_warmup:
            limitations.append(f"Warmup rows {warmup_count} differ from manifest warmup {expected_warmup}")
        stats = {}
        if samples["selection_valid"] and measured_count:
            for metric in METRICS:
                values = [helper.numeric_value(samples["rows"][index].get(metric, ""))
                          for index in samples["measured_row_indices"]]
                if all(helper.is_number(value) for value in values):
                    stats[metric] = sample_statistics(values)
                else:
                    limitations.append(f"Invalid measured CSV values for {metric}; statistics omitted")
        recorded_stats_differences = []
        for metric, recomputed in stats.items():
            for statistic, actual in recomputed.items():
                recorded = row.get("stats", {}).get(metric, {}).get(statistic)
                if recorded is not None and not math.isclose(recorded, actual, rel_tol=1e-10, abs_tol=1e-10):
                    recorded_stats_differences.append({"metric": metric, "statistic": statistic,
                                                       "recorded": recorded, "recomputed": actual})
        if recorded_stats_differences:
            limitations.append("Recorded result statistics differ from recomputed CSV statistics")
        events, series, _ = helper.parse_trace(log_data, limitations)
        for item in series.values():
            helper.summarize_series(item, samples)
        processes[label] = {"case": key[0], "variant": key[1], "round": key[2],
                            "result_metadata": row, "csv_input": csv_input, "log_input": log_input,
                            "samples": samples, "measured_frames": measured_count, "warmup_frames": warmup_count,
                            "recomputed_stats": stats, "recorded_stats_differences": recorded_stats_differences,
                            "raw_trace": {"events": events, "series": series}, "limitations": limitations}
    groups = {}
    for process in processes.values():
        group = groups.setdefault(f"{process['case']}|{process['variant']}", [])
        group.append(process)
    summaries = {}
    expected_rounds = expected_integer(parameters, "rounds")
    coverage_issues = []
    expected_cases = str(parameters.get("cases", "")).split(",")
    expected_variants = str(parameters.get("variants", "")).split(",")
    if expected_rounds and all(expected_cases) and all(expected_variants):
        expected_identities = {key_name((case, variant, round_number))
                               for case in expected_cases for variant in expected_variants
                               for round_number in range(1, expected_rounds + 1)}
        missing = sorted(expected_identities - set(processes))
        unexpected = sorted(set(processes) - expected_identities)
        if missing:
            coverage_issues.append({"missing_processes": missing})
        if unexpected:
            coverage_issues.append({"unexpected_processes": unexpected})
    for label, group in sorted(groups.items()):
        summary = {"case": group[0]["case"], "variant": group[0]["variant"],
                   "processes": len(group), "rounds": sorted(process["round"] for process in group),
                   "stats": {}, "limitations": []}
        if expected_rounds is not None and len(group) != expected_rounds:
            summary["limitations"].append(f"Available rounds {len(group)} differ from expected {expected_rounds}")
        for metric in METRICS:
            if all(metric in process["recomputed_stats"] for process in group):
                summary["stats"][metric] = {
                    statistic: statistics.median(process["recomputed_stats"][metric][statistic] for process in group)
                    for statistic in group[0]["recomputed_stats"][metric]}
        for field in RECORDED_FIELDS:
            if all(helper.is_number(process["result_metadata"].get(field)) for process in group):
                summary[field] = statistics.median(process["result_metadata"][field] for process in group)
        summaries[label] = summary
    return {"directory": str(root), "manifest_input": manifest_input, "results_input": results_input,
            "metadata": manifest, "input_files": files, "processes": processes, "summaries": summaries,
            "coverage_issues": coverage_issues}


def difference(before, after):
    return {"before": before, "after": after, "delta": after - before,
            "change_percent": (after / before - 1) * 100 if before else None}


def compare_campaigns(helper, name, before_path, after_path):
    before, after = read_campaign(helper, before_path), read_campaign(helper, after_path)
    issues = []
    for role, campaign in (("before", before), ("after", after)):
        if campaign["coverage_issues"]:
            issues.append(f"{role}: process coverage differs from manifest; see coverage_issues")
    for parameter in ("scope", "gpu", "cases", "frames", "warmup", "rounds", "size"):
        a = before["metadata"].get("parameters", {}).get(parameter)
        b = after["metadata"].get("parameters", {}).get(parameter)
        if a != b:
            issues.append(f"Protocol mismatch in {parameter}: {a!r} / {b!r}")
    if before["metadata"].get("scene_sha256") != after["metadata"].get("scene_sha256"):
        issues.append("Scene SHA256 differs between campaigns")
    if not before["metadata"].get("scene_sha256") or not after["metadata"].get("scene_sha256"):
        issues.append("Scene SHA256 missing; scene identity is not proven")
    keys_before, keys_after = set(before["processes"]), set(after["processes"])
    if keys_before != keys_after:
        issues.append("Before/after case/variant/round identities differ; incomplete pairs are retained")
    shared_controls = []
    paired_processes = []
    unique_processes = []
    for label in sorted(keys_before | keys_after):
        a, b = before["processes"].get(label), after["processes"].get(label)
        for role, process in (("before", a), ("after", b)):
            if process and process["limitations"]:
                issues.extend(f"{label} ({role}): {item}" for item in process["limitations"])
        shared = False
        if a and b and a["variant"] == "coingl":
            csv_equal = a["csv_input"]["sha256"] == b["csv_input"]["sha256"]
            log_equal = a["log_input"]["sha256"] == b["log_input"]["sha256"]
            claimed_shared = a["result_metadata"].get("shared_control") and b["result_metadata"].get("shared_control")
            shared = bool(claimed_shared and csv_equal and log_equal)
            shared_controls.append({"identity": label, "claimed_shared": bool(claimed_shared),
                                    "csv_sha256_equal": csv_equal, "log_sha256_equal": log_equal,
                                    "counted_once": shared})
            if not shared:
                issues.append(f"{label}: shared CoinGL identity was not proven by metadata and CSV/log hashes")
        if a:
            unique_processes.append(a)
        if b and not shared:
            unique_processes.append(b)
        if a and b:
            paired_processes.append({"identity": label, "case": a["case"], "variant": a["variant"], "round": a["round"],
                                     "shared_control": shared, "stats": {
                metric: {statistic: difference(a["recomputed_stats"][metric][statistic], b["recomputed_stats"][metric][statistic])
                         for statistic in a["recomputed_stats"][metric]}
                for metric in METRICS if metric in a["recomputed_stats"] and metric in b["recomputed_stats"]}})
    comparisons = {}
    for label in sorted(set(before["summaries"]) & set(after["summaries"])):
        a, b = before["summaries"][label], after["summaries"][label]
        item = {"case": a["case"], "variant": a["variant"], "before_processes": a["processes"],
                "after_processes": b["processes"], "rounds_match": a["rounds"] == b["rounds"], "stats": {}}
        for metric in METRICS:
            if metric in a["stats"] and metric in b["stats"]:
                item["stats"][metric] = {statistic: difference(a["stats"][metric][statistic], b["stats"][metric][statistic])
                                          for statistic in a["stats"][metric]}
        for field in RECORDED_FIELDS:
            if field in a and field in b:
                item[field] = difference(a[field], b[field])
        item["limitations"] = a["limitations"] + b["limitations"]
        comparisons[label] = item
    return {"name": name, "before": before, "after": after, "comparisons": comparisons,
            "paired_processes": paired_processes, "shared_controls": shared_controls,
            "comparability_issues": issues, "complete_and_comparable": not issues,
            "unique_counts": {"processes": len(unique_processes),
                              "measured_frames": sum(item["measured_frames"] for item in unique_processes),
                              "warmup_frames": sum(item["warmup_frames"] for item in unique_processes)}}


def command_option(command, name):
    try:
        index = command.index(name)
        return command[index + 1]
    except (ValueError, IndexError):
        return None


def read_diagnostic(helper, directory):
    raw = helper.analyze(directory.resolve())
    groups = {}
    issues = []
    for stem, run in raw["runs"].items():
        commands = run["command_metadata"]
        if len(commands) != 1:
            issues.append(f"{stem}: expected one command metadata record; no on/off pairing inferred")
            continue
        command = commands[0].get("command", [])
        environment = commands[0].get("environment", {})
        animation = command_option(command, "--animation")
        percent = command_option(command, "--animated-percent")
        backend = command_option(command, "--backend")
        renderer = environment.get("WGPU_BACKEND", environment.get("COIN_BGFX_RENDERER"))
        if not animation or not backend:
            issues.append(f"{stem}: command lacks animation/backend; no on/off pairing inferred")
            continue
        case = animation if animation in ("static", "camera") else f"{animation}-{percent}"
        mode = "off" if environment.get(MATRIX_OPTOUT) == "1" else "on"
        label = f"{backend}|{renderer}|{case}"
        modes = groups.setdefault(label, {})
        if mode in modes:
            issues.append(f"{label}: more than one {mode} process; no unique pairing inferred")
            modes[mode].append(stem)
        else:
            modes[mode] = [stem]
    comparisons = {}
    for label, modes in sorted(groups.items()):
        if set(modes) != {"on", "off"} or any(len(stems) != 1 for stems in modes.values()):
            issues.append(f"{label}: incomplete or ambiguous on/off pair")
            continue
        on_stem, off_stem = modes["on"][0], modes["off"][0]
        on, off = raw["runs"][on_stem], raw["runs"][off_stem]
        pair_issues = []
        on_command, off_command = on["command_metadata"][0], off["command_metadata"][0]
        for option in ("--scene", "--animation", "--animated-percent", "--backend", "--transparency",
                       "--size", "--width", "--height", "--warmup", "--frames", "--animation-step"):
            if command_option(on_command.get("command", []), option) != command_option(off_command.get("command", []), option):
                pair_issues.append(f"Command protocol differs in {option}")
        on_environment = {key: value for key, value in on_command.get("environment", {}).items() if key != MATRIX_OPTOUT}
        off_environment = {key: value for key, value in off_command.get("environment", {}).items() if key != MATRIX_OPTOUT}
        if on_environment != off_environment:
            pair_issues.append("Environment differs beyond matrix-cache optout")
        if not on["samples"]["selection_valid"] or not off["samples"]["selection_valid"]:
            pair_issues.append("Measured CSV selection is invalid")
        elif on["samples"]["measured_row_indices"] != off["samples"]["measured_row_indices"] or on["samples"]["row_count"] != off["samples"]["row_count"]:
            pair_issues.append("CSV measured-row selections/cardinality differ")
        for role, command in (("on", on_command), ("off", off_command)):
            if command.get("exit_code") not in (None, 0):
                pair_issues.append(f"{role} command failed with exit_code={command['exit_code']}")
        item = {"on": on_stem, "off": off_stem, "phase_measured_medians_ms": {},
                "counter_measured_medians": {}, "csv_measured_medians_ms": {},
                "complete_and_comparable": not pair_issues, "comparability_issues": pair_issues}
        if not pair_issues:
            for category in ("phase_measured_medians_ms", "counter_measured_medians", "csv_measured_medians_ms"):
                for key in sorted(set(on[category]) & set(off[category])):
                    item[category][key] = difference(off[category][key], on[category][key])
        comparisons[label] = item
    return {"metadata_and_raw_trace": raw, "on_off_comparisons": comparisons, "pairing_issues": issues}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--pair", nargs=3, action="append", default=[], metavar=("NAME", "BEFORE", "AFTER"))
    parser.add_argument("--diagnostic", type=Path, action="append", default=[], help="Raw ablation directory with commands.json and CSV/log files")
    parser.add_argument("--trace-helper", type=Path, default=Path("/tmp/coin-render-validation-diagnostics.py"))
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if not args.pair and not args.diagnostic:
        parser.error("Supply at least one --pair or --diagnostic")
    if len({pair[0] for pair in args.pair}) != len(args.pair):
        parser.error("Campaign pair names must be unique")
    if not args.trace_helper.is_file():
        parser.error(f"Trace helper missing: {args.trace_helper}")
    output = args.output.resolve()
    if output.suffix != ".json" or output.name in ("manifest.json", "commands.json", "results.json", "medians.json"):
        parser.error("Use a new derived .json filename, preserving campaign inputs")
    helper = load_helper(args.trace_helper.resolve())
    report = {"schema_version": 1, "protocol": {
        "sample_selection": "CSV warmup=0/false only; all raw rows retained",
        "summary": "median of per-process statistics, not pooled frames",
        "percentiles": "nearest rank per process; short samples do not establish tail latency",
        "difference": "after minus before; positive milliseconds/percent means slower",
        "diagnostic_difference": "on minus off (off is the before/reference value)",
        "phase_alignment": "scope/key cardinality must equal CSV row count; no guessed last-three slice",
        "phase_overlap": "nested phase times are not additive",
        "shared_control": "CoinGL counted once only after metadata and CSV/log SHA256 equality",
        "matrix_cache_optout": MATRIX_OPTOUT}, "campaigns": {}, "diagnostics": {}}
    for name, before, after in args.pair:
        report["campaigns"][name] = compare_campaigns(helper, name, Path(before), Path(after))
    for directory in args.diagnostic:
        key = str(directory.resolve())
        if key in report["diagnostics"]:
            parser.error(f"Repeated diagnostic input: {directory}")
        report["diagnostics"][key] = read_diagnostic(helper, directory)
    script = Path(__file__).resolve()
    report["analysis_metadata"] = {"script": str(script), "script_sha256": hashlib.sha256(script.read_bytes()).hexdigest(),
                                   "trace_helper": str(args.trace_helper.resolve()),
                                   "trace_helper_sha256": hashlib.sha256(args.trace_helper.read_bytes()).hexdigest(),
                                   "invocation": sys.argv, "python": sys.version}
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    print(f"Wrote {len(report['campaigns'])} campaign pair(s), {len(report['diagnostics'])} diagnostic input(s) to {output}")


if __name__ == "__main__":
    main()
