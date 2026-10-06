import json
from pathlib import Path

root=Path('H:/Git/coin')
out=root/'build/shared-overlay-first-frame-windows-20261005'
source=root/'build/coin-render-source'
s=json.loads((out/'summary.json').read_text())
assert 'installed' in s
evidence=source/'docs/validation/shared-overlay-first-frame-windows-20261005'
evidence.mkdir(parents=True,exist_ok=True)
rows=[]
for r in s['comparisons']:
    b,a=r['before'],r['after']
    rows.append(f"| {r['variant']} | {b['first_median_ms']:.2f} | {a['first_median_ms']:.2f} | {r['first_reduction_percent']:.1f}% | {b['warm_median_ms']:.2f} | {a['warm_median_ms']:.2f} |")
tests='\n'.join(f"| {name} | {value['total']} | {value['failures']} | {value['skipped']} |" for name,value in s['tests'].items())
probes='\n'.join(f"| {name} | {v['before']['payload_ms']:.3f} | {v['after']['payload_ms']:.3f} |" for name,v in s['probes'].items())
images='\n'.join(f"| {r['variant']} | {r['mae_rgb']:.6f} | {r['max_channel_error']} | {r['pixels_over_3']} |" for r in s['image_checks'] if r['kind']=='static-vs-coingl')
animation=[]
for name in sorted({r['variant'] for r in s['animation_checks']}):
    checks=[r for r in s['animation_checks'] if r['variant']==name]
    animation.append(f"| {name} | {len(checks)} | {max(r['mae_rgb'] for r in checks):.6f} | {max(r['max_channel_error'] for r in checks)} | {max(r['pixels_over_3'] for r in checks)} |")
