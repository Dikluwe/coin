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

O gate P20 permanece máximo RGB ≤2 e MAE ≤1,0. Nenhuma tolerância ou política
de raster foi alterada. A frente de melhoria fica aberta para uma decisão
comum de cobertura que também preserve câmera, viewport, RTT e CoinGL.
