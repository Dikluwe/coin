# F15 — captura explícita da janela no perfil RGBA8

A janela pode agora publicar o **quadro apresentado** como RGBA8, com linhas
de cima para baixo, quando o chamador pede a captura antes da submissão:

```cpp
if (target->requestWindowReadbackRGBA()) {
  action.apply(scene);
  if (action.getLastStatus() == CoinRenderAction::SUCCESS)
    target->readbackRGBA(pixels);
}
```

`CoinRenderSceneManager` usa o mesmo alvo; o pedido é feito por
`manager.getRenderTarget()`, seguido de `manager.render()`. O pedido é de um
único frame. Um render normal posterior não mantém pixels publicados, nem
cria staging. `borrowRGBA()` continua exclusivo do alvo offscreen; captura de
janela é obtida por cópia em `readbackRGBA()`. Resize e suspensão invalidam a
captura. Um pedido rejeitado (alvo offscreen, suspenso ou inválido) informa
`FALSE` e diagnóstico. O mesmo limite nominal de 128 MiB do Core é aplicado
ao RGBA solicitado. Se a submissão falha, o último quadro capturado
permanece publicado e o pedido é consumido; o próximo render normal não
repete a captura. A publicação comum só ocorre quando o backend entregou
exatamente `width × height × 4` bytes.

A responsabilidade de CoinRender é a política de captura por frame, tamanho,
formato público e publicação transacional. O BGFX solicita a screenshot da
swapchain **somente naquele frame**, recebe callback do render thread e
normaliza formato BGRA/RGBA e origem. Pedidos têm identificador único para
uma callback atrasada não publicar um quadro errado. O wgpu usa
`COPY_SRC` na superfície apenas quando ela oferece essa capacidade; no
frame solicitado copia a textura adquirida para um buffer mapeável, converte
BGRA quando necessário e publica depois da confirmação de GPU. Caso a
superfície não ofereça `COPY_SRC` ou formato RGBA8/BGRA8, a captura responde
`UNSUPPORTED` com diagnóstico, enquanto apresentação normal permanece
disponível. Nenhum desses mecanismos interpreta a cena Coin.

O perfil qualificado é **cor RGBA8 síncrona de janela Xlib**, em
BGFX/OpenGL, BGFX/Vulkan e wgpu/Vulkan. Readback de depth e tickets
assíncronos continuam no alvo offscreen; a API de janela não promete essas
modalidades. Os custos de captura podem bloquear a CPU, portanto a política
normal da janela segue sem readback.

## Evidência

No AMD Renoir `1002:1638`, Mesa radeonsi/RADV, os três executores
capturaram a cena opaca P17 a 128² com o mesmo checksum FNV-1a
`0xbd7999730dbe6385`. Em runs pareados sem pedido, BGFX reportou zero em
`readback_pipeline_bytes`, staging GPU/CPU e bytes publicados; wgpu reportou
zero em staging de cor/depth e pool livre. Durante a captura, BGFX
registrou 65.536 bytes RGBA publicados sem ring de readback; wgpu registrou
65.536 bytes de staging mapeado, liberado após a cópia. A
[validação reproduzível](../scripts/coinrender/run_window_capture_validation.py)
guarda stdout/stderr de cada uma das seis execuções e verifica ambos os
resultados; o [inventário](inventories/coin-render-window-capture.json)
registra os checksums.

`CoinBgfxWindowTest` passou em OpenGL e Vulkan: pixels vermelhos no centro,
captura completa, pedido de um frame, resize/suspensão/restauração e
apresentação. `CoinRenderSurfaceTest` passou em wgpu/Vulkan: captura completa,
rollback após timeout injetado, pedido consumido, resize e recuperação de
perda de superfície/dispositivo. Ambos mantiveram a verificação de que render
normal não expõe pixels. O checksum igual testa orientação e canais nesta
cena; não substitui a [matriz física P20](coin-render-work-plan.md).

```sh
env __GLX_VENDOR_LIBRARY_NAME=mesa \
  __EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/50_mesa.json \
  VK_DRIVER_FILES=/usr/share/vulkan/icd.d/radeon_icd.json \
  python3 testsuite/qt-quarter/run_isolated.py \
  --server xwayland --weston-prefix /tmp/coin-p16-weston \
  --artifacts /tmp/coin-window-capture-session --exec -- \
  python3 scripts/coinrender/run_window_capture_validation.py \
  --output-dir /tmp/coin-window-capture-output \
  --bgfx-build "$PWD/build-bgfx-recovery/coin-build" \
  --wgpu-build /tmp/coin-p17-wgpu-release \
  --scene /tmp/coin-p17-scenes/opaque-interleaved.iv
```
