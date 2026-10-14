# Luzes exatas no overlay de câmera

O terreno iluminado com câmera móvel ainda diferia por um canal entre janela
e offscreen em Vulkan, mesmo depois de corrigidas as instâncias opacas. A
diferença desaparecia com `COIN_RENDER_DISABLE_CAMERA_OVERLAY=1`. O overlay
reconstruía uma base mundial a partir das posições e direções já convertidas
para espaço de câmera; a inversão e a nova transformação introduziam
arredondamento em ponto flutuante.

O plano agora guarda, quando a captura consegue provar a correspondência com
`SoLightElement`, a matriz de modelo mundial e as coordenadas originais de
cada luz. O overlay aplica a matriz de visualização atual sobre essa origem e
executa a mesma transformação usada na captura completa. Para luzes sem essa
prova, permanece a transformação anterior. A comparação dos snapshots de luz
também passou a ler seus campos, pois comparar a estrutura inteira incluía
bytes de preenchimento indeterminados.

O teste `CoinRenderFrameReuseCoreTest` compara o overlay de uma câmera
rotacionada com uma captura nova e verifica o rollback. Também passaram
`CoinWgpuFfiFrameTest`, `CoinWgpuMultiDeviceTest`, `CoinRenderSurfaceTest` e
`CoinBgfxCoreTest` nas configurações correspondentes.

## Recibos em janela neste Linux

Campanha de 1280×960, dois frames medidos e um warmup. Cada par abaixo é
RGB máximo inicial/final na comparação janela/offscreen. Os CSVs, PPMs e logs
estão em `/mnt/Laranja/Git/externos/coin/build-coinrender-terrain-results/`
com prefixo `light-final-`.

| Implementação e API | Política | Terreno de um milhão | Cidade 40 mil |
| --- | --- | --- | --- |
| wgpu/Vulkan | native | 0/0 | 0/0 |
| wgpu/Vulkan | portable | 0/0 | não repetida |
| BGFX/Vulkan | native | 0/0 | 0/0 |
| BGFX/Vulkan | portable | 0/0 | não repetida |
| wgpu/OpenGL | native | 211/0 | não repetida |
| BGFX/OpenGL | native | 123/3 | não repetida |

Em wgpu/OpenGL, a primeira captura mostrou a falha intermitente de
apresentação X11 já registrada. BGFX/OpenGL conserva uma divergência própria,
inclusive no primeiro frame. A qualificação Windows, outras cenas e a
medição de desempenho mais ampla continuam pendentes.
