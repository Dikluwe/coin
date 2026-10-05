#!/usr/bin/env python3
"""Reproduce four-variant geometry-overlay campaigns and raw diagnostic traces.

Reads archived files only. No benchmark, GPU command, build or Git command is
invoked. Each process is summarized from CSV rows whose warmup flag is false.
Campaign summaries are medians of process summaries, never pooled frames.
The shared CoinGL control is counted once only after CSV and log SHA256 match.
Diagnostic phase/counter series are delegated to the cardinality-guarded raw
trace parser; short/long series are retained without inferred frame statistics.

Example:
  python3 /tmp/coin-render-geometry-overlay-analysis.py
    --pair steady /tmp/coin-render-geometry-overlay-steady-before /tmp/coin-render-geometry-overlay-steady-after
    --diagnostic /tmp/coin-render-geometry-overlay-ablation
    --output /tmp/coin-render-geometry-overlay-analysis/results.json
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
    command, environment = record.get("command", []), record.get("environment", {})
    backend = command_option(command, "--backend")
    renderer = environment.get("WGPU_BACKEND", environment.get("COIN_BGFX_RENDERER"))
    if backend == "wgpu" and renderer == "vulkan":
        return "wgpu-vulkan"
    if backend == "bgfx" and renderer in ("vulkan", "opengl"):
        return "bgfx-" + renderer
    return None


OPTOUT = "COIN_RENDER_DISABLE_GEOMETRY_INTERVAL_VALIDATION"
VARIANTS = ("wgpu-vulkan", "bgfx-vulkan", "bgfx-opengl")
CASES = ("geometry-10", "geometry-100")
EXPECTED = {"processes": 36, "measured_frames": 252, "warmup_frames": 108}


def diagnostic_contract_check(contract):
    if not isinstance(contract, dict) or type(contract.get("configured")) is not bool:
        raise ValueError("Contract needs an explicit configured boolean")
    if contract.get("optout") != OPTOUT or contract.get("scope") != "geometry_overlay_validation":
        raise ValueError("Unexpected geometry overlay optout/scope")
    if contract.get("variants") != list(VARIANTS) or contract.get("cases") != list(CASES):
        raise ValueError("Unexpected diagnostic API/case coverage")
    if contract.get("rounds") != 3 or contract.get("warmup") != 3 or contract.get("frames") != 7:
        raise ValueError("Diagnostic protocol must be 3 rounds, 3 warmups, 7 measured frames")
    if contract.get("alignment") != "validation event belongs to next action marker":
        raise ValueError("Explicit action-marker correlation is required")
    return contract


def normalized_diagnostic_command(command):
    result = list(command)
    if result.count("--samples-output") != 1:
        raise ValueError("Expected one --samples-output option")
    result[result.index("--samples-output") + 1] = "<samples>"
    return result


def finite_number(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value)


def integer_at_least(value, minimum):
    return type(value) is int and value >= minimum


def align_geometry_validation(run, contract, effective_mode):
    """Use verified action markers as frame boundaries before CSV selection.

    No CSV-1 slicing is licensed by count alone. Every event must precede its
    action, the first action must be a full rebuild without a validation event,
    and each later action must be a resource rebuild with exactly one event.
    This is a city-profile gate, not a general guarantee about every scene.
    """
    samples = run["samples"]
    events = run["trace_events"]
    actions = [event for event in events if event["scope"] == "action"]
    validations = [event for event in events if event["scope"] == contract["scope"]]
    issues, rows = [], []
    if len(actions) != samples["row_count"]:
        issues.append("Action marker cardinality differs from CSV; no frame mapping inferred")
    previous_line = 0
    assigned = []
    for index, action in enumerate(actions):
        line = action["log_line_number"]
        matched = [event for event in validations if previous_line < event["log_line_number"] < line]
        assigned.extend(event["log_line_number"] for event in matched)
        previous_line = line
        expected_reuse = "full_rebuild" if index == 0 else "resource_rebuild"
        if action["fields"].get("plan_reuse") != expected_reuse:
            issues.append(f"Action {index} is not {expected_reuse}")
        if len(matched) != (0 if index == 0 else 1):
            issues.append(f"Action {index} has {len(matched)} validation events; expected {0 if index == 0 else 1}")
        event = matched[0] if len(matched) == 1 else None
        checks = []
        if event is not None:
            fields = event["fields"]
            positions, runs = fields.get("positions"), fields.get("runs")
            sorted_items, scratch = fields.get("sorted_items"), fields.get("scratch_bytes")
            mode, elapsed = fields.get("mode"), fields.get("validation_ms")
            checks.extend([
                ("positive position count", integer_at_least(positions, 1)),
                ("nonnegative run/sort/scratch counters", all(integer_at_least(value, 0) for value in (runs, sorted_items, scratch))),
                ("finite nonnegative validation time", finite_number(elapsed) and elapsed >= 0),
            ])
            if effective_mode == "off":
                checks.extend([("literal reference", mode == "literal"),
                               ("literal sorts every position", sorted_items == positions),
                               ("literal run count is not collected", runs == 0),
                               ("literal scratch covers slots", integer_at_least(scratch, 0) and integer_at_least(positions, 1) and scratch >= positions * 4)])
            elif mode == "ordered":
                checks.extend([("ordered run range", integer_at_least(runs, 1) and integer_at_least(positions, 1) and runs <= positions),
                               ("ordered needs no sort/scratch", sorted_items == 0 and scratch == 0)])
            elif mode == "intervals":
                checks.extend([("interval run count", integer_at_least(runs, 1) and integer_at_least(positions, 1) and runs <= positions),
                               ("interval sort count", sorted_items == runs),
                               ("interval scratch bounds", integer_at_least(scratch, 0) and integer_at_least(runs, 1) and integer_at_least(positions, 1) and runs * 8 <= scratch <= min(65536 * 8, positions * 4))])
            else:
                checks.append(("city on-path admitted ordered/intervals proof", False))
            issues.extend(f"Action {index}: {label} failed" for label, passed in checks if not passed)
        row = samples["rows"][index] if index < samples["row_count"] else None
        rows.append({"csv_row_index": index, "csv_frame_index": row.get("frame_index") if row else None,
                     "csv_warmup": row.get("warmup") if row else None,
                     "action_log_line_number": line, "action_fields": action["fields"],
                     "validation_events": matched,
                     "checks": [{"label": label, "passed": passed} for label, passed in checks]})
    if len(assigned) != len(validations) or set(assigned) != {event["log_line_number"] for event in validations}:
        issues.append("Unassigned or multiply assigned geometry validation event")
    if len(validations) != max(0, samples["row_count"] - 1):
        issues.append("Geometry event cardinality differs from verified cold-plus-overlay city pattern")
    return {"rows": rows, "action_count": len(actions), "validation_count": len(validations),
            "issues": issues, "passed": not issues}


def read_diagnostic(helper, directory, contract=None):
    raw = helper.analyze(directory.resolve())
    counts = {"processes": len(raw["runs"]),
              "measured_frames": sum(len(run["samples"].get("measured_row_indices") or []) for run in raw["runs"].values()),
              "warmup_frames": sum(len(run["samples"].get("warmup_row_indices") or []) for run in raw["runs"].values())}
    if contract is None:
        return {"metadata_and_raw_trace": raw, "unique_counts": counts,
                "pairing_status": "pending_geometry_trace_contract", "complete_and_comparable": False}
    contract = diagnostic_contract_check(contract)
    metadata = raw["command_metadata"]
    issues, groups = list(raw["limitations"]), {}
    if counts != EXPECTED:
        issues.append("Diagnostic counts differ from 36 processes/252 measured/108 warmups")
    if metadata.get("expected_unique_counts") != EXPECTED or metadata.get("completed_unique_counts") != EXPECTED:
        issues.append("Recorded expected/completed counts differ from protocol")
    if not metadata.get("binaries_unchanged") or metadata.get("binary_hashes") != metadata.get("binary_hashes_post_campaign"):
        issues.append("Same-build hashes were not confirmed unchanged after ablation")
    binary_hashes = metadata.get("binary_hashes", {})
    if len(binary_hashes) != 6 or any(not re.fullmatch("[0-9a-f]{64}", str(value)) for value in binary_hashes.values()):
        issues.append("Expected six pinned benchmark/library hashes for the two render families")
    source = metadata.get("source_content_revision")
    if not isinstance(source, str) or not re.fullmatch("[0-9a-f]{40}", source):
        issues.append("Actual compiled source SHA is missing")
    if not re.fullmatch("[0-9a-f]{64}", str(metadata.get("scene_sha256", ""))):
        issues.append("Scene SHA256 is missing")
    recorded_stems = [record.get("stem") for record in metadata.get("commands", [])]
    if len(recorded_stems) != len(set(recorded_stems)) or set(recorded_stems) != set(raw["runs"]):
        issues.append("Command identities do not exactly cover CSV/log runs")
    for stem, run in raw["runs"].items():
        run_issues = list(run["limitations"])
        run["first_frame_metadata"] = log_metadata(helper, Path(run["log_input"]["path"]).read_bytes()) if "log_input" in run else {}
        records = run["command_metadata"]
        if len(records) != 1:
            issues.append(stem + ": expected one command record; raw evidence retained")
            continue
        record = records[0]
        command, environment = record.get("command", []), record.get("environment", {})
        variant = diagnostic_variant(record)
        animation, percent = command_option(command, "--animation"), command_option(command, "--animated-percent")
        case = str(animation) + "-" + str(percent)
        mode = "off" if environment.get(OPTOUT) == "1" else "on"
        if variant not in VARIANTS or case not in CASES or type(record.get("round")) is not int or not 1 <= record["round"] <= 3:
            issues.append(stem + ": unexpected API/case/round; no pairing inferred")
            continue
        identity_key = variant + "|" + case
        rounds = groups.setdefault(identity_key, {}).setdefault(mode, {})
        if record["round"] in rounds:
            issues.append(stem + ": duplicate on/off round")
        rounds[record["round"]] = stem
        if record.get("case") != case or record.get("mode") != mode:
            run_issues.append("Explicit identity differs from effective argv/optout")
        if environment.get(OPTOUT) not in (None, "1") or environment.get("COIN_RENDER_TRACE_PHASES") != "1":
            run_issues.append("Trace/optout environment differs from contract")
        if record.get("source_content_revision") != source or record.get("exit_code") != 0 or record.get("timed_out"):
            run_issues.append("Source SHA or command exit/timeout differs from successful protocol")
        if not command or command[0] not in binary_hashes:
            run_issues.append("Command executable is not covered by the same-build binary hashes")
        log = Path(run["log_input"]["path"]).read_text() if "log_input" in run else ""
        if not record.get("adapter_verified_nvidia") or "NVIDIA" not in log:
            run_issues.append("NVIDIA adapter is not verified by record and raw log")
        samples = run["samples"]
        if not samples["selection_valid"] or samples["measured_row_indices"] != list(range(3, 10)) or samples["warmup_row_indices"] != list(range(3)):
            run_issues.append("CSV measured/warmup selection differs from 3 warmups + 7 measured frames")
        if [row.get("frame_index") for row in samples["rows"]] != [str(index) for index in range(-3, 7)]:
            run_issues.append("CSV frame indices are not the expected -3..6 sequence")
        for option, expected in (("--frames", "7"), ("--warmup", "3"), ("--size", "1024"), ("--transparency", "object")):
            if command_option(command, option) != expected:
                run_issues.append("Command differs in " + option)
        alignment = align_geometry_validation(run, contract, mode)
        run["geometry_validation_alignment"] = alignment
        run_issues.extend(alignment["issues"])
        run["geometry_aligned_series"] = {}
        if alignment["passed"] and samples["selection_valid"] and samples["measured_row_indices"]:
            for key in ("positions", "runs", "sorted_items", "scratch_bytes", "validation_ms", "mode"):
                values = [row["validation_events"][0]["fields"].get(key) if row["validation_events"] else None for row in alignment["rows"]]
                selected = [values[index] for index in samples["measured_row_indices"]]
                item = {"values": values, "selected_row_indices": samples["measured_row_indices"], "selected_values": selected,
                        "alignment": "next action marker after validation; first full capture explicitly has no event"}
                if all(finite_number(value) for value in selected):
                    item["measured_median"] = statistics.median(selected)
                    category = "phase_measured_medians_ms" if key.endswith("_ms") else "counter_measured_medians"
                    run[category][contract["scope"] + "." + key] = item["measured_median"]
                else:
                    item["measured_value_counts"] = {str(value): selected.count(value) for value in set(selected)}
                run["geometry_aligned_series"][key] = item
        run["geometry_contract_issues"] = run_issues
        run["geometry_contract_passed"] = not run_issues
        issues.extend(stem + ": " + issue for issue in run_issues)
    expected_groups = {variant + "|" + case for variant in VARIANTS for case in CASES}
    if set(groups) != expected_groups:
        issues.append("Diagnostic API/case coverage differs from contract")
    categories = ("phase_measured_medians_ms", "counter_measured_medians", "csv_measured_medians_ms", "first_frame_metadata")
    summaries, comparisons, pairs = {}, {}, []
    for label, modes in sorted(groups.items()):
        group_issues = []
        if set(modes) != {"on", "off"} or any(set(rounds) != {1, 2, 3} for rounds in modes.values()):
            group_issues.append("Incomplete or extra on/off rounds")
        summaries[label] = {}
        for mode, rounds in sorted(modes.items()):
            runs = [raw["runs"][stem] for _, stem in sorted(rounds.items())]
            result = {"stems": [stem for _, stem in sorted(rounds.items())], "rounds": sorted(rounds), "processes": len(runs)}
            for category in categories:
                keys = set.intersection(*(set(run[category]) for run in runs)) if runs else set()
                result[category] = {key: statistics.median(run[category][key] for run in runs) for key in sorted(keys)}
                result[category + "_process_values"] = {key: [run[category][key] for run in runs] for key in sorted(keys)}
            summaries[label][mode] = result
        for round_number in sorted(set(modes.get("on", {})) & set(modes.get("off", {}))):
            on, off = (raw["runs"][modes[mode][round_number]] for mode in ("on", "off"))
            on_record, off_record = on["command_metadata"][0], off["command_metadata"][0]
            pair_issues = [mode + ": " + issue for mode, run in (("on", on), ("off", off)) for issue in run["geometry_contract_issues"]]
            if normalized_diagnostic_command(on_record["command"]) != normalized_diagnostic_command(off_record["command"]):
                pair_issues.append("Command argv differ beyond samples output path")
            if {key: value for key, value in on_record["environment"].items() if key != OPTOUT} != {key: value for key, value in off_record["environment"].items() if key != OPTOUT}:
                pair_issues.append("Environment differs beyond geometry interval optout")
            if on_record.get("source_content_revision") != off_record.get("source_content_revision"):
                pair_issues.append("On/off source revisions differ")
            positions_key = "positions"
            if on.get("geometry_aligned_series", {}).get(positions_key, {}).get("values") != off.get("geometry_aligned_series", {}).get(positions_key, {}).get("values"):
                pair_issues.append("Aligned on/off position counts differ")
            pair = {"group": label, "round": round_number, "on": modes["on"][round_number], "off": modes["off"][round_number],
                    "comparability_issues": pair_issues, "complete_and_comparable": not pair_issues}
            for category in categories:
                pair[category] = {key: difference(off[category][key], on[category][key]) for key in sorted(set(on[category]) & set(off[category]))}
            pairs.append(pair)
            group_issues.extend(f"round {round_number}: {issue}" for issue in pair_issues)
        comparison = {category: {} for category in categories}
        if set(summaries[label]) == {"on", "off"}:
            on, off = summaries[label]["on"], summaries[label]["off"]
            for category in categories:
                comparison[category] = {key: difference(off[category][key], on[category][key]) for key in sorted(set(on[category]) & set(off[category]))}
        comparison.update(comparability_issues=group_issues, complete_and_comparable=not group_issues)
        comparisons[label] = comparison
        issues.extend(label + ": " + issue for issue in group_issues)
    return {"metadata_and_raw_trace": raw, "contract": contract, "unique_counts": counts,
            "expected_unique_counts": EXPECTED, "group_summaries": summaries,
            "on_off_comparisons": comparisons, "paired_processes": pairs, "pairing_issues": issues,
            "contract_observation_passed": not issues, "complete_and_comparable": not issues,
            "pairing_status": "configured_geometry_contract" if contract["configured"] else "geometry_contract_observation"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pair", nargs=3, action="append", default=[], metavar=("NAME", "BEFORE", "AFTER"))
    parser.add_argument("--diagnostic", type=Path, action="append", default=[])
    parser.add_argument("--diagnostic-contract", type=Path)
    parser.add_argument("--trace-helper", type=Path, default=Path(__file__).with_name("coin-render-geometry-overlay-diagnostics.py"))
    parser.add_argument("--campaign-commands", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if not args.pair and not args.diagnostic:
        parser.error("Supply --pair or --diagnostic")
    if len({pair[0] for pair in args.pair}) != len(args.pair):
        parser.error("Campaign names must be unique")
    helper = load_helper(args.trace_helper.resolve())
    contract = diagnostic_contract_check(json.loads(args.diagnostic_contract.read_text())) if args.diagnostic_contract else None
    output = args.output.resolve()
    if output.suffix != ".json" or output.exists():
        parser.error("Use a new .json output file")
    protected = {Path(directory).resolve() / name for _, before, after in args.pair for directory in (before, after) for name in ("manifest.json", "results.json", "medians.json")}
    protected.update(directory.resolve() / "commands.json" for directory in args.diagnostic)
    protected.update(path.resolve() for path in (args.campaign_commands, args.diagnostic_contract) if path)
    if output in protected:
        parser.error("Output must preserve original inputs")
    report = {"schema_version": 1, "campaigns": {}, "diagnostics": {}, "protocol": {
        "selection": "CSV warmup=0/false only; raw warmup and measured rows retained",
        "summary": "median of process summaries, never pooled frames",
        "difference": "after minus before; on minus off for ablation; positive means slower",
        "diagnostic_alignment": "validation event assigned to next verified action marker before warmup selection",
        "validation_ms": "position validation + undo preparation + uniqueness + scratch cleanup; excludes later draw/model checks",
        "scratch_bytes": "collision scratch capacity; excludes mandatory undo and allocator overhead",
        "overlap": "validation is nested within action/total; phase medians are not additive"}}
    for name, before, after in args.pair:
        report["campaigns"][name] = compare_campaigns(helper, name, Path(before), Path(after))
    for directory in args.diagnostic:
        key = str(directory.resolve())
        if key in report["diagnostics"]:
            parser.error("Repeated diagnostic directory")
        report["diagnostics"][key] = read_diagnostic(helper, directory, contract)
    report["unique_counts"] = {key: sum(campaign["unique_counts"][key] for campaign in report["campaigns"].values()) for key in ("processes", "measured_frames", "warmup_frames")}
    if args.campaign_commands:
        data, metadata = helper.input_file(args.campaign_commands.resolve(), args.campaign_commands.resolve().parent)
        report["campaign_commands"] = {"input": metadata, "metadata": json.loads(data)}
        report["campaign_counts_match_expected"] = report["unique_counts"] == report["campaign_commands"]["metadata"].get("expected_unique_counts")
    if args.diagnostic_contract:
        data, metadata = helper.input_file(args.diagnostic_contract.resolve(), args.diagnostic_contract.resolve().parent)
        report["diagnostic_contract_input"] = {"input": metadata, "metadata": contract}
    report["analysis_metadata"] = {"script": str(Path(__file__).resolve()), "script_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        "trace_helper": str(args.trace_helper.resolve()), "trace_helper_sha256": hashlib.sha256(args.trace_helper.read_bytes()).hexdigest(),
        "invocation": sys.argv, "python": sys.version}
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2, allow_nan=False) + "\n")
    print("Wrote", output)


if __name__ == "__main__":
    main()