report=f'''# Qualificação de objetos: primeiro quadro no Windows

Branch `codex/coin-render-transform-performance`, base `5d518cc300`.
Essa base contém as melhorias Linux `5d08e1396b` e a qualificação Windows
descrita em [linux-updates-windows](coin-render-linux-updates-windows.md).

## Alteração compartilhada

`CoinRenderActionP::qualifyObjectPayloads` consultava as opções de overlay de
material e de Cube durante o registro de cada ocorrência. Na cidade com
40.000 prédios, isso podia fazer até 80.000 consultas ao ambiente do processo.
A alteração lê cada opção uma vez por qualificação e conserva o resultado
local até concluir o registro. A próxima qualificação relê as duas opções.

A leitura local das opções termina com a qualificação. As provas de
propriedade, material, geometria, conflitos e limites continuam completas.
O renderer/lowering BGFX, a bridge/Rust wgpu e o readback não foram alterados.
O ganho ocorre no CoinRender compartilhado, após a submissão, dentro do tempo
do primeiro resultado. A imagem e a admissão dos próximos overlays são iguais.

Um caso de regressão usa o mesmo plano e fontes, desliga cada overlay
independentemente e ambos juntos, e depois religa ambos. A cada qualificação,
confere os mapas de ocorrências completos e o payload exato do plano.
Isso impede transformar a otimização num cache permanente das opções.

## Sondas separadas com tracing

Uma sonda por versão/backend, 1024×1024, um warmup e três quadros medidos.
Estas sondas localizam a etapa; não fazem parte das medianas abaixo.

| Backend OpenGL | Payload antes ms | Payload depois ms |
|---|---:|---:|
{probes}

Esses custos dependem da plataforma e do ambiente do processo. Não se
extrapola o ganho absoluto do CRT Windows para o Linux.

## Medição antes/depois e referência CoinGL

Windows 10, GTX 1060 6GB, driver 581.08. Os binários de ambos os backends da
base foram preservados antes da edição. Ambos os lados foram medidos novamente
em três processos por variante, intercalados, 1024×1024, 30 warmup e 120 quadros
medidos, com readback síncrono de cor e cópia RGBA. Sem builds, testes ou outras
medições GPU concorrentes; tracing e a sonda de identidade CoinGL são separados.
Caches de sistema/driver permanecem. A identidade da GPU/API é validada nos logs.

Os números são medianas das três medianas por processo. O primeiro quadro
exclui parsing, enquadramento, preparação do helper, construção do target e
capability probe. Os logs preservam também tempo até o primeiro resultado
desde `main`, faixas, p95 e p99. A referência principal é `SoGLRenderAction`
via `SoOffscreenRenderer` da mesma revisão local. Três processos não provam
significância estatística; os resultados da campanha anterior são separados.

| Variante | Primeiro antes ms | Primeiro depois ms | Redução | Aquecido antes ms | Aquecido depois ms |
|---|---:|---:|---:|---:|---:|
{chr(10).join(rows)}

## Regressão

| Suíte | Casos | Falhas | Skips |
|---|---:|---:|---:|
{tests}

As suítes BGFX têm 16 casos e as wgpu 17: o gate adicional de annotations é
definido só para wgpu pelo CMake. Todos os casos selecionados foram executados.
Câmera, comparação fast/full e referência CoinGL são obrigatórios nas seis
combinações; há ainda ownership, rollback, limites das provas, material/Cube,
clipping, iluminação, texturas e composição. Os testes CPU da ação foram
executados também imediatamente depois das builds. Não houve skips.
O gate BGFX de clipping fixa Vulkan nas propriedades CTest do projeto; essa
configuração foi mantida nas três suítes. O gate de câmera usa a API selecionada.
A bridge Rust não mudou; sua qualificação anterior consta no relatório base.

## Imagens

Os 21 pares estáticos antes/depois são exatamente iguais em RGB. A comparação
dos seis backends depois com CoinGL preserva as métricas por imagem:

| Variante | MAE RGB | Maior erro de canal | Pixels com erro >3 |
|---|---:|---:|---:|
{images}

Foram feitas 150 comparações animadas contra CoinGL: camera, transforms-10,
materials-10 e geometry-10 na cidade, e materials-10 na paleta de 40.001 slots.
São smoke tests de imagem, com dois warmup e cinco quadros, step 120. Os cinco
quadros lógicos 0/120/240/360/480 têm state digests correspondentes; todas as
35 combinações variante/caso produzem mudança visível. Não se afirma ganho de
desempenho animado com essa amostragem curta.

| Variante | Pares RGB | Maior MAE RGB | Maior erro de canal | Maior contagem de pixels >3 |
|---|---:|---:|---:|---:|
{chr(10).join(animation)}

Diferenças entre backends são registradas, inclusive pixels de borda isolados;
MAE pequeno não prova igualdade. Comparações usam RGB orientado do PPM, sem
correção de pixels, e não igualdade do hash RGBA bruto entre backends.

## Instalação e evidências

As instalações locais `build/coin-render-bgfx-install` e
`build/coin-render-install` foram atualizadas após gates e medições. SHA-256
de Coin4.dll, CoinRender4.dll e benchmark instalado corresponde à build
qualificada de cada backend em `installed-hashes.json`.

Comandos, sondas, logs, resultados de teste, manifests, scripts e métricas:
[evidências](validation/shared-overlay-first-frame-windows-20261005).
[Resumo JSON](validation/shared-overlay-first-frame-windows-20261005/summary.json).
As imagens e cópias de binários permanecem no diretório correspondente de
`build`; hashes estão no manifesto. `qualify.ps1` executa os gates,
`measure.py` a campanha serial e `summarize.py` valida cobertura e imagens.
'''
(source/'docs/coin-render-overlay-options-windows.md').write_text(report,encoding='utf-8')
for path in out.iterdir():
    if path.is_file() and path.suffix in ('.json','.log','.xml','.txt','.ps1','.py','.csv'):
        text=path.read_text(encoding='utf-8-sig',errors='replace')
        text='\n'.join(line.rstrip() for line in text.splitlines()).rstrip()+'\n'
        (evidence/path.name).write_text(text,encoding='utf-8')
print('Report:',source/'docs/coin-render-overlay-options-windows.md')
