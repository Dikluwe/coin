# R30/C19 — superimposition frontal sem depth

O adaptador agora compõe a cena principal e camadas frontais ativas em um
mesmo plano de frame quando o manager está em `AS_IS`, a camada herda a
transparência do manager e não pede `BACKGROUND`, `ZBUFFERON` ou
`CLEARZBUFFER`. Um `SoSeparator` por camada isola o estado; `SoDepthBuffer`
desliga teste/escrita para colocá-la na frente. O root composto é reutilizado
quando a cena e a lista de camadas não mudam. Camadas desativadas são
ignoradas; flags ainda não implementadas continuam `UNSUPPORTED` antes
da submissão.

O teste de produto usa um quadrilátero verde sobre a esfera texturizada,
verifica o centro coberto, frame idêntico em repetição, retorno exato à
cena principal ao desativar a camada e retorno exato da camada ao reativá-la.
Nos sete perfis Linux BGFX/wgpu disponíveis, a máscara CoinGL da camada
coincidiu em **144/144 pixels**, com erro RGB **zero** na camada; fora dela,
CoinRender conservou a imagem base sem alterar um canal. [Logs e
hashes](validation/coin-render-r30-foreground-20261017/verification.json).

A [correção do callback offscreen](coin-render-r30-offscreen-fix-20261017.md)
eliminou a diferença de fundo desta captura: o frame inteiro agora tem erro
RGB zero e ambos os caminhos preservam todos os canais fora da camada. Os
624 canais e o erro 159.120 no recibo antigo são histórico do oráculo que
limpava cor/depth entre passagens. [Duas camadas frontais](coin-render-r30-multilayer-20261017.md)
foram qualificadas depois. `ZBUFFERON` também foi qualificado; faltam
transparência por camada, `CLEARZBUFFER`, background, modos técnicos,
janela/resize e outras plataformas. R30/C19 permanecem parciais.
