# R30 — investigação de ZBUFFERON em superimpositions

**Resolvido na continuação:** a limpeza repetida de `SoOffscreenRenderer`
apagava o depth entre as duas aplicações da action. A [correção e o novo
gate](coin-render-r30-offscreen-fix-20261017.md) qualificam `ZBUFFERON` com
erro RGB integral zero em sete células. O texto abaixo registra a observação
anterior à correção, não o estado atual.

Uma tentativa de admitir `AUTOREDRAW|ZBUFFERON` revelou diferença observável
entre o CoinGL e a composição CoinRender. Na fixture de dois quadrados com a
mesma câmera ortográfica, base vermelha em `z=0` e camada verde em `z=-0.5`,
CoinRender conservou o centro vermelho com teste de profundidade; a captura
CoinGL mostrou o centro verde. Com a camada em `z=+0.5`, CoinRender mostrou
verde. A razão da imagem CoinGL traseira ainda precisa ser isolada, incluindo
o estado de depth herdado da action e a passagem entre `renderScene` da cena
principal e da superimposition. Portanto, essa tentativa não foi promovida a
contrato de compatibilidade.

O adaptador continua a recusar `ZBUFFERON` sem publicar um novo frame. O
`CoinRenderProductTest` agora verifica explicitamente essa recusa mesmo sem
`CLEARZBUFFER`, além do perfil frontal suportado. BGFX OpenGL/Vulkan AMD e
NVIDIA e wgpu Vulkan AMD/NVIDIA e OpenGL AMD passaram no gate físico 7/7.
Para avançar, uma captura isolada de CoinGL deve discriminar entre ausência
de depth da base, limpeza implícita e estado entre actions antes de definir
o comportamento que o adaptador deve reproduzir.
