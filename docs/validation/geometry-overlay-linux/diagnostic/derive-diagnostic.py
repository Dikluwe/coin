#!/usr/bin/env python3
"""Derive geometry-overlay stage phase/counter summaries from preserved CSV and logs.

No benchmark is invoked. Ordinal trace-to-frame alignment is used only when a
scope/key series has exactly the CSV row count. CSV warmup flags, never a fixed
slice or command-line sample count, select the measured samples. Short or long
series stay raw and have no inferred per-frame statistics.

Example:
  python3 /tmp/coin-render-geometry-overlay-diagnostics.py \
    --input /tmp/coin-render-geometry-overlay-ablation \
    --output /tmp/coin-render-geometry-overlay-ablation/derived-profile.json
"""

import argparse
import csv
import hashlib
import io
import json
import math
from pathlib import Path
import re
import statistics
import sys


PHASE_RE = re.compile(r"^COIN_RENDER_PHASE\s+(\S+)(?:\s+(.*))?$")
INTEGER_RE = re.compile(r"^[+-]?\d+$")
NUMBER_RE = re.compile(r"^[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?$")
COMBINE_COUNTERS = ("programs_checked", "cache_hits", "validated")


def numeric_value(value):
    """Preserve strings/statuses and reject nonfinite floats from statistics."""
    if INTEGER_RE.fullmatch(value):
        return int(value)
    if NUMBER_RE.fullmatch(value):
        number = float(value)
        if math.isfinite(number):
            return number
    return value


def is_number(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool)


