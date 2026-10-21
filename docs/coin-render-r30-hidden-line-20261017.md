# R30/C19 — HIDDEN_LINE opaco

O modo `SoRenderManager::HIDDEN_LINE` agora atravessa a cena duas vezes no
mesmo frame: primeiro faces com escrita de profundidade e sem escrita de cor,
depois contornos `LINES` com `BASE_COLOR` e o depth já construído. O manager
encaminha o modo à action, que aplica os overrides de material, draw style e
polygon offset da sequência CoinGL. A troca de modo invalida o plano em cache.

O perfil admitido é geometria opaca na camada base. O plano rejeita
transparência de material ou textura, screen-door, alpha test, sombras,
camadas adicionais, depth clear e textura de produtor GPU; a rejeição
preserva o último frame publicado. Uma textura `REPLACE` RGB opaca é admitida;
alpha não opaco é detectado no payload RGBA, mesmo quando o sinal
`transparentTexture` do estado não o representa. Após restaurar a textura
opaca, a imagem original volta exatamente.

`CoinRenderProductTest` verifica face central preta e contornos brancos
visíveis, menos pixels de linha que `WIREFRAME` (ocultação de arestas),
contornos vermelhos com textura `REPLACE`, rejeição da textura RGBA
translúcida sem alterar o frame e recuperação. Passou nos dois builds locais
BGFX/wgpu (adaptadores padrão) e nas sete células GPU Linux disponíveis: BGFX AMD/NVIDIA ×
Vulkan/OpenGL; wgpu AMD/NVIDIA Vulkan e AMD OpenGL.
[Logs e hashes](validation/coin-render-r30-hidden-line-20261017/verification.json).

O teste funcional foi ampliado com [captura CoinGL corrigida](coin-render-r30-offscreen-fix-20261017.md)
para esfera e cubo inclinado. Geometria mais ampla, transparência, shadows,
RTT, composição, Windows, Android e Metal ainda precisam de qualificação.
`SHADED_HIDDEN_LINES` recebeu implementação e gate próprio depois deste
relatório inicial. R30/C19 continuam parciais.

O [oráculo CoinGL corrigido](coin-render-r30-offscreen-fix-20261017.md)
mostra 409 pixels de aresta no CoinRender e 408 no CoinGL nesta esfera; ambos
ocultam arestas traseiras. No cubo inclinado, CoinRender reduz 229→180 e
CoinGL 230→181. As bordas ainda diferem pixel a pixel; a captura anterior
que mostrava CoinGL HIDDEN_LINE idêntico a WIREFRAME limpava o depth entre
passagens e não é referência válida de paridade.
