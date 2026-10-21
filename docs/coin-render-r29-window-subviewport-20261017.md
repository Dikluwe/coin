# R29 — ANAGLYPH 2×2 em subviewport X11

O `CoinRenderManagerWindowStereoTest` passa do frame 6 (janela 96×96,
viewport completo, textura RGBA 2×2 com alpha variável) ao frame 7 na
mesma janela, com viewport em (8,8) de tamanho 80×80. Ambos preservam
BLEND imediato e a camada verde com `CLEARZBUFFER`. O teste compara
todo o RGB apresentado por XGetImage com o offscreen e exige que a área
vermelha e a área verde diminuam no subviewport, mantendo pelo menos
três níveis vermelhos da textura.

As sete células Linux disponíveis passaram: **56/56 frames, zero pixels
RGB divergentes**. Em todas elas, pixels vermelhos exclusivos caíram de
80 para 68 e pixels verdes exclusivos de 400 para 256. BGFX foi executado
em AMD/NVIDIA × Vulkan/OpenGL; wgpu em AMD/NVIDIA Vulkan e AMD OpenGL.

O gate cobre a troca de viewport entre frames do manager, após resize,
com ANAGLYPH, textura 2×2 e uma camada. Viewports distintos dentro do
mesmo frame, múltiplas camadas adicionais e outras plataformas continuam
fora deste perfil.

[Resultados e logs](validation/coin-render-r29-window-subviewport-20261017/verification.json).
