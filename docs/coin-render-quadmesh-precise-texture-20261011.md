# QuadMesh preciso com textura: primeira borda do CoinGL

Na rota `COIN_QUADMESH_PRECISE_LIGHTING=1`, o `GLRender` do `SoQuadMesh`
constrói um fan de quatro triângulos para cada quad. Antes do primeiro quad
texturizado, `t3` e `t4` estavam nulos. O loop copiava esses ponteiros para
`t1` e `t2` e os desreferenciava para calcular a coordenada de textura do
centro. A matriz física de 2026-10-11 reproduziu `SIGSEGV` no primeiro caso
texturizado. A correção inicializa `t3`/`t4` com as coordenadas da primeira
borda, no mesmo ponto em que a função prepara posições e normais.

O teste `CoinRenderGeometryViewportTest --probe-precise-gl-texture` usa um
QuadMesh 4×2, material PHONG, oito coordenadas UV e uma imagem RGBA 2×2.
No X11 `:0` com GLX NVIDIA físico, o processo terminou com
`precise textured CoinGL covered=2376`. Antes da correção, o mesmo caso
terminava por acesso inválido. Reproduzir com:

```sh
DISPLAY=:0 __NV_PRIME_RENDER_OFFLOAD=1 __GLX_VENDOR_LIBRARY_NAME=nvidia \
COIN_GLX_PIXMAP_DIRECT_RENDERING=1 COIN_QUADMESH_PRECISE_LIGHTING=1 \
LD_LIBRARY_PATH=/tmp/coin-render-closure-bgfx/lib \
/tmp/coin-render-closure-bgfx/bin/CoinRenderGeometryViewportTest \
  --probe-precise-gl-texture
```

O mesmo smoke passou em GLX Mesa/AMD físico com cobertura 2376 ao trocar
`__GLX_VENDOR_LIBRARY_NAME=mesa`, remover o offload NVIDIA, definir
`__EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/50_mesa.json`
e `COIN_GLXGLUE_NO_PBUFFERS=1`. Sem esta última opção, o contexto GLX Mesa
não foi criado nesta sessão X11; essa falha de contexto não é um resultado do
QuadMesh.

O controle `--probe-precise-rejection` passou no mesmo build BGFX/Vulkan:
rejeição sem publicação, serial/pixels anteriores preservados, recuperação e
fallback por material de vértice, com RGB máximo 1 contra CoinGL. A tentativa
de fan centrado no callback portátil foi mantida somente como diagnóstico:
seis combinações sem textura e sem binding por face passaram, mas casos
texturizados e com binding por face excederam o gate RGB 4. Portanto a rota
portátil continua recusando a forma centrada, e este reparo CoinGL não fecha
a frente de iluminação precisa.
