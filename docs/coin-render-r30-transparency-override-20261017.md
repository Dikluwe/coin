# R30 — override BLEND por superimposition

O adaptador agora lê a política de transparência configurada na
`Superimposition` e a associa ao separador de sua camada. A action aplica
essa política em um escopo de estado durante a captura e restaura o estado
ao sair. A chave do root composto inclui o tipo, para que mudar o override
invalide o plano anterior.

O controle fixa o manager em `SCREEN_DOOR` e configura só a camada frontal
semitransparente como `BLEND`. A imagem CoinRender é idêntica à obtida quando
o manager inteiro usa `BLEND` herdado; contra CoinGL, o erro máximo do frame
é de **um nível RGB por canal** nas sete células GPU Linux. Ao remover e
recriar a camada sem override, ela herda `SCREEN_DOOR` e produz imagem
distinta, provando que o valor local foi aplicado. [Logs e hashes](validation/coin-render-r30-transparency-override-20261017/verification.json).

Esse gate qualifica override `BLEND` em uma camada frontal sem depth. Outros
tipos de override, múltiplas políticas no mesmo frame e cruzamentos com
depth/clear, background, janela e plataformas externas ainda requerem
validação própria.
