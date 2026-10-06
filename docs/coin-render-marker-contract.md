# CoinRender — MarkerSet e IndexedMarkerSet

## Captura comum

Os tipos nativos exatos `SoMarkerSet` e `SoIndexedMarkerSet` têm captura de
bitmap na camada comum. O desenho deixa de depender das primitivas de ponto
herdadas de `SoPointSet`/`SoIndexedPointSet`. BGFX e wgpu recebem o mesmo plano
de triângulos em coordenadas de tela; os executores e a ABI C wgpu não mudam.

O Coin fornece dimensões, alinhamento e bytes do registro de marcadores por
uma ponte privada, compilada somente com `COIN_BUILD_RENDER`. A captura copia
os bytes antes de formar a geometria. A API e o layout das classes públicas
permanecem iguais. CoinRender precisa da Coin produzida pelo mesmo build.

Somente runs de bits acesos produzem cobertura. Bits apagados e `NONE` não
escrevem cor nem profundidade. A posição do marcador é projetada pela matriz
modelo/view/projeção; seu tamanho permanece em pixels. O deslocamento do centro
usa a divisão inteira `(dimensão - 1) / 2` do CoinGL, seguida do arredondamento
do raster de bitmap. O perfil qualificado aplica `floor` depois da quantização
da posição raster em passos de 1/2.048 pixel, observada com
`GL_CURRENT_RASTER_POSITION` no driver NVIDIA desta campanha. Isso conserva
frações de pixel e evita deslocamentos em âncoras que a projeção float deixa
ligeiramente abaixo de um inteiro. Não é arredondamento ao inteiro mais próximo.
A precisão raster permitida pelo GL depende da implementação; outros drivers
precisam de qualificação desse perfil. Os bitmaps internos têm armazenamento 9 × 9, incluindo
margens vazias dos desenhos denominados 5 × 5 e 7 × 7.

`SoMarkerSet` usa `startIndex`/`numPoints`; uma lista curta de `markerIndex`
repete o último valor. Seus bindings de material diferentes de `OVERALL`
usam a ordem das ocorrências, começando no material zero, mesmo quando
`startIndex` não é zero. `SoIndexedMarkerSet` usa `coordIndex`, bindings por
ocorrência ou `materialIndex`; o índice de coordenada substitui índices de
material não especificados no binding indexado. `vertexProperty` é aplicado
em um escopo temporário.

A captura ocorre depois dos callbacks anteriores do usuário e conserva os
callbacks posteriores. Subclasses conservam sua callback virtual. Frames com
marcadores são recapturados: mudar um registro global com `addMarker` ou
`removeMarker` não notifica a árvore, portanto a identidade dos nós não prova
que o bitmap continua igual.

## Compatibilidade e limites

O CoinGL desliga iluminação e texturas para estes nós. A captura faz o mesmo,
preservando cor/alpha, teste de alpha, depth test/write e fog do raster. O
`SCREEN_DOOR` não aplica stipple aos pixels de bitmap e usa alpha primário um.
Desligar texturas não limpa a classificação transparente das imagens no
CoinGL. Ambos os nós conservam essa classificação para blend e travessia
diferida. A captura examina o alpha das imagens herdadas, inclusive unidades
desabilitadas, sem transportar essas imagens como texturas dos marcadores.
RGBA/LA inteiramente opacos permanecem opacos; alpha zero também classifica
a imagem como transparente. Políticas explícitas de `SoSceneTexture2`
precedem a inspeção dos pixels, como os flags FORCE do CoinGL.
Planos de corte e o volume de visão testam a âncora 3D; o raster position testa
também a origem inferior esquerda do bitmap. Assim, a limitação nativa que
faz o marcador desaparecer na borda esquerda/inferior é conservada.

O GL legado de `SoIndexedMarkerSet` infere o alinhamento pelo ID do marcador.
Se uma substituição ou remoção torna essa inferência incompatível com o stride
real do registro, CoinRender retorna `UNSUPPORTED` antes de publicar o frame.
Esse caso tem um FIXME no CoinGL e pode fazê-lo ler além dos bytes registrados.
`SoMarkerSet` usa o alinhamento real e admite substituir marcadores internos.

Uma lista curta de `markerIndex` em `SoIndexedMarkerSet` repete o último valor,
como o Coin compilado com `COIN_DEBUG`. O GL legado em Release acessa a lista
sem esse limite; esse perfil não é comparado com o oráculo Release.
IDs positivos sem bitmap registrado são inertes na captura. Isso coincide
com a checagem do GL de `SoIndexedMarkerSet`; o GL Release de `SoMarkerSet`
acessa o registro sem checar esse limite. A política segura para IDs ausentes
não é uma certificação daquele acesso indefinido.

O perfil também recusa raster dentro de `SoShadowGroup` ativo e raster
transparente interceptado pela ordenação de triângulos, seguindo o contrato
de texto/imagem. `vertexProperty` customizado e volumes de visão degenerados
exigem um contrato adicional e são recusados no perfil raster.
`SoComplexity::BOUNDING_BOX` usa agora a
[captura comum de caixas](coin-render-bounding-box-contract.md), preservando
as diferenças de estado entre MarkerSet e IndexedMarkerSet antes do raster.
Entradas recusadas conservam o último frame válido.

Os limites por nó são 1.048.576 ocorrências de coordenadas, 4.096 bitmaps
distintos, 16 MiB de bytes de bitmap copiados e 65.536 runs de cobertura ou
variantes de material. Coordenadas, índices e atributos lidos pela captura
precisam ser válidos. Quando há observadores de primitivas externos, são
validados também os índices que a geração de pontos herdada lerá, inclusive
ocorrências `NONE` que essa geração não interpreta como marcadores vazios.
A classificação de imagens herdadas admite até oito unidades e 128 MiB de
bytes de imagens com alpha por captura de nó. Uma SceneTexture inativa com
política que força transparência é recusada: sua classificação nativa pode
depender da existência de uma imagem GL retida de uma aplicação anterior.

## Validação

O gate `CoinRenderMarkerSetTest` separa `--capture` e `--gpu`. O modo GPU usa
Coin/OpenGL como referência e controles sem marcadores para evitar comparações
vazias. A campanha de qualificação e seus limites ficam registrados em
[validation/marker-linux/summary.json](validation/marker-linux/summary.json).

Resultado em Linux/NVIDIA: **363 comparações GPU** (121 em cada executor)
passaram com RGB idêntico ao Coin/OpenGL. As 363 comparações CPU associadas
tiveram erro máximo de 1/255 e MAE máxima de 0,333333 por canal na região
avaliada. Os gates incluem controles vazios deliberados e identificam
separadamente a extensão segura da lista Indexed curta.

Também passaram duas execuções `--capture`, 49 gates CTest de regressão e
609 comparações GPU existentes de texto/imagem e fragmentos. Não houve skips
na campanha final. A ABI C wgpu continua na revisão 44 e o snapshot de estado
continua com 1.648 bytes neste build.

O objeto `SoMarkerSet` foi compilado com `COIN_BUILD_RENDER=OFF` e não exporta
a ponte; as duas bibliotecas Coin com render habilitado exportam o serviço.
Isso verifica esse objeto e a configuração OFF, não uma compilação completa
da biblioteca OFF. A campanha não qualifica Windows, outras GPUs, alpha de
framebuffer ou todos os consumidores FreeCAD. Somente os tipos nativos exatos
recebem bitmaps; preservar a callback de uma subclasse não certifica sua
semântica GL customizada.
