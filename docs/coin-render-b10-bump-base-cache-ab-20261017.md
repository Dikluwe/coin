# B10 — A/B do cache da textura base bump

O teste GPU `bump_abi_payload_renders_with_dedicated_shader` alternou blocos
sem/com cache na ordem A/B/B/A. Cada bloco teve 12 submissões de uma imagem
RGBA8 opaca 512×512 no caminho bump, mantendo o pipeline R26 em cache e o
mesmo frame 8×8. A métrica inclui codificação, submissão e espera da GPU;
não inclui captura de cena, action pública, readback nem apresentação X11.

| GPU/API | Mediana sem cache | Mediana com cache | Contagem no ramo com cache |
| --- | ---: | ---: | --- |
| NVIDIA/Vulkan | 9,737 ms | 9,477 ms | 1 upload, 23 hits |
| AMD/Vulkan | 10,337 ms | 10,032 ms | 1 upload, 23 hits |
| AMD/OpenGL | 10,006 ms | 9,904 ms | 1 upload, 23 hits |

Cada execução passou os pixels bump/base/mipmap e a invalidação por mudança
de bytes com digest idêntico. A diferença observada é pequena frente ao tempo
total desta fixture; a amostra não qualifica p95 nem ganho de frame real.
O cache evita 23 uploads de 1 MiB nos 24 frames do ramo cacheado. Medir cenas
e texturas reais, tamanhos diferentes e caudas permanece em B10.
