#!/usr/bin/env python3
"""Run the actual FreeCAD viewport macro, then replay its frames independently.

The controls run offscreen in separate processes after the GUI exits. Native
presentation is checked with test-only OS surface captures, never Qt display
images fed back to the host. The interactive worker performs no pixel readback.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import signal
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def execute(args, log, *, env=None, input_text=None, timeout=140):
    with log.open("w") as output:
        process = subprocess.Popen(args, stdin=subprocess.PIPE if input_text is not None else subprocess.DEVNULL,
                                   stdout=output, stderr=subprocess.STDOUT, env=env, text=True,
                                   start_new_session=True)
        try:
            process.communicate(input_text, timeout=timeout)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGTERM)
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait()
            raise RuntimeError("Timed out: " + str(log))
        if process.returncode:
            raise RuntimeError("Process failed; inspect " + str(log))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for arg in ("freecad", "worker", "environment", "assembly", "output"):
        parser.add_argument("--" + arg, type=Path, required=True)
    parser.add_argument("--qt-root", type=Path, help="Qt directory matching FreeCAD's PySide6")
    parser.add_argument("--xvfb", action="store_true")
    parser.add_argument("--presentation", choices=("native", "readback"), default="native")
    args = parser.parse_args()
    args.output = args.output.resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    if (args.output / "session").exists():
        parser.error("Use a fresh output directory")
    env = os.environ.copy()
    env.update(COIN_VIEWPORT_SOURCE=str(ROOT), COIN_VIEWPORT_WORKER=str(args.worker.resolve()),
               COIN_VIEWPORT_ENVIRONMENT=str(args.environment.resolve()), COIN_VIEWPORT_OUTPUT=str(args.output),
               COIN_VIEWPORT_ASSEMBLY=str(args.assembly.resolve()), QT_QPA_PLATFORM="xcb",
               COIN_VIEWPORT_PRESENTATION=args.presentation)
    # This mode owns presentation; never activate the older experimental adapter.
    env.pop("FREECAD_COIN_WGPU", None)
    if args.qt_root:
        env["LD_LIBRARY_PATH"] = str(args.qt_root / "lib")
        env["QT_PLUGIN_PATH"] = str(args.qt_root / "plugins")
    command = [str(args.freecad.resolve()), "--user-cfg", str(args.output / "user.cfg"),
               "--system-cfg", str(args.output / "system.cfg"),
               str(ROOT / "testsuite/coinrender/freecad_retained_viewport.FCMacro")]
    if args.xvfb:
        command = ["xvfb-run", "-a", "-e", str(args.output / "xvfb.log"),
                   "-s", "-screen 0 1440x1080x24", *command]
    execute(command, args.output / "gui.log", env=env)
    result_path = args.output / "results.json"
    if not result_path.exists():
        raise RuntimeError("GUI qualification did not finish; inspect " + str(args.output / "gui.log"))
    results = json.loads(result_path.read_text())
    assert results["status"] == "PASS"
    session = args.output / "session"
    requests = (session / "requests.txt").read_text()
    control = args.output / "current"
    control.mkdir()
    frames = [json.loads(line) for line in (session / "frames.jsonl").read_text().splitlines()]
    first = frames[0]
    execute([str(args.worker.resolve()), str(session / "scene.iv"), str(args.environment.resolve()),
             str(control), "13", str(first["width"]), str(first["height"]), "--current"],
            args.output / "control.log", input_text=requests + "QUIT\n")
    controls = [json.loads(line) for line in (args.output / "control.log").read_text().splitlines()]
    assert len(controls) == len(frames) and all(item["status"] == "ok" for item in controls)
    comparisons = []
    retained = session
    if args.presentation == "native":
        assert all(f["native_window"] and f["pixel_readbacks"] == f["cpu_pixel_bytes"] == f["qt_image_bytes"] == 0 for f in frames)
        assert not list(session.glob("*.rgba")) and not list(session.glob("*.depth"))
        retained = args.output / "retained-control"
        retained.mkdir()
        execute([str(args.worker.resolve()), str(session / "scene.iv"), str(args.environment.resolve()),
                 str(retained), "13", str(first["width"]), str(first["height"])],
                args.output / "retained-control.log", input_text=requests + "QUIT\n")
        control_frames = [json.loads(line) for line in (args.output / "retained-control.log").read_text().splitlines()]
        assert len(control_frames) == len(frames) and all(item["status"] == "ok" for item in control_frames)
    for frame in frames:
        for suffix in ("rgba", "depth"):
            name = f"frame-{frame['id']}.{suffix}"
            actual = (retained / name).read_bytes()
            assert actual == (control / name).read_bytes(), name
            comparisons.append(dict(file=name, bytes=len(actual), sha256=hashlib.sha256(actual).hexdigest()))
    presented = []
    if args.presentation == "native":
        for stage in results["stages"]:
            name = f"frame-{stage['id']}.rgba"
            pixels = (args.output / "presented" / name).read_bytes()
            expected = (control / name).read_bytes()
            assert pixels == expected, f"Native presented pixels differ from the control: {name} ({stage['stage']})"
            presented.append(dict(stage=stage["stage"], file=name, bytes=len(pixels),
                                  sha256=hashlib.sha256(pixels).hexdigest()))
    results.update(exact_callback_comparisons=comparisons, native_presented_color_comparisons=presented,
                   worker_sha256=hashlib.sha256(args.worker.read_bytes()).hexdigest(),
                   scene_sha256=hashlib.sha256((session / "scene.iv").read_bytes()).hexdigest(),
                   assembly_sha256=hashlib.sha256(args.assembly.read_bytes()).hexdigest(),
                   environment_sha256=hashlib.sha256(args.environment.read_bytes()).hexdigest())
    result_path.write_text(json.dumps(results, indent=2) + "\n")
    print(f"FreeCAD retained PBR viewport: PASS, {len(results['stages'])} GUI stages, {len(presented)} exact native surface images, {len(comparisons)} exact offscreen buffer comparisons")


if __name__ == "__main__":
    main()
