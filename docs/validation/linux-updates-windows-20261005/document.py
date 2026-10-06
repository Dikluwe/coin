import json
from pathlib import Path
import re

ROOT=Path('H:/Git/coin')
OUT=ROOT/'build/linux-updates-windows-20261005'
SOURCE=ROOT/'build/coin-render-source'
summary=json.loads((OUT/'summary.json').read_text(encoding='utf-8'))
evidence=SOURCE/'docs/validation/linux-updates-windows-20261005'
evidence.mkdir(parents=True,exist_ok=True)

def normal(text):
    return '\n'.join(line.rstrip() for line in text.splitlines()).rstrip()+'\n'

rows=[]
for row in summary['comparisons']:
    b,a=row['before'],row['after']
    rows.append(f"| {row['variant']} | {b['first_median_ms']:.2f} | {a['first_median_ms']:.2f} | {row['first_reduction_percent']:.1f}% | {b['warm_median_ms']:.2f} | {a['warm_median_ms']:.2f} |")
test_rows='\n'.join(f"| {name} | {value['total']} | {value['failures']} | {value['skipped']} |" for name,value in summary['tests'].items())
before_after=[r for r in summary['image_checks'] if r['kind']=='static-before-after']
classic=[r for r in summary['image_checks'] if r['kind']=='static-vs-coingl']
image_rows='\n'.join(f"| {r['variant']} | {r['mae_rgb']:.6f} | {r['max_channel_error']} | {r['pixels_over_3']} |" for r in classic)
animation_rows=[]
for variant in sorted({r['variant'] for r in summary['animation_checks']}):
    selected=[r for r in summary['animation_checks'] if r['variant']==variant]
    animation_rows.append(f"| {variant} | {len(selected)} | {max(r['mae_rgb'] for r in selected):.6f} | {max(r['max_channel_error'] for r in selected)} | {max(r['pixels_over_3'] for r in selected)} |")
