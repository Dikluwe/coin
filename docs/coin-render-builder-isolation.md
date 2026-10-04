# Captura Coin e montagem mecânica do plano

Terceira das quatro fases de organização em `codex/coin-render-isolation`.
Parte de `15c21e097a`; a quarta extrai cálculos reutilizáveis do lowering BGFX.

`CoinRenderFramePlanBuilder` permanece Wiring: lê action/estado, elementos,
node types, detalhes das primitivas e coordenadas Coin; controla as ocorrências,
callbacks, contornos originais, layers, erros e publicação do plano. A validação
de continuidade dos detalhes de callbacks permanece com essa captura.

`CoinRenderPlanAssemblyCore` recebe snapshots, matrizes, vetores e arrays:
normaliza câmera/estado, transforma luzes, projeta coordenadas homogêneas,
deduplica material/luz/câmera/viewport/textura/sampler/estado e monta os intervalos
das geometrias indexadas e polígonos resolvidos. Não consulta action/estado/nodes.
As comparações e tolerâncias anteriores foram preservadas, incluindo normalização
de zero no hash da matriz e a comparação conservadora dos atributos.

`CoinRenderCubeGeometryCore` aprende a geometria nativa capturada (24 vértices,
36 índices), valida os índices locais e mantém o cache limitado a 32 materiais.
O builder continua decidindo elegibilidade, dimensões e normal binding a partir
do Coin. O Core compartilha intervalos imutáveis ou materializa um novo intervalo,
sem modificar as ocorrências anteriores. O preenchimento de contornos e a captura
por callbacks continuam usando as convenções Coin.

`CoinRenderImageCore` passa a expandir L/LA/RGB/RGBA para RGBA8. A escolha e leitura
da imagem Coin, política de qualidade, wrapping, texgen e resolução dos produtores
RTT continuam na captura. Entradas inválidas na conversão deixam a saída intacta.

## Validação

26 casos selecionados aprovados: seis no RECORDING, cinco comuns por build BGFX/wgpu
e cinco testes GPU por backend (texturas, draw styles, composição, sombras e depth).
O teste novo de montagem funciona sem inicializar SoDB, executar travessia ou criar
GPU. Verifica snapshots distintos, colisão de digest de imagem, dimensões/binding
incompatíveis, cache limitado, índices compartilhados e falha sem modificar saída.
As quatro variantes da cena de 40 mil edifícios preservaram os checksums anteriores.

Foi corrigida uma expectativa antiga do teste de texturas RECORDING: a referência
CPU já implementava SCREEN_DOOR, mas o teste esperava UNSUPPORTED. A mesma falha
foi confirmada recompilando o builder e ImageCore do commit anterior; o comportamento
de produção não mudou. [Evidências e scripts](validation/render-assembly-linux/).
Não se declara ganho de desempenho; Windows/protótipos não foram executados.
