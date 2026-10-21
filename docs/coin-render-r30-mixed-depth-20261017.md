# R30 — BLEND e SCREEN_DOOR com profundidade entre camadas

O manager compõe duas superimpositions frontais sobre um objeto vermelho.
A primeira, azul com transparência 0,5, usa override `BLEND` e `ZBUFFERON`
em Z=0,5. A segunda, verde com transparência 0,5, usa override
`SCREEN_DOOR` em Z=0,25. O teste compara dois estados da segunda camada:
`ZBUFFERON` preserva o depth da primeira; `CLEARZBUFFER` o limpa antes de
desenhar. As imagens resultantes precisam ser diferentes e cada frame RGB
inteiro precisa concordar com CoinGL dentro de um nível por canal.

Os dois estados passaram em todas as sete células GPU Linux disponíveis
(14/14): BGFX Vulkan/OpenGL em AMD/NVIDIA e wgpu Vulkan em AMD/NVIDIA,
OpenGL em AMD. Todos os processos `CoinRenderProductTest` passaram.
[Resultados, logs e hashes](validation/coin-render-r30-mixed-depth-20261017/verification.json).

O teste também executa a ordem inversa: `SCREEN_DOOR` verde primeiro e
`BLEND` azul atrás do objeto base. `ZBUFFERON` bloqueia a camada azul;
`CLEARZBUFFER` a libera. Essa ordem passou nos mesmos dois modos e nas sete
células, elevando o gate para **28/28 comparações**. O clear muda a imagem
em cada ordem. [Logs finais das duas ordens](validation/coin-render-r30-mixed-depth-reverse-20261017/verification.json).

O perfil cobre essas duas ordens, com dois overrides e os modos de depth
indicados. Variações de câmera, janela/resize e demais combinações de
manager continuam abertas.
