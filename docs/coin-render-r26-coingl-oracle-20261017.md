# R26 — oráculo visual do bump legado CoinGL

R26 **continua sem implementação visual no CoinRender**. Esta campanha
estabelece um controle observável do efeito legado antes de transportar a
captura e a iluminação a BGFX e wgpu. O teste GLX existente já exercia
inicialização de programas ARB, display lists, segundo contexto, redraw e
limpeza, mas comparava apenas o mesmo mapa entre contextos. Agora ele alterna
no mesmo cubo iluminado: normal RGB neutra 2×2, normal RGB inclinada 2×2,
height map 3×3 e retorno à normal neutra. Cada atualização faz render novo,
mede o pixel central e, quando solicitado, salva a imagem inteira de 16×16.

| Mapa | Pixel central AMD | Pixel central NVIDIA |
| --- | ---: | ---: |
| Normal neutra | 184,184,184 | 184,184,184 |
| Normal inclinada | 26,26,26 | 26,26,26 |
| Height map | 0,0,0 | 0,0,0 |
| Neutra restaurada | 184,184,184 | 184,184,184 |

Os três mapas produzem imagens distintas. A imagem restaurada é idêntica
byte a byte à neutra inicial em cada GPU. Entre AMD Renoir e NVIDIA RTX 3060,
a diferença máxima por canal é zero na neutra/restaurada e um nível nas
imagens inclinada/height. Os controles de intensidade, imagem completa,
programas GL ativos e ciclo de vida passaram nas duas GPUs. [PPMs, logs,
hashes e métricas](validation/coin-render-r26-coingl-visual-20261017/verification.json).

O legado classifica imagens RGB não cinzas como normais de tangent space;
grayscale passa por `SoHeightMapToNormalMap::convert`, que usa Sobel e wrap.
`SoBumpMapCoordinate`/`SoBumpMapTransform`, tangentes por primitiva,
iluminação difusa/especular, múltiplas luzes, texturas, alpha, sombras e RTT
ainda precisam de captura e execução portáteis. O teste não transfere um
pass para CoinRender; B12 mantém a recusa explícita de mapas ativos até haver
um executor visual qualificado. A correspondência futura deve preservar a
recuperação ao trocar o mapa, além do quadro estático.

## Coordenadas autorais sem textura base

O controle GLX com dois triângulos e `SoBumpMapCoordinate` distinguiu três
mudanças de UV em um normal map 2×2. Sem `SoTexture2` ativo, trocar apenas as
entradas 1–3 alterou **0 bytes** da imagem 16×16; trocar a entrada 0 alterou
**300 bytes** nas duas GPUs. Com textura base branca ativa, trocar as entradas
1–3 alterou **298 bytes na AMD** e **300 na NVIDIA**. O shape foi invalidado
explicitamente a cada troca; sem isso, a cache de geometria conservava o
quadro anterior e tornava o controle inconclusivo. A seleção da primeira UV
na captura CoinRender sem textura base corresponde a este comportamento
visual CoinGL. Isso não qualifica ainda a execução do efeito no CoinRender.
[Logs e hashes](validation/coin-render-r26-bump-uv-20261017/manifest.json).
