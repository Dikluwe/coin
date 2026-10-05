#!/usr/bin/env python3
"""Run/analyze material-resource ablation; execute launches GPU only when invoked.

execute and dry-run accept --suite primary|dimensional|all. The primary matrix
has 42 processes/294 measured frames/126 warmups; the dimensional supplement
has 8/56/24 and is summarized separately. Only optimization optout differs
within each pair; COIN_RENDER_TRACE_PHASES=1 and GPU timestamps are disabled.

analyze reads preserved inputs only, using stderr Action markers before CSV
warmup selection. It never runs a benchmark, build, Cargo, GPU, or Git command.
Library APIs: plan(args), samples(path), parse_trace(path), analyze(directory).
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
OPTOUT = "COIN_WGPU_DISABLE_MATERIAL_RESOURCE_REUSE"
SCOPES = ("rust_material_resources", "rust_instances", "rust_material_snapshot")
METRICS = ("update_ms", "render_ms", "publication_ms", "total_ms")
COUNTS = ("processes", "measured_frames", "warmup_frames")
MATRIX = {
    "primary": {"original": ("materials-10", "transforms-10", "geometry-10"),
                "large40001": ("materials-10", "materials-100", "transforms-10", "static")},
    "dimensional": {"slots257": ("materials-10", "transforms-10"),
                    "slots4097": ("materials-10", "transforms-10")}}
EXPECTED = {"primary": dict(processes=42, measured_frames=294, warmup_frames=126),
            "dimensional": dict(processes=8, measured_frames=56, warmup_frames=24)}
SCENE_ARGS = {"original": "original_scene", "large40001": "large_scene", "slots257": "scene_257", "slots4097": "scene_4097"}
NUMBER = re.compile(r"^[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?$")
INTEGER = re.compile(r"^[+-]?\d+$")
PHASE = re.compile(r"^COIN_RENDER_PHASE\s+(\S+)(?:\s+(.*))?$")


def require(condition, message):
    if not condition:
        raise ValueError(message)


def sha(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def json_read(path):
    return json.loads(Path(path).read_text(), parse_constant=lambda value:
                      (_ for _ in ()).throw(ValueError("Nonfinite JSON: " + value)))


def json_write(path, value):
    Path(path).write_text(json.dumps(value, indent=2, ensure_ascii=False, allow_nan=False) + "\n")


def finite(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value)


def numeric(value):
    if INTEGER.fullmatch(value):
        return int(value)
    if NUMBER.fullmatch(value) and math.isfinite(float(value)):
        return float(value)
    return value


def stats(values):
    require(values and all(finite(value) for value in values), "Statistics require finite observations")
    ordered = sorted(values)
    return dict(median_ms=statistics.median(ordered), min_ms=ordered[0], max_ms=ordered[-1],
                p95_ms=ordered[math.ceil(len(ordered) * .95) - 1], p99_ms=ordered[math.ceil(len(ordered) * .99) - 1],
                over_16_67=sum(value > 1000 / 60 for value in values), over_33_33=sum(value > 1000 / 30 for value in values))


def input_record(path, root):
    return dict(path=str(path.resolve()), relative_path=str(path.relative_to(root)), bytes=path.stat().st_size, sha256=sha(path))


def samples(path):
    """Keep all rows; validate the actual 3-warmup/7-measured CSV selection."""
    with Path(path).open(newline="") as stream:
        reader = csv.DictReader(stream)
        columns, rows = reader.fieldnames or [], list(reader)
    measured, warmup, issues = [], [], []
    for index, row in enumerate(rows):
        flag = str(row.get("warmup", "")).strip().lower()
        if flag in ("0", "false"):
            measured.append(index)
        elif flag in ("1", "true"):
            warmup.append(index)
        else:
            issues.append(f"Unknown warmup flag at row {index}: {flag!r}")
    if len(rows) != 10 or measured != list(range(3, 10)) or warmup != list(range(3)):
        issues.append("CSV must contain exactly 3 warmups followed by 7 measured frames")
    if [row.get("frame_index") for row in rows] != [str(index) for index in range(-3, 7)]:
        issues.append("CSV frame indices must be -3..6")
    values, summaries = {}, {}
    if not issues:
        for metric in METRICS:
            converted = [numeric(row.get(metric, row.get("render_present_ms", "") if metric == "render_ms" else "")) for row in rows]
            if not all(finite(value) and value >= 0 for value in converted):
                issues.append("Nonfinite/missing/negative CSV metric: " + metric)
                continue
            values[metric] = converted
            summaries[metric] = stats([converted[index] for index in measured])
    return dict(columns=columns, rows=rows, row_count=len(rows), measured_row_indices=measured,
                warmup_row_indices=warmup, measured_frames=len(measured), warmup_frames=len(warmup),
                values=values, stats=summaries, issues=issues, selection_valid=not issues)


def parse_trace(path):
    """Preserve stderr chronology. Unknown tokens stay visible, never guessed."""
    events, scopes = [], {}
    for line_number, line in enumerate(Path(path).read_text(errors="replace").splitlines(), 1):
        match = PHASE.match(line)
        if not match:
            continue
        scope, body = match.groups()
        fields, raw_fields, unparsed = {}, {}, []
        for token in (body or "").split():
            if "=" not in token:
                unparsed.append(token)
                continue
            key, value = token.split("=", 1)
            if not key or key in fields:
                unparsed.append(token)
                continue
            fields[key], raw_fields[key] = numeric(value), value
        event = dict(log_line_number=line_number, scope=scope, fields=fields, raw_fields=raw_fields,
                     raw_line=line, unparsed_tokens=unparsed)
        events.append(event)
        scopes.setdefault(scope, []).append(event)
    return dict(events=events, scopes=scopes)


def log_scalars(text):
    """Match the public animation runner's first/RSS/throughput scalar fields."""
    result = {}
    first = re.search(r"^\S+_first_frame_ms=([\d.eE+-]+)", text, re.M)
    if first:
        result["first_ms"] = numeric(first[1])
    detail = re.search(r"^(?:\S+_first_detail|window_first_frame_detail) (.+)$", text, re.M)
    if detail:
        fields = {key: numeric(value) for key, value in re.findall(r"(\w+)=([\d.eE+-]+)(?:\s|$)", detail[1])}
        for output, options in (("first_total_ms", ("total_with_update_ms", "total_ms")),
                                ("result_since_main_ms", ("result_since_main_ms",))):
            for option in options:
                if option in fields:
                    result[output] = fields[option]
                    break
    rss = re.search(r"benchmark_peak_rss_kib=(\d+)", text)
    if rss:
        result["peak_rss_kib"] = int(rss[1])
    throughput = re.search(r"^\S+_throughput frames=\d+ total_ms=([\d.eE+-]+) fps=([\d.eE+-]+)", text, re.M)
    if throughput:
        result["wall_total_ms"], result["throughput_fps"] = map(float, throughput.groups())
    checksum = re.search(r"^(?:gl_)?rgba_fnv64=(\S+)", text, re.M)
    if checksum:
        result["final_rgba_checksum"] = checksum[1]
    result["animation_metadata"] = [line for line in text.splitlines() if line.startswith("animation") or "selection_digest=" in line]
    return result


