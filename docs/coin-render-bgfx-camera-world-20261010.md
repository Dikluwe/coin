# Câmera móvel nas cidades: instâncias BGFX em coordenadas de mundo

Continuação da [correção wgpu](coin-render-wgpu-camera-world-20261010.md)
em 2026-10-10 na branch `codex/coin-render`. O SDK BGFX atual reproduziu a
diferença da cidade de 40 mil em Vulkan/native: RGB 0 inicial, RGB máximo
136 final entre janela e offscreen. A rota de instâncias também guardava
modelo×câmera do frame inicial e aplicava um delta relativo nas câmeras
seguintes.

No perfil opaco qualificado de depth `LESS`, as instâncias agora guardam o
modelo e a normal em coordenadas de mundo. O shader usa a câmera atual desde
o primeiro frame; o patch troca só o uniforme compartilhado e conserva as
instâncias e os meshes. O teste `CoinBgfxCoreTest` compara os bytes dos draws
reconstruídos com os patched sob translação e rotação. A rota anterior de
instâncias estáticas foi preservada para depth `LEQUAL`, vistas não rígidas
e modelos singulares: um controle de materiais coplanares sob `LEQUAL`
detectou mudança de ownership quando essas cenas usavam a nova representação.

## X11 físico

SDK e consumidor BGFX foram reconstruídos desta branch. A campanha usou
1280×960, dois frames medidos e um warmup. BGFX/Vulkan executou na NVIDIA
RTX 3060 Laptop; BGFX/OpenGL executou na AMD Renoir.

| Cena | Vulkan native | Vulkan portable | OpenGL native | OpenGL portable |
| --- | --- | --- | --- | --- |
| Cidade 40 mil | RGB 0 / RGB 0 | RGB 0 / RGB 0 | RGB 94 / RGB 127 | RGB 94 / RGB 127 |
| Cidade um milhão | RGB 0 / RGB 0 | RGB 0 / RGB 0 | RGB 135 / RGB 146 | RGB 135 / RGB 146 |

Cada célula mostra comparação janela/offscreen inicial e final. A cidade
40 mil BGFX/Vulkan/native mediu 6,7–7,1 ms por frame, contra 6,7–7,0 ms
antes da correção; é apenas uma comparação curta. `solids` BGFX/Vulkan/native
continuou RGB 0 inicial/final. O terreno BGFX/Vulkan/native continuou com
diferença final máxima de um canal.

Os sete testes dirigidos `CoinBgfxSurface_vulkan_object`,
`CoinBgfxSurface_opengl_object`, `CoinBgfxCoreTest`,
`CoinBgfxProgramSelectionTest`, `CoinBgfxInstancingTest`,
`CoinBgfxInstancingOpenGLTest` e `CoinRenderFrameReuseCoreTest` passaram.
Os CSVs e PPMs locais estão em
`/mnt/Laranja/Git/externos/coin/build-coinrender-terrain-results/` com
prefixo `bgfx-world-`.

## Pendências

BGFX/OpenGL ainda apresenta diferenças antes de qualquer movimento da câmera.
Na cidade 40 mil com câmera estática, o único pixel inicial diferente em
`(797,478)` manteve RGB máximo 94 no frame final. Sob movimento, o frame
final teve seis pixels diferentes. A causa dessa borda OpenGL não foi
isolada; a correção de câmera não deve ser tomada como solução dela.
Também seguem abertos o terreno com diferença de um canal, a primeira
captura preta intermitente wgpu/OpenGL e a revalidação Windows.
