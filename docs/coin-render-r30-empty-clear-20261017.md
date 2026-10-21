# R30 — CLEARZBUFFER em camada vazia

Quando uma camada `CLEARZBUFFER` não emite draws, o builder agora conserva a
limpeza pendente e a aplica ao primeiro draw da camada seguinte. Sem draw
posterior, não há pixel do frame que observe o depth limpo. A desativação da
camada remove a limpeza; o frame volta a respeitar o depth da cena principal.

O controle usa cena vermelha em `z=0`, uma superimposition vazia com
`ZBUFFERON|CLEARZBUFFER` e uma segunda camada verde em `z=-0.5` com
`ZBUFFERON`. Com a camada vazia ativa, a verde aparece; ao desativá-la, a
vermelha a oculta. A imagem inteira com o clear ativo é RGB idêntica à
captura CoinGL nas sete células GPU Linux. `CoinRenderCompositionTest` também
passou. [Logs e hashes](validation/coin-render-r30-empty-clear-20261017/verification.json).

O perfil foi exercitado com o mesmo viewport nas duas camadas. A
[continuação com subviewport](coin-render-r30-empty-clear-viewport-20261017.md)
qualificou a mudança da região do manager entre frames. Uma alteração de
viewport dentro da sequência de camadas do mesmo frame exigiria contrato
próprio, pois `clearDepthBefore` usa o viewport do draw que consome a limpeza.