def validate_resource_row(row, mode):
    issues = []
    payload = {scope: row["scopes"][scope][0]["fields"] if len(row["scopes"][scope]) == 1 else None for scope in SCOPES}
    material, instance, snapshot = (payload[scope] for scope in SCOPES)
    if material is None:
        return ["Expected exactly one material-resource event before Action"]
    def check(condition, message):
        if not condition:
            issues.append(message)
    for scope, fields in payload.items():
        if fields is None:
            continue
        check(all(finite(value) and value >= 0 for value in fields.values()), scope + " has nonnumeric/nonfinite/negative measurements")
        check(not row["scopes"][scope][0]["unparsed_tokens"], scope + " has unparsed tokens")
    if issues:
        return issues
    required = ("count", "source_bytes", "gpu_bytes", "packed_count", "packed_bytes", "payload_compare_ms", "pack_ms", "resource_ms",
                "instance_hit", "material_hit", "material_buffer_hit", "instance_allocations", "material_allocations",
                "instance_uploaded_bytes", "material_uploaded_bytes", "reuse_enabled", "attempt")
    check(all(key in material for key in required), "Material-resource fields missing")
    if not all(key in material for key in required):
        return issues
    count = material["count"]
    check(type(count) is int and count >= 0, "Material count is not a nonnegative integer")
    if type(count) is not int or count < 0:
        return issues
    check(material["source_bytes"] == count * 72, "Source material stride must be 72 bytes")
    check(material["gpu_bytes"] == max(count, 1) * 80, "GPU material stride/sentinel must be 80 bytes")
    check(material["packed_bytes"] == material["packed_count"] * 80, "Packed material byte count inconsistent")
    check(material["reuse_enabled"] == int(mode == "on") and material["attempt"] == 1, "Optout/attempt marker inconsistent")
    for key in ("instance_hit", "material_hit", "material_buffer_hit", "instance_allocations", "material_allocations"):
        check(material[key] in (0, 1), "Nonboolean resource counter: " + key)
    expected_packed = 0 if mode == "on" and material["material_buffer_hit"] else count
    check(material["packed_count"] == expected_packed, "Lazy/eager material pack count inconsistent")
    check(material["material_allocations"] == int(not material["material_buffer_hit"]), "Material allocation count inconsistent")
    check(material["material_uploaded_bytes"] == (0 if material["material_buffer_hit"] else material["gpu_bytes"]), "Material upload count inconsistent")
    if instance is not None:
        required_instance = ("count", "ranges", "geometry_bytes", "instance_bytes", "material_bytes", "geometry_hit", "payload_hit",
                             "instance_hit", "material_hit", "instance_uploaded_bytes", "material_uploaded_bytes", "uploaded_bytes")
        check(all(key in instance for key in required_instance), "Instance fields missing")
        if all(key in instance for key in required_instance):
            check(instance["instance_bytes"] == instance["count"] * 144, "Instance stride must be 144 bytes")
            check(instance["material_bytes"] == material["gpu_bytes"], "Instance/material GPU lengths differ")
            check(all(instance[key] == material[key] for key in ("instance_hit", "material_hit", "instance_uploaded_bytes", "material_uploaded_bytes")),
                  "Resource/instance scopes disagree")
            check(instance["payload_hit"] == int(bool(instance["instance_hit"] and instance["material_hit"])), "Coupled payload counter inconsistent")
            check(instance["instance_uploaded_bytes"] == (0 if instance["instance_hit"] else instance["instance_bytes"]), "Instance upload count inconsistent")
            check(material["instance_allocations"] == int(not instance["instance_hit"]), "Instance allocation count inconsistent")
            expected_uploaded = (0 if instance["geometry_hit"] else instance["geometry_bytes"]) + instance["instance_uploaded_bytes"] + instance["material_uploaded_bytes"]
            check(instance["uploaded_bytes"] == expected_uploaded, "Aggregate instance upload bytes inconsistent")
            if mode == "off":
                check(instance["instance_hit"] == instance["material_hit"], "Optout must retain coupled instance/material hits")
        check(snapshot is not None, "Instanced frame needs exactly one successful CPU snapshot event")
    else:
        check(material["instance_allocations"] == 0 and material["instance_uploaded_bytes"] == 0, "Absent instance scope disagrees with resource counters")
    if snapshot is not None:
        keys = ("source_bytes", "material_copied_bytes", "instance_copied_bytes", "geometry_reused", "snapshot_ms", "attempt")
        check(all(key in snapshot for key in keys), "Snapshot fields missing")
        if all(key in snapshot for key in keys):
            check(snapshot["source_bytes"] == material["source_bytes"] and snapshot["attempt"] == 1 and snapshot["geometry_reused"] in (0, 1),
                  "Snapshot source/attempt/geometry marker inconsistent")
            check(snapshot["material_copied_bytes"] == (0 if snapshot["geometry_reused"] else snapshot["source_bytes"]), "Snapshot material copy count inconsistent")
            if instance is not None and "instance_bytes" in instance:
                check(snapshot["instance_copied_bytes"] == (0 if snapshot["geometry_reused"] else instance["instance_bytes"]), "Snapshot instance copy count inconsistent")
    return issues


