# R30 — duas políticas de transparência no mesmo frame

O manager usa `BLEND` global. Duas superimpositions sem depth desenham um
quadrado azul com override `BLEND` e um quadrado verde com override
`SCREEN_DOOR`, ambos com transparência 0,5 e sobrepostos ao objeto base
vermelho. O teste executa as duas ordens e compara o frame RGB integral com
CoinGL, exigindo erro máximo de um nível por canal. Ele também exige que
inverter a ordem mude a imagem em ambos os renderizadores.

As duas ordens passaram nas sete células GPU Linux disponíveis (14/14): BGFX Vulkan/OpenGL
em AMD/NVIDIA e wgpu Vulkan em AMD/NVIDIA, OpenGL em AMD. Todos os processos
`CoinRenderProductTest` terminaram com sucesso. [Resultados, logs e hashes finais](validation/coin-render-r30-mixed-transparency-reverse-20261017/verification.json).

Esta prova cobre BLEND→SCREEN_DOOR e SCREEN_DOOR→BLEND com duas camadas sem
depth. Políticas mistas com depth/clear e janela/resize continuam abertas em
R30/C19.
