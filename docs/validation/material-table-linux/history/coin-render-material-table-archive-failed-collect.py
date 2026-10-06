#!/usr/bin/env python3
"""Collect complete material-table evidence, after read-only CPU preflight.

No build/GPU/benchmark/network/Git operation is performed. No destination is
written until all campaigns, raw traces, image records, gates and pins pass.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import sys
import tempfile

sys.dont_write_bytecode = True
PREFIX = 'coin-render-material-table'


def require(condition, message):
    if not condition:
        raise ValueError(message)


def read(path):
    return json.loads(Path(path).read_text())


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


def defaults(directory, repository):
    prefix = lambda suffix: directory / (PREFIX + suffix)
    return {
        'quiet': directory / 'coin-render-material-study-quiet',
        'quiet_helper': directory / 'coin-render-material-study-campaign.py',
        'diagnostic': prefix('-diagnostic'), 'diagnostic_results': prefix('-diagnostic-analysis.json'),
        'diagnostic_helper': prefix('-diagnostic.py'), 'gates': prefix('-gates'),
        'diagnostic_analysis_command': prefix('-diagnostic-analysis-command.json'),
        'diagnostic_analysis_log': prefix('-diagnostic-analysis.log'),
        'build_commands': prefix('-build-commands.json'),
        'build_logs': {'build-wgpu': prefix('-build-wgpu.log'), 'cargo-cpu': prefix('-cargo-cpu.log')},
        'cards': {'baseline': prefix('-baseline-binaries.json'), 'after': prefix('-after-binaries.json'),
                  'coingl': directory / 'coin-render-state-coingl-binaries.json'},
        'binary_post': prefix('-binary-hashes-post.json'),
        'scene_cards': directory / 'coin-render-material-slot-scenes.json', 'scenes_post': prefix('-scenes-post.json'),
        'generator': repository / 'scripts/coinrender/generate_material_slot_scene.py',
        'generator_test_card': prefix('-generator-tests.json'), 'generator_test_log': prefix('-generator-tests.log'),
        'rgb': prefix('-rgb.json'),
        'rgb_directories': {(side, scene): prefix('-rgb-' + side + '-' + scene) for side in ('before', 'after') for scene in ('original', 'large')},
        'rgb_wrappers': {side: prefix('-rgb-' + side + '-commands.json') for side in ('before', 'after')},
        'rgb_wrapper_logs': {(side, scene): prefix('-rgb-' + side + '-' + scene + '-run.log') for side in ('before', 'after') for scene in ('original', 'large')},
        'cross': prefix('-rgb-cross-large'), 'cross_controls_command': prefix('-rgb-large-controls-command.json'),
        'cross_control_directory': prefix('-rgb-large-controls'),
        'cross_controls_log': prefix('-rgb-large-controls-run.log'),
        'cross_analysis_command': prefix('-rgb-cross-analysis-command.json'), 'cross_analysis_log': prefix('-rgb-cross-analysis.log'),
        'coingl_probe': prefix('-coingl-gpu-probe.json'), 'coingl_probe_log': prefix('-coingl-gpu-probe.log'),
        'bgfx_diagnostic': directory / 'coin-render-bgfx-regression-diagnostic',
        'bgfx_results': directory / 'coin-render-bgfx-regression-diagnostic-analysis.json',
        'bgfx_analyzer': directory / 'coin-render-bgfx-regression-diagnostic-analyze.py',
        'bgfx_parser': directory / 'coin-render-geometry-overlay-diagnostics.py',
        'hardware_before': prefix('-hardware-before.json'), 'hardware_after': prefix('-hardware-after.json')}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path('/tmp/coin-render-first-frame'))
    parser.add_argument('--input-directory', type=Path, default=Path('/tmp'))
    parser.add_argument('--stage', type=Path)
    parser.add_argument('--metadata-overrides', type=Path, help='Prose/date only; performance facts are derived')
    parser.add_argument('--preflight-only', action='store_true', help='CPU checks and plan only; write nothing')
    args = parser.parse_args()
    repository, directory = args.root.resolve(), args.input_directory.resolve()
    stage = args.stage.resolve() if args.stage else repository / 'docs/validation/material-table-linux'
    require(not stage.exists(), 'Preserve previous evidence: stage must be absent')
    paths = defaults(directory, repository)
    validator_path = directory / (PREFIX + '-validate-archive.py')
    reporter_path = directory / (PREFIX + '-report.py')
    validator = module('material_collect_validator', validator_path)
    reporter = module('material_collect_reporter', reporter_path)
    # Expensive read-only recomputation occurs after all runtime is complete.
    quiet = validator.quiet_check(paths['quiet'], paths['quiet_helper'])
    diagnostic, diagnostic_cards = validator.diagnostic_check(paths['diagnostic'], paths['diagnostic_results'], paths['diagnostic_helper'])
    analysis_command = read(paths['diagnostic_analysis_command'])
    require(analysis_command['exit_code'] == 0 and analysis_command['script_sha256'] == sha(paths['diagnostic_helper']) and
            analysis_command['input_manifest_sha256'] == sha(paths['diagnostic'] / 'commands.json') and
            analysis_command['output_sha256'] == sha(paths['diagnostic_results']) and
            analysis_command['log_sha256'] == sha(paths['diagnostic_analysis_log']), 'Diagnostic analyzer execution/hash proof differs')
    rgb = validator.rgb_check(paths['rgb_directories'], paths['rgb'], paths['rgb_wrappers'], paths['rgb_wrapper_logs'], require_pixels=True)
    cross = validator.cross_rgb_check(paths['cross'], paths['cross_controls_command'], paths['cross_controls_log'],
                                       paths['cross_analysis_command'], paths['cross_analysis_log'], require_pixels=True)
    gates = validator.gate_check(paths['gates'])
    builds = validator.build_check(paths['build_commands'], paths['build_logs'])
    binaries = validator.binary_check(paths['cards'], paths['binary_post'])
    measurement_manifests = [read(paths['quiet'] / 'commands.json'), read(paths['diagnostic'] / 'commands.json')]
    measurement_manifests += [read(path / 'manifest.json') for path in paths['rgb_directories'].values()]
    measurement_manifests.append(read(paths['cross_control_directory'] / 'manifest.json'))
    validator.binary_manifest_check(paths['cards'], measurement_manifests)
    scene_cards = read(paths['scene_cards'])
    scene_paths = {item['scene']['path']: Path(item['scene']['path']) for item in scene_cards['scenes']}
    original = scene_cards['scenes'][0]['source_city']
    scene_paths[original['path']] = Path(original['path'])
    scenes = validator.scene_check(paths['scene_cards'], paths['scenes_post'], scene_paths, paths['generator'])
    generator_sources = {name: repository / name for name in read(paths['generator_test_card'])['source_files_sha256']}
    generator_tests = validator.generator_tests_check(paths['generator_test_card'], paths['generator_test_log'], generator_sources)
    bgfx = validator.bgfx_diagnostic_check(paths['bgfx_diagnostic'], paths['bgfx_results'], paths['bgfx_analyzer'], paths['bgfx_parser'])
    probe = read(paths['coingl_probe'])
    require(probe['exit_code'] == 0 and probe['source_content_revision'] == validator.CONTROL and
            sha(paths['coingl_probe_log']) == probe['log_sha256'] and probe['environment']['COIN_DEBUG_GLGLUE'] == '1',
            'CoinGL context probe failed/source/hash differs')
    text = paths['coingl_probe_log'].read_text()
    require('NVIDIA' in text and 'RTX 3060' in text, 'CoinGL context probe did not identify NVIDIA RTX3060')
    for name in ('hardware_before', 'hardware_after'):
        require(read(paths[name]).get('commands'), 'Hardware snapshot missing: ' + name)
    metadata = read(directory / (PREFIX + '-report-config-template.json'))
    metadata.update(configured=True, baseline_source_content_revision=validator.BASELINE,
                    after_wgpu_source_content_revision=validator.CURRENT, coingl_source_content_revision=validator.CONTROL,
                    diagnostic_cards=diagnostic_cards, gates=gates, builds=builds, binary_proof=binaries, scene_proof=scenes,
                    generator_tests=generator_tests,
                    rgb_cross=cross, bgfx_regression_diagnostic=bgfx,
                    coingl_context_probe={'exit_code': 0, 'identified_nvidia_rtx3060': True, 'log_sha256': sha(paths['coingl_probe_log']),
                                          'scope': 'Separate context-identification process; no performance claim'})
    metadata['limitations'] = [
        'Amostra curta: três processos por papel/caso; cada valor é mediana de medianas por processo. Faixas mantêm todos os processos, sem descarte de outliers.',
        'Controles são compartilhados e executados uma vez por caso/rodada. Quiet não emite timers de fase; ablação trace é separada e não atribui causalidade ao total quiet.',
        'O cenário estático usa 30 warmups/120 quadros; dinâmicos usam 5/15. Métricas de primeiro quadro dos logs são descritivas e não constituem uma nova campanha de startup.',
        'Quiet CoinGL reporta adapter=not-queried; a seleção NVIDIA é pedida por environment. Um processo separado com debug de contexto confirmou NVIDIA RTX3060/OpenGL, sem fornecer uma série de desempenho adicional.',
    ]
    metadata['history_notes'] = [
        'A primeira tentativa CPU de análise cross usou um diretório como --reference, embora a opção espere uma variante; foi corrigida para agregação explícita. Nenhum processo GPU falhou nem foi repetido por esse erro de análise.',
        'O runner quiet executado é preservado com seu SHA original. Um snapshot independente dos quatro inputs de cena após todo runtime comprova hashes antes/depois; nenhum manifesto executado foi alterado retroativamente.',
    ]
    bootstrap = directory / (PREFIX + '-diagnostic-bootstrap.json')
    bootstrap_log = directory / (PREFIX + '-diagnostic-run.log-bootstrap-initial')
    if bootstrap.exists():
        history = read(bootstrap)
        require(history['exit_code'] == 1 and history['renderer_processes'] == 0 and
                sha(bootstrap_log) == history['log_sha256'], 'Diagnostic argument-bootstrap history differs')
        metadata['history_notes'].append('A primeira chamada do diagnóstico omitiu caminhos de cenas obrigatórios e parou na validação do plano, antes de criar renderer/processos. Após corrigir apenas a CLI, os 50 processos finais foram executados; o bootstrap de zero processos fica em history/, fora dessas contagens.')
    if args.metadata_overrides:
        overrides = read(args.metadata_overrides)
        require(set(overrides) <= {'date', 'limitations', 'history_notes', 'scope_notes', 'reviews'}, 'Only prose/date metadata overrides allowed')
        metadata.update(overrides)
    reporter.validate(metadata, quiet, diagnostic, diagnostic, rgb)
    plan = {}
    def add(path, relative):
        path = Path(path)
        require(path.is_file() and relative not in plan and not Path(relative).is_absolute() and '..' not in Path(relative).parts,
                'Missing/duplicate/unsafe input: ' + str(path))
        with path.open('rb') as stream:
            magic = stream.read(4)
        require(path.suffix.lower() not in {'.ppm', '.so', '.a', '.o', '.exe', '.dll'} and magic != b'\x7fELF', 'Do not archive pixels/binaries')
        plan[relative] = {'original_path': str(path.resolve()), 'bytes': path.stat().st_size, 'sha256': sha(path)}
    def tree(path, relative):
        require(path.is_dir(), 'Evidence directory missing: ' + str(path))
        for child in sorted(path.rglob('*')):
            if child.is_file() and child.suffix.lower() != '.ppm':
                add(child, str(Path(relative) / child.relative_to(path)))
    tree(paths['quiet'], 'quiet')
    tree(paths['diagnostic'], 'diagnostic')
    tree(paths['gates'], 'gates')
    tree(paths['cross'], 'rgb/cross-large')
    add(paths['cross_control_directory'] / 'manifest.json', 'rgb/large-control-manifest.json')
    add(paths['cross_control_directory'] / 'results.json', 'rgb/large-control-results.json')
    for (side, scene), path in paths['rgb_directories'].items():
        tree(path, 'rgb/' + side + '-' + scene)
    tree(paths['bgfx_diagnostic'], 'supplemental/bgfx-regression')
    mapping = {
        'diagnostic_results': 'diagnostic-analysis.json', 'rgb': 'rgb/before-after.json',
        'diagnostic_analysis_command': 'execution/diagnostic-analysis-command.json',
        'diagnostic_analysis_log': 'execution/diagnostic-analysis.log',
        'build_commands': 'execution/build-commands.json', 'binary_post': 'binary-hashes-post.json',
        'scene_cards': 'scenes/cards.json', 'scenes_post': 'scenes/post.json', 'generator': 'generate-material-slot-scene.py',
        'generator_test_card': 'execution/generator-tests.json', 'generator_test_log': 'execution/generator-tests.log',
        'cross_controls_command': 'execution/rgb-controls-command.json', 'cross_controls_log': 'execution/rgb-controls.log',
        'cross_analysis_command': 'execution/rgb-cross-analysis-command.json', 'cross_analysis_log': 'execution/rgb-cross-analysis.log',
        'coingl_probe': 'execution/coingl-context-probe.json', 'coingl_probe_log': 'execution/coingl-context-probe.log',
        'bgfx_results': 'supplemental/bgfx-regression-analysis.json', 'bgfx_analyzer': 'supplemental/analyze-bgfx-regression.py',
        'bgfx_parser': 'supplemental/parse-bgfx-regression.py', 'hardware_before': 'hardware-before.json', 'hardware_after': 'hardware-after.json',
        'quiet_helper': 'study-campaign.py', 'diagnostic_helper': 'material-diagnostic.py'}
    for key, relative in mapping.items():
        add(paths[key], relative)
    for key, path in paths['cards'].items():
        add(path, 'binaries-' + key + '.json')
    for key, path in paths['build_logs'].items():
        add(path, 'execution/' + key + '.log')
    for side, path in paths['rgb_wrappers'].items():
        add(path, 'execution/rgb-' + side + '-commands.json')
    for (side, scene), path in paths['rgb_wrapper_logs'].items():
        add(path, 'execution/rgb-' + side + '-' + scene + '.log')
    scene_mapping = {}
    for original_path, path in scene_paths.items():
        relative = 'scenes/' + path.name
        add(path, relative)
        scene_mapping[original_path] = relative
    for relative, digest in read(paths['gates'] / 'commands.json')['source_files_sha256'].items():
        path = repository / relative
        require(sha(path) == digest, 'Current source differs from tested source file')
        add(path, 'source/' + relative)
    for relative, path in generator_sources.items():
        add(path, 'source/' + relative)
    tools = {directory / (PREFIX + '-report.py'): 'plot-and-report.py', validator_path: 'validate-archive.py',
             Path(__file__): 'collect.py', directory / (PREFIX + '-gates.py'): 'execute-gates.py',
             directory / (PREFIX + '-rgb.py'): 'compare-before-after-rgb.py',
             directory / 'coin-render-wgpu-motion-summary.py': 'before-after-rgb-summary.py',
             repository / 'scripts/coinrender/analyze_animation_images.py': 'analyze-animation-images.py',
             repository / 'scripts/coinrender/run_animation_benchmark.py': 'run-animation-benchmark.py',
             directory / 'coin-render-bgfx-regression-diagnostic.py': 'supplemental/execute-bgfx-regression.py',
             directory / 'coin-render-bgfx-regression-cpu-observation.py': 'supplemental/cpu-observation.py',
             directory / 'coin-render-bgfx-regression-cpu-observation.json': 'supplemental/cpu-observation.json',
             directory / (PREFIX + '-README.md'): 'README.md'}
    for path, relative in tools.items():
        add(path, relative)
    if bootstrap.exists():
        add(bootstrap, 'history/diagnostic-bootstrap.json')
        add(bootstrap_log, 'history/diagnostic-bootstrap.log')
    for name in ('coin-render-material-study-quiet-recomputed.json', 'coin-render-material-study-quiet-audit.json',
                 'coin-render-material-study-quiet-audit.md'):
        path = directory / name
        if path.is_file():
            add(path, 'supplemental/' + name)
    for suffix in ('-gates-run.log', '-rgb-before-original-run.log', '-rgb-before-large-run.log',
                   '-rgb-after-original-run.log', '-rgb-after-large-run.log'):
        # Wrapper logs were already planned with names above.
        path = directory / (PREFIX + suffix)
        if suffix == '-gates-run.log':
            add(path, 'execution/gates-run.log')
    historical = repository / 'docs/coin-render-geometry-overlay-linux.md'
    add(historical, 'supplemental/historical-geometry-overlay-report.md')
    # Optional orchestration/post scripts are copied with their actual names;
    # they are descriptive execution evidence, not inferred passed commands.
    for path in sorted(directory.glob(PREFIX + '-*command*.json')):
        if not any(item['original_path'] == str(path.resolve()) for item in plan.values()):
            add(path, 'execution/' + path.name)
    for name in (PREFIX + '-post.py', PREFIX + '-quiet-run.log', PREFIX + '-diagnostic-run.log',
                 PREFIX + '-post.log', 'coin-render-material-study-quiet-run.log',
                 PREFIX + '-finalize.py'):
        path = directory / name
        if path.is_file():
            add(path, 'execution/' + path.name)
    for relative, card in plan.items():
        require(Path(card['original_path']).stat().st_size == card['bytes'] and sha(card['original_path']) == card['sha256'],
                'Input changed during preflight: ' + relative)
    facts = {'quiet': quiet['completed_unique_counts'], 'diagnostics': diagnostic_cards, 'rgb_pairs': 49, 'cross': cross,
             'gates': gates, 'builds': builds, 'binaries': binaries, 'scenes': scenes,
             'bgfx_diagnostic_counts': bgfx['counts'], 'copied_files': len(plan)}
    if args.preflight_only:
        print(json.dumps(facts, indent=2, ensure_ascii=False))
        return
    stage.parent.mkdir(parents=True, exist_ok=True)
    temporary = Path(tempfile.mkdtemp(prefix='.' + stage.name + '-', dir=stage.parent))
    try:
        for relative, card in plan.items():
            destination = temporary / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(card['original_path'], destination)
            require(sha(destination) == card['sha256'], 'Copy hash mismatch')
        (temporary / 'stage-metadata.json').write_text(json.dumps(metadata, indent=2, ensure_ascii=False, allow_nan=False) + '\n')
        collection = {'schema_version': 1, 'original_roots': {'quiet': str(paths['quiet']), 'diagnostic': str(paths['diagnostic']),
                      'bgfx_diagnostic': str(paths['bgfx_diagnostic'])}, 'scene_files': scene_mapping,
                      'facts': facts, 'copied_files': plan, 'ppm_files_copied': False, 'binaries_copied': False,
                      'runner_json_preserved_verbatim': True}
        (temporary / 'collection-inputs.json').write_text(json.dumps(collection, indent=2, ensure_ascii=False, allow_nan=False) + '\n')
        temporary.rename(stage)
    except BaseException:
        shutil.rmtree(temporary)
        raise
    print('Complete evidence collected; report/figure/inventory are separate:', stage)


if __name__ == '__main__':
    main()
