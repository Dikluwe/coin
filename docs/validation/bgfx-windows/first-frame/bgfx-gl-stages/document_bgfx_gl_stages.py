import json
from pathlib import Path
import statistics

root = Path('H:/Git/coin')
build = root / 'build'
source = build / 'coin-render-source'
summary = json.loads((build / 'bgfx-gl-stages-summary.json').read_text(encoding='utf-8'))
report = source / 'docs/coin-render-large-scenes-windows.md'
evidence = source / 'docs/validation/bgfx-windows/first-frame/bgfx-gl-stages'
evidence.mkdir(parents=True, exist_ok=True)

def normalized(text):
    return '\n'.join(line.rstrip() for line in text.splitlines()).rstrip() + '\n'

header = '## Lowering, preparação e readback do BGFX — 2026-10-03'
document = report.read_text(encoding='utf-8')
assert header not in document
rows = []
for row in summary['comparisons']:
    b, a = row['before'], row['after']
    api = {'opengl': 'OpenGL', 'vulkan': 'Vulkan', 'd3d12': 'D3D12'}[row['api']]
    rows.append(f"| BGFX {api} | {b['first_median_ms']:.2f} | {a['first_median_ms']:.2f} | {row['reduction_percent']:.1f}% | {b['warm_control_ms']:.2f} | {a['warm_control_ms']:.2f} |")
b, a = summary['comparisons'][0]['before'], summary['comparisons'][0]['after']
phases = [("Preparação do target", b['target_trace']['prepare_ms'], a['target_trace']['prepare_ms'])]
phases += [(label, b['trace'][key], a['trace'][key]) for label, key in
    [('Lowering', 'lower_ms'), ('Upload CPU', 'upload_ms'), ('Espera pelo readback', 'read_wait_ms'), ('Inversão de linhas/publicação', 'row_flip_ms')]]
phase_rows = '\n'.join(f'| {label} | {float(before):.2f} | {float(after):.2f} |' for label, before, after in phases)
ablation_rows = '\n'.join(f"| {r['mode']} | {r['first_median_ms']:.2f} |" for r in summary['ablations'])
update_rows = []
for mode in ('camera', 'material'):
    selected = [r for r in summary['runs'] if r['mode'] == mode]
    before = [r for r in selected if r['variant'] == 'before']
    after = [r for r in selected if r['variant'] == 'after']
    update_rows.append(f"| {mode} | {len(before)} | {statistics.median(r['warm_ms'] for r in before):.2f} | {statistics.median(r['warm_ms'] for r in after):.2f} |")
