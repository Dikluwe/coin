# R30/C19 — admissão de índice de cor e superimpositions

O adaptador agora verifica `isRGBMode()` e a presença de superimpositions
ativas antes de submeter um frame. Modo de índice de cor e camada ativa
retornam `UNSUPPORTED` com diagnóstico e preservam a última imagem. Camada
desativada não bloqueia renderização normal; após removê-la, o manager
continua utilizável. O núcleo expõe `Superimposition::isEnabled()` e
`SoRenderManager::hasEnabledSuperimpositions()` para que a ponte consiga
distinguir camada ativa de inativa sem depender do estado GL.

`CoinRenderProductTest` cobre rejeição, preservação da imagem publicada,
desativação e recuperação. Passou nos dois builds locais
e nas sete células GPU Linux disponíveis:
[logs e hashes](validation/coin-render-r30-manager-admission-20261017/verification.json).

A composição de superimpositions, índice de cor real, flags de depth,
transparência por camada e cruzamentos com estéreo continuam sem
implementação. Esta admissão evita uma imagem incompleta silenciosa; R30/C19
permanecem abertos.

Continuação: [superimposition frontal sem depth](coin-render-r30-foreground-20261017.md)
agora é renderizada; a recusa descrita acima aplica-se às outras flags e
ao modo de índice de cor.