rust_results=re.findall(r'test result: ok\. (\d+) passed;', (OUT/'rust-tests.log').read_text(encoding='utf-8',errors='replace'))
report=f'''# Melhorias Linux validadas no Windows — 2026-10-05

Branch `codex/coin-render-transform-performance`, revisão Linux `5d08e1396b`.
Os builds Release MSVC de BGFX e wgpu/Rust foram atualizados a partir dessa
revisão. A campanha usa a GTX 1060 6GB, driver 581.08, Windows 10 deste host;
não combina tempos desta máquina com os resultados Linux.

## Builds, revisão e baseline

O checkout atualizado é `H:/Git/coin/build/coin-render-source`. Os builds são
`build/coin-render-bgfx-msvc` e `build/coin-render-wgpu-msvc`. BGFX recompilou os
shaders GLSL, SPIR-V e DX11 para D3D12, inclusive instancing/opaco simples.
Não houve alteração de implementação para fazer os builds passarem no MSVC.
O oráculo de captura `SoPath` recebeu uma correção somente no teste: padding
de `CoinRenderDrawPacket` após `hasSortingCenter` podia diferir no MSVC, embora
os campos, a captura e os contadores fossem iguais. Ele agora compara todos
os campos declarados do draw e conserva `hasSamePayload` para o restante.
Um teste adversarial confirma que padding distinto é permitido, mas diferenças
de layout, source revision e bits de vértice/sorting center são rejeitadas.
Nenhum critério de cache da implementação foi relaxado.
O teste de estabilização também destruía um target emprestado à ação antes
de desconectá-lo; a substituição podia acessar memória já liberada nas três
APIs wgpu. A desconexão agora acontece antes do delete, e o contrato de
empréstimo foi documentado no header público. Os três re-tests completos
de estabilização passaram, incluindo falha de alocação, rollback e LRU.

Os binários anteriores foram copiados antes da primeira build desta campanha.
BGFX corresponde a `cf3db7ff01`; os hashes wgpu correspondem à instalação
do replay de cubos `30f1f4018f`. São baselines diferentes, explicitadas por
backend. Binários, compilação, revisão, cenas e comandos constam nos manifestos.
CoinGL antes/depois usa `SoGLRenderAction` da respectiva revisão local, através
de `SoOffscreenRenderer`, sem um checkout upstream independente.

## Cidade estática

40.000 prédios, 1024×1024, cor síncrona com cópia RGBA para o consumidor.
Três processos novos por versão/variante, intercalados, 30 warmup e 120 quadros
medidos por processo. Builds, testes e medições GPU foram serializados.
Caches de sistema/driver foram mantidos. Não há tracing nas medianas.
A sonda CoinGL/GPU e os traces experimentais são processos separados.

O primeiro quadro exclui parsing/enquadramento, preparação do helper, criação
do objeto target e capability probe; o prepare lazy do backend fica dentro do
render. Logs preservam primeiro quadro e tempo até seu resultado desde `main`.
Aquecer display lists ou planos/recursos pode ter efeito diferente por renderer.
As faixas de processos, p95 por processo e todos os comandos foram preservados;
três processos não estabelecem significância estatística.

| Variante | Primeiro antes ms | Primeiro depois ms | Redução | Aquecido antes ms | Aquecido depois ms |
|---|---:|---:|---:|---:|---:|
{chr(10).join(rows)}

CoinGL é a referência principal; as comparações internas antes/depois são
complementares. Todos os caminhos depois usam a mesma cena e a GPU NVIDIA.
Os traces separados registram contagens e escolhas dos caminhos, sem atribuir
o ganho total a um cache ou estágio apenas por subtração.

## Regressão e contratos

| Suíte | Casos | Falhas | Skips |
|---|---:|---:|---:|
{test_rows}

As quatro falhas da rodada completa (stabilization nas três APIs e timeout DrawStyle/GL)
permanecem nos XML/logs originais. A tabela reconcilia apenas falhas com um
re-test aprovado do mesmo caso/API, identificado em `verified_reruns` no JSON.
O tempo do teste DrawStyle/GL sem o limite inicial fica em
`draw-style-gl-timing.json`; referências e matriz de casos são mantidas.
A execução direta passou em 103,98 s. A rodada inicial excedeu 120 s;
o limite Windows foi ampliado para 240 s para permitir variação, sem reduzir
os casos ou desativar referências. O re-test CTest passou com a mesma matriz.

O teste de instancing BGFX também foi executado diretamente em D3D12.
O teste de referência de câmera é opt-in: os skips nas suítes iniciais foram
seguidos por execuções obrigatórias separadas em todas as seis combinações de
API/backend, com `COIN_RENDER_REQUIRE_CAMERA_REFERENCE=1`. Essas execuções
estão na tabela; os skips anteriores não indicavam falta de suporte do host.
Cargo passou {sum(map(int,rust_results))} testes ({'+'.join(rust_results)} por
grupo); o gerador passou seis testes CPU. A primeira tentativa do gerador
teve acesso a temporários negado pelo sandbox, inclusive dentro do workspace;
a execução autorizada passou sem mudança do código. Logs das tentativas
e da execução final são preservados.

Os gates cobrem instancing versus fallback, câmera e atributos de material,
geometria/overlays, profundidade, transparência, texturas, RTT, múltiplos targets,
FFI e tickets. Referências GPU/GL foram obrigatórias. Alguns testes de estado
comum são repetidos por backend para validar o conjunto compilado, além das
execuções dedicadas por API.

## Imagens estáticas e animadas

{sum(r['equal'] for r in before_after)} de 21 pares estáticos antes/depois
foram exatamente iguais em RGB. Os demais conservam métricas por par no JSON;
não se declara igualdade quando há diferenças de pixels.

Comparação estática depois contra CoinGL (RGB orientado, sem correção):

| Variante | MAE RGB | Maior erro de canal | Pixels com erro >3 |
|---|---:|---:|---:|
{image_rows}

Os controles animados são testes de imagem, não uma campanha de desempenho:
camera, transforms-10, materials-10 e geometry-10 na cidade original, mais
materials-10 na paleta de 40.001 slots. Cada variante usa dois warmup e cinco
quadros, step 120, comparando quadros lógicos 0/120/240/360/480 com CoinGL.
Capturas ficam fora do intervalo principal. Os state digests correspondem e
todas as variantes produziram imagens diferentes ao longo dos casos dinâmicos.

| Variante | Comparações RGB | Maior MAE RGB | Maior erro de canal | Maior contagem de pixels >3 |
|---|---:|---:|---:|---:|
{chr(10).join(animation_rows)}

Os números descrevem diferenças observadas; um erro médio pequeno não prova
igualdade em bordas nem ausência de diferenças de lighting/arredondamento.
A inspeção visual dos quadros estáticos e do par animado com maior MAE não
mostrou diferença estrutural visível. Esse par (BGFX/OpenGL, câmera, quadro 120)
teve quatro pixels com erro de canal acima de 3; o JSON conserva todas as
métricas, inclusive diferenças maiores em pixels isolados de outros pares.
Hash RGBA bruto entre CoinGL e outros backends não é usado para igualdade:
alpha/orientação do buffer bruto podem diferir. As imagens PPM já são orientadas.
Dados de imagem grandes permanecem em `build/linux-updates-windows-20261005`;
hashes e métricas estão nas evidências versionadas.

## Reprodução

As duas instalações locais foram atualizadas somente depois dos gates e
medições. `installed-hashes.json` confirma que Coin4.dll, CoinRender4.dll e o
benchmark instalado correspondem aos binários qualificados de cada backend.

Os scripts desta campanha estão em
[`validation/linux-updates-windows-20261005`](validation/linux-updates-windows-20261005).
`qualify.ps1` roda os gates nos builds existentes, `measure.py` roda a campanha
serial e `summarize.py` valida amostras, contratos e diferenças de imagem.
A cena de materiais é gerada com `generate_material_slot_scene.py`, usando
`--source-city build/large-scenes/city-40000.iv --objects 40000 --material-slots 40001`.

Resumo completo:
[`summary.json`](validation/linux-updates-windows-20261005/summary.json).
'''
(SOURCE/'docs/coin-render-linux-updates-windows.md').write_text(report,encoding='utf-8')
for path in OUT.iterdir():
    if path.is_file() and path.suffix in ('.json','.log','.xml','.txt','.ps1','.py','.csv'):
        (evidence/path.name).write_text(normal(path.read_text(encoding='utf-8-sig',errors='replace')),encoding='utf-8')
print('Report and evidence:',evidence)
