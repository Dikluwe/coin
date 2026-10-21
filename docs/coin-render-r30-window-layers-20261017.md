# R30/C19 — camadas mistas em janela e resize

Uma fixture do `CoinRenderManagerAdapter` apresenta no X11 uma cena com
base vermelha opaca, camada azul `BLEND` 0,5 e camada verde
`SCREEN_DOOR` 0,5. Ela cobre **as duas ordens** e, na segunda camada,
`ZBUFFERON` ou `CLEARZBUFFER`. Na ordem BLEND→SCREEN_DOOR a verde fica
atrás da azul; na ordem inversa fica à frente. O teste exige que a limpeza
de depth altere pixels no centro em ambas as ordens, além de comparar todo
o RGB apresentado no X11 ao render offscreen do mesmo manager. Cada frame
usa uma cor de fundo própria para distinguir uma apresentação nova de uma
imagem anterior.

O ciclo **64→96→64** em cada um dos quatro cenários passou nas sete células GPU Linux disponíveis: BGFX
AMD/NVIDIA Vulkan/OpenGL e wgpu AMD/NVIDIA Vulkan mais AMD OpenGL. Foram
**84/84 frames** com zero pixels RGB divergentes. Os controles de conteúdo
mostram a camada azul e a alternância esperada da verde quando o depth é
limpo; 72 pixels centrais mudaram em cada ordem na resolução 64². A captura
X11 cobre também a célula wgpu/OpenGL AMD, que não
oferece readback nativo da swapchain. [Logs, adaptadores e hashes](validation/coin-render-r30-window-layers-20261017/verification.json).

Este gate qualifica apresentação e resize das duas ordens com
`ZBUFFERON`/`CLEARZBUFFER`, comparados ao offscreen CoinRender. A correspondência offscreen
com CoinGL para políticas mistas e depth está em
[R30 mixed-depth](coin-render-r30-mixed-depth-20261017.md); esta nova fixture
não declara uma comparação CoinGL de janela. Double buffer, outros modos,
combinações de viewport/câmera e plataformas permanecem abertos.
