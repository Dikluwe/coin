import json
from pathlib import Path

root = Path('H:/Git/coin')
build = root / 'build'
source = build / 'coin-render-source'
summary = json.loads((build / 'bgfx-gl-indexed-summary.json').read_text(encoding='utf-8'))
suite = summary['tests']['bgfx-gl-indexed-tests']
assert suite['total'] > 30 and suite['failed'] == 0 and suite['skipped'] == 0
assert summary['tests']['bgfx-gl-indexed-scoped-clip'] == {'total': 3, 'failed': 0, 'skipped': 0}
assert len(summary['controls']) == 3 and len(summary['installation']) == 3
assert all(item['matches'] for item in summary['installation'])
rows = []
for row in summary['comparisons']:
    label = {'opengl': 'OpenGL', 'vulkan': 'Vulkan', 'd3d12': 'D3D12'}[row['api']]
    rows.append(f"| BGFX {label} | {row['before']['first_median_ms']:.2f} | {row['after']['first_median_ms']:.2f} | {row['reduction_percent']:.1f}% | {row['after']['warm_control_ms']:.2f} |")
control_rows = []
gl = next(row for row in summary['comparisons'] if row['api'] == 'opengl')
phase_rows = '\n'.join(f"| {label} | {float(gl['before']['trace'][key]):.2f} | {float(gl['after']['trace'][key]):.2f} |"
  for label, key in (('Lowering', 'lower_ms'), ('Upload', 'upload_ms'), ('Espera pelo readback', 'read_wait_ms')))
for row in summary['controls']:
    label = 'Coin/OpenGL' if row['backend'] == 'gl' else f"wgpu {'OpenGL' if row['api'] == 'gl' else 'Vulkan'}"
    control_rows.append(f"| {label} | {row['first_median_ms']:.2f} | {row['warm_median_ms']:.2f} |")

