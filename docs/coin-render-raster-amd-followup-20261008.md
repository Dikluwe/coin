# Raster AMD: borda criada por plano de recorte

Continuação controlada do [estudo de junções](coin-render-raster-junctions-study.md)
na branch `codex/coin-portable-sampling-study`. O gate estrito
`CoinRenderDrawStyleTest` foi repetido em BGFX/Vulkan AMD com CoinGL exigido no
Mesa instalado: exit 1, a diferença histórica em `clip=1,width=1,pattern=65535`,
`x=16,y=10..27`, CoinGL 0 contra Core 255. O mesmo binário, sem a comparação
CoinGL, passou o contrato portátil completo (exit 0). Nenhum gate ou tolerância
foi alterado. [Log estrito](validation/raster-amd-followup-20261008/bgfx-amd-vulkan-styles-strict.log)
e [controle portátil](validation/raster-amd-followup-20261008/bgfx-amd-vulkan-styles-portable.log).
Após acrescentar a sonda opcional, os gates padrão foram repetidos:
[estrito exit 1](validation/raster-amd-followup-20261008/bgfx-amd-vulkan-styles-strict-final.log)
e [portátil exit 0](validation/raster-amd-followup-20261008/bgfx-amd-vulkan-styles-portable-final.log).

Uma opção isolada no próprio teste seleciona só essa célula e informa quantos
dos 18 pixels em `y=10..27` são cobertos por linha CoinGL, preenchimento CoinGL
e linha Core. Essa sonda usa o backend CPU para Core e CoinGL para a referência;
os gates completos acima cobrem BGFX/Vulkan. O deslocamento é em pixels de
janela, aplicado ao plano em
coordenadas do objeto antes de rasterizar. Resultados:

| Deslocamento do plano | Coluna | Linha CoinGL | Preenchimento CoinGL | Linha Core |
| ---: | ---: | ---: | ---: | ---: |
| −0,25 px | 16 | 0 | 18 | 18 |
| 0 px | 16 | 0 | 18 | 18 |
| +0,25 px | 16 | 0 | 0 | 18 |
| +0,25 px | 17 | 0 | 18 | 0 |
| +1 px | 17 | 0 | 18 | 18 |
| 0 px, borda original direita | 45 | 18 | 0 | 18 |

O preenchimento prova que a geometria passa pelo plano. A borda original à
direita prova que CoinGL desenha linhas nessa cena. A borda **criada pelo
recorte** não aparece em CoinGL nos quatro deslocamentos, enquanto Core a
inclui no contorno. No deslocamento +0,25 px há também uma diferença de
quantização entre o preenchimento nativo e Core; ela não explica a ausência
contínua da borda nos outros deslocamentos. Esses controles identificam o
mecanismo visível, sem estabelecer ainda se a escolha vem do CoinGL, do estado
GL que ele emite ou da regra do driver para arestas criadas pelo clipping.

Logs: [−0,25](validation/raster-amd-followup-20261008/clip-probe-minus-quarter.log),
[zero](validation/raster-amd-followup-20261008/clip-probe-zero.log),
[+0,25](validation/raster-amd-followup-20261008/clip-probe-plus-quarter.log),
[+1](validation/raster-amd-followup-20261008/clip-probe-plus-one.log).
Todos os quatro processos da sonda retornam 1 porque o gate CoinGL permanece
ativo; esse retorno é o resultado esperado do estudo, não um PASS reclassificado.
Receita para um deslocamento:

```sh
COIN_RENDER_REQUIRE_GL_REFERENCE=1 COIN_DRAWSTYLE_CLIP_PROBE=1 \
COIN_DRAWSTYLE_CLIP_BIAS_PIXELS=0 \
COIN_BGFX_RENDERER=vulkan \
build-bgfx/bin/CoinRenderDrawStyleTest --probe-clipped-boundary
```

O ambiente usa o ICD AMD/RADV e o GLX Mesa do sistema; os comandos completos
com paths locais e logs ficam em
`/mnt/Laranja/Git/externos/coin-portable-sampling-artifacts/20261008-sampling-api-linux/raster-amd-followup-20261008`.

## Controle OpenGL sem Coin — 2026-10-09

