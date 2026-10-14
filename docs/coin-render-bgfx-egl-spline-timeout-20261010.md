# Timeout Part::Spline com BGFX/OpenGL no Xwayland

Em 2026-10-10, o consumidor FreeCAD `Part::Spline` em DPR 1 voltou a
reproduzir o timeout com o cache de programas ativo e desativado. A pilha do
processo mostrou a thread principal esperando em `bgfx::frame()` e a thread de
render do BGFX em `loader_dri3_set_swap_interval()` →
`loader_dri3_wait_for_sbc()` → `xcb_wait_for_special_event()`. O trace da
[falha 68](/mnt/Laranja/Git/externos/coin-render/docs/validation/bgfx-egl-spline-timeout-20261010/hang-trace.txt) localiza
a chamada sem retorno em `eglSwapInterval(0)` na primeira ativação da
swapchain secundária. A [pilha capturada](/mnt/Laranja/Git/externos/coin-render/docs/validation/bgfx-egl-spline-timeout-20261010/hang-stack.txt)
e as falhas com cache desligado excluem o cache de programas como condição
necessária.

No SDK BGFX usado aqui (`81d81fba72c42d348c589514c774bbfe01e110fa`),
`SwapChainGL` fazia duas apresentações iniciais antes de aplicar o intervalo
à superfície. O [patch do SDK](../cmake/patches/bgfx-egl-swap-interval-before-prime.patch)
aplica o intervalo logo após `makeCurrent()` e antes dessas apresentações.
Também guarda o valor por superfície para reaplicá-lo apenas após mudança de
`reset`, evitando chamadas redundantes. O valor solicitado continua sendo
`BGFX_RESET_VSYNC ? 1 : 0`.

## Validação local

Linux Xwayland/Weston, Mesa EGL em AMD Renoir física, FreeCAD real,
`Part::Spline` Bezier 4×1, modo object, DPR 1 e timeout de 45 s:

| SDK | Cache | Resultado |
| --- | --- | ---: |
| Antes do patch, incluindo ajustes parciais | ativo/desligado | 6 timeouts capturados |
| Intervalo antes da primeira apresentação, instrumentado | ativo | 20/20 passes |
| Mesmo patch, sem instrumentação temporária | ativo | 8/8 passes |
| Mesmo patch, sem instrumentação temporária | desligado | 4/4 passes |

As 32 execuções com a ordem corrigida reportaram GPU física, 100 alterações visíveis e 100
regenerações, com remoção concluída. Os cinco testes `CoinBgfxWindowOpenGLTest`,
`CoinBgfxMultiWindowOpenGLTest`, `CoinBgfxMultiWindowOffscreenFirstOpenGLTest`,
`CoinBgfxMultiWindow_weighted_oit_openglTest` e
`CoinBgfxMultiWindow_sorted_layers_openglTest` passaram no X11 local
([log](/mnt/Laranja/Git/externos/coin-render/docs/validation/bgfx-egl-spline-timeout-20261010/window-ctest.log)). O
[CSV](/mnt/Laranja/Git/externos/coin-render/docs/validation/bgfx-egl-spline-timeout-20261010/stress.csv) registra cada
processo. A instrumentação de diagnóstico foi removida do Coin e do patch
final.

Para reproduzir, aplicar o patch na árvore do BGFX antes de compilá-lo e
instalá-lo, então reconstruir `CoinRender` com esse pacote. Exemplo:

```sh
git -C "$BGFX_SOURCE" apply --check \
  "$COIN_SOURCE/cmake/patches/bgfx-egl-swap-interval-before-prime.patch"
git -C "$BGFX_SOURCE" apply \
  "$COIN_SOURCE/cmake/patches/bgfx-egl-swap-interval-before-prime.patch"
cmake --build "$BGFX_BUILD" --parallel
cmake --install "$BGFX_BUILD"
cmake --build "$COIN_BUILD" --parallel
```

O patch foi verificado com `git apply --check` contra a revisão limpa acima e
com `git apply --check --reverse` contra o SDK instalado no ensaio. O CMake do
Coin usa `find_package(bgfx)` e não modifica automaticamente pacotes BGFX
externos. A mudança precisa entrar no SDK distribuído com a entrega. O ensaio
isola este timeout no Xwayland; Windows, Wayland nativo e outros drivers não
foram qualificados por esta campanha.
