# R30/C19 — modos do SoRenderManager

O adaptador `CoinRenderManagerAdapter` agora encaminha `AS_IS`, `WIREFRAME`,
`POINTS` e `BOUNDING_BOX`. Os modos técnicos instalam `SoDrawStyleElement`
(`LINES`/`POINTS`) e
`SoLightModelElement::BASE_COLOR` com os overrides usados pelo CoinGL em
`SoRenderManager::renderSingle`; por isso prevalecem sobre um `SoDrawStyle`
`FILLED` presente no grafo. A troca de modo invalida o plano de frame em cache.
`BOUNDING_BOX` também impõe `SoComplexityTypeElement::BOUNDING_BOX` e seu
override, usando o caminho de caixa já implementado na action.
`WIREFRAME_OVERLAY` atravessa a cena duas vezes no mesmo plano: faces com
polygon offset e, em seguida, linhas com `BASE_COLOR`, binding geral e cor
de overlay com override. A cor configurada por `SoRenderManager::setOverlayColor`
invalida o plano em cache. Uma textura `REPLACE` continua substituindo a cor
difusa dos contornos, como no estado aplicado pelo CoinGL.
O estado `setTexturesEnabled(FALSE)` do manager também é encaminhado: ele
impõe qualidade zero e override de qualidade durante a captura da cena,
seguindo `renderSingle` do CoinGL.

O teste de produto usa uma esfera visível, câmera perspectiva, viewport 64×64,
estilo `FILLED`, complexidade `OBJECT_SPACE` e textura vermelha `REPLACE`
definidos na cena. Ele exige imagens RGBA distintas após `AS_IS` →
`WIREFRAME` → `POINTS` e exige que `BOUNDING_BOX` difira tanto de `AS_IS`
quanto de `WIREFRAME`. Depois, desliga
a textura, exige mudança de pixels, religa e exige restauração exata da imagem
inicial. `HIDDEN_LINE` e estéreo `ANAGLYPH` retornam `UNSUPPORTED`; a primeira
rejeição também verifica que o último frame publicado permanece intacto.
Após restaurar os modos suportados, o manager volta a renderizar. Esses gates
foram executados nas sete células
Linux disponíveis:

| Backend | GPU | API | Resultado |
| --- | --- | --- | --- |
| BGFX | AMD | Vulkan | PASS |
| BGFX | AMD | OpenGL | PASS |
| BGFX | NVIDIA | Vulkan | PASS |
| BGFX | NVIDIA | OpenGL | PASS |
| wgpu | AMD | Vulkan | PASS |
| wgpu | NVIDIA | Vulkan | PASS |
| wgpu | AMD | OpenGL | PASS |

Os [logs](validation/coin-render-r30-draw-mode-20261017/) contêm a identificação
da GPU/API e o resultado integral de `CoinRenderProductTest`. A célula
wgpu/NVIDIA/OpenGL segue indisponível conforme V04; não foi declarada PASS.
O teste prova efeito visual distinto e o override do estilo da cena, mas ainda
não compara pixel a pixel com uma captura CoinGL. Qualificação em Windows,
Android e Metal pertence aos respectivos V.

R30 e C19 continuam parciais: `SHADED_HIDDEN_LINES` e demais
modos técnicos, superimpositions, RGB/index, overlay de framebuffer,
double buffer e cruzamentos com estéreo/composição ainda exigem implementação.
Em particular, `HIDDEN_LINE` no CoinGL usa uma passagem de faces com escrita
de profundidade e máscara de cor desligada, seguida de linhas sujeitas a esse
depth. O estado de escrita de cor agora atravessa plano, BGFX e wgpu e teve
[controle depth-only nas sete células Linux](coin-render-r30-color-write-20261017.md).
Ainda faltam as travessias próprias do modo, oráculo CoinGL e os cruzamentos
de composição/alpha antes de habilitá-lo no adaptador.
R29 continua aberto para renderização por olho e composição estéreo. A rejeição
explícita impede que esses modos sejam apresentados silenciosamente como
`AS_IS` ou `MONO`.

A continuação do teste de produto exige preenchimento visível e contornos
verdes com textura desligada, troca dos contornos para azul, textura `REPLACE`
ligada/desligada com restauração exata, e preservação do frame após rejeição de
`HIDDEN_LINE`. Passou em todas as sete células acima. [Manifesto, logs GPU e
hashes das fontes](validation/coin-render-r30-overlay-20261017/verification.json).

Continuação: [HIDDEN_LINE opaco](coin-render-r30-hidden-line-20261017.md) passou
nas sete células Linux; a rejeição de HIDDEN_LINE descrita acima corresponde
à campanha anterior a essa implementação.
