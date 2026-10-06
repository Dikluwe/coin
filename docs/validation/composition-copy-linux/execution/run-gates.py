#!/usr/bin/env python3
"""Run the focused composition-copy gates, serially and without accepting skips.

Prepared for coordinator execution only. All CTest definitions are recorded before
execution. Direct invocations record their registered definition and CLI override.
The source revision is read immediately before the first execution and rechecked
after each command. The new pure CPU borrower oracle is mandatory (29 gates).
"""
import argparse
import hashlib
import importlib.util
import json
import re
import subprocess
import sys
from pathlib import Path

sys.dont_write_bytecode = True


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def source_revision(root):
    return subprocess.check_output(
        ["git", "rev-parse", "HEAD"], cwd=root, text=True).strip()


def test_definitions(build):
    command = ["ctest", "--test-dir", str(build), "--show-only=json-v1"]
    payload = json.loads(subprocess.check_output(command, text=True))
    definitions = {item["name"]: item for item in payload["tests"]}
    if len(definitions) != len(payload["tests"]):
        raise RuntimeError("Duplicate CTest names in " + str(build))
    return command, definitions


def properties(definition):
    return {item["name"]: item["value"]
            for item in definition.get("properties", [])}


def gate_list(wg, bg, include_borrow):
    gates = []

    def add(name, build, variant, names, category, arguments=None):
        gates.append(dict(name=name, build=build, variant=variant,
                          tests=names, category=category, arguments=arguments))

    # The prior eighteen gates: two Core, four mixed Action/Reuse, twelve GPU.
    add("core", wg, "wgpu-vulkan",
        ["CoinRenderFrameCoreTest", "CoinRenderPlanAssemblyCoreTest"], "core_cpu")
    add("wgpu-action-reuse", wg, "wgpu-vulkan",
        ["CoinRenderActionTest", "CoinRenderFrameReuseCoreTest"], "action_reuse")
    add("bgfx-action-reuse", bg, "bgfx-vulkan",
        ["CoinRenderActionTest", "CoinRenderFrameReuseCoreTest"], "action_reuse")
    for variant in ("wgpu-vulkan", "bgfx-vulkan", "bgfx-opengl"):
        build = wg if variant.startswith("wgpu") else bg
        add(variant + "-camera-transparency", build, variant,
            ["CoinRenderCameraReuseReferenceTest", "CoinRenderTransparencyTest"], "gpu")
    add("wgpu-rtt-runtime", wg, "wgpu-vulkan",
        ["CoinRenderSceneTextureTest", "CoinRenderSceneTextureDirectTest",
         "CoinWgpuMultiDeviceTest", "CoinRenderAsyncActionTest"], "gpu")
    add("bgfx-rtt", bg, "bgfx-vulkan",
        ["CoinRenderRttOwnership_vulkan", "CoinRenderRttOwnership_opengl"], "gpu")

    # Focused CPU oracles for schedule semantics and the two lowering consumers.
    add("composition-range-memo-cpu", wg, "wgpu-vulkan",
        ["CoinRenderCompositionTest"], "core_cpu", ["--range-memo"])
    if include_borrow:
        add("composition-borrow-cpu", wg, "wgpu-vulkan",
            ["CoinRenderCompositionTest"], "core_cpu", ["--borrow"])
    add("wgpu-ffi-frame-cpu", wg, "wgpu-vulkan",
        ["CoinWgpuFfiFrameTest"], "core_cpu")
    add("depth-contract-cpu", wg, "wgpu-vulkan",
        ["CoinRenderDepthContractTest"], "core_cpu")
    add("bgfx-core-cpu", bg, "bgfx-vulkan", ["CoinBgfxCoreTest"], "core_cpu")

    # --gpu requires availability before running the complete composition suite.
    # The default suite alone can silently use only mock tests without a GPU.
    for variant in ("wgpu-vulkan", "bgfx-vulkan", "bgfx-opengl"):
        build = wg if variant.startswith("wgpu") else bg
        add(variant + "-composition-gpu", build, variant,
            ["CoinRenderCompositionTest"], "gpu", ["--gpu"])
    # WG has no registered GPU depth variant; preserve its CPU CTest definition
    # and record the explicit CLI override rather than inventing a CTest name.
    add("wgpu-vulkan-depth-gpu", wg, "wgpu-vulkan",
        ["CoinRenderDepthContractTest"], "gpu", ["--gpu"])
    add("bgfx-vulkan-depth-gpu", bg, "bgfx-vulkan",
        ["CoinRenderDepthContractVulkanTest"], "gpu")
    add("bgfx-opengl-depth-gpu", bg, "bgfx-opengl",
        ["CoinRenderDepthContractOpenGLTest"], "gpu")
    return gates


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path("/tmp/coin-render-first-frame"))
    parser.add_argument("--wgpu-build", type=Path,
                        default=Path("/tmp/coin-render-first-frame-wgpu"))
    parser.add_argument("--bgfx-build", type=Path,
                        default=Path("/tmp/coin-render-first-frame-bgfx"))
    parser.add_argument("--output", type=Path,
                        default=Path("/tmp/coin-render-composition-copy-gates"))
    parser.add_argument("--expected-source-content-revision",
                        help="Require the coordinator's committed production HEAD")
    parser.add_argument("--borrow", action="store_true",
                        help="Compatibility flag; the --borrow oracle is always included")
    args = parser.parse_args()
    args.root = args.root.resolve()
    args.wgpu_build = args.wgpu_build.resolve()
    args.bgfx_build = args.bgfx_build.resolve()
    args.output = args.output.resolve()
    module_path = args.root / "scripts/coinrender/run_animation_benchmark.py"
    spec = importlib.util.spec_from_file_location("animation_runner", module_path)
    runner = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(runner)

    snapshots = {}
    definitions = {}
    for build in (args.wgpu_build, args.bgfx_build):
        show_command, definitions[build] = test_definitions(build)
        snapshots[str(build)] = dict(command=show_command,
                                     definitions=list(definitions[build].values()))
    gates = gate_list(args.wgpu_build, args.bgfx_build, True)
    expected = 29
    category_counts = {}
    for gate in gates:
        category_counts[gate["category"]] = category_counts.get(gate["category"], 0) + len(gate["tests"])
        for name in gate["tests"]:
            if name not in definitions[gate["build"]]:
                raise RuntimeError("Required CTest definition is missing: " + name)
            executable = Path(definitions[gate["build"]][name]["command"][0])
            if not executable.is_absolute() or not executable.is_file():
                raise RuntimeError("Required absolute executable is missing: " + str(executable))
    if sum(category_counts.values()) != expected:
        raise RuntimeError("Unexpected gate cardinality")
    # Check the source mode before execution; the success marker below also
    # rejects an older binary that silently treats an unknown flag as default.
    composition_source = args.root / "testsuite/coinrender/CoinRenderCompositionTest.cpp"
    source_text = composition_source.read_text()
    for required_flag in ["--gpu", "--borrow"]:
        if '"' + required_flag + '"' not in source_text:
            raise RuntimeError("Composition test source lacks required mode " + required_flag)

    args.output.mkdir(exist_ok=False)
    revision = source_revision(args.root)
    if args.expected_source_content_revision and revision != args.expected_source_content_revision:
        raise RuntimeError("Source HEAD differs from --expected-source-content-revision")
    metadata = dict(source_content_revision=revision, expected_gate_executions=expected,
                    expected_categories=category_counts, commands=[], complete=False,
                    ctest_inventory=snapshots, runner_sha256=sha256(Path(__file__)),
                    environment_helper_sha256=sha256(module_path))
    path = args.output / "commands.json"

    def save():
        path.write_text(json.dumps(metadata, indent=2) + "\n")

    save()
    for gate in gates:
        env = runner.environment(gate["build"], gate["variant"], "nvidia")
        env.update(COIN_RENDER_REQUIRE_GL_REFERENCE="1",
                   COIN_RENDER_REQUIRE_CAMERA_REFERENCE="1",
                   COIN_GLX_PIXMAP_DIRECT_RENDERING="1")
        defs = [definitions[gate["build"]][name] for name in gate["tests"]]
        direct = gate["arguments"] is not None
        if direct:
            if len(defs) != 1:
                raise RuntimeError("A direct gate must have exactly one definition")
            command = [defs[0]["command"][0], *gate["arguments"]]
            props = properties(defs[0])
            for setting in props.get("ENVIRONMENT", []):
                key, value = setting.split("=", 1)
                env[key] = value
            cwd = Path(props.get("WORKING_DIRECTORY", str(args.root)))
        else:
            pattern = "^(" + "|".join(re.escape(name) for name in gate["tests"]) + ")$"
            command = ["ctest", "--test-dir", str(gate["build"]), "-R", pattern,
                       "--output-on-failure"]
            cwd = args.root
        item = dict(name=gate["name"], variant=gate["variant"], category=gate["category"],
                    command=command, working_directory=str(cwd), direct_cli_override=direct,
                    expected_executions=len(defs), ctest_definitions=defs,
                    executable_sha256={definition["command"][0]: sha256(Path(definition["command"][0]))
                                       for definition in defs},
                    environment={key: value for key, value in env.items()
                                 if key.startswith(("COIN_", "WGPU_", "__NV", "__GLX", "VK_", "LD_LIBRARY"))})
        if direct and gate["tests"] == ["CoinRenderCompositionTest"]:
            item["required_output_marker"] = {
                "--gpu": "CoinRenderCompositionTest GPU passed",
                "--borrow": "CoinRenderCompositionTest borrow passed",
            }.get(gate["arguments"][0])
        metadata["commands"].append(item)
        save()
        if source_revision(args.root) != revision:
            raise RuntimeError("Source HEAD changed before " + gate["name"])
        print("START", gate["name"], flush=True)
        try:
            result = subprocess.run(command, cwd=cwd, env=env, capture_output=True,
                                    text=True, timeout=360)
            log = result.stdout + result.stderr
            item["exit_code"] = result.returncode
        except subprocess.TimeoutExpired as error:
            log = str(error.stdout or "") + str(error.stderr or "")
            item.update(exit_code=None, timeout=True)
        (args.output / (gate["name"] + ".log")).write_text(log)
        item["skip_observed"] = bool(re.search(r"\[SKIP\]|\bSkipped\b|\bNot Run\b", log, re.IGNORECASE))
        item["source_revision_after"] = source_revision(args.root)
        if not direct:
            last = gate["build"] / "Testing/Temporary/LastTest.log"
            if last.is_file():
                (args.output / (gate["name"] + "-ctest.log")).write_bytes(last.read_bytes())
            passed = re.findall(r"\d+/\d+\s+Test\s+#\d+:\s+\S+\s+\.+\s+Passed\b", log)
            item["observed_passed_executions"] = len(passed)
            ctest_ok = "100% tests passed" in log and len(passed) == len(defs)
        else:
            ctest_ok = True
            item["observed_passed_executions"] = int(item["exit_code"] == 0)
        item["passed"] = (item["exit_code"] == 0 and not item["skip_observed"] and
                          ctest_ok and item["source_revision_after"] == revision and
                          (not item.get("required_output_marker") or
                           item["required_output_marker"] in log))
        save()
        print("END", gate["name"], item["exit_code"], flush=True)
        if not item["passed"]:
            raise RuntimeError(gate["name"] + " failed or skipped\n" + log[-4000:])
    metadata.update(complete=True,
                    passed_gate_executions=sum(item["observed_passed_executions"] for item in metadata["commands"]))
    if metadata["passed_gate_executions"] != expected:
        raise RuntimeError("Completed gate cardinality differs from expected")
    save()
    print("COMPLETE:", expected, "gate executions;", category_counts, flush=True)


if __name__ == "__main__":
    main()
