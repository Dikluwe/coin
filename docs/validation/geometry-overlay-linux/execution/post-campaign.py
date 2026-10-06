#!/usr/bin/env python3
"""Record post-campaign hardware, binary integrity and verification provenance.

Run only after timing and AFTER RGB collection have finished. Importing this
file performs no command, hash scan, annotation, Git access or GPU operation.
Hardware queries are read-only. Verification exits are explicitly derived from
the successful original runner and complete outputs, not reported as directly
observed per-process exits. BEFORE evidence is never overwritten by this tool.
"""

import argparse
from collections import Counter
import csv
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys

sys.dont_write_bytecode = True
PREFIX = "/tmp/coin-render-geometry-overlay"
CONTROL = "4d63bb993022ee8d40802558b0871a4803002b8d"
VARIANTS = ("coingl", "bgfx-vulkan", "bgfx-opengl", "wgpu-vulkan")
CASES = ("geometry-10", "geometry-100", "transforms-10", "materials-10")


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest(path):
    hasher, size = hashlib.sha256(), 0
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            hasher.update(block)
            size += len(block)
    return hasher.hexdigest(), size


def json_input(path):
    path = Path(path).resolve()
    data = path.read_bytes()
    return json.loads(data), {"path": str(path), "bytes": len(data),
                              "sha256": hashlib.sha256(data).hexdigest()}


