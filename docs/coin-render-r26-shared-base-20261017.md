# R26 — textura base compartilhada no frame

O encoder wgpu opaco agora associa cada par `(texture_slot, sampler_slot)` a
um único upload de imagem e sampler no frame. Cada draw conserva seu uniform
próprio de matriz UV. Antes, dois draws com a mesma textura enviavam a imagem
duas vezes à GPU.

O teste GPU codificou dois draws com o mesmo par de slots e verificou uma
imagem base criada/enviada. Os controles de pixel isolado, textura e mipmaps
também passaram em [AMD/Vulkan](validation/coin-render-r26-shared-base-20261017/amd-vulkan.log),
[NVIDIA/Vulkan](validation/coin-render-r26-shared-base-20261017/nvidia-vulkan.log)
e [AMD/OpenGL](validation/coin-render-r26-shared-base-20261017/amd-opengl.log).
Após a mudança, action pública offscreen/B12 e janela X11/resize/alfa
mantiveram seus pixels e seriais nas três células; a suíte Rust passou 48/48.

O reuso é **dentro do frame**. Frames sucessivos ainda enviam a textura base
novamente; custo e caudas dessa transferência continuam em B10. Pares com
samplers distintos usam recursos separados neste perfil.
