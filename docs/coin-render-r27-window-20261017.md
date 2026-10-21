# R27 — MSAA e resolve em janela wgpu

`CoinRenderOptions.sampleCount` agora chega à criação da superfície como uma
propriedade imutável do alvo. A superfície de present continua single-sample;
o wgpu mantém cor e depth 2×/4× como attachments da janela e resolve a cor
após o último passe opaco, antes de readback ou present. Resize, suspensão e
reconfiguração recriam os attachments com a mesma contagem. A capacidade
exata é conferida no formato escolhido pela superfície e em depth32, sem
redução silenciosa de 2× para 1× ou de 4× para 2×.

`CoinRenderMsaaWindowTest` criou uma janela X11 real e observou pixels
apresentados com `XGetImage`. Três cores distintas de clear carimbam cada
frame; o teste aguarda o carimbo antes de contar os pixels de borda. Nas três
células, a aresta vermelha teve 0, 1 e 30 pixels RGB parciais para 1×, 2× e
4×. A janela 4× passou por resize 32→48 e recuperação de um acquire
`OUTDATED` injetado, com novo carimbo e serial crescente.

| GPU/API | 1×/2×/4× pixels parciais | Resize/OUTDATED | Recibo |
| --- | --- | --- | --- |
| AMD RADV/Vulkan | 0/1/30 | PASS | [log](validation/coin-render-r27-window-20261017/amd-vulkan.log) |
| NVIDIA RTX 3060/Vulkan | 0/1/30 | PASS | [log](validation/coin-render-r27-window-20261017/nvidia-vulkan.log) |
| AMD radeonsi/OpenGL | 0/1/30 | PASS | [log](validation/coin-render-r27-window-20261017/amd-opengl.log) |

Na AMD/Vulkan, cinco CTests passaram: `CoinRenderMsaaTest`,
`CoinRenderMsaaWindowTest`, `CoinRenderBumpWindowTest`,
`CoinWgpuFfiFrameTest` e `CoinRenderSurfaceTest`. A suíte Rust serial
passou 48/48. O escopo de janela novo é opaco e com observação X11;
sombras, blend/OIT, BGFX e formatos RTT além de RGBA8 continuam em R27/C18.
Bump opaco MSAA em janela foi qualificado na continuação R26.4. O readback da
superfície depende de `COPY_SRC`, que não está disponível na célula
wgpu/OpenGL AMD; os resultados dessa célula são pixels apresentados.

Na continuação R26.4, `CoinRenderMsaaBumpWindowTest` comparou a imagem X11 inteira com o offscreen do mesmo fixture para bump básico, base MODULATE e material especular em 1×/2×/4×, incluindo resize 64→96→64. AMD/OpenGL, AMD/Vulkan e NVIDIA/Vulkan passaram com delta RGB máximo 4. A falha 307 após encoding preservou frame/serial, e o retry apresentou o novo material; alfa translúcido foi recusado sem present e recuperou ao voltar opaco. Na NVIDIA/Vulkan, frames de swapchain adquiridos e descartados esgotavam o acquire seguinte; a janela agora reconfigura a swapchain após essas recusas. Cinco CTests de janela/bump/MSAA por célula passaram (15/15), e a mutação que removeu essa reconfiguração foi rejeitada (1/1).