def align(trace, sample, mode):
    actions = trace["scopes"].get("action", [])
    issues, rows, assigned = [], [], []
    if len(actions) != sample["row_count"]:
        issues.append("Action cardinality differs from CSV; no mapping inferred")
        return dict(passed=False, issues=issues, rows=[], action_count=len(actions), csv_row_count=sample["row_count"], aligned_series={})
    previous = 0
    for index, action in enumerate(actions):
        events = [event for event in trace["events"] if previous < event["log_line_number"] < action["log_line_number"]]
        previous = action["log_line_number"]
        assigned.extend(event["log_line_number"] for event in events if event["scope"] in SCOPES)
        row = dict(csv_row_index=index, csv_frame_index=sample["rows"][index].get("frame_index"),
                   warmup=index in sample["warmup_row_indices"], action=action,
                   scopes={scope: [event for event in events if event["scope"] == scope] for scope in SCOPES},
                   other_scopes={scope: [event for event in events if event["scope"] == scope] for scope in
                                 sorted({event["scope"] for event in events} - set(SCOPES))})
        row["scope_presence"] = {scope: "absent" if not records else "single" if len(records) == 1 else "multiple"
                                 for scope, records in row["scopes"].items()}
        row["issues"] = []
        if any(len(records) > 1 for records in row["scopes"].values()):
            row["issues"].append("Multiple material/instance/snapshot events before one Action")
        row["issues"].extend(validate_resource_row(row, mode))
        if action["unparsed_tokens"]:
            row["issues"].append("Action marker has unparsed tokens")
        issues.extend(f"CSV row {index}: " + issue for issue in row["issues"])
        rows.append(row)
    wanted = [event["log_line_number"] for event in trace["events"] if event["scope"] in SCOPES]
    if sorted(assigned) != sorted(wanted) or len(set(assigned)) != len(assigned):
        issues.append("Material scope event left after the final Action or assigned more than once")
    series = {}
    if not issues and sample["selection_valid"]:
        for scope in SCOPES:
            records = [row["scopes"][scope] for row in rows]
            keys = set().union(*(set(events[0]["fields"]) for events in records if len(events) == 1)) if any(records) else set()
            for key in sorted(keys):
                values = [events[0]["fields"].get(key) if len(events) == 1 else None for events in records]
                selected = [values[index] for index in sample["measured_row_indices"]]
                item = dict(scope=scope, key=key, values=values, measured_values=selected, selected_row_indices=sample["measured_row_indices"],
                            complete_measured_presence=all(value is not None for value in selected))
                if selected and all(finite(value) for value in selected):
                    item["measured_median"] = statistics.median(selected)
                else:
                    item["statistics_limitation"] = "Scope absent or field missing on measured frames; no median inferred"
                series[scope + "." + key] = item
        # Composition timings are retained only where one event exists on every
        # measured frame. Multiple/general schedules remain explicit raw events.
        other_scopes = {scope for row in rows for scope in row["other_scopes"] if "composition" in scope}
        other_scopes.add("action")
        for scope in sorted(other_scopes):
            records = [[row["action"]] if scope == "action" else row["other_scopes"].get(scope, []) for row in rows]
            keys = set().union(*(set(events[0]["fields"]) for events in records if len(events) == 1)) if any(records) else set()
            for key in sorted(keys):
                values = [events[0]["fields"].get(key) if len(events) == 1 else None for events in records]
                selected = [values[index] for index in sample["measured_row_indices"]]
                item = dict(scope=scope, key=key, values=values, measured_values=selected, selected_row_indices=sample["measured_row_indices"])
                if selected and all(finite(value) for value in selected):
                    item["measured_median"] = statistics.median(selected)
                else:
                    item["statistics_limitation"] = "Scope absent/multiple or nonnumeric field; raw events retained, no median inferred"
                series[scope + "." + key] = item
    return dict(passed=not issues, issues=issues, rows=rows, action_count=len(actions), csv_row_count=sample["row_count"], aligned_series=series)


