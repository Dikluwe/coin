#!/usr/bin/env python3
"""Run the focused geometry-overlay gates, serially and without accepting skips.

Prepared for coordinator execution only. All CTest definitions are recorded before
execution. Direct invocations record their registered definition and CLI override.
The compiled source revision is supplied by the owner; relevant source hashes
are checked before and after each command. Twelve gates run serially.
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


def source_hashes(root):
    names=("src/rendering/coinrender/CoinRenderFrameReuseCore.cpp", "src/rendering/coinrender/CoinRenderFrameReuseCore.h", "src/actions/CoinRenderAction.cpp", "testsuite/coinrender/CoinRenderFrameReuseCoreTest.cpp", "testsuite/actions/CoinRenderActionTest.cpp", "testsuite/coinrender/CoinRenderCompositionTest.cpp", "testsuite/coinwgpu/CoinWgpuMultiDeviceTest.cpp", "src/rendering/coinwgpu/rust_bridge/src/lib.rs", "src/rendering/coinwgpu/rust_bridge/src/instancing.rs")
    return {name:sha256(root/name) for name in names}


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


def gate_list(wg, bg, include_borrow=False):
    gates=[]
    def add(name,build,variant,names,category,arguments=None):
        gates.append(dict(name=name,build=build,variant=variant,tests=names,category=category,arguments=arguments))
    add("core",wg,"wgpu-vulkan",["CoinRenderFrameReuseCoreTest","CoinRenderFrameCoreTest","CoinRenderPlanAssemblyCoreTest"],"core_cpu")
    add("bgfx-reuse-core",bg,"bgfx-vulkan",["CoinRenderFrameReuseCoreTest"],"core_cpu")
    add("wgpu-action",wg,"wgpu-vulkan",["CoinRenderActionTest"],"action_reuse")
    add("bgfx-action",bg,"bgfx-vulkan",["CoinRenderActionTest"],"action_reuse")
    for variant in ("wgpu-vulkan","bgfx-vulkan","bgfx-opengl"):
        add(variant+"-composition-gpu",wg if variant.startswith("wgpu") else bg,variant,["CoinRenderCompositionTest"],"gpu",["--gpu"])
    add("wgpu-multidevice",wg,"wgpu-vulkan",["CoinWgpuMultiDeviceTest"],"gpu")
    add("bgfx-instancing-vulkan",bg,"bgfx-vulkan",["CoinBgfxInstancingTest"],"gpu")
    add("bgfx-instancing-opengl",bg,"bgfx-opengl",["CoinBgfxInstancingOpenGLTest"],"gpu")
    return gates


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path("/tmp/coin-render-first-frame"))
    parser.add_argument("--wgpu-build", type=Path,
                        default=Path("/tmp/coin-render-first-frame-wgpu"))
    parser.add_argument("--bgfx-build", type=Path,
                        default=Path("/tmp/coin-render-first-frame-bgfx"))
    parser.add_argument("--output", type=Path,
                        default=Path("/tmp/coin-render-geometry-overlay-gates"))
    parser.add_argument("--expected-source-content-revision",
                        required=True, help="Actual compiled production revision supplied by the coordinator")
    parser.add_argument("--dry-run", action="store_true",help="Print the twelve gates without CTest/GPU/Git or output writes")
    args = parser.parse_args()
    args.root = args.root.resolve()
    args.wgpu_build = args.wgpu_build.resolve()
    args.bgfx_build = args.bgfx_build.resolve()
    args.output = args.output.resolve()
    if re.fullmatch("[0-9a-f]{40}",args.expected_source_content_revision) is None:
        parser.error("Expected compiled source revision must be a complete SHA")
    if args.dry_run:
        gates=gate_list(args.wgpu_build,args.bgfx_build)
        print(json.dumps({"gates":gates,"expected_executions":sum(len(gate["tests"]) for gate in gates)},indent=2,default=str))
        return
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
    expected = 12
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
    for required_flag in ["--gpu"]:
        if '"' + required_flag + '"' not in source_text:
            raise RuntimeError("Composition test source lacks required mode " + required_flag)

    args.output.mkdir(exist_ok=False)
    revision = args.expected_source_content_revision
    source_files=source_hashes(args.root)
    metadata = dict(source_content_revision=None, source_content_revision_kind="mixed", source_snapshot_revision=revision, source_content_revisions_by_backend={"wgpu": revision, "bgfx": "96e5ed80fa3ab3c9016cf10e6bbfc9b638335a33"}, expected_gate_executions=expected,
                    expected_categories=category_counts, commands=[], complete=False,
                    source_files_sha256=source_files,source_revision_basis="owner supplied compiled content by backend; source hashes checked against the checkout snapshot for mutation",
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
        item = dict(name=gate["name"], variant=gate["variant"], category=gate["category"], source_content_revision=metadata["source_content_revisions_by_backend"]["wgpu" if gate["variant"].startswith("wgpu") else "bgfx"],
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
        if source_hashes(args.root) != source_files:
            raise RuntimeError("Source HEAD changed before " + gate["name"])
        print("START", gate["name"], flush=True)
        try:
            result = subprocess.run(command, cwd=cwd, env=env, capture_output=True,
                                    text=True, timeout=360)
            log = result.stdout + result.stderr
            item["exit_code"] = result.returncode
        except subprocess.TimeoutExpired as error:
            def as_text(value):return value.decode("utf-8",errors="replace") if isinstance(value,bytes) else (value or "")
            log = as_text(error.stdout) + as_text(error.stderr)
            item.update(exit_code=None, timeout=True)
        (args.output / (gate["name"] + ".log")).write_text(log)
        item["skip_observed"] = bool(re.search(r"\[SKIP\]|\bSkipped\b|\bNot Run\b", log, re.IGNORECASE))
        item["source_files_sha256_after"]=source_hashes(args.root)
        item["source_revision_after"]=revision if item["source_files_sha256_after"]==source_files else None
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
