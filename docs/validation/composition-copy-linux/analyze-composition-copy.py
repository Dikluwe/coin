#!/usr/bin/env python3
"""Reproduce four-variant composition-copy campaigns and raw diagnostic traces.

Reads archived files only. No benchmark, GPU command, build or Git command is
invoked. Each process is summarized from CSV rows whose warmup flag is false.
Campaign summaries are medians of process summaries, never pooled frames.
The shared CoinGL control is counted once only after CSV and log SHA256 match.
Diagnostic phase/counter series are delegated to the cardinality-guarded raw
trace parser; short/long series are retained without inferred frame statistics.

Example:
  python3 /tmp/coin-render-composition-copy-analysis.py
    --pair cold /tmp/coin-render-composition-copy-cold-before /tmp/coin-render-composition-copy-cold-after
    --pair steady /tmp/coin-render-composition-copy-steady-before /tmp/coin-render-composition-copy-steady-after
    --diagnostic /tmp/coin-render-composition-copy-ablation
    --output /tmp/coin-render-composition-copy-analysis/results.json
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


def diagnostic_contract_check(contract, require_configured=False):
    """Reject incomplete policy; no intended counter result becomes evidence."""
    if not isinstance(contract, dict) or type(contract.get("configured")) is not bool:
        raise ValueError("Composition diagnostic contract requires explicit configured status")
    if require_configured and contract["configured"] is not True:
        raise ValueError("Composition diagnostic contract has not been admitted from observed traces")
    if contract.get("optout") != "COIN_RENDER_DISABLE_COMPOSITION_BORROW":
        raise ValueError("Unexpected composition optout")
    if type(contract.get("rounds")) is not int or contract["rounds"] < 1:
        raise ValueError("Invalid diagnostic rounds")
    if not isinstance(contract.get("profiles"), dict):
        raise ValueError("Diagnostic requires explicit per-case profiles")
    for key in ("variants", "cases", "metrics", "scopes"):
        if not isinstance(contract.get(key), list) or not contract[key]:
            raise ValueError("Diagnostic contract requires nonempty " + key)
    variants = contract["variants"]
    if len(variants) != len(set(variants)) or not set(variants).issubset(
            {"bgfx-vulkan", "bgfx-opengl", "wgpu-vulkan"}):
        raise ValueError("Invalid diagnostic variants")
    if len(contract["cases"]) != len(set(contract["cases"])):
        raise ValueError("Duplicate diagnostic cases")
    if set(contract["profiles"]) != set(contract["cases"]):
        raise ValueError("Diagnostic profiles must cover exactly all cases")
    for profile in contract["profiles"].values():
        for key in ("frames", "warmup"):
            if type(profile.get(key)) is not int or profile[key] < (0 if key == "warmup" else 1):
                raise ValueError("Invalid diagnostic profile " + key)
    metric_keys = []
    for metric in contract["metrics"]:
        if metric.get("category") not in ("phase_measured_medians_ms", "counter_measured_medians",
                                           "csv_measured_medians_ms", "first_frame_metadata"):
            raise ValueError("Invalid diagnostic metric category")
        if not metric.get("key") or not metric.get("label") or not metric.get("unit"):
            raise ValueError("Diagnostic metrics require explicit key/label/unit")
        metric_keys.append((metric["category"], metric["key"]))
    if len(metric_keys) != len(set(metric_keys)):
        raise ValueError("Duplicate diagnostic metrics")
    if not contract.get("per_frame_assertions"):
        raise ValueError("Diagnostic contract requires per-frame proof assertions")
    for assertion in contract["per_frame_assertions"]:
        if not assertion.get("scope") or not assertion.get("key") or set(assertion.get("expected", {})) != {"on", "off"}:
            raise ValueError("Invalid per-frame diagnostic assertion")
    for derived in contract.get("derived_metrics", []):
        allowed = {scope + "." + key for scope in ("target_composition_copy", "composition_schedule_copy")
                   for key in ("copy_ms", "qualify_ms")}
        if not derived.get("key", "").endswith("_ms") or set(derived.get("source_keys", [])) != allowed or len(derived["source_keys"]) != 4:
            raise ValueError("Primary transfer/lookup sum requires exactly the four disjoint instrumented intervals")
    return contract


def predicate_passed(actual, expected):
    if isinstance(expected, str):
        return isinstance(actual, str) and actual == expected
    if not isinstance(actual, (int, float)) or isinstance(actual, bool) or not math.isfinite(actual):
        return False
    if isinstance(expected, dict):
        if not expected or not set(expected).issubset({"equal", "minimum", "maximum"}):
            raise ValueError("Invalid counter predicate")
        if not all(isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value)
                   for value in expected.values()):
            raise ValueError("Invalid counter predicate value")
        return (("equal" not in expected or actual == expected["equal"]) and
                ("minimum" not in expected or actual >= expected["minimum"]) and
                ("maximum" not in expected or actual <= expected["maximum"]))
    if not isinstance(expected, (int, float)) or isinstance(expected, bool) or not math.isfinite(expected):
        raise ValueError("Invalid counter expectation")
    return actual == expected


def normalized_diagnostic_command(command):
    # The output path is process-specific; every other argv token is compared.
    result = list(command)
    for option in ("--samples", "--samples-output"):
        if option in result:
            index = result.index(option)
            if index + 1 < len(result):
                result[index + 1] = "<samples>"
    return result


def read_diagnostic(helper, directory, contract=None):
    raw = helper.analyze(directory.resolve())
    counts = {"processes": len(raw["runs"]),
              "measured_frames": sum(len(run["samples"].get("measured_row_indices") or []) for run in raw["runs"].values()),
              "warmup_frames": sum(len(run["samples"].get("warmup_row_indices") or []) for run in raw["runs"].values())}
    if contract is None:
        return {"metadata_and_raw_trace": raw, "pairing_status": "pending_composition_trace_contract",
                "pairing_limitation": "No optimization-specific proof is inferred before the composition trace contract is configured",
                "unique_counts": counts}
    contract = diagnostic_contract_check(contract)
    groups, issues = {}, list(raw["limitations"])
    for stem, run in raw["runs"].items():
        run_issues = list(run["limitations"])
        run["first_frame_metadata"] = {}
        if "log_input" in run:
            data = Path(run["log_input"]["path"]).read_bytes()
            if hashlib.sha256(data).hexdigest() != run["log_input"]["sha256"]:
                raise ValueError("Diagnostic log changed while analyzing " + stem)
            run["first_frame_metadata"] = log_metadata(helper, data)
        records = run["command_metadata"]
        if len(records) != 1:
            issues.append(stem + ": expected exactly one command record; raw evidence retained")
            continue
        record = records[0]
        command, environment = record.get("command", []), record.get("environment", {})
        variant = diagnostic_variant(record)
        animation, percent = command_option(command, "--animation"), command_option(command, "--animated-percent")
        case = animation if animation in ("static", "camera") else str(animation) + "-" + str(percent)
        if record.get("case") and record["case"] != case:
            run_issues.append("Explicit case differs from argv")
        round_number = record.get("round")
        if type(round_number) is not int or round_number < 1 or not variant or not animation:
            issues.append(stem + ": variant/case/round identity missing; no pairing inferred")
            continue
        optout_value = environment.get(contract["optout"])
        if optout_value not in (None, "1"):
            run_issues.append("Optout must be absent on or exactly 1 off")
        mode = "off" if optout_value == "1" else "on"
        if record.get("mode") and record["mode"] != mode:
            run_issues.append("Explicit mode differs from effective optout")
        label = variant + "|" + case
        rounds = groups.setdefault(label, {}).setdefault(mode, {})
        if round_number in rounds:
            issues.append(label + ": duplicate " + mode + " round; all raw evidence retained")
            continue
        rounds[round_number] = stem
        if record.get("exit_code") != 0 or record.get("timed_out"):
            run_issues.append("Command did not record successful exit")
        if not record.get("adapter_verified_nvidia"):
            run_issues.append("NVIDIA adapter verification is not recorded")
        samples = run["samples"]
        profile = contract["profiles"].get(case)
        if profile is None:
            run_issues.append("Case is not in configured profiles")
            profile = {"frames": -1, "warmup": -1}
        if (not samples["selection_valid"] or len(samples.get("measured_row_indices") or []) != profile["frames"] or
                len(samples.get("warmup_row_indices") or []) != profile["warmup"] or
                samples["row_count"] != profile["frames"] + profile["warmup"]):
            run_issues.append("CSV row count or measured/warmup selection differs from contract")
        for option, expected in (("--frames", profile["frames"]), ("--warmup", profile["warmup"])):
            if command_option(command, option) != str(expected):
                run_issues.append("Command protocol differs in " + option)
        events_by_scope = {scope: [event for event in run["trace_events"] if event["scope"] == scope]
                           for scope in contract["scopes"]}
        for scope, events in events_by_scope.items():
            if len(events) != samples["row_count"]:
                run_issues.append(scope + ": event cardinality differs from CSV; no guessed slicing")
        checks = []
        for assertion in contract["per_frame_assertions"]:
            if assertion.get("groups") and label not in assertion["groups"]:
                continue
            events = [event for event in run["trace_events"] if event["scope"] == assertion["scope"]]
            all_values = [event["fields"].get(assertion["key"]) for event in events]
            selection = assertion.get("sample_selection", "all")
            if selection == "all":
                indices = list(range(samples["row_count"]))
            elif selection in ("measured", "warmup"):
                indices = samples.get(selection + "_row_indices") or []
            else:
                raise ValueError("Invalid assertion sample_selection")
            values = [all_values[index] for index in indices if index < len(all_values)]
            expected = assertion["expected"][mode]
            passed = (len(all_values) == samples["row_count"] and bool(indices) and len(values) == len(indices) and
                      all(predicate_passed(value, expected) for value in values))
            checks.append({"scope": assertion["scope"], "key": assertion["key"],
                           "actual_values": all_values, "selected_row_indices": indices, "selected_values": values, "expected": expected, "passed": passed})
            if not passed:
                run_issues.append(assertion["scope"] + "." + assertion["key"] + ": per-frame proof failed")
        run["derived_series"] = {}
        for derived in contract.get("derived_metrics", []):
            source_items = [run["trace_series"].get(key) for key in derived["source_keys"]]
            item = {"values": [], "source_keys": derived["source_keys"], "operation": "row-wise sum before measured selection"}
            if all(source is not None and source.get("cardinality_matches_csv") and
                   len(source["values"]) == samples["row_count"] and
                   all(helper.is_number(value) for value in source["values"]) for source in source_items):
                item["values"] = [sum(source["values"][index] for source in source_items) for index in range(samples["row_count"])]
            helper.summarize_series(item, samples)
            run["derived_series"][derived["key"]] = item
            if "measured_median" in item:
                run["phase_measured_medians_ms"][derived["key"]] = item["measured_median"]
            else:
                run_issues.append("Derived transfer/lookup sum unavailable: sources not fully aligned/numeric")
        for metric in contract["metrics"]:
            if metric["key"] not in run.get(metric["category"], {}):
                run_issues.append("Requested metric missing or cardinality mismatch: " + metric["category"] + "/" + metric["key"])
        run["composition_contract_checks"] = checks
        run["composition_contract_issues"] = run_issues
        run["composition_contract_passed"] = not run_issues
        issues.extend(stem + ": " + issue for issue in run_issues)
    expected_labels = {variant + "|" + case for variant in contract["variants"] for case in contract["cases"]}
    if set(groups) != expected_labels:
        issues.append("Diagnostic API/case coverage differs from contract")
    categories = ("phase_measured_medians_ms", "counter_measured_medians", "csv_measured_medians_ms", "first_frame_metadata")
    summaries, comparisons, pairs = {}, {}, []
    for label, modes in sorted(groups.items()):
        group_issues = []
        expected_rounds = set(range(1, contract["rounds"] + 1))
        if set(modes) != {"on", "off"} or any(set(rounds) != expected_rounds for rounds in modes.values()):
            group_issues.append("Incomplete or extra on/off rounds")
        summaries[label] = {}
        for mode, rounds in sorted(modes.items()):
            runs = [raw["runs"][stem] for _, stem in sorted(rounds.items())]
            result = {"stems": [stem for _, stem in sorted(rounds.items())], "rounds": sorted(rounds), "processes": len(runs)}
            for category in categories:
                keys = set.intersection(*(set(run[category]) for run in runs)) if runs else set()
                result[category] = {key: statistics.median(run[category][key] for run in runs) for key in sorted(keys)}
            result["descriptive_scope_summaries"] = {}
            for scope in contract.get("descriptive_scopes", []):
                event_counts, totals, complete = [], [], True
                for run in runs:
                    events = [event for event in run["trace_events"] if event["scope"] == scope]
                    event_counts.append(len(events))
                    values = [event["fields"].get("qualify_ms") for event in events]
                    if all(helper.is_number(value) for value in values):
                        totals.append(sum(values))
                    else:
                        complete = False
                result["descriptive_scope_summaries"][scope] = {
                    "event_counts_by_process": event_counts,
                    "qualify_ms_totals_by_process": totals,
                    "median_event_count_per_process": statistics.median(event_counts) if event_counts else None,
                    "median_all_events_qualify_ms_total_per_process": statistics.median(totals) if totals and complete else None,
                    "all_events_including_warmup": True,
                    "limitation": "Descriptive process totals include warmup events; events are order calls, not CSV frames or isolated new overhead"}
            summaries[label][mode] = result
        for round_number in sorted(set(modes.get("on", {})) & set(modes.get("off", {}))):
            on, off = (raw["runs"][modes[mode][round_number]] for mode in ("on", "off"))
            on_record, off_record = on["command_metadata"][0], off["command_metadata"][0]
            pair_issues = [mode + ": " + item for mode, run in (("on", on), ("off", off))
                           for item in run.get("composition_contract_issues", [])]
            if normalized_diagnostic_command(on_record["command"]) != normalized_diagnostic_command(off_record["command"]):
                pair_issues.append("Command argv differ beyond --samples output path")
            if {k: v for k, v in on_record.get("environment", {}).items() if k != contract["optout"]} != \
                    {k: v for k, v in off_record.get("environment", {}).items() if k != contract["optout"]}:
                pair_issues.append("Environment differs beyond composition optout")
            source_on = on_record.get("source_content_revision", raw["command_metadata"].get("source_content_revision"))
            source_off = off_record.get("source_content_revision", raw["command_metadata"].get("source_content_revision"))
            if not source_on or source_on != source_off:
                pair_issues.append("On/off source revision is missing or differs")
            if on["samples"]["measured_row_indices"] != off["samples"]["measured_row_indices"]:
                pair_issues.append("On/off measured-row selections differ")
            pair = {"group": label, "round": round_number,
                    "on": modes["on"][round_number], "off": modes["off"][round_number],
                    "comparability_issues": pair_issues, "complete_and_comparable": not pair_issues}
            for category in categories:
                pair[category] = {key: difference(off[category][key], on[category][key])
                                  for key in sorted(set(on[category]) & set(off[category]))}
            pairs.append(pair)
            group_issues.extend("round " + str(round_number) + ": " + issue for issue in pair_issues)
        comparison = {category: {} for category in categories}
        if set(summaries[label]) == {"on", "off"}:
            on, off = summaries[label]["on"], summaries[label]["off"]
            for category in categories:
                comparison[category] = {key: difference(off[category][key], on[category][key])
                                       for key in sorted(set(on[category]) & set(off[category]))}
        comparison.update({"comparability_issues": group_issues, "complete_and_comparable": not group_issues})
        comparisons[label] = comparison
        issues.extend(label + ": " + issue for issue in group_issues)
    processes = len(expected_labels) * contract["rounds"] * 2
    return {"metadata_and_raw_trace": raw, "contract": contract, "pairing_status": "configured_composition_contract" if contract["configured"] else "composition_contract_observation",
            "contract_observation_passed": not issues,
            "group_summaries": summaries, "on_off_comparisons": comparisons, "paired_processes": pairs,
            "pairing_issues": issues, "complete_and_comparable": not issues, "unique_counts": counts,
            "expected_unique_counts": {"processes": processes,
                                       "measured_frames": len(contract["variants"]) * contract["rounds"] * 2 * sum(profile["frames"] for profile in contract["profiles"].values()),
                                       "warmup_frames": len(contract["variants"]) * contract["rounds"] * 2 * sum(profile["warmup"] for profile in contract["profiles"].values())}}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--pair", nargs=3, action="append", default=[], metavar=("NAME", "BEFORE", "AFTER"))
    parser.add_argument("--diagnostic", type=Path, action="append", default=[], help="Raw diagnostic directory with commands.json and CSV/log files")
    parser.add_argument("--diagnostic-contract", type=Path, help="Explicit trace/proof policy JSON; without it raw diagnostics remain pending")
    parser.add_argument("--trace-helper", type=Path, default=Path("/tmp/coin-render-composition-copy-diagnostics.py"))
    parser.add_argument("--campaign-commands", type=Path, help="Optional preserved orchestration commands JSON")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if not args.pair and not args.diagnostic:
        parser.error("Supply at least one --pair or --diagnostic")
    if len({pair[0] for pair in args.pair}) != len(args.pair):
        parser.error("Campaign pair names must be unique")
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
    contract = None
    if args.diagnostic_contract:
        contract = diagnostic_contract_check(json.loads(args.diagnostic_contract.read_text()))
        if output == args.diagnostic_contract.resolve():
            parser.error("Output would overwrite diagnostic contract")
    report = {"schema_version": 1, "protocol": {
        "sample_selection": "CSV warmup=0/false only; all raw rows retained",
        "summary": "median of per-process statistics, not pooled frames",
        "percentiles": "nearest rank per process; short samples do not establish tail latency",
        "difference": "after minus before; positive milliseconds/percent means slower",
        "diagnostic_difference": "on minus off (off is the before/reference value)",
        "phase_alignment": "scope/key cardinality must equal CSV row count; no guessed last-three slice",
        "phase_overlap": "nested phase times are not additive",
        "shared_control": "CoinGL counted once only after metadata and CSV/log SHA256 equality",
        "diagnostic_pairing": "configured contract" if contract else "pending composition trace contract; raw data retained"}, "campaigns": {}, "diagnostics": {}}
    for name, before, after in args.pair:
        report["campaigns"][name] = compare_campaigns(helper, name, Path(before), Path(after))
    for directory in args.diagnostic:
        key = str(directory.resolve())
        if key in report["diagnostics"]:
            parser.error(f"Repeated diagnostic input: {directory}")
        report["diagnostics"][key] = read_diagnostic(helper, directory, contract)
    report["unique_counts"] = {
        key: sum(campaign["unique_counts"][key] for campaign in report["campaigns"].values())
        for key in ("processes", "measured_frames", "warmup_frames")}
    if args.campaign_commands:
        root = args.campaign_commands.resolve().parent
        data, metadata = helper.input_file(args.campaign_commands.resolve(), root)
        report["campaign_commands"] = {"input": metadata, "metadata": json.loads(data)}
        expected = report["campaign_commands"]["metadata"].get("expected_unique_counts")
        report["campaign_counts_match_expected"] = report["unique_counts"] == expected
    if args.diagnostic_contract:
        data, metadata = helper.input_file(args.diagnostic_contract.resolve(), args.diagnostic_contract.resolve().parent)
        report["diagnostic_contract_input"] = {"input": metadata, "metadata": contract}
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
