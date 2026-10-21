# R29 — alpha de textura no ANAGLYPH

O ANAGLYPH do manager admite agora `transparentTexture` quando a política efetiva é BLEND imediato. Continuam recusados SCREEN_DOOR com alpha de textura, raster transparente, passes diferidos/OIT e texturas produzidas por RTT, que exigem regras de composição próprias.

`CoinRenderStereoEyeTest` usa cubo branco sem transparência de material e textura RGBA 1×1 `MODULATE` com alpha 128. Os canais R esquerdo e G/B direitos do ANAGLYPH coincidiram exatamente com os olhos diretos; cada olho coincidiu com CoinGL em RGB neste fixture. [Sete células GPU](validation/coin-render-r29-texture-alpha-20261017/verification.json) passaram em BGFX AMD/NVIDIA Vulkan/OpenGL e wgpu AMD/NVIDIA Vulkan mais AMD OpenGL.

Na [janela X11](coin-render-r29-window-anaglyph-20261017.md), um sexto frame troca alpha de material por alpha de textura mantendo BLEND e a camada verde com CLEARZBUFFER. A apresentação RGB correspondeu ao offscreen em 42/42 frames nas sete células. Filtros, mipmaps, multitextura, outros modelos de textura e RTT ainda precisam de cruzamentos estéreo próprios.
