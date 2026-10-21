# R29 — captura direta dos olhos da câmera

`CoinRenderAction` agora interpreta `SoCamera::LEFT_VIEW` e `RIGHT_VIEW` na
captura. A projeção segue a construção de `SoCamera::GLRender`: frustum
deslocado pelo offset e pela distância focal/balanceamento, seguido da
translação do olho. A captura atualiza view volume, matriz de projeção e
matriz de vista no estado da action. Um viewport recortado ou frustum
inválido recebe `UNSUPPORTED` em vez de uma imagem mono silenciosa.

As propriedades de estéreo de `SoCamera` não são fields e seus setters não
alteram o node ID. A action guarda modo, offset e balanceamento de cada
câmera atravessada e compara esses valores antes de reutilizar um plano.
O caminho rápido de patch de câmera não é admitido para um olho estéreo
até ter um contrato próprio.

Uma cena perspectiva fora do plano de paralaxe foi renderizada em MONO,
LEFT, RIGHT, LEFT repetido, LEFT com offset maior, LEFT com balanceamento
alterado, LEFT restaurado e MONO restaurado. As imagens distinguem os
olhos, o offset/balanceamento altera o resultado e os estados restaurados
reproduzem os pixels originais. Os centróides de cobertura LEFT/RIGHT
foram **60,668/34,332** em CoinRender e CoinGL, com 1746 pixels cobertos
por olho, nas sete células GPU Linux disponíveis (BGFX AMD/NVIDIA
Vulkan/OpenGL; wgpu AMD/NVIDIA Vulkan e AMD OpenGL). O teste de produto
passou nas sete células; `CoinRenderActionTest` passou nos dois builds
após atualizar a expectativa do log para incluir `colorWrite=1`.

[Logs, identidade de adaptadores e hashes](validation/coin-render-r29-eye-capture-20261017/verification.json).
Este é o contrato de **um olho por apply direto**. O adaptador de
`SoRenderManager` ainda recusa ANAGLYPH, QUAD_BUFFER/SEPARATE_OUTPUT e os
modos interleaved antes de publicar. Compor os dois olhos num único frame,
camadas/viewport por olho, janela e outras plataformas permanecem em R29/C19.