def input_file(path, root):
    data = path.read_bytes()
    return data, {"path": str(path.resolve()), "relative_path": str(path.relative_to(root)),
                  "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}


def read_samples(data, limitations):
    reader = csv.DictReader(io.StringIO(data.decode("utf-8-sig")))
    columns = reader.fieldnames or []
    rows = list(reader)
    measured = []
    warmup = []
    selection_valid = bool(rows) and "warmup" in columns
    if not selection_valid:
        limitations.append("CSV is empty or lacks warmup; no measured-frame statistics were inferred")
    for index, row in enumerate(rows):
        flag = str(row.get("warmup", "")).strip().lower()
        if flag in ("0", "false"):
            measured.append(index)
        elif flag in ("1", "true"):
            warmup.append(index)
        else:
            selection_valid = False
            limitations.append(f"CSV row {index} has an unknown warmup flag {flag!r}; no measured-frame statistics were inferred")
    if selection_valid and not measured:
        limitations.append("CSV contains no measured rows; medians are omitted")
    return {"columns": columns, "rows": rows, "row_count": len(rows),
            "measured_row_indices": measured if selection_valid else None,
            "warmup_row_indices": warmup if selection_valid else None,
            "selection_valid": selection_valid}


def parse_trace(data, limitations):
    events = []
    series = {}
    scopes = {}
    for line_number, line in enumerate(data.decode("utf-8", errors="replace").splitlines(), 1):
        match = PHASE_RE.match(line)
        if not match:
            continue
        scope, body = match.groups()
        fields = {}
        raw_fields = {}
        tokens = (body or "").split()
        informational = []
        # One known BGFX adapter label contains a space. This exact renderer
        # tag is metadata; grouping it does not relax any measurement parser.
        # Unknown bare tokens in this or any other scope remain limitations.
        if scope == "bgfx_device" and tokens[:2] == ["renderer=OpenGL", "4.3"]:
            tokens[:2] = ["renderer=OpenGL 4.3"]
            informational.append({"scope": scope, "log_line_number": line_number,
                "token": "4.3", "classification": "known_bgfx_renderer_label",
                "message": "Known multiword renderer=OpenGL 4.3 grouped; original token and raw line retained"})
        for token in tokens:
            if "=" not in token:
                limitations.append(f"Unparsed token in trace line {line_number}: {token!r}; original line retained")
                continue
            key, raw_value = token.split("=", 1)
            if not key or key in fields:
                raise ValueError(f"Invalid or duplicate field in trace line {line_number}: {token!r}")
            value = numeric_value(raw_value)
            fields[key] = value
            raw_fields[key] = raw_value
            qualified = f"{scope}.{key}"
            item = series.setdefault(qualified, {"scope": scope, "key": key, "values": [],
                                                 "raw_values": [], "log_line_numbers": []})
            item["values"].append(value)
            item["raw_values"].append(raw_value)
            item["log_line_numbers"].append(line_number)
        event = {"log_line_number": line_number, "scope": scope, "fields": fields,
                 "raw_fields": raw_fields, "raw_line": line}
        if informational:
            event["informational_warnings"] = informational
        events.append(event)
        scopes.setdefault(scope, []).append(event)
    return events, series, scopes


def summarize_series(item, samples):
    values = item["values"]
    item["sample_count"] = len(values)
    item["csv_row_count"] = samples["row_count"]
    matched = len(values) == samples["row_count"] and bool(samples["row_count"])
    item["cardinality_matches_csv"] = matched
    if not matched:
        item["alignment_limitation"] = "Trace cardinality differs from CSV; no per-frame mapping or measured median inferred"
        return
    if not samples["selection_valid"]:
        item["alignment_limitation"] = "CSV warmup selection is invalid; no measured median inferred"
        return
    positions = samples["measured_row_indices"]
    measured = [values[index] for index in positions]
    item["measured_row_indices"] = positions
    item["measured_values"] = measured
    if not measured:
        item["statistics_limitation"] = "No measured rows"
    elif all(is_number(value) for value in measured):
        item["measured_median"] = statistics.median(measured)
    else:
        item["statistics_limitation"] = "Measured series contains nonnumeric or nonfinite values; raw values retained"


def csv_medians(samples):
    result = {}
    if not samples["selection_valid"] or not samples["measured_row_indices"]:
        return result
    for column in samples["columns"]:
        if not column.endswith("_ms"):
            continue
        values = [numeric_value(samples["rows"][index].get(column, ""))
                  for index in samples["measured_row_indices"]]
        if all(is_number(value) for value in values):
            result[column] = statistics.median(values)
    return result


def combine_frames(scopes, samples):
    records = scopes.get("combine_validation_memo", [])
    result = {"scope": "combine_validation_memo", "record_count": len(records),
              "csv_row_count": samples["row_count"], "records": []}
    aligned = bool(records) and len(records) == samples["row_count"] and samples["selection_valid"]
    result["cardinality_matches_csv"] = bool(records) and len(records) == samples["row_count"]
    result["frame_alignment_available"] = aligned
    for index, event in enumerate(records):
        record = {"scope": event["scope"], "occurrence_index": index,
                  "log_line_number": event["log_line_number"], "fields": event["fields"],
                  "raw_fields": event["raw_fields"]}
        if aligned:
            row = samples["rows"][index]
            record["csv_row_index"] = index
            record["frame_index"] = row.get("frame_index")
            record["logical_frame"] = row.get("logical_frame")
            record["warmup"] = index in samples["warmup_row_indices"]
        result["records"].append(record)
    if not aligned:
        result["limitation"] = ("Missing counter scope, cardinality mismatch, or invalid CSV warmup selection; "
                                "counters kept by occurrence, without inferred frames or medians")
    else:
        result["measured_medians"] = {}
        for key in COMBINE_COUNTERS:
            selected = [records[index]["fields"].get(key) for index in samples["measured_row_indices"]]
            if selected and all(is_number(value) for value in selected):
                result["measured_medians"][key] = statistics.median(selected)
    return result


def discover(root, suffix):
    found = {}
    for path in sorted(root.rglob(f"*{suffix}")):
        if path.stem in found:
            raise ValueError(f"Duplicate {suffix} stem {path.stem!r}: {found[path.stem]} and {path}")
        found[path.stem] = path
    return found


def analyze(root):
    files = {}
    commands_path = root / "commands.json"
    metadata = None
    metadata_file = None
    limitations = []
    if commands_path.is_file():
        data, metadata_file = input_file(commands_path, root)
        metadata = json.loads(data)
        files[metadata_file["relative_path"]] = metadata_file
    else:
        limitations.append("commands.json missing; source revision, commands, and recorded binary/scene hashes unavailable")
    commands = metadata.get("commands", []) if isinstance(metadata, dict) else []
    csvs, logs = discover(root, ".csv"), discover(root, ".log")
    command_stems = {item.get("stem") for item in commands if isinstance(item, dict) and item.get("stem")}
    runs = {}
    for stem in sorted(set(csvs) | set(logs) | command_stems):
        run_limitations = []
        run = {"command_metadata": [item for item in commands if isinstance(item, dict) and item.get("stem") == stem],
               "limitations": run_limitations}
        if stem not in csvs or stem not in logs:
            run_limitations.append("CSV/log pair incomplete; no trace-to-frame statistics inferred")
        inputs = {}
        for kind, paths in (("csv", csvs), ("log", logs)):
            if stem in paths:
                data, file_metadata = input_file(paths[stem], root)
                inputs[kind] = data
                run[f"{kind}_input"] = file_metadata
                files[file_metadata["relative_path"]] = file_metadata
        samples = read_samples(inputs["csv"], run_limitations) if "csv" in inputs else {
            "columns": [], "rows": [], "row_count": 0, "selection_valid": False,
            "measured_row_indices": None, "warmup_row_indices": None}
        events, series, scopes = parse_trace(inputs["log"], run_limitations) if "log" in inputs else ([], {}, {})
        for item in series.values():
            summarize_series(item, samples)
        run["samples"] = samples
        run["csv_measured_medians_ms"] = csv_medians(samples)
        run["trace_events"] = events
        run["informational_warnings"] = [warning for event in events
                                         for warning in event.get("informational_warnings", [])]
        run["trace_series"] = series
        run["phase_measured_medians_ms"] = {
            key: value["measured_median"] for key, value in series.items()
            if value["key"].endswith("_ms") and "measured_median" in value}
        run["counter_measured_medians"] = {
            key: value["measured_median"] for key, value in series.items()
            if not value["key"].endswith("_ms") and "measured_median" in value}
        run["combine_validation"] = combine_frames(scopes, samples)
        runs[stem] = run
    return {"schema_version": 1, "input_directory": str(root), "metadata_input": metadata_file,
            "command_metadata": metadata, "input_files": files, "limitations": limitations,
            "protocol": {"selection": "CSV warmup=0/false rows only",
                         "alignment": "ordinal by scope/key, only if series count equals CSV row count",
                         "mismatches": "raw series retained; no inferred per-frame median",
                         "phase_units": "milliseconds for keys ending _ms",
                         "counter_units": "as emitted, without unit conversion",
                         "overlap": "phase scopes may overlap; medians are not additive",
                         "combine_validated": "original validator calls, including rejected misses"},
            "runs": runs}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--input", type=Path, required=True, help="Directory containing commands.json and CSV/log pairs")
    parser.add_argument("--output", type=Path, required=True, help="Derived JSON output; raw inputs are never modified")
    args = parser.parse_args()
    root, output = args.input.resolve(), args.output.resolve()
    if not root.is_dir():
        parser.error(f"Input directory does not exist: {root}")
    if output.suffix != ".json" or output.name == "commands.json":
        parser.error("Output must be a .json path other than commands.json")
    report = analyze(root)
    script = Path(__file__).resolve()
    report["analysis_script"] = {"path": str(script), "sha256": hashlib.sha256(script.read_bytes()).hexdigest(),
                                 "invocation": sys.argv, "python": sys.version}
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    print(f"Wrote {len(report['runs'])} run(s) to {output}")


if __name__ == "__main__":
    main()
