# R36/C26 — exportação após subviewport

O teste de produto agora renderiza um cubo em um viewport deslocado `(2,1,4,6)`
dentro de um alvo offscreen 8×8. Confere que a imagem mudou no interior,
que o pixel externo preservou o fundo e que L, LA, RGB e RGBA mantiveram os
64 pixels do alvo. Os planos de canal do arquivo SGI RGB são comparados byte a
byte com o readback legado. PNG e TIFF são salvos e decodificados, incluindo
o armazenamento TIFF LA como RGBA, com dimensões, canais e pixels conferidos.

O `CoinRenderProductTest` passou nas sete células Linux disponíveis: BGFX
Vulkan/OpenGL em AMD e NVIDIA; wgpu Vulkan em AMD e NVIDIA e OpenGL em AMD.
Os logs de cada processo contêm `legacy_subviewport_exports=4`, os quatro
roundtrips PNG e os quatro TIFF do controle, além do passe final.
[Resultados e logs](validation/coin-render-r36-subviewport-20261017/results.json).

Este gate cobre publicação síncrona RGBA8 offscreen e exportação do alvo
inteiro após subviewport. Não demonstra integração de consumidores, formatos
HDR, codecs adicionais, janela ou plataformas externas.