def write_new(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("x", encoding="utf-8") as stream:
        stream.write(json.dumps(value, indent=2, ensure_ascii=False, allow_nan=False) + "\n")


def option(command, name):
    require(command.count(name) == 1, "Expected exactly one " + name)
    index = command.index(name)
    require(index + 1 < len(command), "Missing value for " + name)
    return command[index + 1]


def card(path, expected_count, expected_roles):
    value, evidence = json_input(path)
    require(re.fullmatch("[0-9a-f]{40}", str(value.get("source_content_revision", ""))), "Card compiled source SHA missing")
    files = value.get("files", [])
    require(len(files) == expected_count and len({item["path"] for item in files}) == expected_count,
            "Card file cardinality/uniqueness differs: " + str(path))
    require(dict(Counter(item["role"] for item in files)) == expected_roles, "Card roles differ: " + str(path))
    for item in files:
        require(re.fullmatch("[0-9a-f]{64}", str(item.get("sha256", ""))), "Pinned file SHA256 missing")
        require(Path(item["path"]).is_absolute(), "Pinned artifact path must be absolute")
        require(item.get("source_content_revision", value["source_content_revision"]) == value["source_content_revision"],
                "File source differs from its source card")
    return value, evidence


def run_query(command):
    record = {"command": command, "timeout_seconds": 30}
    try:
        result = subprocess.run(command, capture_output=True, text=True, timeout=30)
        record.update(exit_code=result.returncode, stdout=result.stdout, stderr=result.stderr, timed_out=False)
    except subprocess.TimeoutExpired as error:
        def text(value):
            return value.decode("utf-8", errors="replace") if isinstance(value, bytes) else (value or "")
        record.update(exit_code=None, stdout=text(error.stdout), stderr=text(error.stderr), timed_out=True)
    except OSError as error:
        record.update(exit_code=None, stdout="", stderr=str(error), timed_out=False)
    return record


def hardware_after(args):
    source, evidence = card(args.final_card, 8, {"wgpu": 4, "bgfx": 4})
    commands = [["uname", "-a"], ["lscpu"],
                ["nvidia-smi", "--query-gpu=name,driver_version,temperature.gpu,pstate,clocks.gr,clocks.mem", "--format=csv,noheader"]]
    result = {"timestamp_utc": datetime.now(timezone.utc).isoformat(),
              "source_content_revision": source["source_content_revision"],
              "source_snapshot_revision": source.get("source_snapshot_revision"),
              "source_card": evidence, "commands": [run_query(command) for command in commands],
              "cpu_governors": {}, "cpu_governor_read_errors": {},
              "gpu_workload_note": "Post-campaign read-only snapshot requested after timing and RGB collection; no clocks or display settings changed"}
    for path in sorted(Path("/sys/devices/system/cpu").glob("cpu*/cpufreq/scaling_governor")):
        try:
            result["cpu_governors"][str(path)] = path.read_text().strip()
        except OSError as error:
            result["cpu_governor_read_errors"][str(path)] = str(error)
    result["queries_succeeded"] = all(item["exit_code"] == 0 and not item["timed_out"] for item in result["commands"])
    result["script_sha256"] = digest(Path(__file__))[0]
    write_new(args.output or Path(PREFIX + "-hardware-after.json"), result)
    print("Hardware snapshot written; queries_succeeded=" + str(result["queries_succeeded"]))
    return 0 if result["queries_succeeded"] else 1


def hashes(args):
    specifications = (("baseline", args.baseline_card, 8, {"wgpu": 4, "bgfx": 4}),
                      ("final", args.final_card, 8, {"wgpu": 4, "bgfx": 4}),
                      ("coingl", args.coingl_card, 4, {"coingl": 4}))
    files, inputs = [], {}
    for stage, path, count, roles in specifications:
        source, inputs[stage] = card(path, count, roles)
        if stage == "coingl":
            require(source["source_content_revision"] == CONTROL, "CoinGL control source differs")
        for item in source["files"]:
            post_sha, post_bytes = digest(item["path"])
            original_bytes = item.get("bytes", post_bytes)
            files.append(dict(item, stage=stage, source_content_revision=source["source_content_revision"],
                              bytes=original_bytes, post_bytes=post_bytes, post_sha256=post_sha,
                              unchanged=item["sha256"] == post_sha and original_bytes == post_bytes))
    require(len(files) == len({item["path"] for item in files}) == 20, "Expected twenty unique pinned artifacts")
    result = {"timestamp_utc": datetime.now(timezone.utc).isoformat(), "snapshot_inputs": inputs,
              "all_unchanged": all(item["unchanged"] for item in files),
              "files": files, "stage_counts": dict(Counter(item["stage"] for item in files)),
              "script_sha256": digest(Path(__file__))[0]}
    write_new(args.output or Path(PREFIX + "-binary-hashes-post.json"), result)
    print("Verified twenty hashes; all_unchanged=" + str(result["all_unchanged"]))
    return 0 if result["all_unchanged"] else 1


def verification_provenance(directory, completion_path, side, render_card_path, coingl_card_path):
    directory = Path(directory).resolve()
    manifest, original = json_input(directory / "manifest.json")
    completion, completion_input = json_input(completion_path)
    runner_records = completion.get("commands", [completion])
    records = [record for record in runner_records if isinstance(record, dict) and record.get("command") and
               "--output" in record["command"] and Path(option(record["command"], "--output")).resolve() == directory]
    require(len(records) == 1, "Expected one successful verification runner command")
    runner_record = records[0]
    require(runner_record.get("exit_code") == 0 and not runner_record.get("timed_out") and not runner_record.get("timeout"),
            "Verification runner did not record successful completion")
    require(option(runner_record["command"], "--mode") == "verify", "Completion command was not image verification")
    render, render_input = card(render_card_path, 8, {"wgpu": 4, "bgfx": 4})
    control, control_input = card(coingl_card_path, 4, {"coingl": 4})
    require(control["source_content_revision"] == CONTROL, "CoinGL control source differs")
    snapshot = render.get("source_snapshot_revision")
    require(re.fullmatch("[0-9a-f]{40}", str(snapshot)), "Render snapshot SHA missing")
    require(runner_record.get("source_snapshot_revision", snapshot) == snapshot,
            "Completion source snapshot differs from source card")
    require(runner_record.get("source_content_revision", render["source_content_revision"]) == render["source_content_revision"],
            "Completion compiled content differs from source card")
    parameters = manifest.get("parameters", {})
    require(parameters.get("mode") == "verify" and parameters.get("scope") == "offscreen" and parameters.get("gpu") == "nvidia" and
            set(parameters.get("variants", "").split(",")) == set(VARIANTS) and
            set(parameters.get("cases", "").split(",")) == set(CASES), "Unexpected verification profile")
    require(len(manifest.get("commands", [])) == 16, "Verification must contain sixteen process commands")
    result_rows, _ = json_input(directory / "results.json")
    expected_identities = {(case, variant, 1) for case in CASES for variant in VARIANTS}
    require(len(result_rows) == 16 and {(item["case"], item["variant"], item["round"]) for item in result_rows} == expected_identities,
            "Sixteen verification results do not exactly cover the profile")
    pinned = {item["path"]: item["sha256"] for source in (render, control) for item in source["files"]}
    for path, sha in manifest.get("binary_hashes", {}).items():
        aliases = (path, path + ".0.10") if path.endswith("/libCoin.so.80") else (path,)
        require(any(pinned.get(alias) == sha for alias in aliases), "Manifest binary hash absent from pinned cards: " + path)
    sources = {variant: CONTROL if variant == "coingl" else render["source_content_revision"] for variant in VARIANTS}
    seen, images = set(), set()
    basis = ("Derived from successful verification runner exit_code=0 and its failure-on-any-benchmark-error contract; "
             "16 complete command/CSV/log sets and 112 capture files checked. Original runner did not emit individual process exits.")
    for record in manifest["commands"]:
        command, environment = record["command"], record.get("environment", {})
        backend = option(command, "--backend")
        variant = "coingl" if backend == "gl" else "wgpu-" + environment.get("WGPU_BACKEND", "") if backend == "wgpu" else "bgfx-" + environment.get("COIN_BGFX_RENDERER", "")
        case = option(command, "--animation") + "-" + option(command, "--animated-percent")
        identity = (case, variant, 1)
        require(identity in expected_identities and identity not in seen, "Verification command identity missing/duplicate")
        seen.add(identity)
        require(record["stem"] == f"{case}-{variant}-1", "Stem differs from effective identity")
        require(command[0] in pinned, "Benchmark command is not a pinned executable")
        require("COIN_RENDER_DISABLE_GEOMETRY_INTERVAL_VALIDATION" not in environment, "Verification unexpectedly disabled interval validation")
        for name, value in (("--warmup", "0"), ("--frames", "7"), ("--size", "1024"), ("--transparency", "object"),
                            ("--animation-step", "100"), ("--capture-frames", "0,1,2,3,4,5,6")):
            require(option(command, name) == value, "Verification command differs in " + name)
        csv_path = directory / "samples" / (record["stem"] + ".csv")
        log_path = directory / "logs" / (record["stem"] + ".log")
        require(Path(option(command, "--samples-output")).resolve() == csv_path, "Samples path differs from process stem")
        with csv_path.open(newline="") as stream:
            rows = list(csv.DictReader(stream))
        require(len(rows) == 7 and [row["frame_index"] for row in rows] == [str(index) for index in range(7)] and
                [row["logical_frame"] for row in rows] == [str(index * 100) for index in range(7)] and
                all(row["warmup"].lower() in ("0", "false") for row in rows), "Verification CSV is not seven measured logical frames")
        captures = [dict(re.findall(r"(\w+)=(\S+)", line)) for line in log_path.read_text().splitlines() if line.startswith("capture ")]
        require(len(captures) == 7 and [capture["frame_index"] for capture in captures] == [str(index) for index in range(7)] and
                [capture["logical_frame"] for capture in captures] == [str(index * 100) for index in range(7)], "Verification capture log incomplete")
        for capture in captures:
            image = Path(capture["image"]).resolve()
            require(image.parent == directory / "images" and image.suffix == ".ppm" and image.is_file() and image.stat().st_size > 0 and image not in images,
                    "Capture image missing, empty, duplicate or outside verification output")
            images.add(image)
        record.update(variant=variant, case=case, round=1, source_content_revision=sources[variant],
                      exit_code=0, timed_out=False, exit_code_basis=basis,
                      exit_code_basis_kind="derived_successful_runner_and_complete_outputs")
    require(seen == expected_identities and len(images) == 112 and set((directory / "images").glob("*.ppm")) == images,
            "Verification coverage/image count differs from sixteen processes and 112 PPMs")
    manifest.update(source_content_revision=render["source_content_revision"], source_snapshot_revision=snapshot,
                    variant_source_content_revisions=sources, source_content_revision_kind="single-render-family",
                    completion_command_manifest=str(Path(completion_path).resolve()),
                    source_provenance="Compiled Render and CoinGL content taken from independently pinned cards; snapshot remains separate",
                    verification_annotation={"side": side, "original_manifest": original,
                        "completion_command_input": completion_input, "render_card_input": render_input, "coingl_card_input": control_input,
                        "processes_checked": 16, "capture_files_checked": 112, "rgb_pixels_recomputed": False,
                        "exit_code_basis": basis, "script_sha256": digest(Path(__file__))[0]})
    return manifest


def annotate_verification(args):
    directory = (args.directory or Path(PREFIX + "-verify-" + args.side)).resolve()
    original_path = directory / "manifest.json"
    output = (args.output or original_path).resolve()
    require(args.side != "before" or output != original_path, "BEFORE is preserved; choose a new --output for a derived annotation")
    manifest = verification_provenance(directory, args.runner_command or Path(PREFIX + "-verify-" + args.side + "-command.json"),
        args.side, args.baseline_card if args.side == "before" else args.final_card, args.coingl_card)
    if output == original_path:
        backup = directory / "manifest-before-provenance.json"
        with backup.open("xb") as stream:
            stream.write(original_path.read_bytes())
        manifest["verification_annotation"]["preserved_original_manifest"] = str(backup)
        temporary = directory / "manifest-provenance-update.json"
        write_new(temporary, manifest)
        temporary.replace(original_path)
    else:
        write_new(output, manifest)
    print("Annotated verification provenance:", output)
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="action", required=True)
    for action in ("hardware-after", "hashes", "annotate-verification"):
        child = subparsers.add_parser(action)
        child.add_argument("--baseline-card", type=Path, default=Path(PREFIX + "-baseline-binaries.json"))
        child.add_argument("--final-card", type=Path, default=Path(PREFIX + "-final-binaries.json"))
        child.add_argument("--coingl-card", type=Path, default=Path("/tmp/coin-render-state-coingl-binaries.json"))
        child.add_argument("--output", type=Path)
        if action == "annotate-verification":
            child.add_argument("--side", choices=("before", "after"), required=True)
            child.add_argument("--directory", type=Path)
            child.add_argument("--runner-command", type=Path)
    args = parser.parse_args()
    try:
        status = {"hardware-after": hardware_after, "hashes": hashes, "annotate-verification": annotate_verification}[args.action](args)
    except (OSError, ValueError, KeyError) as error:
        parser.exit(1, str(error) + "\n")
    raise SystemExit(status)


if __name__ == "__main__":
    main()
