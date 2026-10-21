# R26 — oráculo de luz do bump legado

Em 17/10/2026, `CoinBumpGLXTest` renderizou a cena de bump com um mapa normal
RGB, textura base branca e luz direcional. Inverter a direção da luz alterou
300 bytes RGB de 16×16 nas duas GPUs físicas. A soma RGB do pixel central foi
240 com a luz frontal e 0 com a luz invertida em ambas. A fixture exige essa
ordem; o teste de UV já existente continuou aprovado no mesmo processo.

| GPU/driver GLX | Teste | Bytes alterados | Centro frontal→invertido |
| --- | --- | ---: | ---: |
| AMD Radeon Graphics/Renoir, Mesa 25.2.8 | PASS | 300 | 240→0 |
| NVIDIA GeForce RTX 3060 Laptop, 615.71.09 | PASS | 300 | 240→0 |

O núcleo comum `coin_render_legacy_bump_light_vector` projeta a direção da
luz em S/T/N por vértice, como `soshape_bumprender::calcTSBCoords`; para luz
pontual, subtrai a posição do vértice. `coin_render_legacy_bump_dot3`
normaliza o vetor interpolado e calcula o DOT3 com o normal RGB. O
`CoinRenderFrameCoreTest` aprovou os casos direcionais, pontuais e inválidos
nos builds BGFX e wgpu. Esses testes verificam a referência de iluminação;
CoinRender ainda recusa `SoBumpMap` ativo pelo B12 e não possui executor visual
R26. Falta transportar os dados aos shaders e comparar a imagem gerada.

Logs físicos: [AMD](validation/coin-render-r26-bump-light-20261017/amd.log),
[NVIDIA](validation/coin-render-r26-bump-light-20261017/nvidia.log).
