# P22 — Wayland nativo e AppKit/Metal (parcial)

P22 compõe as superfícies nativas com o `CoinRenderTarget` já existente. O
Wiring valida o descriptor e mantém o contrato de tamanho em pixels de
framebuffer; a Infra wgpu cria `RawWindowHandle::Wayland` ou uma superfície
`CoreAnimationLayer`. Travessia Coin, Core de composição, publicação de RGBA,
resize e recuperação do alvo permanecem comuns. O host continua dono de
`wl_display`/`wl_surface` ou `CAMetalLayer` e despacha os eventos na thread da
janela; o alvo deve ser destruído antes dos handles.

| Combinação | Estado | Limite |
|---|---|---|
| Linux/Wayland/wgpu Vulkan | Implementada e executada com Weston headless + RADV | Falta comparação visual com Coin/GL, HiDPI real e outros compositores/drivers |
| macOS/AppKit/wgpu Metal | Descriptor, FFI e smoke preparados | Sem toolchain/host macOS; build, apresentação e Retina aguardam execução nativa |
| BGFX/Wayland ou Metal | Sem mecanismo neste conector | Seleção Metal rejeita sem fallback; implementação específica é pendente |

`COIN_RENDER_RENDERER_METAL` e os alvos de consulta Wayland, Win32 e AppKit
foram acrescentados à API tipada. Um probe desses alvos relata fatos do
adaptador ativo, não qualifica apresentação. Um pedido de Metal no Linux é
`UNSUPPORTED`; não seleciona Vulkan/OpenGL. A revisão privada da ponte segue
30, sem nova estrutura FFI: ela já transportava dois handles e renderer.

O smoke `coin_render_wayland_smoke` usa GTK somente como host da janela e dos
eventos. Pede Vulkan explicitamente, cria dois `wl_surface`, apresenta e captura
RGBA8, verifica ausência de readback em render normal, redimensiona, suspende
com tamanho zero, restaura e registra escala, checksum e adaptador. Com o build
wgpu e GTK3 de desenvolvimento disponíveis, a execução isolada foi:

```sh
env __GLX_VENDOR_LIBRARY_NAME=mesa \
  __EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/50_mesa.json \
  VK_DRIVER_FILES=/usr/share/vulkan/icd.d/radeon_icd.json \
  python3 testsuite/qt-quarter/run_isolated.py \
  --server xwayland --weston-shell desktop-shell.so \
  --weston-prefix /tmp/coin-p16-weston \
  --artifacts /tmp/coin-p22-wayland-verified --exec -- \
  env WAYLAND_DISPLAY=coin-isolated GDK_BACKEND=wayland \
  LD_LIBRARY_PATH=/tmp/coin-p17-wgpu-release/lib \
  /tmp/coin-p17-wgpu-release/bin/coin_render_wayland_smoke
```

Na AMD RADV/Renoir, as duas janelas produziram o mesmo checksum na dimensão
412×364; após resize da primeira para 572×474, a segunda manteve a dimensão e
o checksum. O probe relatou Vulkan. O desktop-shell é necessário para o resize;
o kiosk-shell força fullscreen e rejeita a geometria decorada do GTK. A sessão
headless valida a rota nativa, mas não uma tela física nem escala fracionária.

O smoke `coin_render_appkit_smoke` usa Cocoa para criar duas janelas e
`CAMetalLayer`, converte pontos para pixels de backing, pede Metal, apresenta,
captura, redimensiona, minimiza/restaura e consulta a API Metal. Em macOS com
Xcode e Rust instalados, configurar com `COIN_BUILD_RENDER=ON`,
`COIN_RENDER_BACKEND=RUST_BRIDGE`, `COIN_BUILD_RENDER_WINDOW_EXAMPLE=ON` e
`COIN_BUILD_LEGACY_GL_RENDERER=OFF`, compilar e executar esse alvo. É preciso
registrar modelo/driver/GPU, formato, versão do macOS, arquitetura, escala de
backing e resultados; só então marcar a célula como executada.

## Para fechar P22

- Wayland: comparar pixels com Coin/GL por fixture e tolerância, testar compositor
  real, scale 1/2/fracionário, `xdg_surface` configure, resize, perda/recriação,
  múltiplas janelas e destruição ordenada.
- AppKit/Metal: compilar e executar em macOS Intel e Apple Silicon conforme a
  matriz disponível; validar Retina, mudança de monitor, duas janelas,
  minimização, recuperação, captura e comparação com Coin/GL.
- Registrar por combinação backend/API/driver/OS/alvo, sem promover o probe de
  adaptador ou o smoke parcial a perfil qualificado.

As validações que exigem outro SO ou processador também ficam no
[registro externo](coin-render-platform-validation-pending.md).
