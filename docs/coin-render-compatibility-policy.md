# Compatibilidade com CoinGL e tratamento de bugs

Diretriz estabelecida em 2026-10-06: CoinGL é a referência de compatibilidade
para CoinRender. Bugs confirmados, acessos inválidos e comportamento indefinido
não devem ser reproduzidos para obter igualdade com uma imagem ou execução
legada. O objetivo é preservar a semântica válida da cena nos executores CPU,
BGFX e wgpu.

## Classificação de uma diferença

- **Comportamento válido do Coin:** preservar e comparar com CoinGL nas cenas
  e plataformas qualificadas. Uma escolha visual inesperada não basta para
  classificá-la como bug.
- **Bug confirmado ou comportamento indefinido:** definir um resultado correto
  e seguro, registrar a divergência deliberada e testar esse contrato. O
  resultado defeituoso do CoinGL não serve como resultado esperado.
- **Diferença ainda sem diagnóstico:** manter a investigação aberta, com cena
  mínima e evidência. Não declarar paridade nem uma exceção por bug sem base.
- **Recurso ainda sem suporte:** registrar a lacuna e o diagnóstico existente;
  a ausência de suporte não é uma correção de bug do CoinGL.

## Evidência e testes

Uma exceção precisa identificar a cena mínima, a revisão do Coin e o ambiente,
a causa demonstrada no código ou na execução e a semântica esperada. Quando
aplicável, usar documentação do Coin, a especificação relevante e um cálculo
ou teste independente para sustentar o resultado. Diferença entre drivers,
precisão de raster ou tolerância numérica exige diagnóstico próprio.

O teste de regressão deve detectar o defeito e verificar o resultado correto,
incluindo restauração de estado quando pertinente. Para uma entrada inválida,
o contrato pode ser rejeição explícita com preservação do último frame válido,
ou um tratamento seguro definido. Não basta aumentar a tolerância, omitir
pixels discrepantes ou remover a comparação que revelou o problema.

As comparações CoinGL continuam cobrindo o domínio válido. A evidência deve
separar paridade comprovada, divergência deliberada por bug, diferenças em
investigação e lacunas de suporte. Uma exceção conhecida não certifica o restante
do recurso. Desempenho e compatibilidade de ABI têm qualificações separadas.

## Exemplo já tratado

O [contrato de marcadores](coin-render-marker-contract.md#compatibilidade-e-limites)
documenta acessos sem limite no CoinGL Release para listas curtas ou IDs sem
bitmap. CoinRender aplica o tratamento seguro descrito nesse contrato, sem
usar o acesso indefinido como referência. Esses casos são distinguidos das
comparações de bitmaps válidos.