section = f'''{header}

As alterações são do backend BGFX no `coin-render`; não alteram a biblioteca
BGFX, o OpenGL original do Coin, os shaders nem o backend wgpu.

No lowering, cores finitas são verificadas uma vez por material usado em cada
frame. Materiais uniformes fornecem um template por draw; a transformação e
normalização da última normal são reutilizadas somente quando os bits da
normal de entrada são idênticos, dentro do mesmo draw. Materiais não usados
com RGB não finito continuam permitidos; um RGB não finito usado é rejeitado.
O caminho completo escreve na alocação de saída sem copiar outro vértice inteiro.

No OpenGL, cenas grandes elegíveis usam um prefixo de 124 bytes do vértice de
188 bytes. Ele mantém todos os atributos usados com os mesmos bits e omite
somente os UVs extras não usados. Na cidade, são os mesmos 960.024 vértices e
1.440.036 índices, com 113,53 MiB de vértices em vez de 172,12 MiB: 34,0% menos
bytes de vértices. O upload continua transferindo sua alocação por `makeRef`.
O buffer em cache distingue os dois layouts; compactos descartam os dados CPU
após upload e não participam de patches de material.

O formato compacto exige mais de 32 MiB no formato completo, batching opaco
PHONG já qualificado, ao menos 256 draws, nenhum grupo de sombra, textura
primária/extra ou barreira de overlay/profundidade, matrizes model/view afins,
W igual a 1 e ausência de override de fog-eye-depth. Toda a composição deve
ser elegível e cada range indexado deve ter até 4.096 vértices. Se não for,
o frame inteiro conserva o formato completo. Vulkan e D3D12 usam 188 bytes.

A preparação cria recursos fullscreen e programas de conversão de profundidade
quando necessários para profundidade ou transparência. Targets somente cor
não alocam de início o framebuffer R32F, a textura de staging e o vetor CPU
de profundidade. Ativar profundidade depois, resize e RTT continuam cobertos.

Os callbacks de cache do BGFX/OpenGL usam agora um cache de binários de programas
em memória do processo: até 32 MiB, 128 entradas e 8 MiB por entrada, imutáveis
e protegidas por mutex. As chaves do BGFX incluem shaders, plataforma, GPU e
versão do driver. O cache não mantém handles ou targets e não grava em disco.
Ele permite reutilização após destruir/recriar o runtime no mesmo processo.
O capability probe do benchmark faz essa inicialização antes do primeiro frame;
uma aplicação que renderiza diretamente em um processo novo ainda precisa
compilar os programas na primeira inicialização. Os traces registram hits/writes.

No readback síncrono padrão (pipeline depth 1), a publicação troca os vetores
com o target e recicla a alocação anterior, eliminando a cópia de publicação e
o vetor CPU duplicado do último resultado. Em 1024×1024 somente cor, esse vetor
duplicado tinha 4 MiB; não é uma medição de pico RSS. A inversão de linhas,
blit GPU e staging continuam. Tickets assíncronos mantêm armazenamento próprio;
pipelines 2/3 conservam a publicação anterior. O caminho síncrono só retorna
sucesso após o readback atual completar, sem publicar uma imagem anterior.

### Medição da versão final

Baseline `9fd10a080b`, seis processos novos por versão em OpenGL e três em
Vulkan/D3D12, alternados e seriais,
cidade de 40.000 prédios, 1024×1024, `--warmup 1 --frames 3`.
Primeiro quadro inclui renderização e cópia RGBA síncrona; parsing e capability
probe ficam fora. Sem tracing nas medianas; caches de sistema/driver mantidos.
Controle aquecido separado: quatro quadros de aquecimento e oito medidos.
Todas as medianas aquecidas estáticas dos processos cronometrados ficaram
abaixo de 30 ms. As faixas e amostras individuais estão no JSON.
Três pares OpenGL foram acrescentados depois da variação inicial; todos os
seis pares são incluídos na mediana, sem excluir os processos iniciais.

| API | Primeiro antes (ms) | Primeiro depois (ms) | Redução | Aquecido antes (ms) | Aquecido depois (ms) |
|---|---:|---:|---:|---:|---:|
{chr(10).join(rows)}

Uma execução separada com tracing no OpenGL:

| Intervalo | Antes (ms) | Depois (ms) |
|---|---:|---:|
{phase_rows}

O intervalo de espera agrega comandos pendentes, upload do driver, execução
e cópia/leitura da GPU. Sua queda não mede isoladamente a cópia GPU→CPU.
O intervalo CPU de preparação não prova redução isolada da compilação: parte
dos recursos é consumida mais tarde pela thread de renderização. Os ganhos
do pacote devem ser avaliados pela mediana completa e pelos controles abaixo.

Três processos adicionais por opção usam o novo binário, sem tracing, com
as mesmas condições estáticas. São controles separados, sem significância
estatística estabelecida; não atribuir todo o ganho a uma opção por subtração.
`compact-cache-off` desliga somente o cache de programas; `full-layout`
desliga somente os vértices compactos. Lowering, recursos lazy e troca de
buffers permanecem ativos em ambos.

| Controle OpenGL | Primeiro mediano (ms) |
|---|---:|
{ablation_rows}

Controles de atualização em 10.000 prédios, warmup 1 e três quadros:

| Atualização | Processos por versão | Aquecido antes (ms) | Aquecido depois (ms) |
|---|---:|---:|---:|
{chr(10).join(update_rows)}

Câmera tem apenas um processo por versão. Esses controles verificam os caminhos
de reuse/rebuild, sem estabelecer uma distribuição de desempenho para câmera.

### Qualificação

A suíte de 50 casos passou sem falhas nem skips: profundidade, transparência,
texturas, clipping, janelas, sombras GPU, múltiplos targets e RTT. As referências
GPU e sombras eram obrigatórias. Mais quatro execuções verificaram offscreen
e readback/tickets em Vulkan e D3D12. O Core compara todos os atributos mantidos
bit a bit, índices, draws, retenção/descarte de geometria, fallback de overlay,
rejeição de patches compactos e limites/imutabilidade do cache de programas.
O teste GPU compara geometria visível pequena/grande, cache estático e mudanças
compacto→completo→compacto, além dos testes existentes de perda de dispositivo.

Os 44 processos principais produziram 22 pares de imagens idênticos pelo SHA-256
do PPM e hash RGBA. Mais oito processos compararam câmera/material nos pipelines
OpenGL 2/3: os quatro pares também são exatos. O teste `CoinBgfxReadbackModes`
exige pixels imediatos e falha no screen-door com pipeline 2 tanto na baseline
quanto depois; portanto ele não qualifica o contrato atrasado. Os controles
dinâmicos acima verificam sua preservação, sem afirmar entrega imediata.
As seis imagens dos controles de opções também são exatas.
A instalação BGFX local corresponde por hash ao build qualificado; `Coin4.dll`
e o benchmark permanecem idênticos à baseline. Os pilotos iniciais também estão
preservados: não substituem as medianas finais nem foram usados para excluir
amostras. A primeira tentativa de teste Core tinha uma fixture SCREEN_DOOR e
uma barreira inválida em layer zero; ambas foram corrigidas antes da qualificação.

Evidências e scripts:
[`bgfx-gl-stages-summary.json`](validation/bgfx-windows/first-frame/bgfx-gl-stages/bgfx-gl-stages-summary.json).

'''
document = document.replace('## Reproduzir\n', section + '## Reproduzir\n', 1)
report.write_text(document, encoding='utf-8')
for path in build.glob('bgfx-gl-stages-*'):
    if path.is_file() and path.suffix in ('.log', '.json', '.xml', '.txt'):
        (evidence / path.name).write_text(normalized(path.read_text(encoding='utf-8-sig', errors='replace')), encoding='utf-8')
for name in ('measure_bgfx_gl_stages.ps1', 'measure_bgfx_gl_stages_updates.ps1',
             'measure_bgfx_gl_stages_ablation.ps1', 'measure_bgfx_gl_stages_pipeline.ps1', 'qualify_bgfx_gl_stages.ps1',
             'measure_bgfx_gl_stages_gl_extra.ps1',
             'qualify_bgfx_gl_stages_other_apis.ps1', 'install_bgfx_gl_stages.ps1',
             'summarize_bgfx_gl_stages.py', 'document_bgfx_gl_stages.py'):
    (evidence / name).write_text(normalized((build / name).read_text(encoding='utf-8-sig')), encoding='utf-8')
print('Report and text evidence saved:', evidence)
