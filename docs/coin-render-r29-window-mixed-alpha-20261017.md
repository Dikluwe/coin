# R29 — material × textura alpha em janela X11

O frame 8 de `CoinRenderManagerWindowStereoTest` restaura o viewport
completo de 96×96 após o subviewport e mantém a textura RGBA 2×2 com
alpha 128/64/192/96, `MODULATE`, BLEND imediato e a camada verde com
`CLEARZBUFFER`. Ele altera apenas a transparência do material de 0 para
0,5 em relação ao frame 6; usa a mesma cor de fundo. O teste exige que
mais de 100 pixels mudem e que o frame apresentado por XGetImage seja
RGB idêntico ao offscreen.

Nas sete células Linux disponíveis, **63/63 frames passaram**, sem pixel
RGB divergente. O frame combinado mudou 380 pixels frente ao frame com
somente alpha da textura em cada célula e preservou 16 níveis vermelhos
distintos. BGFX passou em AMD/NVIDIA Vulkan/OpenGL; wgpu em AMD/NVIDIA
Vulkan e AMD OpenGL. A comparação por olho frente ao CoinGL do mesmo
perfil está no [gate offscreen](coin-render-r29-material-texture-alpha-20261017.md).

O recibo vale para essa combinação X11 de textura/material, uma camada,
BLEND imediato e RGBA8. Políticas diferidas/OIT, RTT, sombras e outros
formatos/plataformas seguem abertos.

[Resultados e logs](validation/coin-render-r29-window-mixed-alpha-20261017/verification.json).
