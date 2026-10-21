# R30 — override SCREEN_DOOR por superimposition

O override `SCREEN_DOOR` de uma camada frontal foi comparado com a mesma
política herdada do manager. CoinRender produziu imagens idênticas nas duas
configurações; CoinGL também. A comparação integral CoinRender × CoinGL
agora tem erro máximo **zero RGB nas sete células GPU Linux disponíveis**:
BGFX Vulkan/OpenGL em AMD/NVIDIA e wgpu Vulkan em AMD/NVIDIA, OpenGL em AMD.
Todos os sete processos `CoinRenderProductTest` terminaram com sucesso.

A divergência anterior (máximo 255) vinha da ordem dos bits no bitmap do
stipple. `SoGLLazyElement` empacota o primeiro pixel no bit alto (`0x80 >>
bit`), enquanto `glPolygonStipple` consome o bit baixo primeiro. O Bayer de
CoinRender agora espelha X dentro de cada byte (`x ^ 7`), de modo consistente
no shader BGFX, no WGSL e na referência CPU. O teste passou a exigir
igualdade RGB integral com CoinGL; a regressão `CoinRenderTransparencyTest`
passou em CPU e GPU.

[Resultados, logs e hashes](validation/coin-render-r30-screen-door-20261017/verification.json).
O teste usa manager `BLEND` e override `SCREEN_DOOR`. Em seguida, com
`ZBUFFERON`, a camada à frente preserva 18 pixels azuis do stipple; atrás
do objeto base, nenhum pixel azul atravessa o depth. Com `CLEARZBUFFER`,
os 18 pixels reaparecem atrás. Os três frames têm RGB integral idêntico ao
CoinGL nas sete células, 21/21 casos. [Logs e hashes do cruzamento com
depth](validation/coin-render-r30-screen-door-depth-20261017/verification.json).

Janela, políticas misturadas no mesmo frame e demais configurações de
camadas continuam abertas em R30/C19.
