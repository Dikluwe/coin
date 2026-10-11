from pathlib import Path
import csv
import hashlib
import json
import os
import statistics
import subprocess

root = Path("/mnt/Laranja/Git/externos/coin-render-artifacts/nvidia-isolated-abba-20261011")
root.mkdir(parents=True, exist_ok=True)
binary = Path("/tmp/coin-render-closure-wgpu/bin/coin_render_window_benchmark")
scene = Path("/mnt/Laranja/Git/externos/coin-portable-sampling-artifacts/20261008-sampling-api-linux/scenes/city-1000000.iv")
environment = os.environ.copy()
environment.update({
    "WGPU_BACKEND": "vulkan",
    "VK_DRIVER_FILES": "/usr/share/vulkan/icd.d/nvidia_icd.json",
    "VK_ICD_FILENAMES": "/usr/share/vulkan/icd.d/nvidia_icd.json",
    "__NV_PRIME_RENDER_OFFLOAD": "1",
    "COIN_SAMPLING_STUDY": "fetch",
})
rows = []
for index, policy in enumerate(("native", "portable", "portable", "native") * 2):
    sample = root / f"{index}-{policy}.csv"
    log = root / f"{index}-{policy}.log"
    command = [
        str(binary), "--backend", "wgpu-vulkan", "--scene", str(scene),
        "--width", "1280", "--height", "720", "--frames", "120",
        "--warmup", "30", "--animation", "static", "--sampling-policy",
        policy, "--samples-output", str(sample),
    ]
    with log.open("w") as output:
        result = subprocess.run(
            command, env=environment, stdout=output,
            stderr=subprocess.STDOUT, timeout=240,
        )
    content = log.read_text(errors="replace")
    report = next(
        (line for line in content.splitlines()
         if line.startswith("window_benchmark ")), None,
    )
    values = []
    if sample.exists():
        with sample.open() as input_file:
            values = [
                float(row["render_present_ms"])
                for row in csv.DictReader(input_file)
                if row["warmup"] == "0"
            ]
    valid = (
        result.returncode == 0 and len(values) == 120 and report is not None
        and 'vendor_id=0x10de' in report
        and 'adapter="NVIDIA' in report
        and 'readback=none' in report
    )
    row = {
        "index": index, "policy": policy, "valid": valid,
        "exit": result.returncode,
        "median_ms": statistics.median(values) if values else None,
        "p95_ms": sorted(values)[113] if values else None,
        "max_ms": max(values) if values else None,
        "over_500ms": sum(value > 500 for value in values),
        "command": command, "report": report,
        "log": log.name,
        "log_sha256": hashlib.sha256(log.read_bytes()).hexdigest(),
        "samples": sample.name,
        "samples_sha256": hashlib.sha256(sample.read_bytes()).hexdigest()
        if sample.exists() else None,
    }
    rows.append(row)
    (root / "summary.json").write_text(json.dumps({
        "binary_sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
        "scene_sha256": hashlib.sha256(scene.read_bytes()).hexdigest(),
        "display": environment.get("DISPLAY"),
        "xauthority": environment.get("XAUTHORITY"),
        "rows": rows,
    }, indent=2) + "\n")
    print(index, policy, "valid", valid, "median_ms", row["median_ms"], flush=True)
if not all(row["valid"] for row in rows):
    raise SystemExit(1)
