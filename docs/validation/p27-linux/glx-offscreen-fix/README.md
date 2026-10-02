# Provas da correção NVIDIA/GLX

As sondas são diagnósticos manuais complementares às fixtures CTest. Executar
da raiz do repositório; os comandos abaixo usam o build BGFX desta campanha.

```sh
g++ docs/validation/p27-linux/glx-offscreen-fix/visual-probe.cpp \
  -o /tmp/coin-glx-visual-probe -lGL -lX11
g++ docs/validation/p27-linux/glx-offscreen-fix/resolver-probe.cpp \
  -o /tmp/coin-glx-resolver-probe -lGL -lEGL -lX11
env __NV_PRIME_RENDER_OFFLOAD=1 __GLX_VENDOR_LIBRARY_NAME=nvidia \
  /tmp/coin-glx-visual-probe
env __NV_PRIME_RENDER_OFFLOAD=1 __GLX_VENDOR_LIBRARY_NAME=nvidia \
  /tmp/coin-glx-resolver-probe
```

`visual-proof.txt` mostra que buffer simples não encontra visual e buffer
duplo encontra. `resolver-proof.txt` mostra 15 configurações via GLX e zero
via EGL, mesmo solicitando as mesmas propriedades no mesmo display NVIDIA.
`pbuffer-creation.txt` registra a criação efetiva, não apenas a extensão
anunciada. `pbuffer-tests.txt` contém oito fixtures de GPU e referência GL.

```sh
g++ docs/validation/p27-linux/glx-offscreen-fix/egl-current-context-probe.cpp \
  -o /tmp/coin-egl-context-probe -Iinclude \
  -Ibuild-bgfx-recovery/coin-build/include \
  -Lbuild-bgfx-recovery/coin-build/lib \
  -Wl,-rpath,"$PWD/build-bgfx-recovery/coin-build/lib" -lCoin -lEGL -lGL -lX11
env COIN_EGL=1 /tmp/coin-egl-context-probe
```

O teste EGL cria um contexto EGL ativo antes de usar Coin, renderiza um
produtor RTT e seu consumidor, e verifica ausência de erro GL e pixels
visíveis. `egl-current-context.txt` identifica a AMD física e o resultado.
Isso valida a resolução EGL; não qualifica toda a matriz de produto EGL.

`native-eight-negative-control.txt` confirma que a NVIDIA também oferece
apenas oito unidades relevantes ao Coin/GL: a exigência de nove é recusada
com status 1. A correção do contexto não elimina esse limite.
