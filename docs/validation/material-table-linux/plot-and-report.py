#!/usr/bin/env python3
"""Generate a material-table report/PNG/SVG from complete real evidence only.

No benchmark, build, GPU, network or Git command is invoked. The archive
validator imports validate() and report() to rederive Markdown without plots.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import statistics

BASELINE = '96e5ed80fa3ab3c9016cf10e6bbfc9b638335a33'
CURRENT = 'a0bda8b5ccc8d04efa96c5c807b88e5aa4e34c9d'
CONTROL = '4d63bb993022ee8d40802558b0871a4803002b8d'
REPORT = 'coin-render-material-table-linux.md'
FIGURE = 'material-table'
ROLES = ('coingl-control', 'bgfx-vulkan-control', 'bgfx-opengl-control', 'wgpu-before', 'wgpu-after')
LABELS = {'coingl-control': 'Coin/OpenGL', 'bgfx-vulkan-control': 'BGFX/Vulkan',
          'bgfx-opengl-control': 'BGFX/OpenGL', 'wgpu-before': 'wgpu antes', 'wgpu-after': 'wgpu depois'}
SCENARIOS = (('original', 'materials-10'), ('original', 'transforms-10'),
             ('high', 'materials-10'), ('high', 'materials-100'), ('high', 'transforms-10'), ('high', 'static'))
CASES = {'materials-10': 'Materiais 10%', 'materials-100': 'Materiais 100%',
         'transforms-10': 'Transformações 10%', 'geometry-10': 'Geometria 10%', 'static': 'Estático'}
EXPECTED = {'quiet': {'processes': 90, 'measured_frames': 2925, 'warmup_frames': 825},
            'primary': {'processes': 42, 'measured_frames': 294, 'warmup_frames': 126},
            'dimensional': {'processes': 8, 'measured_frames': 56, 'warmup_frames': 24}}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def read(path):
    return json.loads(Path(path).read_text(), parse_constant=lambda value:
                      (_ for _ in ()).throw(ValueError('Nonfinite JSON: ' + value)))


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def finite(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value)


def number(value, digits=2):
    if value is None:
        return 'n/d'
    require(finite(value), 'Expected finite measurement')
    return f'{value:.{digits}f}'


def escape(value):
    return str(value).replace('|', '\\|').replace('\n', ' ')


def label(scene, case):
    return ('Original' if scene == 'original' else 'Base 40.001 slots' if scene in ('high', 'large40001') else
            'Base ' + scene.removeprefix('slots') + ' slots' if scene.startswith('slots') else scene) + ' / ' + CASES.get(case, case)


def delta(pair):
    return f"{pair['delta_ms']:+.2f} ms ({pair['change_percent']:+.2f}%)" if pair.get('change_percent') is not None else 'n/d'


def diagnostic_campaign(data, name):
    return data.get('campaigns', {}).get(name, data)


def diagnostic_pair(comparison, category, key, digits=3, divisor=1):
    pair = comparison.get(category, {}).get(key)
    if pair is None:
        return 'n/d'
    return f"{number(pair['off'] / divisor, digits)}→{number(pair['on'] / divisor, digits)}"


def validate(metadata, quiet, primary, dimensional, rgb):
    require(metadata.get('configured') is True, 'Report metadata not admitted yet')
    require(metadata['baseline_source_content_revision'] == BASELINE and
            metadata['after_wgpu_source_content_revision'] == CURRENT and
            metadata['coingl_source_content_revision'] == CONTROL, 'Report compiled sources differ')
    require(quiet.get('complete_and_comparable') is True and quiet.get('completed_unique_counts') == EXPECTED['quiet'], 'Quiet campaign incomplete')
    sources = {role: CONTROL if role == 'coingl-control' else CURRENT if role == 'wgpu-after' else BASELINE for role in ROLES}
    require(quiet.get('input_source_content_revisions') == sources and quiet.get('binaries_unchanged') is True, 'Quiet source/binary proof differs')
    groups = {(group['scene_key'], group['case'], group['role']): group for group in quiet['groups']}
    require(len(groups) == len(quiet['groups']) == 30 and set(groups) ==
            {(scene, case, role) for scene, case in SCENARIOS for role in ROLES}, 'Quiet group coverage differs')
    for group in groups.values():
        require(group['processes'] == 3 and group['rounds'] == [1, 2, 3], 'Quiet process/round count differs')
        for metric, values in group['process_median_values_ms'].items():
            require(len(values) == 3 and all(finite(value) and value >= 0 for value in values) and
                    statistics.median(values) == group['stats'][metric]['median_ms'] and
                    group['process_median_range_ms'][metric] == {'minimum': min(values), 'maximum': max(values)}, 'Quiet median/range inconsistent')
    comparisons = {(entry['scene_key'], entry['case']): entry for entry in quiet['comparisons']}
    require(len(comparisons) == 6 and set(comparisons) == set(SCENARIOS) and
            len(quiet['pairs']) == 18 and all(pair['complete_and_comparable'] for pair in quiet['pairs']), 'Quiet pairing incomplete')
    for identity, comparison in comparisons.items():
        for metric, pair in comparison['stats'].items():
            before = groups[identity + ('wgpu-before',)]['stats'][metric]['median_ms']
            after = groups[identity + ('wgpu-after',)]['stats'][metric]['median_ms']
            require(pair['before'] == before and pair['after'] == after and pair['delta_ms'] == after - before and
                    pair['change_percent'] == ((after / before - 1) * 100 if before else None), 'Quiet comparison inconsistent')
    # The collector also rederives all CSV/chronological stderr proofs. These
    # checks reject partial campaigns even if a caller supplies admitted cards.
    diagnostic_cards = metadata.get('diagnostic_cards', {})
    require(set(diagnostic_cards) == {'primary', 'dimensional'}, 'Diagnostic raw-proof cards missing')
    for name, data in (('primary', primary), ('dimensional', dimensional)):
        campaign = diagnostic_campaign(data, name)
        card = diagnostic_cards[name]
        require(card.get('counts') == EXPECTED[name] and card.get('raw_recomputed') is True and
                card.get('complete_and_comparable') is True and card.get('proofs_passed') == EXPECTED[name]['processes'], 'Diagnostic evidence incomplete: ' + name)
        require(card.get('results_sha256') is not None and campaign.get('unique_counts') == EXPECTED[name] and
                campaign.get('complete_and_comparable') is True and not campaign.get('issues'), 'Diagnostic source JSON incomplete')
        expected_groups = 7 if name == 'primary' else 4
        rounds = 3 if name == 'primary' else 1
        require(len(campaign['on_off_comparisons']) == len(campaign['group_summaries']) == expected_groups and
                len(campaign['paired_processes']) == expected_groups * rounds and
                all(pair['complete_and_comparable'] and not pair['issues'] for pair in campaign['paired_processes']), 'Diagnostic group pairing incomplete')
        for group_name, modes in campaign['group_summaries'].items():
            require(set(modes) == {'off', 'on'} and campaign['on_off_comparisons'][group_name]['complete_and_comparable'], 'Diagnostic modes incomplete')
            for mode in ('off', 'on'):
                group = modes[mode]
                require(group['processes'] == rounds and group['rounds'] == list(range(1, rounds + 1)), 'Diagnostic round count differs')
                for category in ('csv_measured_medians_ms', 'phase_measured_medians_ms', 'counter_measured_medians'):
                    for key, value in group[category].items():
                        values = group[category + '_process_values'][key]
                        require(len(values) == rounds and all(finite(item) for item in values) and statistics.median(values) == value and
                                group[category + '_process_ranges'][key] == {'minimum': min(values), 'maximum': max(values)}, 'Diagnostic median/range differs')
    require(rgb.get('pairs') == 49 and rgb.get('processes') == 14 and rgb.get('produced_ppm_files') == 98 and
            rgb.get('all_rgb_identical') is True and rgb.get('rgb_pixels_recomputed') is True and len(rgb.get('results', [])) == 49,
            'RGB before/after evidence incomplete')
    require(rgb['compiled_source_revisions'] == {'before': BASELINE, 'after': CURRENT}, 'RGB compiled sources differ')
    identities = set()
    for result in rgb['results']:
        key = (result['scene_label'], result['case'], result['logical_frame'])
        require(key not in identities and result['logical_frame'] in range(0, 601, 100), 'RGB frame coverage differs')
        identities.add(key)
        require(result['rgb_mae'] == result['max_channel_error'] == result['pixels_different'] == result['pixels_over3'] == 0 and
                all(result['before'][key] == result['after'][key] for key in ('ppm_sha256', 'rgb_fnv64', 'rgba_fnv64')), 'RGB pair differs')
    gates = metadata.get('gates', {})
    require(gates.get('executions') == gates.get('passed') == 12 and gates.get('failed') == gates.get('skipped') == 0 and
            gates.get('registered_category_counts') == {'core_cpu': 4, 'action_reuse': 2, 'gpu': 6}, 'Gate admission missing')
    cargo = metadata.get('builds', {}).get('cargo-cpu', {})
    require(cargo.get('tests_passed', 0) > 0 and cargo.get('tests_failed') == 0 and len(cargo.get('new_oracles', [])) == 2, 'Cargo CPU admission missing')
    require(metadata.get('generator_tests', {}).get('tests_passed') == 6 and
            metadata.get('generator_tests', {}).get('tests_failed') == 0, 'Generator CPU admission missing')
    require(metadata.get('binary_proof') == {'artifacts': 20, 'all_unchanged': True}, 'Twenty binary pins/post proof required')
    bgfx = metadata.get('bgfx_regression_diagnostic', {})
    require(bgfx.get('counts') == {'processes': 18, 'measured_frames': 270, 'warmup_frames': 90} and
            bgfx.get('raw_recomputed') is True and len(bgfx.get('comparisons', {})) == 3 and
            set(bgfx.get('read_wait_ranges_ms', {})) == {'bgfx-vulkan', 'bgfx-opengl'}, 'Separate BGFX diagnostic proof missing')
    cross = metadata.get('rgb_cross', {})
    require(cross.get('new_control_processes') == 12 and cross.get('new_control_ppm_files') == 84 and
            cross.get('experimental_comparisons') == 84 and cross.get('self_comparisons') == 28 and
            cross.get('reused_wgpu_processes') == 4 and cross.get('reused_wgpu_ppm_files') == 28, 'Cross RGB proof missing')
    require(metadata.get('scene_proof', {}).get('all_unchanged') is True and
            metadata.get('scene_proof', {}).get('scenes') == 4, 'Four scene hashes after campaigns required')
    return {'groups': groups, 'comparisons': comparisons, 'diagnostics': {'primary': diagnostic_campaign(primary, 'primary'),
            'dimensional': diagnostic_campaign(dimensional, 'dimensional')},
            'rgb': rgb, 'rgb_cross': cross, 'bgfx_regression': bgfx, 'gates': gates, 'cargo': cargo}


def report(metadata, quiet, primary, dimensional, rgb, data, figure_ref):
    lines = ['# CoinRender: tabela de materiais no Linux', '',
             'A mudança separa a reutilização dos recursos de materiais e instâncias no perfil instanced já validado do wgpu. A comparação usa Coin/OpenGL do Coin3D como referência e mantém BGFX como controle.', '',
             f"Fontes compiladas: Render antes/BGFX `{BASELINE}`; wgpu depois `{CURRENT}`; CoinGL `{CONTROL}`. Os 20 artefatos permaneceram com os mesmos hashes após as campanhas.", '',
             '## Comparação sem traces', '',
             'Mediana de três medianas por processo. Os colchetes mostram mínimo–máximo dessas três medianas; não são intervalos de confiança. Controles CoinGL/BGFX são únicos por caso/rodada e não são duplicados como amostras antes/depois.', '',
             '| Cena / caso | Coin/OpenGL ms | BGFX/Vulkan ms | BGFX/OpenGL ms | wgpu antes ms | wgpu depois ms | Δ wgpu |',
             '|---|---:|---:|---:|---:|---:|---:|']
    for scene, case in SCENARIOS:
        values = []
        for role in ROLES:
            group = data['groups'][scene, case, role]
            span = group['process_median_range_ms']['total_ms']
            values.append(f"{number(group['stats']['total_ms']['median_ms'])} [{number(span['minimum'])}–{number(span['maximum'])}]")
        pair = data['comparisons'][scene, case]['stats']['total_ms']
        lines.append('| ' + ' | '.join([label(scene, case)] + values + [delta(pair)]) + ' |')
    lines.extend(['', f'![Medianas e faixas por processo]({figure_ref})', '',
                  '### Fases do quadro e variações positivas', '',
                  '| Cena / caso | Update antes→depois ms | Render antes→depois ms | Publicação antes→depois ms | Total Δ |',
                  '|---|---:|---:|---:|---:|'])
    for identity in SCENARIOS:
        comparison = data['comparisons'][identity]
        values = [f"{number(comparison['stats'][metric]['before'])}→{number(comparison['stats'][metric]['after'])}" for metric in ('update_ms', 'render_ms', 'publication_ms')]
        lines.append('| ' + ' | '.join([label(*identity)] + values + [delta(comparison['stats']['total_ms'])]) + ' |')
    increases = [(label(*identity), comparison['stats']['total_ms']) for identity, comparison in data['comparisons'].items()
                 if comparison['stats']['total_ms']['delta_ms'] > 0]
    lines.extend(['', ('Aumentos do total observado: ' + '; '.join(name + ' ' + delta(pair) for name, pair in increases) + '.')
                  if increases else 'Nenhum dos seis agregados de total aumentou nesta amostra. As diferenças por par e os outliers permanecem nos dados.', ''])
    for note in metadata.get('limitations', []):
        lines.extend([str(note), ''])
    lines.extend(['## Recursos de materiais: contrato e ablação', '',
                  'A fonte usa 72 bytes por slot; a tabela GPU usa 80 bytes com padding zero. Igualdade byte a byte contra o último payload próprio bem sucedido licencia hits de instância e material separadamente, após a prova de geometria. Dados sem referência e signed zero continuam presentes. Buffers imutáveis são publicados somente após sucesso; erro/retry mantém a base anterior e o escopo por device.', '',
                  'O packing GPU é realizado quando falta uma tabela reutilizável. A opção privada `COIN_WGPU_DISABLE_MATERIAL_RESOURCE_REUSE=1` restaura os hits acoplados e o packing literal antecipado. BGFX não foi alterado nesta etapa; seu material continua no payload de 160 bytes por instância.', '',
                  'A ablação trace principal tem 42 processos (294 medidos/126 warmups); o suplemento dimensional tem oito (56/24), separados. `payload_compare_ms`, `pack_ms` e `resource_ms` são fases CPU. `snapshot_ms` cobre vários componentes do snapshot, não apenas cópia de materiais. Eventos `attempt=1` descrevem tentativas; não demonstram commit. Eventos são alinhados ao próximo Action concluído antes de excluir warmups pelo CSV.', ''])
    lines.extend(['| Cena / caso (trace) | Total off→on ms | Δ total | Compare off→on ms | Pack off→on ms | Resource off→on ms | Upload instâncias off→on MiB | Upload materiais off→on MiB |',
                  '|---|---:|---:|---:|---:|---:|---:|---:|'])
    prefix = 'rust_material_resources.'
    for group_name, comparison in data['diagnostics']['primary']['on_off_comparisons'].items():
        scene, case = group_name.split('|')
        values = [diagnostic_pair(comparison, 'csv_measured_medians_ms', 'total_ms', 2)]
        total_pair = comparison['csv_measured_medians_ms']['total_ms']
        values += [f"{total_pair['change_percent']:+.2f}%" if total_pair['change_percent'] is not None else 'n/d']
        values += [diagnostic_pair(comparison, 'phase_measured_medians_ms', prefix + key) for key in ('payload_compare_ms', 'pack_ms', 'resource_ms')]
        values += [diagnostic_pair(comparison, 'counter_measured_medians', prefix + key, 3, 2**20) for key in ('instance_uploaded_bytes', 'material_uploaded_bytes')]
        lines.append('| ' + ' | '.join([label(scene, case)] + values) + ' |')
    increases = []
    for group_name, comparison in data['diagnostics']['primary']['on_off_comparisons'].items():
        pair = comparison['csv_measured_medians_ms']['total_ms']
        if pair['delta'] > 0:
            selected = [item for item in data['diagnostics']['primary']['paired_processes'] if
                        item['scene'] + '|' + item['case'] == group_name]
            positive = sum(item['csv_measured_medians_ms']['total_ms']['delta'] > 0 for item in selected)
            increases.append(f"{label(*group_name.split('|'))}: {pair['change_percent']:+.2f}%, {positive}/{len(selected)} pares positivos")
    lines.extend(['', 'Aumentos do total na ablação trace: ' + '; '.join(increases) + '.' if increases else 'Nenhum agregado trace principal aumentou nesta amostra.', '',
                  'Quiet e trace são campanhas distintas. Melhorias locais de packing/recursos e sinais diferentes no total de uma mesma cena são observações preservadas; os timers não estabelecem a causa da variação do total.', ''])
    lines.extend(['', 'Snapshot e dimensão observada (bytes lógicos; mediana por processo):', '',
                  '| Cena / caso | Slots off→on | Material fonte copiado off→on MiB | Instâncias copiadas off→on MiB | Snapshot off→on ms |',
                  '|---|---:|---:|---:|---:|'])
    for group_name, comparison in data['diagnostics']['primary']['on_off_comparisons'].items():
        scene, case = group_name.split('|')
        values = [diagnostic_pair(comparison, 'counter_measured_medians', prefix + 'count', 0)]
        values += [diagnostic_pair(comparison, 'counter_measured_medians', 'rust_material_snapshot.' + key, 3, 2**20) for key in ('material_copied_bytes', 'instance_copied_bytes')]
        values += [diagnostic_pair(comparison, 'phase_measured_medians_ms', 'rust_material_snapshot.snapshot_ms')]
        lines.append('| ' + ' | '.join([label(scene, case)] + values) + ' |')
    lines.extend(['', 'Suplemento dimensional: uma rodada off/on por caso, separado do N3 principal.', '',
                  '| Cena / caso | Slots off→on medidos | Total off→on ms | Pack off→on ms | Upload instâncias off→on MiB | Upload materiais off→on MiB |',
                  '|---|---:|---:|---:|---:|---:|'])
    for group_name, comparison in data['diagnostics']['dimensional']['on_off_comparisons'].items():
        scene, case = group_name.split('|')
        values = [diagnostic_pair(comparison, 'counter_measured_medians', prefix + 'count', 0),
                  diagnostic_pair(comparison, 'csv_measured_medians_ms', 'total_ms', 2),
                  diagnostic_pair(comparison, 'phase_measured_medians_ms', prefix + 'pack_ms')]
        values += [diagnostic_pair(comparison, 'counter_measured_medians', prefix + key, 3, 2**20) for key in ('instance_uploaded_bytes', 'material_uploaded_bytes')]
        lines.append('| ' + ' | '.join([label(scene, case)] + values) + ' |')
    lines.extend(['', '### Próximo custo observado', '',
                  'O snapshot CPU ainda copia 5.760.144 bytes de instâncias no caso materials-10 e 2.880.072 bytes da tabela estável no caso transforms-10 da cena base 40.001 slots. Os buffers GPU já podem ser conservados nesses casos; o snapshot continua mantendo o payload fonte atual. Uma proposta futura é propriedade compartilhada por payload, com igualdade exata e commit transacional. Essa proposta não foi implementada nesta etapa.', ''])
    lines.extend(['', '## Imagem e gates', '',
                  'Os 49 pares wgpu antes/depois (sete casos × sete quadros lógicos 0–600) são byte a byte iguais em RGB. Foram 14 processos e 98 PPMs. A verificação confirmou movimento nos casos animados e estabilidade no estático; cenas e state digests pareados constam nos dados.', '',
                  f"Os 12 gates passaram sem skips; Cargo passou {data['cargo']['tests_passed']} testes CPU, incluindo os dois novos oráculos de bytes/hits. Os seis testes CPU do gerador também passaram. A categoria registrada `core_cpu` conserva quatro execuções: duas CPU e duas Core com integração GPU. Action/Reuse tem duas execuções mistas; seis gates são GPU dedicados.", ''])
    lines.extend(['Comparação com CoinGL no cenário de muitos materiais: 12 processos de controles e 84 PPMs novos; os quatro processos/28 PPMs wgpu depois foram reutilizados. A agregação contém 84 comparações experimentais contra CoinGL e 28 auto-comparações CoinGL excluídas do resumo. São 26 processos de imagem/182 PPMs únicos no conjunto.', '',
                  '| Variante vs CoinGL | Maior MAE RGB | Maior erro de canal | Maior contagem de pixels >3 |',
                  '|---|---:|---:|---:|'])
    for variant, maxima in data['rgb_cross']['maxima'].items():
        lines.append(f"| {escape(variant)} | {number(maxima['rgb_mae'], 6)} | {number(maxima['max_channel_error'], 0)} | {number(maxima['pixels_over3'], 0)} |")
    lines.extend(['## Diagnóstico BGFX separado', '',
                  'A etapa histórica registrou BGFX/Vulkan geometry-10 67,62→100,77 ms (+49,03%); o resultado é preservado. O diagnóstico posterior usou a mesma fonte 96e5, 18 processos/270 medidos/90 warmups nos três backends, com traces CPU e sem GPU timestamps. Não substitui a comparação histórica nem prova sua causa.', '',
                  '| Variante (diagnóstico geometry-10) | Total off→on ms | Δ | Maior espera observada ms |',
                  '|---|---:|---:|---:|'])
    for variant, comparison in data['bgfx_regression']['comparisons'].items():
        pair = comparison['median_of_process_medians']['total_ms']
        wait = data['bgfx_regression']['read_wait_ranges_ms'].get(variant)
        lines.append(f"| {escape(variant)} | {number(pair['off'])}→{number(pair['on'])} | {pair['change_percent']:+.2f}% | {number(wait['maximum']) if wait else 'n/d'} |")
    lines.extend(['',
                  'Os 90 quadros medidos de cada API BGFX conservaram instancing=1, 40.001 instâncias, 24 vértices, um draw e geometria GPU reutilizada. As esperas e os pares/outliers permanecem nos dados. O diagnóstico não mostrou evidência específica de stall BGFX que justificasse uma correção de backend; a causa do +49,03% histórico permanece indeterminada.', '',
                  'Quadros lentos mostraram aumentos em update/validação/lowering CPU. `update_ms` inclui setters/notificações; não há notify isolado. O marcador Action antecede commit/qualificação final, e o residual fora das fases não identifica essa causa. Observações `/proc/stat` e pressão CPU a 1 Hz incluem startup/warmups; não atribuem causa a um quadro ou à campanha histórica.', '',
                  '## Protocolo e reprodução', '',
                  'Quiet: 90 processos, 2.925 medidos e 825 warmups. Cinco casos dinâmicos usam 5 warmups/15 quadros; o estático de 40.001 slots usa 30/120. Três rodadas e cinco papéis: CoinGL, BGFX/Vulkan, BGFX/OpenGL, wgpu antes/depois. Cena offscreen 1024×1024; dados de animação e setters determinísticos, controles compartilhados, nenhum processo descartado.', '',
                  'Os slots declarados pelo gerador são previsão da cena estática. Capturas podem internar materiais/clonar ocorrências; os contadores da tabela real são medidos por frame. O total inclui update, render/readback e publicação. Fases sobrepostas e medianas não devem ser somadas como decomposição causal.', '',
                  '[Evidências e reprodução CPU](validation/material-table-linux/README.md). CSVs, logs, comandos, fontes/hashes, cenas/geradores e ferramentas estão arquivados; PPMs e binários ficam fora. A validação relocada rederiva estatísticas/provas/logs e Markdown; conferências de pixels foram feitas antes da coleta e são preservadas por hashes/métricas.', ''])
    for note in metadata.get('history_notes', []):
        lines.extend([str(note), ''])
    return '\n'.join(lines)


def create_figure(data, output):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    colors = ('#777777', '#315b9c', '#43a2a0', '#b8b5dc', '#7347a8')
    fig, axes = plt.subplots(2, 3, figsize=(13.5, 7.4), constrained_layout=True)
    for ax, identity in zip(axes.flat, SCENARIOS):
        for index, role in enumerate(ROLES):
            group = data['groups'][identity + (role,)]
            median = group['stats']['total_ms']['median_ms']
            span = group['process_median_range_ms']['total_ms']
            ax.bar(index, median, color=colors[index], width=.68)
            ax.errorbar(index, median, yerr=[[median - span['minimum']], [span['maximum'] - median]], fmt='none', color='black', capsize=4, linewidth=1)
        ax.set_title(label(*identity), fontsize=10)
        ax.set_xticks(range(5), ['CoinGL', 'BG/V', 'BG/GL', 'WG antes', 'WG depois'], fontsize=8)
        ax.set_ylim(bottom=0)
        ax.set_ylabel('Total do quadro (ms)')
        ax.grid(axis='y', alpha=.2)
        ax.set_axisbelow(True)
    fig.suptitle('Tabela de materiais: mediana de três medianas por processo\nBarras: total; hastes: mínimo–máximo entre processos; controles únicos', fontsize=12)
    for suffix in ('.png', '.svg'):
        fig.savefig(output / (FIGURE + suffix), dpi=180)
    plt.close(fig)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('metadata', 'quiet', 'diagnostic-primary', 'diagnostic-dimensional', 'rgb'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--figure-reference', default='validation/material-table-linux/material-table.png')
    args = parser.parse_args()
    paths = {name: getattr(args, name.replace('-', '_')).resolve() for name in
             ('metadata', 'quiet', 'diagnostic-primary', 'diagnostic-dimensional', 'rgb')}
    inputs = {name: read(path) for name, path in paths.items()}
    data = validate(inputs['metadata'], inputs['quiet'], inputs['diagnostic-primary'], inputs['diagnostic-dimensional'], inputs['rgb'])
    output = args.output.resolve()
    require(output.is_relative_to(Path('/tmp')) and not output.exists(), 'Report output must be a fresh directory under /tmp')
    output.mkdir()
    create_figure(data, output)
    (output / REPORT).write_text(report(inputs['metadata'], inputs['quiet'], inputs['diagnostic-primary'], inputs['diagnostic-dimensional'], inputs['rgb'], data, args.figure_reference))
    card = {'inputs': {name: {'path': str(path), 'bytes': path.stat().st_size, 'sha256': sha(path)} for name, path in paths.items()},
            'reporter_sha256': sha(Path(__file__)), 'figure_reference': args.figure_reference,
            'outputs': {path.name: {'bytes': path.stat().st_size, 'sha256': sha(path)} for path in output.iterdir()}}
    (output / 'report-inputs.json').write_text(json.dumps(card, indent=2, ensure_ascii=False, allow_nan=False) + '\n')
    print('Complete evidence admitted; report and scientific PNG/SVG written:', output)


if __name__ == '__main__':
    main()
