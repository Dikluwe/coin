# P20 AMD/Vulkan: controles de cobertura e alfa

Em 2026-10-11, os binários atuais repetiram a célula offscreen 128×128 da
cena `transparent-overlap.iv` na AMD Renoir física `1002:1638`, Mesa 25.2.8,
X11 `:0`, GLX/EGL Mesa e ICD RADV fixados. O arquivo de cena tem SHA-256
`7fc70de5b45323521ff68714298049625b1f66ba1248a42c3952879d895a4caf`.
O trace do contexto BGFX/OpenGL identificou `AMD Radeon Graphics (radeonsi,
renoir)`; ambos os relatórios Vulkan identificaram `1002:1638`.
Binários `coin_render_gl_benchmark`: BGFX
`ce21dc689e0c9245d615ece2a2020c5c14541c8c8747c8dc05b53d70b1b1e1c6`;
wgpu `4cf0873765479c2fe73327612dfb7c353dcb5ec2fb45aa536d3d03e42bc11102`.

| Variante, 49 quads | Pixels RGB >3 contra CoinGL em Vulkan | Pixels >3 em BGFX/OpenGL | BGFX/wgpu Vulkan |
| --- | ---: | ---: | --- |
| Transparência original | 4 | 0 | RGB idêntico |
| Todos opacos | 6 | 0 | RGB idêntico |
| Alfa 0,5 uniforme | 4 | 0 | RGB idêntico |

A variante original reproduziu **os mesmos quatro pixels e valores RGB** do
[diagnóstico anterior](../local-diagnostics-20261009/p20-edge-analysis.json):
`(36,32)`, `(82,65)`, `(80,79)` e `(59,87)`. Tornar os quads opacos não
removeu a divergência: `(36,32)` manteve as cores originais e surgiram cinco
outros pixels de borda. Portanto alfa não é causa suficiente. A coincidência
dos dois executores Vulkan, somada ao controle BGFX/OpenGL, localiza a
diferença na rota de cobertura/seleção de fragmentos por API, mas não distingue
sozinha quantização subpixel, ownership de aresta ou depth.

O segundo script manteve todos os 49 quads no grafo para preservar o
enquadramento, deixando visível apenas o fundo e um quad por vez. O pixel
`(36,32)` divergiu nos 48 ensaios individuais, ligado à borda do fundo.
Os outros três pontos mostraram divergência isolada somente com os quads
24, 39 e 30, respectivamente. Uma cena com fundo e esses três quads conservou
os quatro pontos originais e apresentou mais um pixel divergente porque as
demais camadas foram ocultadas. Esses controles associam a diferença à
cobertura de geometria específica; não são uma prova do registrador exato ou
de que uma regra de snap única preserve os outros oráculos.

## Projeção independente das quatro arestas

O [projetor Coin](project_quads.cpp) recria a câmera e o `viewAll` do
benchmark, sem consultar as imagens dos backends, e grava as quatro posições
projetadas de cada quad em [projected-quads.txt](projected-quads.txt). O
[modelo de cobertura](coverage_model.py) calcula os sinais das arestas no
centro de cada pixel após quantizar os vértices para as grades de 1/256 e
1/4096 de pixel. Nenhuma das quatro amostras cai exatamente sobre uma aresta
quantizada, portanto a regra de empate top/left não afeta este controle.

| Pixel, quad | Grade 1/256 | Grade 1/4096 | Diferença RGB no quad isolado |
| --- | --- | --- | ---: |
| `(36,32)`, fundo 0 | Dentro | Fora | 16 |
| `(82,65)`, 24 | Dentro | Fora | 59 |
| `(59,87)`, 30 | Dentro | Fora | 102 |
| `(80,79)`, 39 | Fora | Dentro | 109 |

Os quatro sinais previstos coincidem com as capturas físicas correspondentes
([valores e distância à aresta](coverage-model.json)). Isso sustenta que a
diferença **destes quatro pixels** vem da precisão de cobertura: RADV usa a
grade de oito bits e a referência CoinGL/radeonsi desta máquina acompanha a
grade mais fina, como no estudo de câmera. Não há trace do registrador GPU
nem prova de que a mesma explicação cubra outras cenas ou drivers. A menor
distância calculada é `0,000011` pixel, então o caso do quad 24 é sensível à
precisão da projeção; a captura física corrobora o sinal neste host.

Os scripts [variants.py](variants.py) e [single_quads.py](single_quads.py)
partem da cena gerada por `scripts/coinrender/generate_p17_scenes.py` e aceitam
os diretórios dos builds. [Resumo das variantes](variants-summary.json),
[quads isolados](single-quads-summary.json) e
[cena reduzida](selected-summary.json) estão versionados. Capturas PPM e logs
integrais estão em
`/mnt/Laranja/Git/externos/coin-render-artifacts/p20-edge-controls-20261011`.
Para reproduzir, usar `DISPLAY=:0`,
`VK_DRIVER_FILES=VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/radeon_icd.json`,
`__GLX_VENDOR_LIBRARY_NAME=mesa` e
`__EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/50_mesa.json`;
os scripts habilitam pixmap GLX direto e recusam falha de execução.
Para repetir a projeção, compilar `project_quads.cpp` contra a `libCoin` do
build e passar o `.iv` original; em seguida executar `coverage_model.py` com
o texto projetado e a pasta de capturas `single-quads`.

## Sonda física de resolução maior

Sem modificar o renderer, a mesma cena foi capturada em BGFX/Vulkan com lados
`128 × fator`, para fatores ímpares de 1 a 15. O
[comparador](supersample_probe.py) lê o pixel central de cada grupo e compara
os 128×128 valores com CoinGL. Fator ímpar alinha o centro amostrado exatamente
ao centro do pixel original.

| Fator | Pixels RGB >3 | Erro máximo |
| ---: | ---: | ---: |
| 1 | 4 | 17 |
| 3 | 0 | 1 |
| 5 | 1 | 16 |
| 7 | 1 | 5 |
| 9 | 0 | 1 |
| 11 | 1 | 5 |
| 13 | 1 | 5 |
| 15 | 0 | 1 |

Uma captura wgpu/Vulkan em 384×384 foi **RGB idêntica** à BGFX/Vulkan 3×;
ambas passaram o gate contra CoinGL com máximo 1. A sequência não é monótona:
mais resolução nem sempre preserva o sinal da aresta perto de meio pixel.
O fator 3 multiplica a área de renderização por nove e o 15 por 225. São
sondas de causa, não uma política de produção nem um benchmark de custo.
[Resultados calculados](supersample-summary.json) e PPMs/logs integrais em
`/mnt/Laranja/Git/externos/coin-render-artifacts/p20-edge-controls-20261011/supersample`.

O gate P20 permanece máximo RGB ≤2 e MAE ≤1,0. Nenhuma tolerância ou política
de raster foi alterada. A frente de melhoria fica aberta para uma decisão
comum de cobertura que também preserve câmera, viewport, RTT e CoinGL.
