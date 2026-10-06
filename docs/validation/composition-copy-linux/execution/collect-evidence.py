#!/usr/bin/env python3
"""Collect only complete composition-copy evidence; never launch tests or a GPU.

All inputs, counts, source cards and gate/build results are checked before any
archive file is written. The coordinator must finish and admit the diagnostic
contract, summaries, post-campaign hashes and final gates before invoking this.
The report/figures/inventory are generated separately after collection.
"""
import argparse
from collections import Counter
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
PREFIX = "coin-render-composition-copy"
BASELINE = "24bc92d8d60d3a1ce782e7787d2060a6b08261fb"
CONTROL = "4d63bb993022ee8d40802558b0871a4803002b8d"
PRODUCTION = "f6d05aedb83d01204c2ebb20fee78768a587e963"
FIRST_TEST_SOURCE = "56248de16058e3b8a6996a943ae06099002bf94f"
COUNTS = {
    "cold": {"processes": 63, "measured_frames": 63, "warmup_frames": 0},
    "steady": {"processes": 105, "measured_frames": 1575, "warmup_frames": 525},
    "ablation": {"processes": 36, "measured_frames": 36, "warmup_frames": 90},
}
PROBE_COUNTS = {"processes": 6, "measured_frames": 90, "warmup_frames": 30}
CATEGORIES = {"core_cpu": "Core puro", "action_reuse": "Action/Reuse misto", "gpu": "GPU"}
EXPECTED_CATEGORIES = {"core_cpu": 7, "action_reuse": 4, "gpu": 18}
VARIANTS = ("coingl", "bgfx-vulkan", "bgfx-opengl", "wgpu-vulkan")


def require(condition, message):
    if not condition:
        raise ValueError(message)


