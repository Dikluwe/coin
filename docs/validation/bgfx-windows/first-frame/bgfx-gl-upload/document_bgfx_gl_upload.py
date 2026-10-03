import json
from pathlib import Path
import shutil
import statistics

root = Path('H:/Git/coin')
build = root / 'build'
source = build / 'coin-render-source'
summary = json.loads((build / 'bgfx-gl-upload-summary.json').read_text(encoding='utf-8'))
report = source / 'docs/coin-render-large-scenes-windows.md'
evidence = source / 'docs/validation/bgfx-windows/first-frame/bgfx-gl-upload'
evidence.mkdir(parents=True, exist_ok=True)

def normalized(text):
    return '\n'.join(line.rstrip() for line in text.splitlines()).rstrip() + '\n'

header = '## Upload por referência no BGFX/OpenGL — 2026-10-03'
document = report.read_text(encoding='utf-8')
assert header not in document
rows = []
for row in summary['comparisons']:
    b, a = row['before'], row['after']
    api_name = {'opengl': 'OpenGL', 'vulkan': 'Vulkan', 'd3d12': 'D3D12'}[row['api']]
    rows.append(f"| BGFX {api_name} | {b['first_median_ms']:.2f} | {a['first_median_ms']:.2f} | {row['reduction_percent']:.1f}% | {b['warm_control_ms']:.2f} | {a['warm_control_ms']:.2f} |")
gl = summary['comparisons'][0]
b, a = gl['before'], gl['after']
phase_rows = '\n'.join(f"| {label} | {float(b['trace'][key]):.2f} | {float(a['trace'][key]):.2f} |"
    for label, key in [('Lowering', 'lower_ms'), ('Upload', 'upload_ms'), ('Espera pelo readback', 'read_wait_ms')])
update_rows = []
for mode in ('camera', 'material'):
    selected = [r for r in summary['runs'] if r['mode'] == mode]
    before = [r for r in selected if r['variant'] == 'before']
    after = [r for r in selected if r['variant'] == 'after']
    update_rows.append(f"| {mode} | {len(before)} | {statistics.median(r['warm_ms'] for r in before):.2f} | {statistics.median(r['warm_ms'] for r in after):.2f} |")
