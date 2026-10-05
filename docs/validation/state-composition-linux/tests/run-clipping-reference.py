#!/usr/bin/env python3
"""Repeat the clipping gate and explicitly require its optional Coin/GL oracle."""
import json
import os
from pathlib import Path
import subprocess
from datetime import datetime, timezone

evidence = Path(__file__).resolve().parent
record = {
    "name": "clipping-reference-supplemental",
    "command": ["/tmp/coin-render-first-frame-wgpu/bin/CoinRenderClipPlaneTest"],
    "environment": {
        "LD_LIBRARY_PATH": "/tmp/coin-render-first-frame-wgpu/lib",
        "__NV_PRIME_RENDER_OFFLOAD": "1",
        "__GLX_VENDOR_LIBRARY_NAME": "nvidia",
        "VK_ICD_FILENAMES": "/usr/share/vulkan/icd.d/nvidia_icd.json",
        "WGPU_BACKEND": "vulkan",
        "COIN_GLX_PIXMAP_DIRECT_RENDERING": "1",
    },
    "working_directory": "/tmp/coin-render-first-frame-wgpu/testsuite",
    "started_utc": datetime.now(timezone.utc).isoformat(),
    "required_output": "Coin/GL reference passed",
    "forbidden_output": "[SKIP]",
}
manifest = evidence / "clipping-reference-command.json"
manifest.write_text(json.dumps(record, indent=2) + "\n")
environment = os.environ.copy()
environment.update(record["environment"])
result = subprocess.run(
    record["command"], cwd=record["working_directory"], env=environment,
    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=60,
)
(evidence / "clipping-reference.log").write_text(result.stdout)
record["exit_code"] = result.returncode
record["reference_observed"] = record["required_output"] in result.stdout
record["skip_observed"] = record["forbidden_output"] in result.stdout
record["passed"] = (
    result.returncode == 0 and record["reference_observed"]
    and not record["skip_observed"]
)
record["finished_utc"] = datetime.now(timezone.utc).isoformat()
manifest.write_text(json.dumps(record, indent=2) + "\n")
print(result.stdout, end="")
print("Explicit clipping reference requirement:", record["passed"])
raise SystemExit(0 if record["passed"] else 1)
