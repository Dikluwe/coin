# R30 — CLEARZBUFFER vazio com viewport do manager

O controle de `CLEARZBUFFER` vazio foi ampliado para o viewport
`(16,16,32,32)` em alvo 64×64. A segunda camada, atrás da cena principal,
aparece no centro após o clear; os pixels RGB fora da subregião continuam
pretos. Ao restaurar o viewport 64×64 no frame seguinte, a imagem RGBA
volta exatamente à captura anterior de viewport inteiro. O gate passou em
sete células GPU Linux. [Logs e hashes](validation/coin-render-r30-empty-clear-viewport-20261017/verification.json).

Esse teste cobre a mudança de viewport **entre frames**, que é a operação
oferecida pelo manager. As camadas de um frame compartilham a action e seu
viewport; a captura CoinGL de `SoOffscreenRenderer` força o viewport do alvo
inteiro, portanto não foi usada como oráculo da subregião. O controle de
paridade CoinGL para o frame inteiro permanece no
[teste anterior](coin-render-r30-empty-clear-20261017.md).
