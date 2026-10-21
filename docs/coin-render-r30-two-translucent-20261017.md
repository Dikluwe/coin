# R30 — duas camadas frontais BLEND

O controle acrescenta uma segunda superimposition verde com transparência
0,5 após a camada azul também com transparência 0,5, ambas herdando `BLEND`
do manager. A cena opaca de base é vermelha. O centro muda de roxo para
aproximadamente `(64,128,64)`, provando a ordem de composição.

Nas sete células GPU Linux, a imagem RGB inteira diferiu do CoinGL em no
máximo **um nível por canal**. A soma dos erros foi 36 na AMD e 72 na NVIDIA,
nos dois executores disponíveis. [Logs e hashes](validation/coin-render-r30-two-translucent-20261017/verification.json).

O perfil qualifica duas camadas frontais `BLEND` sem depth, com a mesma
política herdada. Continuam abertos overrides de transparência por camada,
cruzamentos com depth/clear, outros algoritmos de transparência, janela e
plataformas externas.
