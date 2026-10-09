# Gate estrito CoinGL no AMD Renoir

Em 2026-10-09, a comparação estrita de `CoinRenderDrawStyleTest` foi fechada
no Renoir/radeonsi deste PC com Mesa 25.2.8. O contrato portátil já passava;
o bloqueio estava na referência CoinGL do driver. O reparo não modifica pixels
esperados nem tolerâncias do CoinRender.

## Evidência e correção

O [oracle OpenGL sem Coin](coin-render-raster-amd-followup-20261008.md)
mostrou que o radeonsi omitia a borda criada por `glClipPlane` em
`GL_POLYGON_MODE=LINE`: 0/18 pixels, contra 18/18 no preenchimento, na linha
explícita, no polígono pré-recortado, na NVIDIA e no llvmpipe. O mesmo caminho
omitia os vértices de interseção em modo `POINT`. A seção 14.6.4 da
[especificação OpenGL 4.6 Compatibility](https://registry.khronos.org/OpenGL/specs/gl/glspec46.compatibility.pdf)
exige rasterização das bordas/vértices de contorno do polígono recortado.

`SoIndexedFaceSet::GLRender` conserva o desenho original e, somente em
`radeonsi, renoir` com `Mesa 25.2.8`, complementa as duas interseções de um
quad simples com o plano de recorte. Em `LINES`, desenha a borda explícita; em
`POINTS`, desenha os pontos com UV e normais interpoladas. O atributo GL
corrente é salvo e restaurado. O caminho é restrito a material geral, sem
atributos de vértice personalizados; `LINES` exige cor constante e ausência de
textura. Cenas fora desse perfil mantêm o caminho original e não são
qualificadas por esta correção. A regra interna de flags de aresta do Mesa
continua por localizar e corrigir no driver.

O teste tinha também uma exigência excessiva: uma única fase de stipple para
dois polígonos separados. A especificação permite escolher a primeira aresta
rasterizada de **cada** polígono. A comparação agora exige fase consistente
por cópia, entre larguras, repetições, padrões e caminhos; o contador normativo
do CoinRender continua exato. A sonda de stipple original passou com a
biblioteca CoinGL sem o reparo acima, confirmando que essa mudança no teste é
independente da falha de clipping.

## Gates

O gate estrito completo passou com `COIN_RENDER_REQUIRE_GL_REFERENCE=1`:

| Ambiente | Resultado |
| --- | --- |
| CoinGL AMD radeonsi Renoir/Mesa 25.2.8 + CoinRender CPU/BGFX Vulkan AMD | PASS, exit 0 |
| CoinGL llvmpipe/Mesa 25.2.8 + CoinRender CPU/BGFX Vulkan AMD | PASS, exit 0 |

Os logs dos gates após compilação limpa estão em
[`validation/raster-amd-strict-gate-20261009`](validation/raster-amd-strict-gate-20261009).
O alvo `CoinRenderDrawStyleTest` foi compilado com CMake/Ninja a partir desta
branch antes dos dois gates; a compilação terminou com exit 0.
O gate cobre as cenas do teste, inclusive recorte, stipple, modelos de textura,
matriz UV, névoa e iluminação; não qualifica todos os tipos de face ou outras
versões Mesa. A diferença de iluminação do estudo anterior permanece atribuída
à diagonal distinta de `GL_QUADS` e é uma divergência documentada, sem mudança
na semântica portátil.

Reprodução no display local, depois de compilar `CoinRenderDrawStyleTest` com
BGFX e GL legado:

```sh
DISPLAY=:0 XAUTHORITY="$HOME/.Xauthority" \
  __GLX_VENDOR_LIBRARY_NAME=mesa \
  COIN_GLXGLUE_NO_PBUFFERS=1 COIN_GLX_PIXMAP_DIRECT_RENDERING=1 \
  COIN_RENDER_REQUIRE_GL_REFERENCE=1 COIN_BGFX_RENDERER=vulkan \
  build/bin/CoinRenderDrawStyleTest
```