section = f'''{header}

O upload completo de vértices acima de 32 MiB agora transfere a alocação do
`std::vector` ao BGFX por `makeRef`, com callback de liberação. O dono desse
bloco é independente do target e do plano em cache. O BGFX pode liberá-lo na
thread de renderização ou durante shutdown, depois de consumir os comandos.
Não há espera extra nem uma referência a memória temporária da stack.
Se a alocação do pequeno objeto dono falhar, o caminho anterior com `copy`
continua disponível. Uploads pequenos, patches de material e o caminho direto
de RTT mantêm a cópia anterior. Os índices ainda são enviados com `copy`.

Na cidade de 40.000 prédios, elimina-se uma cópia CPU de 172,12 MiB por upload
completo. O payload GPU, o layout de 188 bytes, os 960.024 vértices e os
1.440.036 índices permanecem iguais. Não é uma medição de redução do pico RSS:
o número se refere ao bloco cuja cópia deixa de existir.

Os contadores do plano sobrevivem à transferência e à retenção repetida.
O cálculo do orçamento inclui os vértices transferidos, para também descartar
os índices CPU dos planos grandes. Planos pequenos continuam com os dados
necessários aos patches de material. Nenhum shader ou código wgpu mudou.

### Medição da versão final

Baseline `b70db88d49`, três processos novos antes/depois por API, alternados,
cidade de 40.000 prédios em 1024×1024, `--warmup 1 --frames 3`. Primeiro quadro
inclui renderização e cópia RGBA síncrona, sem parsing/capability probe.
Sem tracing nas medianas; caches do driver/sistema mantidos. Build, testes e
processos GPU foram serializados. O controle aquecido separado usa quatro
quadros de aquecimento e oito medidos. Todas as medianas aquecidas dos processos
cronometrados estáticos ficaram abaixo de 30 ms. Faixas e amostras estão no JSON.

| Caminho | Primeiro antes (ms) | Primeiro depois (ms) | Redução | Aquecido antes (ms) | Aquecido depois (ms) |
|---|---:|---:|---:|---:|---:|
{chr(10).join(rows)}

Não foi observado ganho no quadro estático aquecido. Esse controle variou
menos de 1 ms entre versões; o trabalho GPU e o reuse dos buffers não mudam.

Uma execução separada com tracing no OpenGL:

| Intervalo | Antes (ms) | Depois (ms) |
|---|---:|---:|
{phase_rows}

O tempo de upload mede o intervalo CPU do backend. O driver ainda precisa
receber a geometria e renderizar; esse trabalho aparece também na espera pelo
readback. Não se trata de upload GPU de custo zero. Na execução com tracing
depois, preparar o target custou {float(a['target_trace']['prepare_ms']):.2f} ms,
e a espera pelo readback custou {float(a['trace']['read_wait_ms']):.2f} ms.
Essas parcelas e o lowering permanecem como oportunidades de melhoria.

Os controles de atualização OpenGL usam 10.000 prédios, warmup 1 e três quadros:

| Atualização | Processos por versão | Aquecido antes (ms) | Aquecido depois (ms) |
|---|---:|---:|---:|
{chr(10).join(update_rows)}

O controle de câmera tem somente um processo por versão. Esses controles
verificam principalmente a correção dos caminhos de reuse/rebuild; não
estabelecem uma distribuição de desempenho para câmera.

### Qualificação

A suíte de 50 casos passou sem falhas nem skips, incluindo OpenGL,
profundidade, transparência, clipping, texturas, janelas, sombras GPU,
múltiplos targets e RTT. Os testes GPU de referência/sombras foram obrigatórios.
O Core verifica contadores e dados depois da transferência, retenção repetida
e descarte dos índices restantes. O teste offscreen excede 32 MiB, verifica
cache estático e rebuild de material, injeta perda de dispositivo depois de
enfileirar um novo upload grande e confirma recuperação com a mesma imagem.
Esse teste de ciclo de vida também passou em Vulkan e D3D12, em processos separados.

Os {len(summary['runs'])} processos finais geraram {len(summary['image_checks'])} pares de imagens
idênticos à baseline pelo SHA-256 do PPM e pelo hash RGBA, incluindo as três
APIs, tracing, controle aquecido, câmera e material. A instalação local BGFX
foi atualizada; hashes do build e da instalação conferem. `Coin4.dll` e o
benchmark permanecem idênticos à baseline.

A rodada parcial inicial foi interrompida pelo tratamento de stderr de diagnóstico
do PowerShell 5. A rodada completa da primeira implementação também foi
preservada; ela precede a correção que descarta os índices CPU restantes.
Os resultados acima pertencem a uma nova rodada completa com a versão final,
sem excluir amostras individuais. Os pilotos estão em `pilots/`.

Evidências, scripts e logs:
[`bgfx-gl-upload-summary.json`](validation/bgfx-windows/first-frame/bgfx-gl-upload/bgfx-gl-upload-summary.json).

'''
document = document.replace('## Reproduzir\n', section + '## Reproduzir\n', 1)
report.write_text(document, encoding='utf-8')
for path in build.glob('bgfx-gl-upload-*'):
    if path.is_file() and path.suffix in ('.log', '.json', '.xml', '.txt') and path.stem != 'bgfx-gl-upload-core':
        text = path.read_text(encoding='utf-8-sig', errors='replace')
        (evidence / path.name).write_text(normalized(text), encoding='utf-8')
for filename in ('measure_bgfx_gl_upload.ps1', 'measure_bgfx_gl_upload_updates.ps1',
                 'qualify_bgfx_gl_upload.ps1', 'install_bgfx_gl_upload.ps1',
                 'summarize_bgfx_gl_upload.py', 'document_bgfx_gl_upload.py'):
    shutil.copyfile(build / filename, evidence / filename)
for dirname in ('bgfx-gl-upload-pilot', 'bgfx-gl-upload-retention-pilot'):
    destination = evidence / 'pilots' / dirname
    destination.mkdir(parents=True, exist_ok=True)
    for path in (build / dirname).iterdir():
        if path.is_file() and path.suffix in ('.log', '.json'):
            (destination / path.name).write_text(normalized(path.read_text(encoding='utf-8-sig', errors='replace')), encoding='utf-8')
print('Report and text evidence saved:', evidence)