def plan(args):
    commands = []
    suites = tuple(MATRIX) if args.suite == "all" else (args.suite,)
    for suite in suites:
        rounds = 3 if suite == "primary" else 1
        profiles = [(scene, case) for scene, cases in MATRIX[suite].items() for case in cases]
        for round_index in range(rounds):
            ordered = profiles[round_index:] + profiles[:round_index]
            if round_index % 2:
                ordered.reverse()
            for index, (scene, case) in enumerate(ordered):
                scene_path = getattr(args, SCENE_ARGS[scene])
                require(scene_path is not None, "Missing scene argument for " + scene)
                animation, percent = ("static", 10) if case == "static" else (case.rsplit("-", 1)[0], int(case.rsplit("-", 1)[1]))
                for mode in (("on", "off") if (round_index + index) % 2 == 0 else ("off", "on")):
                    stem = f"{suite}-{scene}-{case}-{mode}-{round_index+1}"
                    command = [str(args.wgpu_build / "bin/coin_render_gl_benchmark"), "--backend", "wgpu", "--scene", str(scene_path),
                               "--animation", animation, "--animated-percent", str(percent), "--transparency", "object", "--size", "1024",
                               "--warmup", "3", "--frames", "7", "--samples-output", str(args.output / (stem + ".csv"))]
                    commands.append(dict(stem=stem, campaign=suite, scene=scene, case=case, variant="wgpu-vulkan", mode=mode,
                                         round=round_index+1, command=command, source_content_revision=args.source_content_revision,
                                         csv=stem+".csv", stdout=stem+".stdout.log", stderr=stem+".stderr.log", log=stem+".log"))
    require(len(commands) == sum(EXPECTED[suite]["processes"] for suite in suites), "Plan counts differ")
    return commands


def difference(off, on):
    return dict(off=off, on=on, delta=on-off, change_percent=(on/off-1)*100 if off else None)


def normalized_command(command):
    result = list(command)
    require(result.count("--samples-output") == 1, "Need one samples-output argument")
    result[result.index("--samples-output")+1] = "<samples>"
    return result


def relative_file(root, name):
    require(isinstance(name, str) and not Path(name).is_absolute(), "Artifact path must be relative")
    path = (root / name).resolve()
    require(path.is_relative_to(root), "Artifact path outside diagnostic directory")
    return path


