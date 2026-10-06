# P07: primeiro perfil de UV default e funções de coordenadas

Implementação de 2026-10-06 em `codex/coin-render`, sobre `601ccdf2e5`.
Esta entrega fecha um perfil de captura de coordenadas; P07 continua aberto
para qualidade/filtros/formatos gerais, produtores RTT e qualificação ampliada.

## Contrato comum

A cena entrega coordenadas por uma destas entradas:

- **DEFAULT:** o gerador nativo do shape determina o mapa. Faces indexadas
  usam o default do Coin; Cube/Cone/Cylinder/Sphere conservam suas UV canônicas.
- **FUNCTION:** a função Coin recebe posição e normal da primitiva em espaço
  do objeto, com o estado Coin vigente. A captura copia imediatamente ST/R/Q.
- **EXPLICIT:** permanece no contrato existente de arrays e índices.

Matrizes de textura continuam separadas das coordenadas e são aplicadas nos
executores. Q é preservado e dividido por fragmento no contrato projetivo.
Nenhum ponteiro de função ou estado Coin atravessa a fronteira GPU; não houve
mudança dos layouts de FramePlan, vértices ou FFI Rust, então na revisão 45. A rodada posterior
[P02/P04/P05/P06](coin-render-geometry-viewport-contract.md) usa revisão 46
para a codificação de viewport vazio, sem alterar os layouts.

Wiring lê o estado e executa os callbacks; o Core indexado decide apenas sobre
os dados explícitos que recebe. DEFAULT/FUNCTION e arrays explícitos vazios
encaminham faces indexadas ao gerador nativo. BGFX/wgpu recebem os mesmos
snapshots e reutilizam seus shaders/recursos atuais.

Para os quatro tipos nativos exatos Cube/Cone/Cylinder/Sphere, o gerador de
primitivas publica UV canônicas sem consultar a função. A captura aplica a
FUNCTION autoral nesses tipos. Subclasses conservam sua geração virtual;
subclasses de IndexedFaceSet foram qualificadas com fast path ligado/desligado.
Não há certificação genérica de subclasses que só desenham por GLRender.

## Multitextura e correção do bundle Coin

Até oito unidades mantêm coordenadas e matrizes independentes. Funções nas
unidades adicionais são amostradas pela captura. DEFAULT adicional de um
shape canônico conserva suas UV; shapes gerais instalam o default temporário
Coin durante a geração. Dados explícitos adicionais continuam exigindo detalhes
válidos de índice conforme o contrato P08.

A unidade principal do callback é a primeira unidade habilitada. O bundle
nativo juntava flags de todos os modos: uma função adicional podia fazer uma
unidade principal explícita ser tratada como função inexistente; um default
adicional podia substituir a função autoral principal pelo mapa de bbox.

`SoTextureCoordinateBundle` agora escolhe o modo e o getter da unidade
principal para callbacks/picking. O caminho de envio GL conserva sua decisão
existente. A correção não muda headers, layout ou símbolos públicos do Coin.
O teste verifica de forma independente explícita+função, função+default,
default+função, default+explícita, explícita+default e uma unidade principal acima
de zero. A seleção usa o estado de habilitação Coin, inclusive quando a unidade
principal não tem uma imagem efetiva; não usa a presença do payload como atalho.

Funções adicionais podem ser chamadas para cada amostra de primitiva emitida;
não se promete o mesmo número de chamadas dos loops GL. Uma função deve
fornecer coordenadas válidas para posição/normal e o estado corrente. O callback
principal já avaliado pelo gerador nativo não é chamado novamente pela captura;
o gate compara sua contagem com uma SoCallbackAction testemunha.

## Reuso, erro e recuperação

Frames que capturam FUNCTION exigem nova travessia em cada `apply`: a função
pode consultar estado ou dados externos sem notificar o grafo. Defaults que o
gerador representa como função também usam essa política conservadora.
Isso não proíbe reuso de recursos/planos idênticos depois da nova captura.
Não se declara ganho de desempenho neste incremento.

