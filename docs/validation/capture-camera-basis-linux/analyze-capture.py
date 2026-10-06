#!/usr/bin/env python3
"""Reproduce four-variant capture campaigns and three-round cold ablation traces.

Reads archived files only. No benchmark, GPU command, build or Git command is
invoked. Each process is summarized from CSV rows whose warmup flag is false.
Campaign summaries are medians of process summaries, never pooled frames.
The shared CoinGL control is counted once only after CSV and log SHA256 match.
Diagnostic phase/counter series are delegated to the cardinality-guarded raw
trace parser; short/long series are retained without inferred frame statistics.

Example:
  python3 /tmp/coin-render-capture-analysis.py
    --pair cold /tmp/coin-render-capture-cold-before /tmp/coin-render-capture-cold-after
    --pair steady /tmp/coin-render-capture-steady-before /tmp/coin-render-capture-steady-after
    --diagnostic /tmp/coin-render-capture-ablation
    --output /tmp/coin-render-capture-analysis/results.json
"""

import argparse
import hashlib
import importlib.util
import json
import math
import re
from pathlib import Path
import statistics
import sys

sys.dont_write_bytecode = True

METRICS = ("update_ms", "render_ms", "publication_ms", "total_ms")
RECORDED_FIELDS = ("first_ms", "first_total_ms", "result_since_main_ms", "peak_rss_kib", "throughput_fps")
CAPTURE_OPTOUT = "COIN_RENDER_DISABLE_CAPTURE_CAMERA_BASIS_REUSE"
CAPTURE_SCOPE = "capture_camera_basis"
DIAGNOSTIC_VARIANTS = ("bgfx-vulkan", "bgfx-opengl", "wgpu-vulkan")


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


def log_metadata(helper, data):
    text = data.decode("utf-8", errors="replace")
    values = {}
    first = re.search(r"^\S+_first_frame_ms=([\d.eE+-]+)", text, re.M)
    if first:
        values["first_ms"] = helper.numeric_value(first[1])
    detail = re.search(r"^(?:\S+_first_detail|window_first_frame_detail) (.+)$", text, re.M)
    if detail:
        fields = {key: helper.numeric_value(value) for key, value in
                  re.findall(r"(\w+)=([\d.eE+-]+)(?:\s|$)", detail[1])}
        if "render_present_ms" in fields:
            values["first_ms"] = fields["render_present_ms"]
        for output, options in (("first_total_ms", ("total_with_update_ms", "total_ms")),
                                ("result_since_main_ms", ("result_since_main_ms",))):
            for option in options:
                if option in fields:
                    values[output] = fields[option]
                    break
    rss = re.search(r"benchmark_peak_rss_kib=(\d+)", text)
    if rss:
        values["peak_rss_kib"] = int(rss[1])
    throughput = re.search(r"^\S+_throughput frames=\d+ total_ms=([\d.eE+-]+) fps=([\d.eE+-]+)", text, re.M)
    if throughput:
        values["throughput_fps"] = helper.numeric_value(throughput[2])
    return values


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
        recomputed_log_metadata = log_metadata(helper, log_data)
        recorded_log_differences = []
        for field, actual in recomputed_log_metadata.items():
            recorded = row.get(field)
            if recorded is not None and (not helper.is_number(actual) or
                    not math.isclose(recorded, actual, rel_tol=1e-10, abs_tol=1e-10)):
                recorded_log_differences.append({"field": field, "recorded": recorded, "recomputed": actual})
        if recorded_log_differences:
            limitations.append("Recorded first/RSS/throughput metadata differs from preserved log")
        stem = Path(row["samples"]).stem
        command_metadata = [record for record in manifest.get("commands", []) if record.get("stem") == stem]
        expected_source = manifest.get("variant_source_content_revisions", {}).get(row["variant"])
        if len(command_metadata) != 1:
            limitations.append("Expected one manifest command per result process")
        else:
            record = command_metadata[0]
            if record.get("exit_code") != 0 or record.get("timed_out"):
                limitations.append("Command exit success is not recorded")
            if not expected_source or record.get("source_content_revision") != expected_source:
                limitations.append("Command source revision differs from manifest variant source")
            if row.get("source_content_revision") != expected_source:
                limitations.append("Result source revision differs from manifest variant source")
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
                            "command_metadata": command_metadata,
                            "recomputed_log_metadata": recomputed_log_metadata,
                            "recorded_log_differences": recorded_log_differences,
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
            if all(helper.is_number(process["recomputed_log_metadata"].get(field)) for process in group):
                summary[field] = statistics.median(process["recomputed_log_metadata"][field] for process in group)
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