def analyze(directory):
    """Recompute recorded CSV/trace evidence; preserve incomparability explicitly."""
    root = Path(directory).resolve()
    manifest_path = root / "commands.json"
    manifest = json_read(manifest_path)
    runs, files, issues = {}, {"commands.json": input_record(manifest_path, root)}, []
    require(manifest.get("parameters", {}).get("optout") == OPTOUT and manifest.get("parameters", {}).get("trace") is True and
            manifest.get("parameters", {}).get("gpu_timestamps") is False, "Unexpected diagnostic optout/trace contract")
    suites = manifest.get("parameters", {}).get("campaigns", [])
    require(suites and set(suites) <= set(MATRIX), "Unexpected campaign selection")
    require(len(set(suites)) == len(suites), "Duplicate campaign selection")
    for key in ("binary_hashes", "source_files_sha256", "scenes_sha256"):
        values = manifest.get(key, {})
        if not values or not all(isinstance(value, str) and re.fullmatch("[0-9a-f]{64}", value) for value in values.values()):
            issues.append("Missing/invalid input hash map: " + key)
    if re.fullmatch("[0-9a-f]{40}", str(manifest.get("source_content_revision", ""))) is None:
        issues.append("Missing/invalid compiled source revision")
    if manifest.get("complete") is not True:
        issues.append("Execution manifest is incomplete")
    if manifest.get("binaries_unchanged") is not True or manifest.get("binary_hashes") != manifest.get("binary_hashes_post_campaign"):
        issues.append("Binary hashes were not proven unchanged")
    if manifest.get("source_files_sha256") != manifest.get("source_files_sha256_post_campaign"):
        issues.append("Rendering source files changed during diagnostic execution")
    if manifest.get("scenes_sha256") != manifest.get("scenes_sha256_post_campaign"):
        issues.append("Scene hashes changed during diagnostic execution")
    for record in manifest.get("commands", []):
        stem = record["stem"]
        require(stem not in runs, "Duplicate command identity: " + stem)
        run_issues, inputs = [], {}
        run = dict(command_metadata=record, issues=run_issues, input_files=inputs)
        runs[stem] = run
        if record.get("campaign") not in suites:
            run_issues.append("Command campaign outside selected suites")
            issues.append(stem + ": command campaign outside selected suites")
        for kind in ("csv", "stdout", "stderr", "log"):
            path = relative_file(root, record.get(kind))
            if not path.is_file():
                run_issues.append("Missing preserved " + kind)
                continue
            inputs[kind] = input_record(path, root)
            files[inputs[kind]["relative_path"]] = inputs[kind]
            if record.get("artifact_hashes", {}).get(kind) != inputs[kind]["sha256"]:
                run_issues.append("Recorded artifact SHA256 differs: " + kind)
        if len(inputs) != 4:
            continue
        stdout = relative_file(root, record["stdout"]).read_text(errors="replace")
        stderr = relative_file(root, record["stderr"]).read_text(errors="replace")
        combined = relative_file(root, record["log"]).read_text(errors="replace")
        if combined != stdout + stderr:
            run_issues.append("Combined log differs from exact stdout+stderr preservation")
        run["samples"] = samples(relative_file(root, record["csv"]))
        run_issues.extend(run["samples"]["issues"])
        run["log_scalars"] = log_scalars(combined)
        if run["log_scalars"] != record.get("frame_scalars"):
            run_issues.append("Recorded frame scalars differ from preserved logs")
        run["raw_trace"] = parse_trace(relative_file(root, record["stderr"]))
        if record.get("exit_code") != 0 or record.get("timed_out"):
            run_issues.append("Benchmark failed or timed out")
        if record.get("source_content_revision") != manifest.get("source_content_revision"):
            run_issues.append("Command source differs from manifest")
        if record.get("scene_sha256") != manifest.get("scenes_sha256", {}).get(record.get("scene")):
            run_issues.append("Command scene SHA differs from manifest")
        environment = record.get("environment", {})
        if environment.get("COIN_RENDER_TRACE_PHASES") != "1" or environment.get("WGPU_BACKEND") != "vulkan" or \
                "COIN_WGPU_GPU_TIMESTAMPS" in environment or "COIN_WGPU_TRACE_PHASES" in environment:
            run_issues.append("Environment differs from explicit Vulkan/trace/no-GPU-timestamp protocol")
        if environment.get(OPTOUT) != ("1" if record.get("mode") == "off" else None):
            run_issues.append("Effective optout differs from mode")
        if record.get("adapter_verified_nvidia") is not True or "NVIDIA" not in combined:
            run_issues.append("NVIDIA adapter identity missing")
        for option, value in (("--backend", "wgpu"), ("--transparency", "object"), ("--size", "1024"), ("--warmup", "3"), ("--frames", "7")):
            command = record["command"]
            if command.count(option) != 1 or command[command.index(option)+1] != value:
                run_issues.append("Command protocol differs in " + option)
        public_stats = record.get("public_runner_csv_stats", {})
        for metric, fields in public_stats.items():
            for key, recorded in fields.items():
                actual = run["samples"]["stats"].get(metric, {}).get(key)
                if actual is None or not finite(recorded) or not math.isclose(actual, recorded, rel_tol=1e-10, abs_tol=1e-10):
                    run_issues.append("Public runner CSV statistics differ: " + metric + "." + key)
        run["alignment"] = align(run["raw_trace"], run["samples"], record["mode"])
        run_issues.extend(run["alignment"]["issues"])
        run["phase_measured_medians_ms"] = {key: item["measured_median"] for key, item in run["alignment"]["aligned_series"].items()
                                              if key.endswith("_ms") and "measured_median" in item}
        run["counter_measured_medians"] = {key: item["measured_median"] for key, item in run["alignment"]["aligned_series"].items()
                                            if not key.endswith("_ms") and "measured_median" in item}
        run["csv_measured_medians_ms"] = {metric: fields["median_ms"] for metric, fields in run["samples"]["stats"].items()}
        run["complete_and_comparable"] = not run_issues
    campaigns = {}
    categories = ("csv_measured_medians_ms", "phase_measured_medians_ms", "counter_measured_medians", "log_scalars")
    for campaign_name in suites:
        actual = [run for run in runs.values() if run["command_metadata"].get("campaign") == campaign_name]
        rounds = 3 if campaign_name == "primary" else 1
        expected_identities = {(scene, case, mode, round_number) for scene, cases in MATRIX[campaign_name].items()
                               for case in cases for mode in ("off", "on") for round_number in range(1, rounds+1)}
        identities = [(item["command_metadata"]["scene"], item["command_metadata"]["case"], item["command_metadata"]["mode"],
                       item["command_metadata"]["round"]) for item in actual]
        campaign_issues = [stem + ": " + issue for stem, run in runs.items() if run["command_metadata"].get("campaign") == campaign_name for issue in run["issues"]]
        if len(set(identities)) != len(identities) or set(identities) != expected_identities:
            campaign_issues.append("Campaign scene/case/mode/round coverage differs")
        counts = dict(processes=len(actual), measured_frames=sum(run.get("samples", {}).get("measured_frames", 0) for run in actual),
                      warmup_frames=sum(run.get("samples", {}).get("warmup_frames", 0) for run in actual))
        if counts != EXPECTED[campaign_name] or manifest.get("expected_counts", {}).get(campaign_name) != EXPECTED[campaign_name]:
            campaign_issues.append("Actual/expected campaign process/frame counts differ")
        if counts != manifest.get("completed_counts", {}).get(campaign_name):
            campaign_issues.append("Recorded completed counts differ from actual CSV selection")
        groups, pairs, comparisons = {}, [], {}
        for scene, cases in MATRIX[campaign_name].items():
            for case in cases:
                label = scene + "|" + case
                group = groups.setdefault(label, {})
                for mode in ("off", "on"):
                    selected = sorted((run for run in actual if run["command_metadata"]["scene"] == scene and run["command_metadata"]["case"] == case
                                       and run["command_metadata"]["mode"] == mode), key=lambda run: run["command_metadata"]["round"])
                    result = dict(processes=len(selected), rounds=[run["command_metadata"]["round"] for run in selected],
                                  stems=[run["command_metadata"]["stem"] for run in selected])
                    for category in categories:
                        common = set.intersection(*(set(run.get(category, {})) for run in selected)) if selected else set()
                        values = {key: [run[category][key] for run in selected] for key in common
                                  if all(finite(run[category][key]) for run in selected)}
                        result[category] = {key: statistics.median(data) for key, data in values.items()}
                        result[category + "_process_values"] = values
                        result[category + "_process_ranges"] = {key: dict(minimum=min(data), maximum=max(data)) for key, data in values.items()}
                    group[mode] = result
                local_issues = []
                for round_number in range(1, rounds+1):
                    selected = {run["command_metadata"]["mode"]: run for run in actual if run["command_metadata"]["scene"] == scene
                                and run["command_metadata"]["case"] == case and run["command_metadata"]["round"] == round_number}
                    if set(selected) != {"on", "off"}:
                        continue
                    off, on = selected["off"], selected["on"]
                    a, b = off["command_metadata"], on["command_metadata"]
                    pair_issues = list(off["issues"])+list(on["issues"])
                    if normalized_command(a["command"]) != normalized_command(b["command"]):
                        pair_issues.append("Pair argv differs beyond sample path")
                    env_a, env_b = ({key: value for key, value in item.get("environment", {}).items() if key != OPTOUT} for item in (a,b))
                    if env_a != env_b or not a.get("environment_without_optout_sha256") or a.get("environment_without_optout_sha256") != b.get("environment_without_optout_sha256"):
                        pair_issues.append("Pair environment differs beyond optout")
                    for key in ("source_content_revision", "scene_sha256"):
                        if a.get(key) != b.get(key):
                            pair_issues.append("Pair source/scene differs: " + key)
                    for key in ("rust_material_resources.count", "rust_material_resources.source_bytes", "rust_material_resources.gpu_bytes"):
                        sa = off.get("alignment", {}).get("aligned_series", {}).get(key, {}).get("values")
                        sb = on.get("alignment", {}).get("aligned_series", {}).get(key, {}).get("values")
                        if sa is None or sa != sb:
                            pair_issues.append("Pair per-frame material dimensions differ: " + key)
                    pair = dict(scene=scene, case=case, round=round_number, off=a["stem"], on=b["stem"], issues=pair_issues, complete_and_comparable=not pair_issues)
                    for category in categories:
                        pair[category] = {key: difference(off[category][key], on[category][key]) for key in
                                          set(off.get(category, {})) & set(on.get(category, {})) if finite(off[category][key]) and finite(on[category][key])}
                    pairs.append(pair)
                    local_issues.extend(f"round {round_number}: " + issue for issue in pair_issues)
                comparison = {category: {key: difference(group["off"][category][key], group["on"][category][key]) for key in
                                         set(group["off"][category]) & set(group["on"][category])} for category in categories}
                comparison.update(issues=local_issues, complete_and_comparable=not local_issues and all(group[mode]["processes"] == rounds for mode in ("off", "on")))
                comparisons[label] = comparison
                campaign_issues.extend(label + ": " + issue for issue in local_issues)
        campaigns[campaign_name] = dict(unique_counts=counts, expected_counts=EXPECTED[campaign_name], group_summaries=groups,
                                       on_off_comparisons=comparisons, paired_processes=pairs, issues=campaign_issues, complete_and_comparable=not campaign_issues)
    all_issues = issues + [name + ": " + issue for name, campaign in campaigns.items() for issue in campaign["issues"]]
    return dict(schema_version=1, input_directory=str(root), command_metadata=manifest, input_files=files, runs=runs, campaigns=campaigns,
                complete_and_comparable=not all_issues, issues=all_issues, protocol={
                    "selection": "CSV warmup flags, after verified stderr events-before-next-Action mapping",
                    "summary": "median of per-process medians, never pooled frames; primary and dimensional supplement separate",
                    "difference": "on minus off; positive means slower", "scope_absence": "explicit absent/single/multiple per frame; never guessed",
                    "snapshot_ms": "whole snapshot including geometry, states, order and bound work; not isolated material memcpy",
                    "bytes": "source materials72, GPU materials80, instances144; copied/uploaded payload bytes, not allocator capacity",
                    "phases": "CPU attempts precede final submission success; phase medians overlap and are not additive",
                    "tail": "seven measured frames and N3/N1 are short descriptive samples, not latency-tail evidence"})


