# R29 — alpha combinado de material e textura no ANAGLYPH

O gate offscreen agora percorre três perfis consecutivos: textura RGBA
1×1 com alpha 128, textura RGBA 2×2 com alpha 128/64/192/96, e essa
mesma textura 2×2 multiplicada pela transparência 0,5 do material.
Todos usam `SoTexture2::MODULATE` e BLEND imediato. A comparação
normaliza a orientação do buffer CoinGL, verifica cada olho direto e
exige composição exata de R esquerdo e G/B direitos. O terceiro perfil
também deve produzir imagem diferente do segundo.

As sete células Linux disponíveis passaram: BGFX AMD/NVIDIA
Vulkan/OpenGL e wgpu AMD/NVIDIA Vulkan, AMD OpenGL. O erro de composição
foi zero em todas; o maior erro RGB por olho frente ao CoinGL foi 1
na AMD e 2 na NVIDIA. As faixas vermelhas cobertas mudaram de 65–225
na textura isolada para 32–140 com o material transparente na sonda
AMD/Vulkan. Isso confirma que ambas as fontes de alpha afetaram o frame.

O gate cobre BLEND imediato offscreen com essa textura 2×2. Janela,
outros modelos de textura, mips, RTT e políticas diferidas ainda exigem
validação específica.

[Resultados estruturados e logs](validation/coin-render-r29-material-texture-alpha-20261017/verification.json).
