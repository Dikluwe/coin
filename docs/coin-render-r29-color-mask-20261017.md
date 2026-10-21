# R29 — escrita de cor por canal

O FramePlan agora carrega `colorWriteMask` (R=1, G=2, B=4, A=8), com padrão RGBA completo. A máscara participa da igualdade de estado, da montagem e do teste de reuso de frame; BGFX, wgpu e a referência CPU aplicam os canais individualmente. `colorWrite=false` continua produzindo depth-only. A ponte wgpu preserva `color_write=1` para RGBA completo e usa `0x10 | mask` somente para máscaras parciais, sem alterar a ABI.

`CoinRenderDepthContractTest` compôs um quad vermelho com máscara R e um verde com máscara G, ambos sem depth test/write, e verificou RGB vermelho+verde com azul e alfa preservados. Os controles depth-only e o transporte wgpu também passaram. Builds BGFX e wgpu: teste CPU aprovado em ambos; [matriz GPU](validation/coin-render-r29-color-mask-20261017/verification.json): 7/7 células AMD/NVIDIA × BGFX Vulkan/OpenGL e wgpu Vulkan/AMD OpenGL aprovadas. wgpu NVIDIA/OpenGL permanece indisponível neste host.

Este mecanismo passou a compor o [ANAGLYPH opaco do manager](coin-render-r29-anaglyph-20261017.md). Transparência, RTT, sombras, janela e demais modos continuam abertos em R29.