section = '''## Geometria indexada no BGFX/OpenGL — 2026-10-03

A captura compartilhada já fornecia 24 vértices e 36 índices por cubo,
mas o lowering BGFX expandia novamente um vértice por índice. Esta alteração
mantém um remapeamento local ao item de composição, convertendo cada índice
de origem apenas uma vez. A ordem dos índices continua intacta, assim como
todos os atributos, as transformações e as assinaturas de material. O hash
de material continua calculado por ocorrência no fluxo original de índices.

O remapeamento exige pelo menos 256 desenhos no quadro e ausência de sombras.
O item precisa passar pelas regras existentes de batching opaco PHONG: sem
blend, screen door, overlay, clear-depth ou atributos de strokes, e com
transformações afins. Reutiliza até 16 KiB de scratch
para intervalos de até 4096 vértices. Os demais caminhos mantêm a expansão
anterior. Não há compartilhamento entre itens, desenhos, transformações ou diferentes posições na ordenação
transparente. `firstVertex`/`vertexCount` refletem o intervalo compacto, também
quando os desenhos opacos se juntam em um batch. Os shaders não mudaram.

Na cidade de 40.000 prédios, o backend passa de 1.440.036 para 960.024 vértices:
33,3% menos vértices convertidos e enviados. O payload de vértices BGFX cai de
258,19 para 172,12 MiB; os 1.440.036 índices e o batch opaco único permanecem.
Essa redução afeta a conversão CPU, a cópia para upload e a espera pela GPU.

### Medição pareada

Baseline `30f1f4018f`, três processos novos antes/depois por API, alternando as
versões, 1024×1024, `--warmup 1 --frames 3`. O primeiro quadro inclui renderização
e cópia RGBA síncrona; parsing e capability probe ficam fora. Não há tracing
nas medianas. Os processos foram executados em sequência, sem compilação ou
testes GPU concorrentes. Caches do driver e do sistema foram mantidos.
O controle aquecido separado usa quatro quadros de aquecimento e oito medidos.

| Caminho | Primeiro antes (ms) | Primeiro depois (ms) | Redução | Aquecido depois (ms) |
|---|---:|---:|---:|---:|
''' + '\n'.join(rows) + '''

O controle OpenGL com tracing separa os intervalos:

| Intervalo | Antes (ms) | Depois (ms) |
|---|---:|---:|
''' + phase_rows + '''

A preparação do renderer permanece incluída no primeiro quadro. Os logs separam
a preparação da submissão. A espera pelo readback inclui trabalho
pendente de criação, upload e renderização no driver; não é uma medida isolada
de custo de cópia dos pixels. As faixas das três amostras estão no JSON.

Após a suíte, novos controles do Coin e wgpu usaram o mesmo tamanho/cena,
três processos por caminho e quatro quadros de aquecimento/oito medidos:

| Controle | Primeiro quadro (ms) | Aquecido (ms) |
|---|---:|---:|
''' + '\n'.join(control_rows) + '''

O BGFX/OpenGL se aproxima dos outros caminhos no primeiro quadro, mas ainda
não os iguala nesta máquina. O benefício aquecido é menor, porque os buffers
estáticos já eram reutilizados; a principal redução está no primeiro upload.

No controle OpenGL de 10.000 prédios com material mudando, a mediana aquecida de
três processos caiu de 346,58 para 285,37 ms. Uma amostra depois ficou em 456,12 ms;
ela foi mantida no cálculo e a faixa completa está no JSON. O controle de câmera
teve um processo por versão, de 348,28 para 300,73 ms, sem inferência estatística.

### Verificação e instalação

O build Release e o teste Core passaram. O teste Core adicional compara bit a bit o
fluxo de atributos expandido com a entrada compacta, com materiais diferentes,
300 transformações, batching opaco e os fallbacks para transparência e intervalos
maiores. A suíte final de CASE_COUNT casos passou, sem falhas nem skips, com renderer padrão
OpenGL. Os três casos adicionais de clipping em Vulkan/camadas e OpenGL/OIT/camadas
também passaram. Inclui janelas,
readback, materiais, Gouraud, texturas/multitextura, clipping, estilos, profundidade,
transparência, sombras GPU, múltiplos targets e RTT. Referência OpenGL e GPU de
sombras foram obrigatórias.

A tentativa inicial aplicava índices também aos desenhos pequenos e transparentes.
A rodada de 152 casos foi interrompida após um erro de acesso à memória em
clipping Vulkan/camadas e dois timeouts em clipping OpenGL. A repetição Vulkan
passou. O teste OpenGL/OIT direto passou, mas custou 81,60 s contra 8,97 s na
baseline. Essa tentativa ampla foi descartada. Na versão final com guard de
batching opaco, os três testes passaram, com 4,81 s em OpenGL/OIT e 2,70 s em
OpenGL/camadas. Os logs da tentativa e das rechecagens foram preservados.

Uma rodada posterior de 34 processos também foi descartada integralmente após
os tempos da própria baseline oscilarem: o primeiro OpenGL chegou a 28.949 ms,
com mediana aquecida de 173,7 ms. Uma nova rodada completa foi executada após
observar baixa carga na GPU. Todas as amostras cronometradas finais ficaram
abaixo de 30 ms na mediana aquecida. Não foram removidas amostras individuais
da rodada final; as duas rodadas descartadas estão em `attempts/`.

As IMAGE_COUNT comparações antes/depois de imagem passaram por SHA-256 do PPM e hash RGBA,
incluindo cena estática nas três APIs e mudanças de câmera/material no OpenGL.
A instalação local BGFX foi atualizada e os hashes de `Coin4.dll`,
`CoinRender4.dll` e do benchmark conferem com o build qualificado.
`Coin4.dll` permanece idêntico à baseline. O build wgpu não foi alterado.

Logs, scripts, resultados JUnit, hashes e medição consolidada:
[`bgfx-gl-indexed-summary.json`](validation/bgfx-windows/first-frame/bgfx-gl-indexed/bgfx-gl-indexed-summary.json).

'''.replace('CASE_COUNT', str(suite['total'])).replace('IMAGE_COUNT', str(len(summary['image_checks']) + sum(row['sample_count'] for row in summary['updates'])))
report = source / 'docs/coin-render-large-scenes-windows.md'
text = report.read_text(encoding='utf-8')
assert '## Geometria indexada no BGFX/OpenGL' not in text
assert '\n## Reproduzir\n' in text
report.write_text(text.replace('\n## Reproduzir\n', '\n' + section + '## Reproduzir\n', 1), encoding='utf-8')
destination = source / 'docs/validation/bgfx-windows/first-frame/bgfx-gl-indexed'
destination.mkdir(parents=True, exist_ok=True)
files = sorted(set(build.glob('bgfx-gl-indexed-*.log')) | set(build.glob('bgfx-gl-indexed-*.xml')) |
  set(build.glob('bgfx-gl-indexed-*.json')) | set(build.glob('bgfx-gl-indexed-*.txt')))
files = [path for path in files if path.name != 'bgfx-gl-indexed-available-tests.json']
files += [build / name for name in ('measure_bgfx_gl_indexed.ps1', 'qualify_bgfx_gl_indexed.ps1',
  'measure_bgfx_gl_controls.ps1', 'measure_bgfx_gl_updates.ps1', 'summarize_bgfx_gl_indexed.py', 'install_bgfx_gl_indexed.ps1', 'document_bgfx_gl_indexed.py')]
for path in files:
    raw = path.read_text(encoding='utf-8-sig', errors='replace')
    normalized = '\n'.join(line.rstrip() for line in raw.splitlines()).rstrip() + '\n'
    (destination / path.name).write_text(normalized, encoding='utf-8')
print('Report updated; archived', len(files), 'text evidence files.')
for attempt in ('wide', 'unstable'):
    origin = build / f'bgfx-gl-indexed-{attempt}-attempt'
    target = destination / 'attempts' / attempt
    target.mkdir(parents=True, exist_ok=True)
    for path in origin.iterdir():
        if path.suffix not in ('.json', '.log', '.xml', '.txt'):
            continue
        raw = path.read_text(encoding='utf-8-sig', errors='replace')
        normalized = '\n'.join(line.rstrip() for line in raw.splitlines()).rstrip() + '\n'
        (target / path.name).write_text(normalized, encoding='utf-8')
