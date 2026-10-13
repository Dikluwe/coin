# Compatibilidade com CoinGL e tratamento de bugs

Diretriz estabelecida em 2026-10-06 e refinada em 2026-10-12: CoinGL é a
referência de compatibilidade para CoinRender, mas não define sozinho o
resultado correto. Bugs confirmados, acessos inválidos e comportamento
indefinido não devem ser reproduzidos para obter igualdade com uma imagem ou
execução legada. O objetivo é preservar a semântica válida da cena nos
executores BGFX e wgpu; o backend CPU é auxiliar de testes.

## Ordem para definir o resultado esperado

1. Descrever a entrada e a semântica documentada dos nós e estados Coin. Fixar
   unidades, espaços de coordenadas, projeção, ordem de composição, formatos,
   espaço de cor e precisão relevantes ao caso.
2. Aplicar a matemática do modelo visual escolhido e os contratos das APIs
   gráficas. Uma iluminação aproximada herdada do Coin não se torna fisicamente
   baseada por trocar de backend; uma mudança de modelo exige contrato e modo
   próprios. Nos casos em que a especificação permite mais de um resultado,
   declarar a política portátil antes de comparar pixels.
3. Calcular uma expectativa independente para a propriedade testada, como
   transformações, cobertura, profundidade, interpolação, texels, mistura ou
   contribuição de luz. Esse controle deve poder detectar um erro compartilhado
   por CoinGL e CoinRender.
4. Comparar CoinGL, BGFX e wgpu no mesmo ambiente e classificar cada diferença.
   CoinGL confirma compatibilidade histórica no domínio válido; não substitui
   os três passos anteriores.

Conversões exigidas pela API ou pelo driver, como origem da textura, intervalo
de profundidade e apresentação em janela, pertencem ao executor. O resultado
observável deve continuar obedecendo ao contrato comum. Quando uma regra
portátil deliberada difere de CoinGL, registrar ambos os resultados e a razão;
não chamar a divergência de paridade visual exata.

## Classificação de uma diferença

- **Comportamento válido do Coin:** preservar e comparar com CoinGL nas cenas
  e plataformas qualificadas. Uma escolha visual inesperada não basta para
  classificá-la como bug.
- **Bug confirmado ou comportamento indefinido:** definir um resultado correto
  e seguro, registrar a divergência deliberada e testar esse contrato. O
  resultado defeituoso do CoinGL não serve como resultado esperado.
- **Diferença ainda sem diagnóstico:** manter a investigação aberta, com cena
  mínima e evidência. Não declarar paridade nem uma exceção por bug sem base.
- **Adaptação de API ou driver:** identificar a transformação necessária,
  verificar o resultado observável e não transportar a convenção da API para
  a semântica da cena.
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
