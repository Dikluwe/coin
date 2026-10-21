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

## Entrega determinística e resultado nativo por máquina

Decisão de 18/10/2026 após a investigação B22: a garantia do caminho nativo é
preparar e entregar corretamente à GPU a semântica da cena, com estado,
geometria, atributos, uniforms, formatos e ordem de operações definidos.
A mesma entrada e configuração devem produzir uma entrega equivalente,
independente de estado residual ou do histórico de cache não previsto no
contrato. Identificadores de recursos e endereços não precisam coincidir.

Igualdade de pixels entre GPUs, drivers ou APIs não é critério geral de
aceite do caminho nativo. Pequenas diferenças numéricas de rasterização,
interpolação e precisão, caracterizadas com entradas corretas e sem evidência
de violação da API, são aceitas como resultado nativo de cada ambiente.
Não alterar a geometria, os comparadores ou a precisão do produto apenas
para igualar tais resultados entre máquinas. Perfis canônicos explicitamente
solicitados conservam seu contrato próprio, sem impor esse custo ao caminho
nativo por uma exigência geral de igualdade entre GPUs.

O tamanho visual da diferença, sozinho, não determina a classificação.
Entradas erradas, estados ausentes, acessos inválidos, uso indefinido da API,
falhas de publicação ou perda de coerência geométrica continuam sendo bugs.
Casos ainda sem diagnóstico precisam da investigação de vizinhança descrita
abaixo antes de receber a classificação de variação numérica aceita.

B22 fica encerrado como variação numérica nativa aceita no domínio 32F
investigado, sem correção do renderizador. As diferenças estritas registradas
permanecem fatos diagnósticos; não são convertidas em imagens idênticas ou
passes históricos. Elas deixam de ser bloqueio de aceite por paridade entre
máquinas nesse caso. Outras divergências não recebem essa classificação
automaticamente. Ver [investigação B22](coin-render-b22-native-interpolation-20261018.md).

No modo preciso de `SoQuadMesh`, o centro geométrico é a média dos quatro
vértices, mas os atributos usam pesos legados quantizados pelo expoente
binário das razões de distâncias quadradas; builds sem `ilogb`/`_logb` usam
o expoente fixo zero do fallback legado. Para razão um, os pesos são
aproximadamente `(0,23205; 0,23205; 0,26795; 0,26795)`, em vez de quatro
quartos. O GL original e a captura portátil preservam essa regra. Um oráculo
de textura/LOD deve considerar o triângulo do fan e suas derivadas; assumir
interpolação afim nesse centro produz uma referência incorreta. Testemunhos
numéricos devem evitar a aresta entre triângulos, onde as derivadas podem
diferir. Isso não classifica outras divergências como variação aceita.

## Comportamento não documentado e investigação de vizinhança

Diretriz de 18/10/2026: quando a documentação não determinar o resultado,
formular primeiro uma expectativa plausível para o uso em CAD, compatível
com a geometria, a matemática e as regras definidas da API. Por exemplo,
oclusão deve respeitar a profundidade e uma superfície contínua não deve
adquirir furos sem uma causa geométrica ou de cobertura. Essa expectativa é
uma hipótese explícita de contrato; não prova a intenção histórica do Coin.

Um resultado estranho em uma borda ou limiar deve ser investigado junto com
sua vizinhança antes de ser classificado como requisito ou bug:

1. Comparar pixels, primitivas e amostras adjacentes, incluindo interior,
   contorno e lados do limiar. Variar uma entrada por vez, com pequenas
   translações de câmera/geometria, profundidade, UV, alpha ou resolução,
   registrando os valores efetivos e o intervalo em que o efeito aparece.
2. Verificar se a transição é esperada. Silhuetas rasterizadas, quantização,
   alpha-test e comparadores exatos podem mudar de forma descontínua;
   suavidade visual não substitui suas regras. Uma pequena variação é um
   instrumento de diagnóstico, não um epsilon aplicado à implementação.
3. Fazer alterações pequenas e controladas no software para testar a causa:
   desativar o cache suspeito, reconstruir o estado em lugar de reutilizá-lo,
   explicitar um binding herdado ou substituir uma operação por uma forma
   equivalente. Quando a alteração preservar a semântica, exigir resultado
   equivalente; quando mudar deliberadamente a hipótese investigada,
   explicitar antes qual efeito ela deve produzir.
4. Executar original e variante sobre as mesmas entradas e vizinhança,
   preservando um controle não afetado. Verificar persistência, desaparecimento
   ou deslocamento da anomalia e restauração após retirar a alteração.
   Persistir não prova correção; desaparecer não prova, sozinho, a causa.
5. Sustentar a classificação pela combinação de contrato plausível, regra
   gráfica e evidência causal. Diferenças legítimas ficam delimitadas; bugs
   confirmados recebem correção e regressão que cubra o caso e sua vizinhança.
   Se a evidência não distinguir as hipóteses, manter a causa não discriminada.

Não transformar um pixel legado isolado em contrato, nem deslocar a entrada,
a referência ou a tolerância somente para fazer a comparação passar. As
alterações de diagnóstico são descartáveis; somente a correção justificada e
os testes relevantes permanecem no produto.

## Classificação de uma diferença

- **Comportamento válido do Coin:** preservar e comparar com CoinGL nas cenas
  e plataformas qualificadas. Uma escolha visual inesperada não basta para
  classificá-la como bug.
- **Bug confirmado ou comportamento indefinido:** definir um resultado correto
  e seguro, registrar a divergência deliberada e testar esse contrato. O
  resultado defeituoso do CoinGL não serve como resultado esperado.
- **Variação numérica nativa aceita:** entradas e estado corretos, diferença
  caracterizada e sem evidência de violação do contrato; aceitar o resultado
  do ambiente e manter comparações entre máquinas como diagnóstico.
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