def execute(args):
    commands = plan(args)
    require(not args.output.exists(), "Use a new output directory")
    runner_path = args.repository / "scripts/coinrender/run_animation_benchmark.py"
    spec = importlib.util.spec_from_file_location("material_table_public_runner", runner_path)
    runner = importlib.util.module_from_spec(spec); spec.loader.exec_module(runner)
    suites = tuple(MATRIX) if args.suite == "all" else (args.suite,)
    scene_paths = {scene: getattr(args, SCENE_ARGS[scene]) for suite in suites for scene in MATRIX[suite]}
    binaries = {str(args.wgpu_build / relative): sha(args.wgpu_build / relative) for relative in
                ("bin/coin_render_gl_benchmark", "lib/libCoinRender.so", "lib/libCoin.so.80")}
    source_files = [args.repository / "src/rendering/coinwgpu/rust_bridge/src" / name for name in ("lib.rs", "instancing.rs")]
    source_hashes = {str(path): sha(path) for path in source_files}
    metadata = dict(source_content_revision=args.source_content_revision, source_revision_basis="owner supplied actual compiled content; file hashes checked for mutation",
                    source_files_sha256=source_hashes, scenes={name: str(path) for name, path in scene_paths.items()},
                    scenes_sha256={name: sha(path) for name,path in scene_paths.items()}, binary_hashes=binaries,
                    commands=[], expected_counts={suite: EXPECTED[suite] for suite in suites}, complete=False,
                    parameters=dict(campaigns=list(suites), variant="wgpu-vulkan", gpu="nvidia", optout=OPTOUT, trace=True,
                                    gpu_timestamps=False, warmup=3, frames=7, size=1024, transparency="object",
                                    rounds={suite: 3 if suite == "primary" else 1 for suite in suites}, matrix={suite: MATRIX[suite] for suite in suites},
                                    ordering="rotate scene/case profiles by round, reverse odd rounds, alternate on/off order"),
                    script_sha256=sha(Path(__file__)), runner_script_sha256=sha(runner_path), invocation=sys.argv)
    args.output.mkdir(parents=True)
    def save():
        json_write(args.output / "commands.json", metadata)
    save()
    for record in commands:
        environment = runner.environment(args.wgpu_build, "wgpu-vulkan", "nvidia")
        for key in (OPTOUT, "COIN_WGPU_TRACE_PHASES", "COIN_WGPU_GPU_TIMESTAMPS"):
            environment.pop(key, None)
        environment["COIN_RENDER_TRACE_PHASES"] = "1"
        if record["mode"] == "off":
            environment[OPTOUT] = "1"
        relevant = {key: value for key, value in environment.items() if key.startswith(("COIN_", "WGPU_", "__NV", "__GLX", "VK_", "LD_LIBRARY"))}
        normalized_env = {key: value for key,value in environment.items() if key != OPTOUT}
        record.update(environment=relevant, environment_without_optout_sha256=hashlib.sha256(json.dumps(normalized_env,sort_keys=True).encode()).hexdigest(),
                      scene_sha256=metadata["scenes_sha256"][record["scene"]], timeout_seconds=args.timeout,
                      timed_command=["/usr/bin/time", "-f", "benchmark_peak_rss_kib=%M", *record["command"]])
        metadata["commands"].append(record);save()
        print("START",record["stem"],flush=True)
        try:
            result = subprocess.run(record["timed_command"],cwd=args.repository,env=environment,capture_output=True,text=True,timeout=args.timeout)
            stdout,stderr=result.stdout,result.stderr
            record.update(exit_code=result.returncode,timed_out=False)
        except subprocess.TimeoutExpired as error:
            def text(value):
                return value.decode("utf-8",errors="replace") if isinstance(value,bytes) else (value or "")
            stdout,stderr=text(error.stdout),text(error.stderr)
            record.update(exit_code=None,timed_out=True)
        for kind, content in (("stdout",stdout),("stderr",stderr),("log",stdout+stderr)):
            (args.output / record[kind]).write_text(content)
        record["artifact_hashes"]={kind:sha(args.output / record[kind]) for kind in ("stdout","stderr","log")}
        if (args.output / record["csv"]).is_file():
            record["artifact_hashes"]["csv"]=sha(args.output / record["csv"])
        record["adapter_verified_nvidia"]="NVIDIA" in stdout+stderr
        save()
        require(record["exit_code"]==0 and not record["timed_out"],"Process failed/timed out; raw artifacts preserved: "+record["stem"])
        require(record["adapter_verified_nvidia"],"NVIDIA identity absent: "+record["stem"])
        selected = samples(args.output / record["csv"])
        record["sample_counts"] = {key: selected[key] for key in ("measured_frames", "warmup_frames", "row_count", "measured_row_indices", "warmup_row_indices")}
        record["sample_issues"] = selected["issues"]
        save()
        require(selected["selection_valid"], "Invalid actual CSV selection; raw artifacts preserved: " + record["stem"])
        record["public_runner_csv_stats"]=runner.csv_stats(args.output / record["csv"],7)
        record["frame_scalars"]=log_scalars(stdout+stderr);save()
        print("END",record["stem"],flush=True)
    metadata.update(binary_hashes_post_campaign={path:sha(Path(path)) for path in binaries},
                    source_files_sha256_post_campaign={str(path):sha(path) for path in source_files},
                    scenes_sha256_post_campaign={name:sha(path) for name,path in scene_paths.items()})
    metadata["binaries_unchanged"]=metadata["binary_hashes_post_campaign"]==binaries
    metadata["complete"]=metadata["binaries_unchanged"] and metadata["source_files_sha256_post_campaign"]==source_hashes and metadata["scenes_sha256_post_campaign"]==metadata["scenes_sha256"]
    metadata["completed_counts"]={suite:dict(processes=sum(record["campaign"]==suite for record in commands),
        measured_frames=sum(record["sample_counts"]["measured_frames"] for record in commands if record["campaign"] == suite),
        warmup_frames=sum(record["sample_counts"]["warmup_frames"] for record in commands if record["campaign"] == suite)) for suite in suites}
    save();require(metadata["complete"],"Binary/source/scene changed during campaign")
    print("Execution complete; run analyze separately for correlated frame summaries",flush=True)


