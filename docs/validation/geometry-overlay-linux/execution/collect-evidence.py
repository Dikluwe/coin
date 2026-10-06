#!/usr/bin/env python3
"""Collect complete geometry-overlay evidence; never run builds, benchmarks or GPU.

All protocol/count/raw recomputation checks finish before any destination write.
Report/figures/inventory are produced separately by the coordinator.
"""
import argparse
from collections import Counter
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import shutil
import sys
import tempfile
sys.dont_write_bytecode = True
PREFIX='coin-render-geometry-overlay'
CURRENT='96e5ed80fa3ab3c9016cf10e6bbfc9b638335a33'
BASELINE='9a594fa39c7ca7924f9931863d4bdd7a37165cee'
BASELINE_SNAPSHOT='a2e19d360db9c4ef187550ae1513fddd0585a371'
CONTROL='4d63bb993022ee8d40802558b0871a4803002b8d'
COUNTS={'steady':{'processes':84,'measured_frames':1260,'warmup_frames':420},
        'ablation':{'processes':36,'measured_frames':252,'warmup_frames':108}}
VARIANTS=('coingl','bgfx-vulkan','bgfx-opengl','wgpu-vulkan')
CATEGORIES={'core_cpu':'Core (inclui integração GPU)','action_reuse':'Action/Reuse misto','gpu':'GPU dedicado'}


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


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root',type=Path,default=Path('/tmp/coin-render-first-frame'))
    parser.add_argument('--input-directory',type=Path,default=Path('/tmp'))
    parser.add_argument('--stage',type=Path)
    parser.add_argument('--metadata-overrides',type=Path,help='Optional descriptive notes/date only')
    args=parser.parse_args();root=args.root.resolve();directory=args.input_directory.resolve()
    stage=args.stage.resolve() if args.stage else root/'docs/validation/geometry-overlay-linux'
    require(not stage.exists(), 'Archive destination must be absent')
    source=lambda suffix:directory/(PREFIX+suffix)
    summary_directory=source('-steady-summary');summary=read(summary_path(summary_directory))
    analysis=read(source('-analysis/results.json'));contract=read(source('-diagnostic-contract.json'))
    gates=read(source('-gates/commands.json'));gate_commands,categories=gate_specification(gates)
    require(sum(categories.values())==12, 'Require actual12 final gate definitions')
    metadata=read(source('-report-config-template.json'))
    metadata.update(source_content_revision=CURRENT,baseline_source_content_revision=BASELINE,
        baseline_source_snapshot_revision=BASELINE_SNAPSHOT,coingl_source_content_revision=CONTROL,
        baseline_variant_source_content_revisions={variant:CONTROL if variant=='coingl' else BASELINE for variant in VARIANTS},
        expected_counts=COUNTS,diagnostic_contract=contract,
        scene_sha256=analysis['campaigns']['steady']['before']['metadata']['scene_sha256'],
        gate_commands=gate_commands,gate_counts={CATEGORIES[key]:value for key,value in categories.items()},
        build_count=2,rgb_comparisons=112,verification_ppm_files=224,verification_processes=32,all_rgb_identical=True)
    metadata['scope_notes']=[
        'Cidade de 40.000 objetos, offscreen 1024 × 1024, quatro variantes na mesma GPU NVIDIA; sem campanha de startup nesta etapa.',
        'Baseline Render medido em '+BASELINE+'; snapshot de organização/documentação '+BASELINE_SNAPSHOT+'. Atual '+CURRENT+'; controle CoinGL '+CONTROL+'.',
    ]
    metadata['implementation_notes']=[
        'Validação usa valores atuais, sem cache persistente ou confiança em revisão. Slots estritamente crescentes dispensam sort e scratch; na primeira inversão, runs consecutivos inclusivos são reconstruídos e apenas seus intervalos são ordenados.',
        'União usa uint64_t(last)+1. Sobreposição/duplicação recusa a transação. Capacidade opcional ≤65536 runs e ≤4×N bytes; cap/OOM usa o algoritmo literal de slots+sort após liberar scratch. Updates e undo mantêm a ordem original; mutações continuam após toda a validação.',
        'Optout privada COIN_RENDER_DISABLE_GEOMETRY_INTERVAL_VALIDATION=1 força o caminho literal. Materiais/transformações sem posições não consultam essa opção nem emitem evento geometry_overlay_validation.',
    ]
    metadata['limitations']=[
        'validation_ms cobre posições, preparação do undo, unicidade e limpeza do scratch; exclui fprintf, validação posterior de draws/modelos e as escritas finais do overlay. Não é tempo de whole overlay, sort isolado ou memcpy.',
        'scratch_bytes mede a capacidade do scratch de colisão usada na fase, sem undo obrigatório ou overhead do allocator. ordered usa zero; intervals ordena R runs; literal ordena N slots.',
        'Na ablação, o primeiro full capture não emite esta fase. Eventos são associados ao próximo marcador Action antes de usar as flags warmup do CSV; nenhum slice arbitrário da sequência bruta é tratado como medido.',
        'A comparação principal e a ablação preservam regressões e outliers. Medianas por processo/rodada, com amostras curtas, não estabelecem latência de cauda nem causalidade de toda variação do quadro.',
        'Snapshots de hardware antes/depois não acompanham clocks continuamente. Binários e PPMs não integram este arquivo de evidências; hashes e logs permanecem registrados.',
        'A terceira rodada teve outliers grandes, mantidos nos dados e nas faixas do gráfico. load-observation.json preserva um snapshot de carga durante essa rodada: a lista GPU amostrada mostrou nosso benchmark, e ps incluiu Codex/navegadores. Essa amostra não prova a causa da variação nem acompanha a carga de toda a campanha.',
    ]
    metadata['history_notes']=[
        'O bootstrap do executor de gates falhou ao localizar um caminho de fonte antes de executar qualquer teste. O caminho foi corrigido; diretório vazio, log e comandos da tentativa permanecem em history/, fora dos12 gates finais.',
        'A categoria registrada core_cpu é preservada no manifesto: suas quatro execuções são dois Core CPU (FrameCore e PlanAssembly) e duas integrações GPU (FrameReuseCore WG/BG, que executam Target/backend real). As outras execuções são dois Action/Reuse mistos e seis GPU dedicados; não há quatro Core CPU puros.',
    ]
    metadata['evidence_reference']='validation/geometry-overlay-linux/README.md'
    if args.metadata_overrides:
        overrides=read(args.metadata_overrides)
        require(set(overrides)<={'date','scope_notes','implementation_notes','limitations','history_notes','reviews'}, 'Only prose/date overrides allowed')
        metadata.update(overrides)
    validator=module('geometry_collect_preflight',source('-validate-archive.py'))
    focused,probe_manifest=validator.probe_check(source('-regression-probe'),source('-regression-probe.py'),
                                               root/'scripts/coinrender/run_animation_benchmark.py',metadata)
    metadata['focused_probe']=focused
    metadata['limitations'].append(
        'O probe suplementar usa a mesma build BGFX/Vulkan, geometry-10, sem trace: seis processos, 90 medidos e 30 warmups. Todas as três diferenças por par permanecem, inclusive o primeiro par positivo. Não substitui a campanha principal ou a ablação e não identifica a causa da variação observada.')
    snapshot_paths={'baseline':source('-baseline-binaries.json'),'final':source('-final-binaries.json'),
                    'coingl':directory/'coin-render-state-coingl-binaries.json'}
    facts,verified_analysis,verified_summary=validator.data_check(summary_directory,source('-analysis/results.json'),
        source('-diagnostics.py'),source('-analysis.py'),source('-ablation'),metadata,source('-gates'),
        snapshot_paths,source('-binary-hashes-post.json'),source('-build-commands.json'),extra_binary_manifests=[probe_manifest])
    facts['focused_probe']={'counts':focused['unique_counts'],'total_ms':focused['summary']['comparisons']['total_ms'],
                           'pairs_preserved':3,'raw_csv_log_recomputation_identical':True}
    require(verified_analysis==analysis and verified_summary==summary, 'Inputs changed during preflight')
    validator.copied_files_check(summary_directory)
    validator.bootstrap_check(source('-gates-bootstrap-initial'),source('-gates-run.log-bootstrap-initial'),
                              source('-execution-commands.json-bootstrap-initial'))
    final_observation=read(source('-diagnostic-observation.json'))
    initial_observation=read(source('-diagnostic-observation-initial.json'))
    require(final_observation['diagnostics']==analysis['diagnostics'] and
            final_observation['diagnostic_contract_input']['metadata']==contract and
            final_observation['diagnostic_contract_input']['input']['sha256']==sha(source('-diagnostic-contract.json')) and
            initial_observation['diagnostic_contract_input']['metadata']['configured'] is False and
            len(initial_observation['diagnostics'])==1 and not initial_observation['campaigns'],
            'Final/initial diagnostic observation provenance differs')
    initial=next(iter(initial_observation['diagnostics'].values()))
    require(initial['unique_counts']==COUNTS['ablation'] and initial['contract_observation_passed'] is True,
            'Initial diagnostic observation counts/proof differs')
    reporter=module('geometry_collect_report',source('-report.py'))
    reporter.validate(metadata,analysis,summary)
    for side in ('before','after'):
        require(read(source('-hardware-'+side+'.json')).get('commands'), 'Hardware snapshot missing')
    require(read(source('-hardware-after-main.json')).get('commands'), 'Hardware snapshot before quiet probe missing')
    require(read(source('-load-observation.json')), 'Round3 load snapshot missing')
    campaign_commands=read(source('-campaign-commands.json'))
    require(campaign_commands['source_content_revision']==CURRENT and
            campaign_commands['expected_unique_counts']==COUNTS['steady'] and
            len(campaign_commands['commands'])==1 and campaign_commands['commands'][0].get('exit_code')==0,
            'Matched orchestration completion differs')
    plan={};empty_directories=set()
    def add(path,relative):
        require(path.is_file() and relative not in plan and not Path(relative).is_absolute() and '..' not in Path(relative).parts,
                'Missing/duplicate/unsafe archive input: '+str(path))
        with path.open('rb') as stream:magic=stream.read(4)
        require(path.suffix.lower() not in {'.ppm','.so','.a','.o','.exe','.dll'} and magic!=b'\x7fELF', 'Do not archive pixels/binaries')
        plan[relative]={'original_path':str(path.resolve()),'bytes':path.stat().st_size,'sha256':sha(path)}
    def tree(path,relative):
        require(path.is_dir(), 'Missing evidence directory: '+str(path));files=sorted(child for child in path.rglob('*') if child.is_file())
        if not files:empty_directories.add(relative)
        for child in files:add(child,str(Path(relative)/child.relative_to(path)))
    tree(summary_directory,'steady');tree(source('-ablation'),'diagnostic');tree(source('-gates'),'gates')
    tree(source('-regression-probe'),'regression-probe')
    tree(source('-gates-bootstrap-initial'),'history/gates-bootstrap-initial')
    require(not any(source('-gates-bootstrap-initial').iterdir()), 'Bootstrap history unexpectedly contains test executions')
    for suffix,relative in (
        ('-analysis/results.json','analysis.json'),('-analysis.py','analyze-geometry-overlay.py'),
        ('-diagnostics.py','diagnostic/derive-diagnostic.py'),('-diagnostic-contract.json','diagnostic-contract.json'),
        ('-diagnostic-observation.json','diagnostic-observation.json'),
        ('-diagnostic-observation-initial.json','diagnostic-observation-initial.json'),
        ('-README.md','README.md'),('-report.py','plot-and-report.py'),('-validate-archive.py','validate-archive.py'),
        ('-baseline-binaries.json','binaries-before.json'),('-final-binaries.json','binaries-after.json'),
        ('-binary-hashes-post.json','binary-hashes-post-campaign.json'),('-hardware-before.json','hardware-before.json'),
        ('-hardware-after.json','hardware-after.json'),('-build-commands.json','build-commands.json'),
        ('-hardware-after-main.json','hardware-after-main.json'),('-regression-probe.py','regression-probe.py'),
        ('-load-observation.json','load-observation.json'),
        ('-campaign-commands.json','campaign-commands.json'),('-matched.py','execution/matched.py'),
        ('-run-matched.py','execution/run-matched.py'),('-ablation.py','execution/run-ablation.py'),
        ('-gates.py','execution/run-gates.py'),('-collect.py','execution/collect-evidence.py'),
        ('-report-config-template.json','execution/report-config-template.json'),
        ('-gates-run.log-bootstrap-initial','history/gates-run.log-bootstrap-initial'),
        ('-execution-commands.json-bootstrap-initial','history/execution-commands.json-bootstrap-initial')):
        add(source(suffix),relative)
    add(snapshot_paths['coingl'],'binaries-coingl.json')
    add(directory/'coin-render-wgpu-motion-matched.py','execution/matched-rows.py')
    add(root/'scripts/coinrender/run_animation_benchmark.py','execution/benchmark-runner.py')
    for item in read(source('-build-commands.json'))['commands']:add(Path(item['log']),'builds/'+Path(item['log']).name)
    for path in sorted(directory.glob(PREFIX+'-*-run.log')):add(path,'orchestration/'+path.name)
    for suffix,relative in (('-post.py','execution/post-campaign.py'),
                            ('-execution-commands.json','execution-commands.json'),
                            ('-verify-before-command.json','execution/verify-before-command.json'),
                            ('-verify-after-command.json','execution/verify-after-command.json')):
        if source(suffix).is_file():add(source(suffix),relative)
    # Counts, raw recomputation, proofs, gates, builds and all file hashes are
    # complete before creating any output directory.
    stage.parent.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.geometry-overlay-collect-',dir=stage.parent) as temporary:
        candidate=Path(temporary)/'stage';candidate.mkdir()
        for relative in empty_directories:(candidate/relative).mkdir(parents=True,exist_ok=True)
        for relative,item in plan.items():
            destination=candidate/relative;destination.parent.mkdir(parents=True,exist_ok=True)
            shutil.copy2(item['original_path'],destination)
            require(destination.stat().st_size==item['bytes'] and sha(destination)==item['sha256'], 'Archive input changed while copying')
        for name,data in (('stage-metadata.json',metadata),('collection-inputs.json',
            {'source_content_revision':CURRENT,'all_inputs_validated_before_writes':True,'copied_files':plan,
             'empty_directories':sorted(empty_directories),'validated_counts_and_proofs':facts})):
            (candidate/name).write_text(json.dumps(data,indent=2,ensure_ascii=False,allow_nan=False)+'\n')
        require(not stage.exists(), 'Destination appeared during collection');candidate.rename(stage)
    print('Collected',stage,'with',len(plan),'evidence files; no report generated')


if __name__=='__main__':main()
