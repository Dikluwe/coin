# R29 — textura 2×2 em janela X11 e resize

O teste `CoinRenderManagerWindowStereoTest` agora tem sete frames por célula.
O frame 6 atualiza a textura RGBA de 1×1 para 2×2, alpha
128/64/192/96, e redimensiona a janela de 64 para 96 pixels. O manager
ANAGLYPH mantém a camada verde com `CLEARZBUFFER` e usa BLEND imediato.
O teste lê os pixels realmente apresentados com XGetImage e compara todo
o RGB com o render offscreen da mesma revisão. No frame 6, exige pelo
menos três níveis vermelhos distintos; foram observados 26–27, contra
dois no frame 5, comprovando que a textura nova foi usada.

BGFX AMD/NVIDIA Vulkan/OpenGL e wgpu AMD Vulkan/OpenGL e NVIDIA Vulkan
passaram: **7/7 células, 49/49 frames, zero pixels RGB divergentes**.
O perfil é X11, RGBA8, `SoTexture2::MODULATE`, BLEND imediato, textura
2×2 e camada/resize descritos. Filtros/mips, RTT, outras políticas de
transparência e plataformas continuam fora deste gate.

[Resultados estruturados e logs](validation/coin-render-r29-window-texture-2x2-20261017/verification.json).