def diagnostic_variant(record):
    if record.get("variant"):
        return record["variant"]
    command = record.get("command", [])
    environment = record.get("environment", {})
    backend = command_option(command, "--backend")
    renderer = environment.get("WGPU_BACKEND", environment.get("COIN_BGFX_RENDERER"))
    if backend == "wgpu" and renderer == "vulkan":
        return "wgpu-vulkan"
    if backend == "bgfx" and renderer in ("vulkan", "opengl"):
        return "bgfx-" + renderer
    return None


def diagnostic_round(record, stem):
    value = record.get("round")
    if value is None:
        suffix = re.search(r"-(\d+)$", stem)
        value = suffix[1] if suffix else None
    try:
        return int(value) if value is not None else None
    except (TypeError, ValueError):
        return None


def read_diagnostic(helper, directory, expected_rounds=3):
    raw = helper.analyze(directory.resolve())
    groups, issues = {}, []
    total_measured = total_warmup = 0
    for stem, run in raw["runs"].items():
        if "log_input" in run:
            data = Path(run["log_input"]["path"]).read_bytes()
            if hashlib.sha256(data).hexdigest() != run["log_input"]["sha256"]:
                raise ValueError(f"Diagnostic log changed while analyzing {stem}")
            run["first_frame_metadata"] = log_metadata(helper, data)
        else:
            run["first_frame_metadata"] = {}
        total_measured += len(run["samples"].get("measured_row_indices") or [])
        total_warmup += len(run["samples"].get("warmup_row_indices") or [])
        commands = run["command_metadata"]
        if len(commands) != 1:
            issues.append(f"{stem}: expected one command metadata record; no pairing inferred")
            continue
        record = commands[0]
        command, environment = record.get("command", []), record.get("environment", {})
        variant = diagnostic_variant(record)
        round_number = diagnostic_round(record, stem)
        animation = command_option(command, "--animation")
        percent = command_option(command, "--animated-percent")
        case = animation if animation in ("static", "camera") else f"{animation}-{percent}"
        if not variant or not animation or round_number is None:
            issues.append(f"{stem}: variant/case/round could not be proven; no pairing inferred")
            continue
        mode = "off" if environment.get(CAPTURE_OPTOUT) == "1" else "on"
        label = f"{variant}|{case}"
        rounds = groups.setdefault(label, {}).setdefault(mode, {})
        if round_number in rounds:
            issues.append(f"{label}: duplicate {mode} round {round_number}; duplicate retained only in raw data")
            continue
        rounds[round_number] = stem
        samples = run["samples"]
        measured = len(samples.get("measured_row_indices") or [])
        warmup = len(samples.get("warmup_row_indices") or [])
        if not samples["selection_valid"] or measured != 1 or warmup != 0 or samples["row_count"] != 1:
            issues.append(f"{stem}: cold ablation requires one measured CSV row and zero warmup rows")
        records = [event for event in run["trace_events"] if event["scope"] == CAPTURE_SCOPE]
        run["capture_basis_events"] = records
        expected = {"calls": 1, "prepares": 2 if mode == "off" else 1, "reuse": 0 if mode == "off" else 1}
        actual = records[0]["fields"] if len(records) == 1 else None
        run["capture_basis_counter_check"] = {
            "expected": expected, "actual": actual,
            "passed": actual is not None and all(actual.get(key) == value for key, value in expected.items())}
        if len(records) != samples["row_count"] or len(records) != 1:
            issues.append(f"{stem}: capture_camera_basis event cardinality differs from the one-row cold CSV")
        if not run["capture_basis_counter_check"]["passed"]:
            issues.append(f"{stem}: capture basis counters do not match expected cold preparation/reuse counts")
        if record.get("exit_code") != 0:
            issues.append(f"{stem}: missing/failed command exit code {record.get('exit_code')!r}")
        if not record.get("adapter_verified_nvidia"):
            issues.append(f"{stem}: NVIDIA adapter verification is not recorded")
    expected_labels = {f"{variant}|static" for variant in DIAGNOSTIC_VARIANTS}
    if set(groups) != expected_labels:
        issues.append("Diagnostic groups differ from the three expected API/static groups")
    comparisons, summaries, paired_processes = {}, {}, []
    for label, modes in sorted(groups.items()):
        group_issues = []
        expected = set(range(1, expected_rounds + 1))
        if set(modes) != {"on", "off"} or any(set(values) != expected for values in modes.values()):
            group_issues.append("Incomplete on/off rounds; all raw records retained")
        summaries[label] = {}
        for mode, rounds in sorted(modes.items()):
            runs = [raw["runs"][stem] for _, stem in sorted(rounds.items())]
            summary = {"stems": [stem for _, stem in sorted(rounds.items())],
                       "rounds": sorted(rounds), "processes": len(runs),
                       "phase_measured_medians_ms": {}, "counter_measured_medians": {},
                       "csv_measured_medians_ms": {}, "first_frame_metadata": {}}
            for category in ("phase_measured_medians_ms", "counter_measured_medians", "csv_measured_medians_ms", "first_frame_metadata"):
                keys = set.intersection(*(set(run[category]) for run in runs)) if runs else set()
                for key in sorted(keys):
                    summary[category][key] = statistics.median(run[category][key] for run in runs)
            summaries[label][mode] = summary
        on_rounds, off_rounds = modes.get("on", {}), modes.get("off", {})
        for round_number in sorted(set(on_rounds) & set(off_rounds)):
            on, off = raw["runs"][on_rounds[round_number]], raw["runs"][off_rounds[round_number]]
            on_record, off_record = on["command_metadata"][0], off["command_metadata"][0]
            pair_issues = []
            for role, run, record in (("on", on, on_record), ("off", off, off_record)):
                samples = run["samples"]
                if record.get("exit_code") != 0 or not record.get("adapter_verified_nvidia"):
                    pair_issues.append(f"{role} success/adapter verification is missing")
                if not samples["selection_valid"] or samples["row_count"] != 1 or \
                        len(samples.get("measured_row_indices") or []) != 1 or \
                        len(samples.get("warmup_row_indices") or []) != 0:
                    pair_issues.append(f"{role} is not a one-row, zero-warmup cold sample")
                if run["limitations"]:
                    pair_issues.extend(f"{role}: {issue}" for issue in run["limitations"])
            source_on = on_record.get("source_content_revision", raw.get("command_metadata", {}).get("source_content_revision"))
            source_off = off_record.get("source_content_revision", raw.get("command_metadata", {}).get("source_content_revision"))
            if not source_on or source_on != source_off:
                pair_issues.append("On/off source identity is missing or differs")
            for option in ("--scene", "--animation", "--animated-percent", "--backend", "--transparency",
                           "--size", "--width", "--height", "--warmup", "--frames", "--animation-step"):
                if command_option(on_record.get("command", []), option) != command_option(off_record.get("command", []), option):
                    pair_issues.append(f"Command protocol differs in {option}")
            on_environment = {key: value for key, value in on_record.get("environment", {}).items() if key != CAPTURE_OPTOUT}
            off_environment = {key: value for key, value in off_record.get("environment", {}).items() if key != CAPTURE_OPTOUT}
            if on_environment != off_environment:
                pair_issues.append("Environment differs beyond capture-basis reuse optout")
            if on["samples"]["measured_row_indices"] != off["samples"]["measured_row_indices"] or on["samples"]["row_count"] != off["samples"]["row_count"]:
                pair_issues.append("CSV measured-row selections/cardinality differ")
            if not on["capture_basis_counter_check"]["passed"] or not off["capture_basis_counter_check"]["passed"]:
                pair_issues.append("Cold capture basis counters differ from the expected prepare/reuse proof")
            paired = {"group": label, "round": round_number,
                      "on": on_rounds[round_number], "off": off_rounds[round_number],
                      "comparability_issues": pair_issues, "complete_and_comparable": not pair_issues}
            for category in ("phase_measured_medians_ms", "counter_measured_medians", "csv_measured_medians_ms", "first_frame_metadata"):
                paired[category] = {key: difference(off[category][key], on[category][key])
                                    for key in sorted(set(on[category]) & set(off[category]))}
            paired_processes.append(paired)
            group_issues.extend(f"round {round_number}: {issue}" for issue in pair_issues)
        comparison = {"phase_measured_medians_ms": {}, "counter_measured_medians": {},
                      "csv_measured_medians_ms": {}, "first_frame_metadata": {}, "comparability_issues": group_issues,
                      "complete_and_comparable": not group_issues}
        if set(summaries[label]) == {"on", "off"}:
            on_summary, off_summary = summaries[label]["on"], summaries[label]["off"]
            for category in ("phase_measured_medians_ms", "counter_measured_medians", "csv_measured_medians_ms", "first_frame_metadata"):
                comparison[category] = {key: difference(off_summary[category][key], on_summary[category][key])
                                        for key in sorted(set(on_summary[category]) & set(off_summary[category]))}
        comparisons[label] = comparison
        issues.extend(f"{label}: {issue}" for issue in group_issues)
    return {"metadata_and_raw_trace": raw, "group_summaries": summaries,
            "on_off_comparisons": comparisons, "paired_processes": paired_processes,
            "pairing_issues": issues, "complete_and_comparable": not issues,
            "unique_counts": {"processes": len(raw["runs"]), "measured_frames": total_measured,
                              "warmup_frames": total_warmup},
            "expected_unique_counts": {"processes": len(DIAGNOSTIC_VARIANTS)*expected_rounds*2,
                                       "measured_frames": len(DIAGNOSTIC_VARIANTS)*expected_rounds*2,
                                       "warmup_frames": 0}}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--pair", nargs=3, action="append", default=[], metavar=("NAME", "BEFORE", "AFTER"))
    parser.add_argument("--diagnostic", type=Path, action="append", default=[], help="Raw ablation directory with commands.json and CSV/log files")
    parser.add_argument("--trace-helper", type=Path, default=Path("/tmp/coin-render-capture-diagnostics.py"))
    parser.add_argument("--campaign-commands", type=Path, help="Optional preserved orchestration commands JSON")
    parser.add_argument("--diagnostic-rounds", type=int, default=3)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if not args.pair and not args.diagnostic:
        parser.error("Supply at least one --pair or --diagnostic")
    if len({pair[0] for pair in args.pair}) != len(args.pair):
        parser.error("Campaign pair names must be unique")
    if args.diagnostic_rounds < 1:
        parser.error("Diagnostic rounds must be positive")
    if not args.trace_helper.is_file():
        parser.error(f"Trace helper missing: {args.trace_helper}")
    output = args.output.resolve()
    protected_inputs = {Path(directory).resolve() / name for _, before, after in args.pair
                        for directory in (before, after)
                        for name in ("manifest.json", "results.json", "medians.json")}
    protected_inputs.update(directory.resolve() / "commands.json" for directory in args.diagnostic)
    if args.campaign_commands:
        protected_inputs.add(args.campaign_commands.resolve())
    if output.suffix != ".json" or output.name in ("manifest.json", "commands.json", "medians.json") or output in protected_inputs:
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
        "capture_basis_reuse_optout": CAPTURE_OPTOUT}, "campaigns": {}, "diagnostics": {}}
    for name, before, after in args.pair:
        report["campaigns"][name] = compare_campaigns(helper, name, Path(before), Path(after))
    for directory in args.diagnostic:
        key = str(directory.resolve())
        if key in report["diagnostics"]:
            parser.error(f"Repeated diagnostic input: {directory}")
        report["diagnostics"][key] = read_diagnostic(helper, directory, args.diagnostic_rounds)
    report["unique_counts"] = {
        key: sum(campaign["unique_counts"][key] for campaign in report["campaigns"].values())
        for key in ("processes", "measured_frames", "warmup_frames")}
    if args.campaign_commands:
        root = args.campaign_commands.resolve().parent
        data, metadata = helper.input_file(args.campaign_commands.resolve(), root)
        report["campaign_commands"] = {"input": metadata, "metadata": json.loads(data)}
        expected = report["campaign_commands"]["metadata"].get("expected_unique_counts")
        report["campaign_counts_match_expected"] = report["unique_counts"] == expected
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
