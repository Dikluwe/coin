# Câmera móvel nas cidades: instâncias wgpu em coordenadas de mundo

Continuação de 2026-10-10 na branch `codex/coin-render`. A campanha X11
encontrava diferenças RGB entre janela e offscreen nas cidades de 40 mil e
um milhão de prédios, mesmo com a mesma cena e a mesma câmera final. O
[diagnóstico anterior](coin-render-wgpu-terrain-buffer-20261010.md) isolou
o patch de câmera da rota de batching opaco wgpu: desabilitar o patch dava
RGB 0, mas aumentava o tempo medido da cidade de 40 mil de aproximadamente
8 ms para 77–85 ms por frame.

As instâncias guardavam matrizes modelo×câmera relativas ao primeiro frame.
Cada câmera seguinte atualizava apenas um estado comum com o delta entre a
câmera inicial e a atual. A composição em precisão de `float` não reproduzia
exatamente a reconstrução do frame atual nas bordas. As instâncias qualificadas
agora guardam modelo e normal em coordenadas de mundo; o estado comum usa a
câmera atual diretamente. O patch continua a atualizar somente esse estado,
sem reenvio das instâncias. A rota de vértices opacos já assados mantém o
tratamento anterior e suas próprias condições de admissão.

Um teste de contrato compara os bytes do estado e das instâncias de um patch
com câmera rotacionada aos de uma reconstrução nova. Os seis testes dirigidos
`CoinRenderFrameReuseCoreTest`, `CoinWgpuCacheTest`, `CoinWgpuFfiFrameTest`,
`CoinWgpuLargeBindingsTest`, `CoinWgpuMultiDeviceTest` e
`CoinRenderSurfaceTest` passaram.

## Capturas físicas

SDK e consumidor instalados foram reconstruídos nesta branch. No X11 `:0`,
wgpu/Vulkan usou a NVIDIA RTX 3060 Laptop e wgpu/OpenGL usou AMD Renoir.
Cada processo executou 1280×960, dois frames medidos e um warmup, sem
readback durante a medição.

| Cena | Vulkan native | Vulkan portable | OpenGL native | OpenGL portable |
| --- | --- | --- | --- | --- |
| Cidade 40 mil | RGB 0 / RGB 0 | RGB 0 / RGB 0 | RGB 0 / RGB 0 | RGB 0 / RGB 0 |
| Cidade um milhão | RGB 0 / RGB 0 | RGB 0 / RGB 0 | RGB 0 / RGB 0 | RGB 0 / RGB 0 |

Cada célula mostra comparação janela/offscreen inicial e final. O controle
`COIN_WGPU_DISABLE_INSTANCE_COMMON_STATE=1` também passou na cidade 40 mil
wgpu/Vulkan/native. Três repetições adicionais de cidade 40 mil
wgpu/OpenGL/native passaram, inclusive a primeira captura GDI. Isso não
encerra a investigação da primeira captura preta histórica, que é
intermitente.

Na cidade de 40 mil wgpu/Vulkan/native, os frames medidos ficaram em
8,1–8,2 ms nesta rodada, comparados a 8,4–8,6 ms no registro anterior.
Na cidade de um milhão, as quatro combinações ficaram em 246–297 ms por
frame; a campanha anterior registrava 252–262 ms. São amostras curtas, sem
qualificação estatística de desempenho. As capturas PPM e os CSVs locais
estão em `/mnt/Laranja/Git/externos/coin/build-coinrender-terrain-results/`
com prefixo `world-`.

O controle `solids` wgpu/Vulkan/native continuou com RGB 0 nas duas capturas.
O terreno wgpu/Vulkan/native continuou com diferença final máxima de um canal;
portanto, o gate visual amplo do checklist permanece aberto. Os casos BGFX
e a qualificação Windows também não foram alterados por esta correção.