def main():
    parser=argparse.ArgumentParser(description=__doc__,formatter_class=argparse.RawDescriptionHelpFormatter)
    sub=parser.add_subparsers(dest="mode",required=True)
    for name in ("execute","dry-run"):
        command=sub.add_parser(name)
        command.add_argument("--repository",type=Path,default=Path("/tmp/coin-render-first-frame"))
        command.add_argument("--wgpu-build",type=Path,default=Path("/tmp/coin-render-first-frame-wgpu"))
        command.add_argument("--original-scene",type=Path,default=Path("/tmp/coin-render-city-40000.iv"))
        command.add_argument("--large-scene",type=Path)
        command.add_argument("--scene-257",type=Path)
        command.add_argument("--scene-4097",type=Path)
        command.add_argument("--suite",choices=("primary","dimensional","all"),default="all")
        command.add_argument("--source-content-revision",required=True)
        command.add_argument("--output",type=Path,required=True)
        command.add_argument("--timeout",type=float,default=180)
    command=sub.add_parser("analyze")
    command.add_argument("--input",type=Path,required=True)
    command.add_argument("--output",type=Path,required=True)
    args=parser.parse_args()
    if args.mode=="analyze":
        output=args.output.resolve()
        require(not output.exists() and output.name!="commands.json", "Use a new analysis JSON output")
        result=analyze(args.input)
        output.parent.mkdir(parents=True,exist_ok=True);json_write(output,result)
        print("Wrote",output,"complete_and_comparable=",result["complete_and_comparable"])
        return 0 if result["complete_and_comparable"] else 1
    require(re.fullmatch("[0-9a-f]{40}",args.source_content_revision) is not None and args.timeout>0,"Supply full compiled source SHA and positive timeout")
    for key in ("repository","wgpu_build","original_scene","large_scene","scene_257","scene_4097","output"):
        value=getattr(args,key)
        if value is not None:
            setattr(args,key,value.resolve())
    commands=plan(args)
    if args.mode=="dry-run":
        suites=tuple(MATRIX) if args.suite=="all" else (args.suite,)
        print(json.dumps(dict(expected_counts={suite:EXPECTED[suite] for suite in suites},optout=OPTOUT,trace=True,gpu_timestamps=False,commands=commands),indent=2))
        return 0
    execute(args)
    return 0


if __name__=="__main__":
    sys.exit(main())