O [reproducer mínimo](../testsuite/reproducers/raster-clip-edge/RasterClipEdgeGlOracle.cpp)
usa o mesmo quadrilátero, viewport 64×64 e plano em `x=16,5` pixels.
`glPolygonMode(GL_LINE)` com `glClipPlane` reproduziu a ausência da borda
apenas no radeonsi da AMD; o mesmo contexto desenhou a borda quando recebeu
um polígono já recortado ou uma linha explícita. NVIDIA e llvmpipe desenharam
a borda também quando ela foi criada pelo plano:

| GL da máquina | Polígono recortado, linha | Preenchimento recortado | Polígono pré-recortado, linha | Linha explícita |
| --- | ---: | ---: | ---: | ---: |
| AMD radeonsi, Mesa 25.2.8 | 0/18 | 18/18 | 18/18 | 18/18 |
| NVIDIA 615.71.09 | 18/18 | 18/18 | 18/18 | 18/18 |
| llvmpipe, Mesa 25.2.8 | 18/18 | 18/18 | 18/18 | 18/18 |

As contagens são da coluna `x=16`, `y=10..27`. A borda original direita
(`x=45`) teve 18/18 em todos os perfis. Os
[logs OpenGL](validation/raster-clip-edge-20261009/amd-radeonsi.log)
incluem versões, controles e `glGetError=0`; há também os controles
[NVIDIA](validation/raster-clip-edge-20261009/nvidia.log) e
[llvmpipe](validation/raster-clip-edge-20261009/llvmpipe.log).
Na sonda CoinGL original, [AMD](validation/raster-clip-edge-20261009/coingl-amd.log)
repetiu `line=0`, `filled=18`, `core_line=18` e saiu com código 1;
[NVIDIA](validation/raster-clip-edge-20261009/coingl-nvidia.log)
deu `line=18`, `filled=18`, `core_line=18` e código 0.

