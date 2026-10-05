#!/usr/bin/env python3
"""Run the quiet material-table study serially, or rederive it from raw data.

Six scene/case groups, three rounds and five roles produce 90 processes,
2,925 measured frames and 825 warmups. The high-slot static control uses
30 warmups/120 measured frames; dynamic cases use 5/15. CoinGL and both BGFX APIs are shared
controls: each runs once per group/round. Only `run` without --dry-run launches
benchmarks. Revisions are owner-supplied; no Git/build command is invoked.
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
BASELINE = "96e5ed80fa3ab3c9016cf10e6bbfc9b638335a33"
COINGL = "4d63bb993022ee8d40802558b0871a4803002b8d"
SCENE_HASHES = {
    "original": "bb9ccc612cd38ffb749ee599d9350a80974649eab5bf477d2f344538a42b2576",
    "high": "e85dedc3ae203ff45507c5eea78b07655bbf6789678cb94c4fb84cc6cacd5cd7",
}
SCENARIOS = (("original", "materials-10"), ("original", "transforms-10"),
             ("high", "materials-10"), ("high", "materials-100"),
             ("high", "transforms-10"), ("high", "static"))
ROLES = ("coingl-control", "bgfx-vulkan-control", "bgfx-opengl-control", "wgpu-before", "wgpu-after")
VARIANTS = {"coingl-control": "coingl", "bgfx-vulkan-control": "bgfx-vulkan",
            "bgfx-opengl-control": "bgfx-opengl", "wgpu-before": "wgpu-vulkan", "wgpu-after": "wgpu-vulkan"}
BACKENDS = {"coingl": "gl", "bgfx-vulkan": "bgfx", "bgfx-opengl": "bgfx", "wgpu-vulkan": "wgpu"}
ROUNDS, WARMUP, FRAMES = 3, 5, 15
EXPECTED = {"processes": 90, "measured_frames": 2925, "warmup_frames": 825}
METRICS = ("update_ms", "render_ms", "publication_ms", "total_ms")
ARTIFACTS = ("bin/coin_render_gl_benchmark", "bin/coin_render_window_benchmark", "lib/libCoin.so.80.0.10", "lib/libCoinRender.so")
TRACE_KEYS = ("COIN_RENDER_TRACE_PHASES", "COIN_WGPU_TRACE_PHASES", "COIN_WGPU_GPU_TIMESTAMPS", "COIN_DEBUG_GLGLUE")


def sha(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2, allow_nan=False) + "\n", encoding="utf-8")


def load_helper(path):
    spec = importlib.util.spec_from_file_location("material_study_environment", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def statistics_ms(values):
    ordered = sorted(values)
    if not ordered or not all(math.isfinite(value) and value >= 0 for value in ordered):
        raise ValueError("Timing samples must be finite and nonnegative")
    return {"median_ms": statistics.median(ordered), "min_ms": ordered[0], "max_ms": ordered[-1],
            "p95_ms": ordered[math.ceil(len(ordered) * .95) - 1], "p99_ms": ordered[math.ceil(len(ordered) * .99) - 1],
            "over_16_67": sum(value > 1000 / 60 for value in values), "over_33_33": sum(value > 1000 / 30 for value in values)}


def sample_counts(scene_key, case):
    return (30, 120) if (scene_key, case) == ("high", "static") else (WARMUP, FRAMES)


def read_samples(path, warmup_count=WARMUP, frame_count=FRAMES):
    with Path(path).open(newline="") as stream:
        rows = list(csv.DictReader(stream))
    measured, warmup = [], []
    for index, row in enumerate(rows):
        flag = row.get("warmup", "").strip().lower()
        if flag in ("0", "false"):
            measured.append(index)
        elif flag in ("1", "true"):
            warmup.append(index)
        else:
            raise ValueError("Unknown warmup flag at row " + str(index))
        for metric in METRICS:
            value = float(row[metric])
            if not math.isfinite(value) or value < 0:
                raise ValueError("Nonfinite/negative CSV metric at row " + str(index) + ": " + metric)
    if warmup != list(range(warmup_count)) or measured != list(range(warmup_count, warmup_count + frame_count)):
        raise ValueError("CSV warmup/measured row counts or ordering differ from the command protocol")
    if [row.get("frame_index") for row in rows] != [str(index) for index in range(-warmup_count, frame_count)]:
        raise ValueError("CSV logical frame indices differ from the command protocol")
    path = Path(path)
    return {"rows": rows, "measured_row_indices": measured, "warmup_row_indices": warmup,
            "measured_frames": len(measured), "warmup_frames": len(warmup),
            "stats": {metric: statistics_ms([float(rows[index][metric]) for index in measured]) for metric in METRICS},
            "csv_sha256": sha(path), "csv_bytes": path.stat().st_size}


def read_log(log, variant, frame_count=FRAMES):
    adapter = re.search(r"^adapter=(.+?) vendor_id=(\S+) device_id=(\S+) backend=(\S+)", log, re.M)
    rss = re.findall(r"^benchmark_peak_rss_kib=(\d+)$", log, re.M)
    if adapter is None or len(rss) != 1:
        raise ValueError("Missing adapter report or single process peak RSS")
    observed_nvidia = "NVIDIA" in adapter[1] and adapter[2].lower() == "0x10de"
    required = variant != "coingl"
    if required and not observed_nvidia:
        raise ValueError("Experimental renderer did not report the NVIDIA adapter")
    backend_expected = BACKENDS[variant]
    if adapter[4] != backend_expected:
        raise ValueError("Adapter backend differs from the planned renderer")
    result = {"adapter": adapter[1], "vendor_id": adapter[2], "device_id": adapter[3],
              "adapter_verified_nvidia": observed_nvidia, "adapter_verification_required": required,
              "adapter_verification_basis": "benchmark adapter/vendor report" if required else "CoinGL control reports not-queried; NVIDIA requested by environment only",
              "peak_rss_kib": int(rss[0]), "phase_trace_seen": "COIN_RENDER_PHASE " in log,
              "animation_metadata": [line for line in log.splitlines() if line.startswith("animation") or "selection_digest=" in line]}
    if result["phase_trace_seen"]:
        raise ValueError("Quiet process unexpectedly emitted phase traces")
    first = re.search(r"^\S+_first_frame_ms=([\d.eE+-]+)", log, re.M)
    detail = re.search(r"^\S+_first_detail (.+)$", log, re.M)
    throughput = re.search(r"^\S+_throughput frames=(\d+) total_ms=([\d.eE+-]+) fps=([\d.eE+-]+)", log, re.M)
    if first is None or detail is None or throughput is None or int(throughput[1]) != frame_count:
        raise ValueError("Missing first-frame or measured-throughput report")
    fields = dict(re.findall(r"(\w+)=([\d.eE+-]+)(?:\s|$)", detail[1]))
    result.update(first_ms=float(first[1]), first_total_ms=float(fields["total_with_update_ms"]),
                  result_since_main_ms=float(fields["result_since_main_ms"]),
                  wall_total_ms=float(throughput[2]), throughput_fps=float(throughput[3]))
    if not all(math.isfinite(result[key]) and result[key] >= 0 for key in
               ("first_ms", "first_total_ms", "result_since_main_ms", "wall_total_ms", "throughput_fps")):
        raise ValueError("Nonfinite/negative process-level measurement")
    checksum = re.search(r"^(?:gl_)?rgba_fnv64=(\S+)", log, re.M)
    result["final_rgba_checksum"] = checksum[1] if checksum else None
    animation = re.search(r"^animation_detail (.+)$", log, re.M)
    result["animation_contract"] = dict(re.findall(r"(\w+)=(\S+)", animation[1])) if animation else None
    return result


def normalize_command(record):
    command = list(record["command"])
    command[0] = "<benchmark>"
    command[command.index("--samples-output") + 1] = "<samples>"
    environment = {key: value for key, value in record["environment"].items() if key != "LD_LIBRARY_PATH"}
    return command, environment, record["scene_sha256"]


def difference(before, after, unit="ms"):
    return {"before": before, "after": after, "delta_" + unit: after - before, "unit": unit,
            "change_percent": (after / before - 1) * 100 if before else None}


def validate_record_contract(record):
    contract = record["log_measurements"]["animation_contract"]
    animation, percent = ("static", 10) if record["case"] == "static" else (record["case"].rsplit("-", 1)[0], int(record["case"].rsplit("-", 1)[1]))
    selected = 0 if animation == "static" else (40000 * percent + 99) // 100
    clones = selected if animation == "materials" else 0
    expected = {"mode": animation, "animated_percent": str(percent), "eligible": "40000",
                "selected": str(selected), "prepared_clones": str(clones), "frame_step": "1"}
    if contract is None or any(contract.get(key) != value for key, value in expected.items()):
        raise ValueError("Reported animation/selection differs from the scenario: " + record["stem"])
    expected_warmup, expected_frames = sample_counts(record["scene_key"], record["case"])
    command = record["command"]
    if record["warmup"] != expected_warmup or record["frames"] != expected_frames or command[command.index("--warmup") + 1] != str(expected_warmup) or command[command.index("--frames") + 1] != str(expected_frames):
        raise ValueError("Recorded/command frame counts differ from the scenario: " + record["stem"])


def summarize(records):
    successful = [record for record in records if record.get("exit_code") == 0 and not record.get("timed_out")
                  and record.get("status") == "passed" and "samples" in record and "log_measurements" in record]
    groups, pairs, comparisons = [], [], []
    for scene_key, case in SCENARIOS:
        selected = [record for record in successful if (record["scene_key"], record["case"]) == (scene_key, case)]
        for role in ROLES:
            group = sorted((record for record in selected if record["role"] == role), key=lambda record: record["round"])
            if not group:
                continue
            item = {"scene_key": scene_key, "case": case, "role": role, "variant": VARIANTS[role],
                    "processes": len(group), "stems": [record["stem"] for record in group],
                    "rounds": [record["round"] for record in group], "stats": {}, "process_median_values_ms": {},
                    "process_median_range_ms": {}, "process_measurements": {}}
            for metric in METRICS:
                values = [record["samples"]["stats"][metric]["median_ms"] for record in group]
                item["stats"][metric] = {key: statistics.median(record["samples"]["stats"][metric][key] for record in group)
                                         for key in group[0]["samples"]["stats"][metric]}
                item["process_median_values_ms"][metric] = values
                item["process_median_range_ms"][metric] = {"minimum": min(values), "maximum": max(values)}
            for key in ("peak_rss_kib", "first_ms", "first_total_ms", "result_since_main_ms", "wall_total_ms", "throughput_fps"):
                values = [record["log_measurements"][key] for record in group]
                item["process_measurements"][key] = {"median": statistics.median(values), "minimum": min(values), "maximum": max(values), "values": values}
            groups.append(item)
        for round_number in range(1, ROUNDS + 1):
            wg = {record["role"]: record for record in selected if record["round"] == round_number and record["role"].startswith("wgpu-")}
            if set(wg) != {"wgpu-before", "wgpu-after"}:
                continue
            before, after = wg["wgpu-before"], wg["wgpu-after"]
            fields = ("mode", "animated_percent", "eligible", "selected", "prepared_clones", "frame_step", "time_step_seconds", "selection_fnv64")
            before_contract, after_contract = before["log_measurements"]["animation_contract"], after["log_measurements"]["animation_contract"]
            animation_equal = before_contract is not None and after_contract is not None and all(before_contract.get(key) == after_contract.get(key) for key in fields)
            pairs.append({"scene_key": scene_key, "case": case, "round": round_number, "before": before["stem"], "after": after["stem"],
                          "complete_and_comparable": normalize_command(before) == normalize_command(after) and animation_equal,
                          "animation_contract_equal": animation_equal,
                          "stats": {metric: {key: difference(before["samples"]["stats"][metric][key], after["samples"]["stats"][metric][key])
                                             for key in ("median_ms", "min_ms", "max_ms", "p95_ms", "p99_ms")} for metric in METRICS}})
        wg_groups = {group["role"]: group for group in groups if (group["scene_key"], group["case"]) == (scene_key, case) and group["role"].startswith("wgpu-")}
        if set(wg_groups) == {"wgpu-before", "wgpu-after"}:
            before, after = wg_groups["wgpu-before"], wg_groups["wgpu-after"]
            comparisons.append({"scene_key": scene_key, "case": case, "variant": "wgpu-vulkan",
                                "stats": {metric: difference(before["stats"][metric]["median_ms"], after["stats"][metric]["median_ms"]) for metric in METRICS},
                                "process_measurements": {key: difference(before["process_measurements"][key]["median"], after["process_measurements"][key]["median"],
                                                                          "kib" if key == "peak_rss_kib" else "fps" if key == "throughput_fps" else "ms")
                                                         for key in before["process_measurements"]}})
    counts = {"processes": len(successful), "measured_frames": sum(record["samples"]["measured_frames"] for record in successful),
              "warmup_frames": sum(record["samples"]["warmup_frames"] for record in successful)}
    identifiers = [(record["scene_key"], record["case"], record["round"], record["role"]) for record in successful]
    complete = counts == EXPECTED and len(identifiers) == len(set(identifiers)) and len(groups) == 30 and all(group["rounds"] == [1, 2, 3] for group in groups)
    return {"expected_unique_counts": EXPECTED, "completed_unique_counts": counts, "groups": groups, "pairs": pairs,
            "comparisons": comparisons, "complete": complete,
            "complete_and_comparable": complete and len(pairs) == 18 and all(pair["complete_and_comparable"] for pair in pairs),
            "summary_method": "median of three per-process measured medians; warmups excluded by CSV flags; no pooled frames",
            "difference": "after minus before; positive timing values mean slower",
            "limitations": ["Shared controls are single measurements, not independent before/after samples.",
                            "CoinGL binary reports adapter=not-queried; NVIDIA environment is requested but not observed in its adapter report.",
                            "Total timing includes update, render/readback and publication. Quiet timings do not attribute cost to a particular phase.",
                            "Material counts during animation must be observed in the separate diagnostic campaign."]}


def plans(args):
    builds = {"coingl-control": args.coingl_build, "bgfx-vulkan-control": args.bgfx_build,
              "bgfx-opengl-control": args.bgfx_build, "wgpu-before": args.before_wgpu_build, "wgpu-after": args.after_wgpu_build}
    scenes = {"original": args.original_scene, "high": args.high_scene}
    records = []
    for round_index in range(ROUNDS):
        scenarios = list(SCENARIOS[round_index:] + SCENARIOS[:round_index])
        if round_index % 2:
            scenarios.reverse()
        for scenario_index, (scene_key, case) in enumerate(scenarios):
            controls = list(ROLES[:3])
            offset = (round_index + scenario_index) % len(controls)
            controls = controls[offset:] + controls[:offset]
            wg = ["wgpu-before", "wgpu-after"] if (round_index + scenario_index) % 2 == 0 else ["wgpu-after", "wgpu-before"]
            position = (round_index + scenario_index) % 4
            roles = controls[:position] + wg + controls[position:]
            for role in roles:
                animation, percent = ("static", 10) if case == "static" else (case.rsplit("-", 1)[0], int(case.rsplit("-", 1)[1]))
                warmup_count, frame_count = sample_counts(scene_key, case)
                stem = f"{scene_key}-{case}-{role}-{round_index + 1}"
                source = COINGL if role == "coingl-control" else args.after_source_content_revision if role == "wgpu-after" else BASELINE
                records.append({"stem": stem, "execution_index": len(records) + 1, "scene_key": scene_key, "scene": str(scenes[scene_key]),
                                "case": case, "role": role, "variant": VARIANTS[role], "round": round_index + 1,
                                "shared_control": role.endswith("-control"), "source_content_revision": source,
                                "warmup": warmup_count, "frames": frame_count,
                                "build": str(builds[role]), "status": "planned",
                                "command": [str(builds[role] / "bin/coin_render_gl_benchmark"), "--backend", BACKENDS[VARIANTS[role]],
                                            "--scene", str(scenes[scene_key]), "--animation", animation, "--animated-percent", str(percent),
                                            "--transparency", "object", "--size", str(args.size), "--warmup", str(warmup_count), "--frames", str(frame_count),
                                            "--samples-output", str(args.output / "samples" / (stem + ".csv"))]})
    if len(records) != EXPECTED["processes"] or len({record["stem"] for record in records}) != EXPECTED["processes"]:
        raise ValueError("Internal plan cardinality error")
    return records


def file_card(path):
    return {"path": str(path), "bytes": path.stat().st_size, "sha256": sha(path)}


def verify_pin(card_path, source, paths):
    card = json.loads(card_path.read_text())
    pinned = {str(Path(item["path"]).resolve()): item for item in card["files"]}
    for path in paths:
        item = pinned.get(str(path))
        if item is None or item["sha256"] != sha(path):
            raise ValueError("Missing or changed binary pin: " + str(path))
        if item.get("source_content_revision", card.get("source_content_revision")) != source:
            raise ValueError("Binary-card source does not match the role: " + str(path))
    return file_card(card_path)


def derive(directory):
    directory = Path(directory).resolve()
    manifest = json.loads((directory / "commands.json").read_text())
    records = []
    for stored in manifest["commands"]:
        record = dict(stored)
        if record.get("status") == "passed":
            sample_path = directory / "samples" / (record["stem"] + ".csv")
            log_path = directory / "logs" / (record["stem"] + ".log")
            if sha(sample_path) != record["samples"]["csv_sha256"] or sha(log_path) != record["raw_outputs"]["log"]["sha256"]:
                raise ValueError("Archived raw input hash differs: " + record["stem"])
            expected_warmup, expected_frames = sample_counts(record["scene_key"], record["case"])
            if (record["warmup"], record["frames"]) != (expected_warmup, expected_frames):
                raise ValueError("Recorded frame counts differ from the scenario protocol")
            record["samples"] = read_samples(sample_path, expected_warmup, expected_frames)
            record["log_measurements"] = read_log(log_path.read_text(), record["variant"], expected_frames)
            validate_record_contract(record)
            if record["source_content_revision"] != manifest["role_source_content_revisions"][record["role"]] or record["scene_sha256"] != SCENE_HASHES[record["scene_key"]]:
                raise ValueError("Record source/scene differs from the campaign roles: " + record["stem"])
            if record["samples"] != stored["samples"] or record["log_measurements"] != stored["log_measurements"]:
                raise ValueError("Recorded process measurements differ from raw inputs: " + record["stem"])
        records.append(record)
    result = summarize(records)
    result.update(schema_version=1, campaign_commands_sha256=sha(directory / "commands.json"),
                  runner_sha256=manifest["runner"]["sha256"], input_source_content_revisions=manifest["role_source_content_revisions"],
                  binaries_unchanged=manifest.get("binaries_unchanged", False), campaign_status=manifest["status"])
    result["complete_and_comparable"] = result["complete_and_comparable"] and result["binaries_unchanged"] and manifest["status"] == "completed"
    return result


def run(args):
    commands = plans(args)
    plan = {"expected_unique_counts": EXPECTED, "commands": commands, "trace": False,
            "ordering": "rotate scenarios and roles; alternate adjacent wgpu before/after order; controls once per scenario/round"}
    if args.dry_run:
        print(json.dumps(plan, indent=2))
        return
    readiness = []
    if args.after_source_content_revision is None:
        readiness.append("--after-source-content-revision is missing")
    for artifact in ARTIFACTS:
        if not (args.after_wgpu_build / artifact).is_file():
            readiness.append("Missing after artifact: " + str(args.after_wgpu_build / artifact))
    if readiness:
        if not args.skip_quiet_if_not_ready:
            raise ValueError("Quiet campaign is not ready: " + "; ".join(readiness))
        args.output.mkdir(parents=True, exist_ok=False)
        write_json(args.output / "commands.json", dict(plan, schema_version=1, status="skipped_not_ready", reasons=readiness,
                                                      invocation=sys.argv, runner=file_card(Path(__file__).resolve()),
                                                      completed_unique_counts={"processes": 0, "measured_frames": 0, "warmup_frames": 0},
                                                      complete_and_comparable=False))
        print("Quiet campaign skipped; no process executed: " + "; ".join(readiness))
        return
    helper_path = args.repository / "scripts/coinrender/run_animation_benchmark.py"
    helper = load_helper(helper_path)
    builds = {"coingl-control": args.coingl_build, "bgfx-control": args.bgfx_build, "wgpu-before": args.before_wgpu_build, "wgpu-after": args.after_wgpu_build}
    if args.before_wgpu_build == args.after_wgpu_build:
        raise ValueError("Before/after wgpu builds must be separate paths")
    binaries = []
    for stage, build in builds.items():
        source = COINGL if stage == "coingl-control" else args.after_source_content_revision if stage == "wgpu-after" else BASELINE
        for relative in ARTIFACTS:
            binaries.append(dict(file_card(build / relative), stage=stage, artifact=relative, source_content_revision=source))
    pins = [verify_pin(args.baseline_binaries_card, BASELINE, [build / relative for build in (args.before_wgpu_build, args.bgfx_build) for relative in ARTIFACTS]),
            verify_pin(args.coingl_binaries_card, COINGL, [args.coingl_build / relative for relative in ARTIFACTS])]
    if args.after_binaries_card:
        pins.append(verify_pin(args.after_binaries_card, args.after_source_content_revision, [args.after_wgpu_build / relative for relative in ARTIFACTS]))
    scenes = {key: file_card(path) for key, path in (("original", args.original_scene), ("high", args.high_scene))}
    if any(card["sha256"] != SCENE_HASHES[key] for key, card in scenes.items()):
        raise ValueError("Scene SHA differs from the preserved original/high-slot scene")
    args.output.mkdir(parents=True, exist_ok=False)
    for subdirectory in ("logs", "samples", "stdout", "stderr"):
        (args.output / subdirectory).mkdir()
    metadata = {"schema_version": 1, "status": "running", "expected_unique_counts": EXPECTED, "commands": [],
                "planned_commands": commands, "runner": file_card(Path(__file__).resolve()), "parser": file_card(Path(__file__).resolve()),
                "environment_helper": file_card(helper_path), "binary_pins": pins, "binary_hashes": binaries,
                "scenes": scenes, "invocation": sys.argv, "source_revision_basis": "Owner-supplied compiled-content SHA; optional after pin and mandatory baseline/control pins; no Git inference",
                "role_source_content_revisions": {role: COINGL if role == "coingl-control" else args.after_source_content_revision if role == "wgpu-after" else BASELINE for role in ROLES},
                "parameters": {"rounds": ROUNDS, "dynamic_warmup": WARMUP, "dynamic_frames": FRAMES, "static_warmup": 30, "static_frames": 120,
                               "counts_basis": "Per-command scenario protocol; high/static uses 30/120, all five dynamic scenarios use 5/15",
                               "size": args.size, "timeout_seconds": args.timeout,
                               "gpu": "nvidia", "scope": "offscreen", "transparency": "object", "trace": False}, "binaries_unchanged": False}

    def save():
        metadata["completed_unique_counts"] = summarize(metadata["commands"])["completed_unique_counts"]
        write_json(args.output / "commands.json", metadata)

    save()
    try:
        for planned in commands:
            record = dict(planned)
            env = helper.environment(Path(record["build"]), record["variant"], "nvidia")
            for key in list(env):
                if key.startswith(("COIN_RENDER_DISABLE_", "COIN_WGPU_DISABLE_", "COIN_BGFX_DISABLE_")) or key in TRACE_KEYS:
                    env.pop(key, None)
            saved_env = {key: value for key, value in env.items() if key.startswith(("COIN_", "WGPU_", "__NV", "__GLX", "__EGL", "VK_", "LD_LIBRARY"))}
            record.update(environment=saved_env, trace_environment_absent=all(key not in env for key in TRACE_KEYS),
                          disabled_flags_absent=not any(key.startswith(("COIN_RENDER_DISABLE_", "COIN_WGPU_DISABLE_", "COIN_BGFX_DISABLE_")) for key in env),
                          scene_sha256=scenes[record["scene_key"]]["sha256"], timeout_seconds=args.timeout, status="running",
                          timed_command=["/usr/bin/time", "-f", "benchmark_peak_rss_kib=%M", *record["command"]])
            metadata["commands"].append(record)
            save()
            print("START", record["execution_index"], record["stem"], flush=True)
            try:
                completed = subprocess.run(record["timed_command"], cwd=args.repository, env=env, capture_output=True, text=True, timeout=args.timeout)
                stdout, stderr = completed.stdout, completed.stderr
                record.update(exit_code=completed.returncode, timed_out=False)
            except subprocess.TimeoutExpired as error:
                def text(value):
                    return value.decode("utf-8", errors="replace") if isinstance(value, bytes) else value or ""
                stdout, stderr = text(error.stdout), text(error.stderr)
                record.update(exit_code=None, timed_out=True)
            paths = {"stdout": args.output / "stdout" / (record["stem"] + ".stdout"),
                     "stderr": args.output / "stderr" / (record["stem"] + ".stderr"), "log": args.output / "logs" / (record["stem"] + ".log")}
            for key, value in (("stdout", stdout), ("stderr", stderr), ("log", stdout + stderr)):
                paths[key].write_text(value, encoding="utf-8")
            record["raw_outputs"] = {key: file_card(path) for key, path in paths.items()}
            record["status"] = "failed" if record["exit_code"] != 0 or record["timed_out"] else "validating"
            save()
            if record["status"] == "failed":
                raise RuntimeError("Process failed or timed out; raw outputs retained: " + record["stem"])
            record["samples"] = read_samples(args.output / "samples" / (record["stem"] + ".csv"), record["warmup"], record["frames"])
            record["log_measurements"] = read_log(stdout + stderr, record["variant"], record["frames"])
            validate_record_contract(record)
            record["adapter_verified_nvidia"] = record["log_measurements"]["adapter_verified_nvidia"]
            record["status"] = "passed"
            save()
            print("END", record["stem"], "total_median_ms=" + str(record["samples"]["stats"]["total_ms"]["median_ms"]), flush=True)
        metadata["status"] = "completed"
    except BaseException as error:
        metadata["status"] = "failed"
        metadata["failure"] = {"type": type(error).__name__, "message": str(error)}
        raise
    finally:
        metadata["binary_hashes_post_campaign"] = []
        for initial in binaries:
            path = Path(initial["path"])
            post = sha(path) if path.is_file() else None
            metadata["binary_hashes_post_campaign"].append(dict(initial, post_sha256=post, unchanged=post == initial["sha256"]))
        metadata["binaries_unchanged"] = all(card["unchanged"] for card in metadata["binary_hashes_post_campaign"])
        save()
    result = derive(args.output)
    write_json(args.output / "results.json", result)
    if not result["complete_and_comparable"]:
        raise RuntimeError("Campaign did not satisfy complete/comparable protocol")
    print(json.dumps({"completed_unique_counts": result["completed_unique_counts"], "complete_and_comparable": True,
                      "comparisons": result["comparisons"]}, indent=2), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="operation", required=True)
    execution = subparsers.add_parser("run", help="Serial quiet campaign (or print plan with --dry-run)")
    paths = {"repository": "/tmp/coin-render-first-frame", "before-wgpu-build": "/tmp/coin-render-material-table-baseline/wgpu",
             "after-wgpu-build": "/tmp/coin-render-first-frame-wgpu", "bgfx-build": "/tmp/coin-render-material-table-baseline/bgfx",
             "coingl-build": "/tmp/coin-render-material-geometry-baseline/coingl", "original-scene": "/tmp/coin-render-city-40000.iv",
             "high-scene": "/tmp/coin-render-city-materials-40001.iv", "output": "/tmp/coin-render-material-study-quiet",
             "baseline-binaries-card": "/tmp/coin-render-material-table-baseline-binaries.json",
             "coingl-binaries-card": "/tmp/coin-render-state-coingl-binaries.json"}
    for name, default in paths.items():
        execution.add_argument("--" + name, type=Path, default=Path(default))
    execution.add_argument("--after-source-content-revision", help="Full compiled-content SHA, supplied by the campaign owner")
    execution.add_argument("--after-binaries-card", type=Path, help="Optional after-build card; if supplied every WG artifact must match")
    execution.add_argument("--size", type=int, default=1024)
    execution.add_argument("--timeout", type=float, default=1800)
    execution.add_argument("--skip-quiet-if-not-ready", action="store_true", help="Save skipped plan and execute zero processes if after source/artifacts are missing")
    execution.add_argument("--dry-run", action="store_true", help="Print all 90 argv records; execute/write nothing")
    analysis = subparsers.add_parser("analyze", help="CPU-only rederivation from preserved logs/CSVs")
    analysis.add_argument("input", type=Path)
    analysis.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.operation == "analyze":
        if args.output.exists():
            parser.error("Analysis output must be a fresh path")
        result = derive(args.input)
        write_json(args.output, result)
        print(json.dumps({"complete_and_comparable": result["complete_and_comparable"], "counts": result["completed_unique_counts"]}, indent=2))
        return
    if args.after_source_content_revision is not None and re.fullmatch("[0-9a-f]{40}", args.after_source_content_revision) is None:
        parser.error("After source must be a full SHA")
    if args.size < 1 or not math.isfinite(args.timeout) or args.timeout <= 0:
        parser.error("Size and timeout must be positive")
    for key, value in vars(args).items():
        if isinstance(value, Path):
            setattr(args, key, value.resolve())
    try:
        run(args)
    except (OSError, ValueError, RuntimeError) as error:
        parser.error(str(error))


if __name__ == "__main__":
    main()
