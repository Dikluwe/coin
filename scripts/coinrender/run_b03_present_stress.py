#!/usr/bin/env python3
"""Capture independent B03.2 first-present observations on the selected GPU."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import time

parser = argparse.ArgumentParser()
parser.add_argument("--executable", type=Path, required=True)
parser.add_argument("--output", type=Path, required=True)
parser.add_argument("--runs-per-mode", type=int, default=20)
args = parser.parse_args()
if args.runs_per_mode < 1:
    parser.error("--runs-per-mode must be positive")
args.output.mkdir(parents=True, exist_ok=True)
env = dict(os.environ)
env.update(DISPLAY=":0", WGPU_BACKEND="gl",
           __GLX_VENDOR_LIBRARY_NAME="mesa",
           __EGL_VENDOR_LIBRARY_FILENAMES="/usr/share/glvnd/egl_vendor.d/50_mesa.json",
           COIN_RENDER_TRACE_PHASES="1")
rows = []
for mode in ("managed", "override"):
    for index in range(args.runs_per_mode):
        command = [str(args.executable), "--terrain", "--trace-observer"]
        if mode == "managed":
            command.append("--managed")
        started = time.monotonic()
        try:
            result = subprocess.run(command, env=env, capture_output=True,
                                    text=True, timeout=30)
            output = result.stdout + result.stderr
            code = result.returncode
        except subprocess.TimeoutExpired as error:
            def decoded(value):
                return value if isinstance(value, str) else (value or b"").decode(errors="replace")
            output = decoded(error.stdout) + decoded(error.stderr)
            code = 124
        name = f"{mode}-{index:02d}"
        (args.output / f"{name}.log").write_text(output)
        observations = re.findall(
            r"first_present frame=(\d+) .*fixed_50ms_fresh=(\d+) "
            r"eventual_fresh=(\d+) .*serial_unchanged=(\d+) no_new_render=1",
            output)
        pixels = re.findall(
            r"first_present_terrain_pixels frame=(\d+) mismatches=(\d+) "
            r"max_rgb=(\d+) .*result=(\w+)", output)
        adapter = re.search(r"first_present_adapter name=([^\n]+)", output)
        valid = (code == 0 and len(observations) == 3 and len(pixels) == 3
                 and all(fixed == fresh == stable == "1"
                         for _, fixed, fresh, stable in observations)
                 and all(mismatch == maximum == "0" and outcome == "PASS"
                         for _, mismatch, maximum, outcome in pixels)
                 and adapter is not None and "AMD Radeon" in adapter.group(1))
        rows.append(dict(mode=mode, run=index, code=code, pass_gate=valid,
                         observations=observations, pixels=pixels,
                         adapter=adapter.group(1) if adapter else None,
                         seconds=round(time.monotonic() - started, 3),
                         log=f"{name}.log"))
        print(name, "PASS" if valid else "FAIL", flush=True)
        if not valid:
            break
    if rows and not rows[-1]["pass_gate"]:
        break
summary = dict(executable=str(args.executable),
               executable_sha256=hashlib.sha256(args.executable.read_bytes()).hexdigest(),
               environment={key: env[key] for key in
                            ("DISPLAY", "WGPU_BACKEND", "__GLX_VENDOR_LIBRARY_NAME",
                             "__EGL_VENDOR_LIBRARY_FILENAMES", "COIN_RENDER_TRACE_PHASES")},
               passed=sum(row["pass_gate"] for row in rows), total=len(rows), rows=rows)
(args.output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
print(f"B03.2 stress {summary['passed']}/{summary['total']}")
raise SystemExit(0 if summary["passed"] == summary["total"] else 1)