Coordenadas não finitas são rejeitadas pelo validador comum antes da submissão,
preservando o frame anterior. Qualidade fora do perfil continua UNSUPPORTED;
qualidade zero desliga a imagem, e uma entrada válida posterior volta a renderizar.
TEXGEN sem contrato de callback CPU é rejeitado antes da geração.

## Capacidades e limites

A máscara legada de capacidades acrescenta
`COIN_RENDER_FEATURE_PROCEDURAL_TEXTURE_COORDINATES` (bit 11). É um fato de
implementação deste perfil, não uma qualificação de todos os shapes/devices.
Versão e tamanhos V1/V2/V3 permanecem iguais; clientes antigos podem ignorar o
bit novo. Probes físicos continuam independentes dessa máscara.

Qualidade/filtros continuam limitados ao perfil atual (off/linear), texturas 2D
L/LA/RGB/RGBA, wraps REPEAT/CLAMP e modelos legados já implementados.
`SoComplexity::BOUNDING_BOX` com FUNCTION/texgen ou unidades adicionais mantém
os limites de seu contrato. Texturas 3D/cubo, shaders próprios e MSAA não são
certificados por esta capacidade. Os nós de geração Environment/Cube/Cylinder/
Sphere ainda pedem campanhas específicas de equivalência com seus caminhos GL;
o gate inicial qualifica Plane e uma função autoral homogênea.

A matriz herdada por produtores FBO/pbuffer RTT continua aberta. Não foi
alterada para obter igualdade neste incremento.

## Gate e evidência

`CoinRenderProceduralTextureTest` executa **71 cenários por processo**, com
expectativas analíticas de coordenadas default/plane/autoral. Nas execuções GPU,
**29 referências CoinGL** são obrigatórias, comparadas com CPU e executor.

Cenas qualificadas: faces indexadas, sua subclasse, Cube/Sphere/Cone/Cylinder,
LineSet e PointSet; UV canônicas/default, Plane e função ST/R/Q; modos mistos,
unidades esparsas e oito funções, matriz projetiva, NaN sem publicação,
recuperação, mudança de dados externos sem notificação, qualidade
rejeitada/desativada/reativada e consulta de capacidades.

Linhas/pontos são comparados CPU/GPU em amostras cobertas; os oráculos GL deste
gate cobrem superfícies preenchidas. Comparações de pixels usam um interior
não vazio para separar o contrato de UV das bordas de raster já conhecidas;
a validação analítica dos snapshots cobre os vértices das superfícies preenchidas.
Matrizes gerais de raster, bindings e todas as combinações de nós continuam abertas.

Os logs e comandos finais ficam em [validation/p07-procedural-20261006](validation/p07-procedural-20261006).

## Resultado da integração

Builds Release completos: **249 BGFX e 187 wgpu aprovados**, zero falhas,
com dois skips por suíte (`BumpProgramGLX` e `CoinRenderCameraReuseReferenceTest`).
`CoinTests`, incluído nessas campanhas, passou **418 casos e 309.076 checks**.

Perfil físico executado: NVIDIA, CoinGL com pbuffer GLX, BGFX/Vulkan/OpenGL e
wgpu/Vulkan. Sombras GPU e referência GL foram exigidas nos perfis habilitados;
o oráculo específico de oito mapas permanece fora desta campanha. Windows,
Intel física, macOS e Android não foram reexecutados.

A primeira campanha BGFX encontrou seis expectativas antigas de rejeição em
`CoinBgfxSurfaceFeaturesTest`. Elas foram substituídas por comparações CPU/GPU/GL
com uma textura variável na unidade 7 e a unidade 0 desabilitada. O esperado
visual e as tolerâncias existentes foram preservados. A regressão da unidade
habilitada sem imagem falhou antes da correção e passou na revisão final.

Comandos/ambiente, hashes dos fontes, resultados e limites estão no
[manifesto](validation/p07-procedural-20261006/summary.json). Os tempos CTest
não representam uma campanha de desempenho; as suítes GPU rodaram concorrentes.
