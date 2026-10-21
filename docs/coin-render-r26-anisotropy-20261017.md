# R26/C17 — sampler anisotrópico na base bump wgpu

O encoder bump opaco agora aceita a anisotropia já transportada pelo plano
`CoinRender`: fatores 1/2/4/8/16, com fatores acima de 1 restritos a
`LINEAR_MIPMAP_LINEAR`. O sampler wgpu usa `anisotropy_clamp` validado pelo
mesmo contrato do caminho comum. Pedidos 3 e fator 2 com filtro incompatível
são recusados antes da submissão. Texturas lineares e sRGB continuam separadas
no cache por formato e parâmetros do sampler.

O teste direto de GPU passou fatores 2/4/8/16 em AMD/Vulkan, NVIDIA/Vulkan e
AMD/OpenGL, com pixel constante `[229,90,45,255]`. A action pública passou
`SoBumpMap` + `SoTexture2` sRGB sob `SoComplexity::textureQuality=1` e
`CoinRenderOptions::maxTextureAnisotropy=8` nas três células. O plano capturado
registrou `filter=3`, anisotropia 8; o centro sRGB foi `[44,11,6,255]` na
AMD e `[44,11,5,255]` na NVIDIA. Rejeição de alpha 128, preservação de cor,
depth e serial, e recuperação continuaram passando. B12 CPU 11/11 e a suíte
Rust 48/48 também passaram. O teste de janela linear/sRGB, resize e
recuperação de alpha passou novamente nas três células.

A continuação visual usou uma textura RGBA8 256×256 de faixas horizontais
alternadas a cada oito texels, com mipmaps completos gerados por média 2×2.
O triângulo cobre 8×8 pixels e varia UV em 1,0 no eixo U e 0,125 no V;
assim o footprint é oblíquo e a escala de minificação difere oito vezes
entre eixos. O teste deslocou V em oito fases e leu o mesmo pixel central
em cada fase, mantendo todo o resto do frame igual. Fator 1 produziu
`[229,229,229,229,229,229,229,229]`; fator 8 produziu
`[127,255,255,127,127,255,255,127]` em **cada** uma das três células.
O gate exige variação ≤4 sem anisotropia e ≥64 com fator 8, demonstrando
que o sampler preserva sinal que o mip isotrópico atenua nesta cena.

Isto qualifica o efeito visual nesse footprint e textura, sem prometer a
mesma resposta em todos os ângulos, formatos ou drivers. Texturas de produção,
BGFX, transparência e plataformas externas permanecem abertos em R26/C17/R08.
