# Perspectiva AMD/Vulkan contra CoinGL — 2026-10-12

Repeti `CoinRenderCameraReuseReferenceTest` no checkout atual com
`COIN_RENDER_REQUIRE_CAMERA_REFERENCE=1`,
`COIN_RENDER_REQUIRE_GL_REFERENCE=1`, `DISPLAY=:0`, ICD Radeon,
`COIN_GLX_PIXMAP_DIRECT_RENDERING=1` e `COIN_GLXGLUE_NO_PBUFFERS=1`.
BGFX usou `COIN_BGFX_RENDERER=vulkan`; o build wgpu usou a ponte Rust.
O gate do teste permanece MAE ≤0,10 e no máximo 131 pixels com erro RGB >3.

| Executor | Frame 5 MAE | Pixels >3 | Reuso/reconstrução |
| --- | ---: | ---: | --- |
| wgpu/Vulkan AMD | 0,147283 | 47 | RGB 0 |
| BGFX/Vulkan AMD | 0,149582 | 49 | RGB 0 |

No controle wgpu/OpenGL da mesma AMD, os nove frames de perspectiva e os
nove ortográficos passaram; no frame 5 de perspectiva, MAE foi 0,0592906,
máximo RGB 2 e nenhum pixel >3. O gate Vulkan continua falhando sem que
se relaxe a tolerância.

O teste falhou nos dois executores, como na medição anterior. Salvei o par
`perspective-CoinGL-5-{actual,reference}.ppm` com
`COIN_RENDER_CAMERA_REFERENCE_IMAGES` e comparei os RGBs sem mudar o gate.
As referências CoinGL dos dois processos são byte a byte iguais. Os 47
pixels wgpu com erro >3 são compartilhados pelo BGFX; 44 estão em `x=195`.
Em wgpu, 45 pixels são pretos no GPU e cobertos no CoinGL, 2 são o inverso,
e nenhum tem ambas as imagens cobertas. BGFX acrescenta dois pontos
`(122,206)` e `(253,158)`; seu total inclui 46 GPU pretos/CoinGL cobertos,
2 inversos e 1 ponto coberto em ambos. Isso localiza o desvio dominante na
seleção de cobertura de arestas, sem provar ainda qual convenção/precisão
produziu a diferença.

SHA-256 das imagens PPM da sessão: CoinGL
`2f3b229dc67b2c5dbf5331145ad8890d39659fe1bffb91eea6b9699f528ff0dd`,
wgpu `9fe2e824f89d4f24a4f3691b83d09ef08a5fbb8209480d83e5c09b110bca9a42`
e BGFX `1f266f124fef4375098b24f68bd7746a3d37c48bdd1e428a6d62a072845bc416`.
Os PPMs locais estão em `/tmp/coin-render-perspective-20261012/` e
`/tmp/coin-render-perspective-bgfx-20261012/`; o teste reproduz as métricas.
