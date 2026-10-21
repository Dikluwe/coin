# R30/C19 — SHADED_HIDDEN_LINES: primeiro perfil

`SoRenderManager::SHADED_HIDDEN_LINES` agora é encaminhado pelo adaptador.
A action captura três travessias no mesmo plano: faces `FILLED`/PHONG com
polygon offset; linhas visíveis com depth `LEQUAL` e sem escrita de depth;
linhas ocultas com depth `GREATER`, largura 1 e padrão `0xF0F0` fator 2.
A sequência segue `SoRenderManager::renderSingle` do CoinGL. O estado da
action é restaurado após cada frame.

A admissão atual usa o mesmo perfil opaco de `HIDDEN_LINE`: sem alpha de
material/textura, screen-door, alpha test, sombras, camadas ou limpeza
intermediária de depth. Textura translúcida retorna `UNSUPPORTED` e
preserva o último frame. O teste de produto exige superfície central
preenchida, pixels diferentes do preenchimento puro e de `HIDDEN_LINE`,
e preservação de frame após rejeição. Passou nos builds locais BGFX/wgpu e
nas sete células GPU Linux disponíveis:
[logs e hashes](validation/coin-render-r30-shaded-hidden-lines-20261017/verification.json).

Um controle adicional inspeciona o plano final: faces com depth write e offset,
arestas visíveis com `LEQUAL`, e arestas ocultas com `GREATER` e sem depth
write. O expansor consome o padrão de linha e gera **2.166 índices** na passagem
tracejada contra **9.024** na passagem sólida, de modo idêntico nas sete
células. A comparação visual pixel a pixel com CoinGL ainda falta. Não se declara paridade de pixels,
nem qualificação de alpha/sombras/RTT/composição ou outras plataformas.
R30/C19 continuam parciais.

O [controle CoinGL](coin-render-r30-coingl-oracle-20261017.md) posterior
passou o gate RGB definido nas sete células Linux disponíveis: erro total
14.670–16.410 e máximo 10 por canal. OpenGL e demais cruzamentos ainda
exigem qualificação própria.