A especificação [OpenGL 4.6 Compatibility, §§13.7 e 14.6.4](https://registry.khronos.org/OpenGL/specs/gl/glspec46.compatibility.pdf)
descreve arestas criadas pelo clipping como bordas e sua rasterização no modo
`LINE`. O controle isola uma diferença na combinação de clipping de polígono
e modo de linha do radeonsi deste PC; não demonstra que CoinGL suprimiu a
borda nem certifica conformidade completa do driver. A causa interna do Mesa
e outros modos de linha continuam para investigação. Não houve mudança no
renderizador, no gate estrito ou na tolerância portátil.

Para repetir o controle OpenGL:

```sh
c++ testsuite/reproducers/raster-clip-edge/RasterClipEdgeGlOracle.cpp \
  -o /tmp/coin-raster-clip-edge-gl -lGL -lglut
DISPLAY=:0 XAUTHORITY=/home/dikluwe/.Xauthority \
  __GLX_VENDOR_LIBRARY_NAME=mesa /tmp/coin-raster-clip-edge-gl
```

## Separação do caminho de clipping e da iluminação — 2026-10-09

O oracle agora testa também um vertex shader que exporta
`gl_ClipDistance[0]` para o mesmo plano. Na AMD Renoir/radeonsi, o polígono
recortado em `GL_LINE` ainda tem `x16=0/18`, enquanto o preenchimento tem
`18/18`. No llvmpipe, a linha por `glClipPlane` e por `gl_ClipDistance` tem
`18/18`. Portanto, trocar a origem do plano não resolve a perda da borda no
caminho de hardware desta GPU. O Mesa 25.2.8 encaminha os planos fixos por
`ucp_mask` e as distâncias do shader por `clipdist_mask` em
`si_emit_clip_regs()` (`src/gallium/drivers/radeonsi/si_state.c`); ambos
convergem no clipping antes do modo de polígono. `GL_TRIANGLES` recortados
também perdem quase toda a borda criada pelo plano (`x16=1/18`). Isso descarta
uma causa exclusiva de `GL_POLYGON` ou do CoinGL. O Renoir é GFX9 e o
`si_pipe.c` só habilita NGG por padrão a partir de GFX10; desabilitar NGG
não é uma hipótese útil nesta máquina. Ainda falta identificar a
regra exata do clipper/flags de aresta do radeonsi e testar uma correção do
driver. Nenhuma tolerância ou driver instalado foi modificado.
[Logs AMD](validation/raster-clip-edge-20261009/amd-shader-clip.log) e
[llvmpipe](validation/raster-clip-edge-20261009/llvmpipe-shader-clip.log).

Uma alteração experimental no Mesa privado forçou
`S_02881C_USE_VTX_EDGE_FLAG(false)` em `si_get_vs_out_cntl()`.
O carregamento do `libgallium` experimental foi confirmado no processo; a
borda nova continuou em `0/18`, tanto com plano fixo quanto com distância de
shader. Essa flag de saída do vertex shader, isoladamente, não é a causa.
O fonte e o build privados foram restaurados depois do teste.
[Log](validation/raster-clip-edge-20261009/amd-private-no-vtx-edgeflag.log).

A diferença anterior de geometria completa
`bindings/0/7/generated-0/alpha-0/fast-0` foi reproduzida em uma sonda
isolada (`--probe-lighting` em `CoinRenderGeometryViewportTest`). Na AMD,
CPU/GPU tem erro máximo 1 canal e CPU/CoinGL 11 com três luzes. Sem luzes
diretas, CPU/CoinGL cai a 1; só a direcional dá 7, só a pontual ou o spot dão
3. Material uniforme reduz o máximo a 4, enquanto normal uniforme preserva
11. Uma paleta de materiais **afim sobre cada quad** reduz o erro a 3. O modo
`BASE_COLOR`, que elimina o cálculo de luz, ainda diverge até 18: logo a
diferença principal não vem da fórmula das luzes.
[Medições das variantes](validation/raster-clip-edge-20261009/lighting-amd.log).

O caminho callback de `SoFaceSet` decompõe `QUADS` nos triângulos 0–1–2 e
0–2–3 (`src/shapenodes/soshape_primdata.cpp`), usados por CPU/GPU. O caminho
GL direto envia `GL_QUADS` (`src/shapenodes/SoFaceSet.cpp`), e radeonsi usa
`DI_PT_QUADLIST` (`si_state_draw.cpp`). O
[oracle de diagonais](../testsuite/reproducers/raster-clip-edge/RasterQuadDiagonalGlOracle.cpp)
com cores não afins encontrou, no interior do quad:

| Renderizador GL | `GL_QUADS` vs 0–2 | `GL_QUADS` vs 1–3 |
| --- | ---: | ---: |
| AMD radeonsi Renoir | máximo 17; 1926 canais >1 | **máximo 0; 0 canais >1** |
| llvmpipe | **máximo 0; 0 canais >1** | máximo 17; 1926 canais >1 |

Logs: [AMD](validation/raster-clip-edge-20261009/quad-diagonal-amd.log) e
[llvmpipe](validation/raster-clip-edge-20261009/quad-diagonal-llvmpipe.log).

Assim, as diferenças de iluminação observadas nesta célula são explicadas
pela diagonal oposta de triangulação do quad e pela interpolação dos valores
de vértice já iluminados. Não há evidência nesta sonda de erro na equação de
iluminação do CoinRender. A comparação estrita CoinGL dessa célula continua
falhando na AMD; CPU/GPU mantém máximo 1. A comparação portátil não deve
trocar sua diagonal para seguir um comportamento particular do driver.

Para repetir a sonda de quad:

```sh
c++ testsuite/reproducers/raster-clip-edge/RasterQuadDiagonalGlOracle.cpp \
  -o /tmp/coin-raster-quad-diagonal -lGL -lglut
DISPLAY=:0 XAUTHORITY=/home/dikluwe/.Xauthority \
  __GLX_VENDOR_LIBRARY_NAME=mesa /tmp/coin-raster-quad-diagonal
```

Para repetir cada variante de iluminação no build wgpu do projeto, use
`CoinRenderGeometryViewportTest --probe-lighting MODE`, com `MODE` igual a
`all`, `base`, `ambient-only`, `directional-only`, `point-only`, `spot-only`,
`overall-material`, `overall-normal`, `uniform-normal` ou `affine-material`.
Na AMD deste PC, o contexto CoinGL requer
`COIN_GLXGLUE_NO_PBUFFERS=1 COIN_GLX_PIXMAP_DIRECT_RENDERING=1`.