def read(path):
    require(path.is_file(), "Missing completed input: " + str(path))
    return json.loads(path.read_text(), parse_constant=lambda value:
                      (_ for _ in ()).throw(ValueError("Nonfinite JSON: " + value)))


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def module(name, path):
    require(path.is_file(), "Missing helper: " + str(path))
    spec = importlib.util.spec_from_file_location(name, path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


def summary_path(directory):
    paths = [directory / name for name in ("summary.json", "report-summary.json")
             if (directory / name).is_file()]
    require(len(paths) == 1, "Require one completed summary JSON in " + str(directory))
    return paths[0]


def gate_specification(manifest):
    specification, categories = {}, Counter()
    for record in manifest["commands"]:
        name = record["name"]
        require(name not in specification, "Repeated gate command name: " + name)
        category = record["category"]
        require(category in CATEGORIES, "Unknown actual gate category: " + category)
        tests = [definition["name"] for definition in record["ctest_definitions"]]
        require(tests and len(tests) == len(set(tests)), "Incomplete/duplicate actual gate definitions")
        entry = dict(tests=tests, category=CATEGORIES[category], source_category=category,
                     variant=record["variant"], direct_cli_override=record["direct_cli_override"])
        if entry["direct_cli_override"]:
            require(len(tests) == 1 and record["command"][0] == record["ctest_definitions"][0]["command"][0],
                    "Direct gate does not use its registered executable")
            entry["arguments"] = record["command"][1:]
            if record.get("required_output_marker"):
                entry["required_output_marker"] = record["required_output_marker"]
        specification[name] = entry
        categories[category] += len(tests)
    return specification, dict(categories)


def gate_status(manifest):
    executions = sum(len(record["ctest_definitions"]) for record in manifest["commands"])
    passed = sum(record["observed_passed_executions"] for record in manifest["commands"])
    return {"executions": executions, "passed": passed, "failed": executions - passed}


def checked_builds(directory, revision):
    path = directory / (PREFIX + "-build-commands.json")
    manifest = read(path)
    commands = manifest["commands"]
    require(len(commands) >= 7, "Build history is incomplete")
    required = {
        ("wgpu", PRODUCTION, PREFIX + "-build-wgpu.log"),
        ("wgpu", FIRST_TEST_SOURCE, PREFIX + "-build-wgpu-final.log"),
        ("bgfx", FIRST_TEST_SOURCE, PREFIX + "-build-bgfx.log"),
    }
    observed, records = set(), []
    for item in commands:
        command = item["command"]
        require(item.get("exit_code") == 0 and command[:2] == ["cmake", "--build"],
                "Build did not record a successful exit")
        build = Path(command[2])
        family = "wgpu" if build.name.endswith("-wgpu") else "bgfx" if build.name.endswith("-bgfx") else None
        require(family is not None, "Unexpected build family")
        source = item["source_revision_at_start"]
        require(re.fullmatch("[0-9a-f]{40}", source) is not None, "Build source SHA missing")
        log = Path(item["log"])
        require(log.is_file() and log.stat().st_size > 0, "Build log missing/empty: " + str(log))
        require(re.search(r"^FAILED:|ninja: build stopped|fatal error:", log.read_text(), re.M) is None,
                "Build log contains a failure: " + str(log))
        observed.add((family, source, log.name))
        records.append(dict(family=family, source_content_revision=source, exit_code=item["exit_code"],
                            command=command, archive_log="builds/" + log.name, sha256=sha(log)))
    require(required <= observed, "Initial WG f6 / incremental WG 562 / BG 562 builds are missing")
    require(all(any(record["family"] == family and record["source_content_revision"] == revision
                    for record in records) for family in ("wgpu", "bgfx")),
            "Final test-source builds are missing")
    return manifest, records


def binary_checks(snapshots, post, revision, manifests):
    require({name: len(value["files"]) for name, value in snapshots.items()} ==
            {"baseline": 8, "final": 8, "coingl": 4}, "Binary snapshot counts differ")
    require(snapshots["baseline"]["source_content_revision"] == BASELINE and
            snapshots["final"]["source_content_revision"] == revision and
            snapshots["coingl"]["source_content_revision"] == CONTROL, "Binary source cards differ")
    records = post["files"]
    require(post.get("all_unchanged") is True and len(records) == len({item["path"] for item in records}) == 20,
            "Post-campaign binary hashes are incomplete")
    verified, counts = {}, Counter()
    for item in records:
        require(item.get("unchanged") is True and item["sha256"] == item.get("post_sha256"),
                "A binary changed during the campaign")
        stage = item["stage"]
        require(stage in snapshots, "Unknown post-campaign binary stage")
        original = [record for record in snapshots[stage]["files"] if record["path"] == item["path"]]
        require(len(original) == 1 and original[0]["sha256"] == item["sha256"] and
                original[0]["role"] == item["role"], "Post-campaign hash/role differs from snapshot")
        expected_source = CONTROL if stage == "coingl" else revision if stage == "final" else BASELINE
        require(original[0].get("source_content_revision", snapshots[stage]["source_content_revision"]) == expected_source,
                "Per-binary source differs from source card")
        if "bytes" in original[0]:
            require(original[0]["bytes"] == item.get("bytes"), "Post-campaign binary size differs")
        verified[item["path"]] = item["sha256"]
        counts[stage] += 1
    require(dict(counts) == {"baseline": 8, "final": 8, "coingl": 4}, "Post-campaign binary categories differ")
    for manifest in manifests:
        for path, digest in manifest["binary_hashes"].items():
            candidates = (path, path + ".0.10") if path.endswith("/libCoin.so.80") else (path,)
            require(any(verified.get(candidate) == digest for candidate in candidates),
                    "Campaign binary absent/different in post-campaign snapshots: " + path)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path("/tmp/coin-render-first-frame"))
    parser.add_argument("--input-directory", type=Path, default=Path("/tmp"))
    parser.add_argument("--stage", type=Path,
                        help="Defaults to ROOT/docs/validation/composition-copy-linux; must be absent")
    parser.add_argument("--metadata-overrides", type=Path,
                        help="Optional scope/review/limitation prose; factual source/count cards are immutable")
    args = parser.parse_args()
    root, directory = args.root.resolve(), args.input_directory.resolve()
    stage = args.stage.resolve() if args.stage else root / "docs/validation/composition-copy-linux"
    require(not stage.exists(), "Use an absent stage directory")
    source = lambda suffix: directory / (PREFIX + suffix)
    snapshots = {"baseline": read(source("-baseline-binaries.json")),
                 "final": read(source("-final-binaries.json")),
                 "coingl": read(directory / "coin-render-state-coingl-binaries.json")}
    revision = snapshots["final"]["source_content_revision"]
    require(re.fullmatch("[0-9a-f]{40}", revision) is not None, "Final pinned source SHA missing")
    checkout = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip()
    require(checkout == revision, "Checkout HEAD differs from final pinned source; collect before documentation commits")
    summaries = {name: read(summary_path(source("-" + name + "-summary"))) for name in ("cold", "steady")}
    analysis = read(source("-analysis/results.json"))
    contract = read(source("-diagnostic-contract.json"))
    require(contract.get("configured") is True, "Diagnostic contract has not been admitted from completed observations")
    require(set(analysis["campaigns"]) == {"cold", "steady"} and len(analysis["diagnostics"]) == 1,
            "Analysis campaign/diagnostic coverage differs")
    require(analysis.get("unique_counts") == {"processes": 168, "measured_frames": 1638, "warmup_frames": 525} and
            analysis.get("campaign_counts_match_expected") is True, "Aggregate timing counts are incomplete")
    manifests = []
    for name in ("cold", "steady"):
        campaign = analysis["campaigns"][name]
        require(campaign.get("complete_and_comparable") is True and campaign["unique_counts"] == COUNTS[name],
                "Incomplete/incomparable timing campaign: " + name)
        for side in ("before", "after"):
            manifest = campaign[side]["metadata"]
            expected_sources = {variant: CONTROL if variant == "coingl" else BASELINE if side == "before" else revision
                                for variant in VARIANTS}
            require(manifest["variant_source_content_revisions"] == expected_sources, "Timing source map differs")
            require(all(item.get("exit_code") == 0 and not item.get("timed_out") for item in manifest["commands"]),
                    "Timing process failed or timed out")
            manifests.append(manifest)
        require(all(item.get("counted_once") is True and item.get("csv_sha256_equal") is True and
                    item.get("log_sha256_equal") is True for item in campaign["shared_controls"]),
                "Shared CoinGL control identity is not proven")
    diagnostic = next(iter(analysis["diagnostics"].values()))
    require(diagnostic.get("complete_and_comparable") is True and diagnostic["unique_counts"] == COUNTS["ablation"] and
            diagnostic.get("contract_observation_passed") is True and diagnostic.get("contract") == contract,
            "Diagnostic pairing/counts/observed contract are incomplete")
    require(analysis["diagnostic_contract_input"]["metadata"] == contract, "Analysis used a different diagnostic contract")
    observation = read(source("-diagnostic-observation.json"))
    require(observation["diagnostics"] == analysis["diagnostics"],
            "Admitted diagnostic observation differs from final analysis")
    inclusive = read(source("-inclusive-description.json"))
    inclusive_helper = module("composition_inclusive_preflight", source("-inclusive-analysis.py"))
    require(inclusive.get("process_count") == 36 and inclusive.get("all_sources_cardinality_matches_csv") is True,
            "Descriptive inclusive subtotal is incomplete/unaligned")
    require(inclusive["provenance"]["input"]["sha256"] == sha(source("-diagnostic-observation.json")) and
            inclusive["provenance"]["script"]["sha256"] == sha(source("-inclusive-analysis.py")),
            "Descriptive subtotal input/helper provenance differs")
    require({key: value for key, value in inclusive.items() if key != "provenance"} == inclusive_helper.derive(observation),
            "Descriptive inclusive subtotal recomputation differs")
    manifests.append(diagnostic["metadata_and_raw_trace"]["command_metadata"])
    probe_contract = read(source("-material-probe-contract.json"))
    probe_observation = read(source("-material-probe-observation.json"))
    require(probe_contract.get("configured") is True and not probe_observation["campaigns"] and
            len(probe_observation["diagnostics"]) == 1, "Supplemental probe contract/observation is incomplete")
    probe = next(iter(probe_observation["diagnostics"].values()))
    require(probe.get("complete_and_comparable") is True and probe.get("contract_observation_passed") is True and
            probe["unique_counts"] == PROBE_COUNTS and probe["contract"] == probe_contract,
            "Supplemental probe pairing/counts/proof differs")
    probe_manifest = probe["metadata_and_raw_trace"]["command_metadata"]
    require(probe_manifest["source_content_revision"] == revision and
            probe_manifest["scene_sha256"] == analysis["campaigns"]["cold"]["before"]["metadata"]["scene_sha256"] and
            len(probe_manifest["commands"]) == 6 and
            all(item.get("exit_code") == 0 and not item.get("timed_out") and
                item.get("adapter_verified_nvidia") is True and item["source_content_revision"] == revision
                for item in probe_manifest["commands"]), "Supplemental probe source/status/GPU differs")
    require(len(probe["metadata_and_raw_trace"]["runs"]) == 6 and
            all(run.get("composition_contract_passed") is True and run["composition_contract_checks"] and
                all(check.get("passed") is True for check in run["composition_contract_checks"])
                for run in probe["metadata_and_raw_trace"]["runs"].values()), "Supplemental probe six proofs differ")
    manifests.append(probe_manifest)
    gates = read(source("-gates/commands.json"))
    gate_commands, gate_categories = gate_specification(gates)
    require(gate_categories == EXPECTED_CATEGORIES, "Final gate category count differs")
    require(gates.get("complete") is True and gates.get("passed_gate_executions") == 29,
            "Final 29 gates are not complete")
    build_manifest, build_runs = checked_builds(directory, revision)
    post = read(source("-binary-hashes-post.json"))
    hardware = {side: read(source("-hardware-" + side + ".json")) for side in ("before", "after")}
    require(all(snapshot.get("commands") for snapshot in hardware.values()), "Hardware snapshots are missing")
    require(read(source("-hardware-after-main.json")).get("commands"), "Pre-probe hardware snapshot missing")
    verify = summaries["steady"]["campaigns"]
    require(all(label in verify for label in ("verify-before", "verify-after")), "Verification summaries missing")
    for label in ("verify-before", "verify-after"):
        dataset = verify[label]
        require(len(dataset["processes"]) == len(dataset["groups"]) == 28, "Verification process count differs")
        require(sum(item["measured_frames"] for item in dataset["processes"]) == 196 and
                sum(item["warmup_frames"] for item in dataset["processes"]) == 0, "Verification sample count differs")
        manifests.append(dataset["manifest"])
    rgb = summaries["steady"]["rgb_before_after"]
    require(rgb == read(source("-steady-summary/rgb-before-after.json")) and rgb.get("all_rgb_identical") is True and
            rgb["comparison_count"] == len(rgb["results"]) == 196, "RGB comparisons are incomplete/different")
    binary_checks(snapshots, post, revision, manifests)
    metadata = read(source("-report-config-template.json"))
    metadata.update(source_content_revision=revision, production_change_revision=PRODUCTION,
                    baseline_source_content_revision=BASELINE,
                    baseline_variant_source_content_revisions={variant: CONTROL if variant == "coingl" else BASELINE for variant in VARIANTS},
                    coingl_source_content_revision=CONTROL, diagnostic_contract=contract,
                    scene_sha256=analysis["campaigns"]["cold"]["before"]["metadata"]["scene_sha256"],
                    expected_counts=COUNTS, gate_commands=gate_commands,
                    gate_counts={CATEGORIES[category]: count for category, count in gate_categories.items()},
                    all_rgb_identical=True, rgb_comparisons=196, verification_ppm_files=392, verification_processes=56,
                    build_runs=build_runs, historical_gate_runs=[])
    validator = module("composition_archive_preflight", source("-validate-archive.py"))
    analysis_helper = module("composition_probe_preflight", source("-analysis.py"))
    trace_helper = module("composition_probe_trace_preflight", source("-diagnostics.py"))
    require(analysis_helper.read_diagnostic(trace_helper, source("-material-probe"), probe_contract) == probe,
            "Supplemental probe raw CSV/trace recomputation differs")
    require(probe_observation["analysis_metadata"]["script_sha256"] == sha(source("-analysis.py")) and
            probe_observation["analysis_metadata"]["trace_helper_sha256"] == sha(source("-diagnostics.py")),
            "Supplemental probe helper provenance differs")
    metadata["supplemental_probe"] = dict(directory="supplemental/material-probe",
        contract="supplemental/material-probe-contract.json", observation="supplemental/material-probe-observation.json",
        runner="supplemental/material-probe.py", expected_counts=PROBE_COUNTS)
    for name in ("cold", "steady"):
        for side in ("before", "after"):
            validator.sources_check(analysis["campaigns"][name][side]["metadata"], side, metadata)
    for label in ("verify-before", "verify-after"):
        validator.sources_check(verify[label]["manifest"], label.removeprefix("verify-"), metadata)
    final_status = validator.gate_check(source("-gates"), metadata, gate_commands)
    require(final_status["status_counts"] == {"executions": 29, "passed": 29, "failed": 0}, "Final gate outcomes differ")
    historical_directories = sorted(directory.glob(PREFIX + "-gates*initial"))
    required_history = {PREFIX + "-gates-initial", PREFIX + "-gates-retry-initial"}
    require(required_history <= {path.name for path in historical_directories}, "Initial gate attempts are missing")
    for original in historical_directories:
        manifest = read(original / "commands.json")
        commands, _ = gate_specification(manifest)
        status = gate_status(manifest)
        if original.name in required_history:
            require(status == {"executions": 4, "passed": 3, "failed": 1}, "Initial gate attempt count differs")
        validator.gate_check(original, metadata, commands, status, manifest["source_content_revision"])
        metadata["historical_gate_runs"].append(dict(directory=original.name.removeprefix(PREFIX + "-"),
            source_content_revision=manifest["source_content_revision"], commands=commands, expected_status_counts=status))
    metadata["scope_notes"] = [
        "Cidade de 40.000 objetos, offscreen 1024 × 1024, quatro variantes na mesma GPU NVIDIA. Metadados e snapshots de hardware antes/depois estão preservados; clocks não foram acompanhados em cada amostra.",
        "Mudança de produção: `" + PRODUCTION + "`. A fonte atual inclui as correções dos oracles e está vinculada aos binários congelados.",
    ]
    metadata["gates"] = [label + ": " + str(count) + " execuções finais aprovadas sem skips."
                         for label, count in metadata["gate_counts"].items()]
    metadata["history_notes"] = [
        "As duas primeiras tentativas tiveram quatro execuções cada: três passaram e o novo fixture Action falhou. A primeira não admitia overlays por usar Material fora do escopo isolado do objeto. A segunda esperava sucesso no retry sem recuperar o Target: BACKEND_ERROR deixa TARGET_ERROR e bloqueia apply até resize. O oracle agora cobre bloqueio, recuperação pública e sucesso. As expectativas/estrutura do teste foram corrigidas, sem mudança adicional de produção. Comandos, fontes e resultados de cada tentativa permanecem separados dos 29 gates finais.",
        str(len(build_runs)) + " builds com exit_code=0 são preservados em build-commands.json e builds/: WG inicial na produção f6, WG incremental e BG na fonte 562, seguidos dos builds dos oracles corrigidos. O fixture BG foi ajustado para PHONG antes das execuções de teste.",
    ]
    metadata["reviews"] = [
        "Revisão independente: profile/admission antes da ativação do recibo, restauração por RAII, wrapper local não copiável/não movível, bytes dos itens preservados e ausência de referência em tickets/caches/Rust. Os caminhos gerais de composição e anexação de sombras permanecem owned.",
        "Oracles com optout comparam composição, payloads, status e diagnóstico; os testes dos consumidores exigem instanciação efetiva e igualdade dos arrays/header. O fixture Action cobre captura real, overlays admitidos, REUSE, falha, rollback e retry.",
    ]
    metadata["limitations"] += [
        "A optout privada é conjunta para Target e schedule. A ablação não produz intervenções independentes das duas cópias; seus counters/intervalos descrevem partes instrumentadas do mesmo experimento.",
        "SCREEN_DOOR, incluindo nível zero, permanece fora do perfil de empréstimo. Esta campanha usa SORTED_OBJECT_BLEND com materiais opacos e preserva os campos e a ordem da composição opaca, incluindo sortObject.",
        "Os PPMs e os binários não integram o Git. A validação relocável confere métricas/hashes e logs de captura; só reabre pixels se os arquivos originais ainda estiverem disponíveis.",
        "Cold conserva todos os outliers. Processos novos na mesma sessão não reinicializam o driver/computador. Amostras curtas não estabelecem latência de cauda; regressões steady permanecem no relatório.",
    ]
    total_pair = probe["on_off_comparisons"]["bgfx-vulkan|materials-10"]["csv_measured_medians_ms"]["total_ms"]
    transfer_pair = probe["on_off_comparisons"]["bgfx-vulkan|materials-10"]["phase_measured_medians_ms"]["composition_transfer.copy_lookup_ms"]
    metadata["limitations"] += [
        "Probe suplementar BGFX/Vulkan materials-10 na mesma build, três pares on/off: seis processos, 90 quadros medidos e 30 warmups, separados de matched168 e ablação36. Total off→on " +
        f"{total_pair['before']:.6f}→{total_pair['after']:.6f} ms ({total_pair['delta']:+.6f} ms; {total_pair['change_percent']:+.3f}%). " +
        f"Transferência/lookup {transfer_pair['before']:.6f}→{transfer_pair['after']:.6f} ms. " +
        "O aumento de +2,997 ms (+5,72%) observado na comparação principal não se repetiu com essa magnitude no probe; a variação positiva do probe permanece registrada. Ele não substitui a campanha principal nem estabelece causalidade. hardware-after-main.json preserva o snapshot anterior ao probe; hardware-after.json e os 20 hashes pós-campanha foram atualizados após ele.",
    ]
    metadata["ablation_notes"] += [
        "diagnostic-observation.json preserva a observação do contrato admitido. inclusive-description.json e inclusive-analysis.py preservam, separadamente, o subtotal descritivo de classify+sort+finish mais transferência/lookup, somado por linha medida. Esse subtotal inclui a classificação existente, não mede overhead incremental isolado e não substitui o primário transfer+lookup nem total_ms.",
        "composition_schedule_copy.copy_ms inclui a realização literal completa do schedule, não apenas memcpy. A variação líquida conjunta é lida de total_ms; a classificação e o subtotal inclusivo permanecem evidência descritiva.",
    ]
    if args.metadata_overrides:
        overrides = read(args.metadata_overrides)
        allowed = {"date", "scope_notes", "reviews", "limitations", "history_notes"}
        require(set(overrides) <= allowed, "Overrides may only replace descriptive prose/date")
        metadata.update(overrides)
    reporter = module("composition_report_preflight", source("-report.py"))
    reporter.validate(metadata, analysis, summaries["cold"], summaries["steady"])

    # Prepare and hash the entire copy plan before opening the destination.
    plan = {}

    def add(path, relative):
        require(path.is_file(), "Missing archive input: " + str(path))
        require(relative not in plan and not Path(relative).is_absolute() and ".." not in Path(relative).parts,
                "Duplicate/unsafe archive destination: " + relative)
        with path.open("rb") as stream:
            magic = stream.read(4)
        require(path.suffix.lower() not in {".ppm", ".so", ".a", ".o", ".exe", ".dll"} and magic != b"\x7fELF",
                "Pixels/binaries must remain outside the archive: " + str(path))
        plan[relative] = dict(original_path=str(path.resolve()), bytes=path.stat().st_size, sha256=sha(path))

    def tree(path, relative):
        require(path.is_dir(), "Missing archive directory: " + str(path))
        for child in sorted(path.rglob("*")):
            if child.is_file():
                add(child, str(Path(relative) / child.relative_to(path)))

    for name in ("cold", "steady"):
        folder = source("-" + name + "-summary")
        validator.copied_files_check(folder)
        tree(folder, name)
    tree(source("-ablation"), "diagnostic")
    tree(source("-material-probe"), "supplemental/material-probe")
    tree(source("-gates"), "gates")
    for original in historical_directories:
        tree(original, original.name.removeprefix(PREFIX + "-"))
    for suffix, relative in (
        ("-analysis/results.json", "analysis.json"),
        ("-analysis.py", "analyze-composition-copy.py"),
        ("-diagnostics.py", "diagnostic/derive-diagnostic.py"),
        ("-diagnostic-contract.json", "diagnostic-contract.json"),
        ("-diagnostic-observation.json", "diagnostic-observation.json"),
        ("-inclusive-description.json", "inclusive-description.json"),
        ("-inclusive-analysis.py", "inclusive-analysis.py"),
        ("-README.md", "README.md"),
        ("-report.py", "plot-and-report.py"),
        ("-validate-archive.py", "validate-archive.py"),
        ("-baseline-binaries.json", "binaries-before.json"),
        ("-final-binaries.json", "binaries-after.json"),
        ("-binary-hashes-post.json", "binary-hashes-post-campaign.json"),
        ("-hardware-before.json", "hardware-before.json"),
        ("-hardware-after.json", "hardware-after.json"),
        ("-hardware-after-main.json", "hardware-after-main.json"),
        ("-material-probe.py", "supplemental/material-probe.py"),
        ("-material-probe-contract.json", "supplemental/material-probe-contract.json"),
        ("-material-probe-observation.json", "supplemental/material-probe-observation.json"),
        ("-campaign-commands.json", "campaign-commands.json"),
        ("-build-commands.json", "build-commands.json"),
        ("-matched.py", "execution/matched.py"),
        ("-run-matched.py", "execution/run-matched.py"),
        ("-ablation.py", "execution/run-ablation.py"),
        ("-gates.py", "execution/run-gates.py"),
        ("-collect.py", "execution/collect-evidence.py"),
        ("-post.py", "execution/post-campaign.py"),
        ("-report-config-template.json", "execution/report-config-template.json"),
    ):
        add(source(suffix), relative)
    add(directory / "coin-render-state-coingl-binaries.json", "binaries-coingl.json")
    add(directory / "coin-render-wgpu-motion-matched.py", "execution/matched-rows.py")
    add(root / "scripts/coinrender/run_animation_benchmark.py", "execution/benchmark-runner.py")
    for item in build_manifest["commands"]:
        path = Path(item["log"])
        add(path, "builds/" + path.name)
    for path in sorted(directory.glob(PREFIX + "-*-run.log")):
        add(path, "orchestration/" + path.name)
    require(any(key.startswith("orchestration/") for key in plan), "Orchestration logs are missing")
    # Every acceptance/cardinality check above has finished before any write.
    stage.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".composition-copy-collect-", dir=stage.parent) as temporary:
        candidate = Path(temporary) / "stage"
        candidate.mkdir()
        for relative, item in plan.items():
            destination = candidate / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(item["original_path"], destination)
            require(destination.stat().st_size == item["bytes"] and sha(destination) == item["sha256"],
                    "Input changed while copying: " + item["original_path"])
        (candidate / "stage-metadata.json").write_text(json.dumps(metadata, indent=2, ensure_ascii=False, allow_nan=False) + "\n")
        (candidate / "collection-inputs.json").write_text(json.dumps(
            dict(source_content_revision=revision, all_inputs_validated_before_writes=True,
                 copied_files=plan, matched_unique_counts=analysis["unique_counts"],
                 diagnostic_unique_counts=diagnostic["unique_counts"], supplemental_probe_unique_counts=probe["unique_counts"],
                 final_gate_results=final_status),
            indent=2, ensure_ascii=False, allow_nan=False) + "\n")
        require(not stage.exists(), "Destination appeared during collection")
        candidate.rename(stage)
    print("Collected", stage, "from", revision, "with", len(plan), "copied evidence files")


if __name__ == "__main__":
    main()
