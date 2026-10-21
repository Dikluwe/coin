# R26/C17 — base sRGB no bump wgpu

O perfil opaco de `SoBumpMap` + `SoTexture2` agora aceita a opção pública
`storedTextureColorSpace = COIN_RENDER_TEXTURE_SRGB`, com RGBA8 e cadeia
completa de mipmaps. O upload usa `Rgba8UnormSrgb`; o sampling do shader
decodifica RGB para linear antes de multiplicar o difuso calculado pelo mapa
normal. O alfa permanece linear e cada texel ainda precisa ter alfa 255.
Formato e bytes integram a identidade do cache entre frames: a mesma imagem
reinterpretada como linear e sRGB usa entradas GPU separadas.

Na action pública offscreen, uma textura 1×1 `[128,128,128,255]` produziu
centro linear `[102,26,13,255]` e centro sRGB `[44,11,6,255]` em AMD/Vulkan
e AMD/OpenGL; NVIDIA/Vulkan obteve `[44,11,5,255]`. O esperado sRGB vem da
decodificação analítica de 128/255 para aproximadamente 0,21586 linear,
aplicada ao difuso `[204,51,25]` sem textura. Em cada célula, mudar alfa
255→128 recusou a submissão sem mudar cor, depth ou serial; restaurar 255
republicou a mesma imagem. Os 11 controles B12 permaneceram aprovados.

O encoder direto verificou formatos FFI 3 (sRGB base) e 4 (sRGB com cadeia
2×2→1×1) nas três células: pixel após difuso + especular
`[171,75,37–38,255]`, contra `[229,90,45,255]` com base linear. Uma troca
linear→sRGB com os mesmos bytes gerou upload separado no cache. Na janela
X11, após a sequência linear e resize 256²→320×192, um alvo recriado com a
opção sRGB apresentou centro `[44,11,6]` com tolerância de um canal nas três
células; a sequência linear anterior permaneceu exata. A suíte Rust completa
passou 48/48 em AMD/Vulkan serializado.

Este recibo cobre o bump opaco wgpu no Linux disponível. BGFX, transparência,
samplers avançados, texturas maiores/variáveis, fog, sombras e plataformas
externas seguem abertos em R26/C17 e nos V correspondentes.
