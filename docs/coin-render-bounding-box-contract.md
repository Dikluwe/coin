# CoinRender: Complexity BOUNDING_BOX

Implementação na branch `codex/coin-render-transform-performance`, base
`bebbd1852c`, tomando o caminho Coin/OpenGL como referência.

## Captura comum

O adaptador calcula `SoShape.computeBBox` no estado efetivo da ocorrência e
captura seis quads da caixa local. O centro de posicionamento é o meio de
min/max; o centro ponderado de `computeBBox` continua servindo à ordenação
transparente. A transformação do objeto permanece no estado do desenho.

FILLED usa 24 vértices e 36 índices, ou 12 triângulos, por ocorrência. As faces
têm normais e UV canônicas, material zero, CCW, sem culling e iluminação
unilateral, como `GLRenderBoundingBox`. Bindings e normais originais não
substituem esses atributos.

BGFX, wgpu e o raster CPU consomem os planos comuns existentes. Shaders,
protocolo FFI e layouts de transporte permanecem iguais; a ABI privada wgpu
continua 45. OBJECT_SPACE e SCREEN_SPACE conservam a geração normal.
Separators, overrides e campos ignorados são interpretados pelos elementos Coin.
Os fast paths não substituem caixas nem antecipam callbacks registrados
posteriormente pelo usuário.

## Estilos e transparência

FILLED, LINES, POINTS e INVISIBLE mantêm sua semântica. Linhas e pontos partem
dos seis contornos. Arestas compartilhadas e
cantos repetidos conservam suas ocorrências por face. Caixas degeneradas
finitas também produzem linhas/pontos; somente a captura de caixas solicita
essa preservação ao Core de estilos.

No perfil NVIDIA qualificado, POINTS acompanha a divisão interna dos quads
em dois triângulos, conservando flags de arestas externas. Uma aresta criada
por clipping pode acrescentar um ponto na diagonal interna cortada. O gate
transparente corta exatamente dois cantos e confirma 100 pixels nativos,
incluindo esse ponto, com restauração A/B/A. LINES conserva os contornos
sem desenhar diagonais. As regras de flags e clipping estão nas seções
10.1.17, 13.7 e 14.6.4 da
[especificação OpenGL de compatibilidade](https://registry.khronos.org/OpenGL/specs/gl/glspec46.compatibility.pdf).
A escolha da diagonal foi qualificada contra este driver; não é uma garantia
geral para todos os drivers OpenGL.

A caixa captura diffuse RGBA com a precisão de `glColor4ub` usada pelo
`SoMaterialBundle.sendFirst`, antes da iluminação ou composição transparente.
Ambient, specular e emission mantêm sua precisão original. SCREEN_DOOR conserva
o alpha de classificação/stipple e envia primary alpha opaco, como o nativo.

O CoinGL decide o adiamento transparente antes da caixa. A captura conserva
a classificação do array de materiais e dos images armazenados, mesmo quando
MarkerSet desativa a textura. Usa o helper compartilhado com marcadores,
incluindo políticas RTT. O nativo escolhe a caixa antes de ordenar triângulos:
SORTED_OBJECT_SORTED_TRIANGLE_ADD/BLEND tornam-se SORTED_OBJECT_ADD/BLEND
somente no snapshot da caixa, preservando a ordem de suas faces.

O raster CPU passou a interpolar depth a partir de um vértice de referência.
Isso preserva planos de depth constante e evita que arredondamentos da soma
baricêntrica alterem LEQUAL entre faces coincidentes.

## Estado específico e callbacks

A captura acontece após todos os pre callbacks, respeitando PRUNE e ABORT.
Observers recebem as primitivas originais, com a captura dessas primitivas
suspensa enquanto a caixa permanece no plano. Pre/post callbacks são mantidos.

VertexProperty é aplicado antes da caixa nos vertex shapes que fazem isso
no CoinGL. PointSet lê seus vértices em computeBBox, mas conserva o material
herdado. MarkerSet aplica a propriedade, desativa textura e usa BASE_COLOR;
IndexedMarkerSet conserva iluminação e textura herdadas nesse momento.
Text2 e Image usam sua caixa antes da preparação de raster. Seus métodos
especiais também escolhem a modalidade depois dos callbacks. Imagem vazia e
as saídas nativas anteriores a shouldGLRender continuam sem geometria.

Subclasses com computeBBox compatível são admitidas. Overrides customizados
de GLRender que não seguem shouldGLRender dependem de adaptação explícita.

## Texturas e domínio

Na unidade zero, DEFAULT e EXPLICIT usam as UV próprias da caixa, ignorando
arrays explícitos da shape. A matriz completa é aplicada, com a política
PROJECTIVE ou DIRECT_ST do shader de sombras.

FUNCTION ativo retorna UNSUPPORTED: Plane/Object/Environment podem manter
texgen GL ativo, enquanto funções somente CPU são ignoradas pelo cubo nativo.
Unidades adicionais ativas também são recusadas: o cubo nativo envia somente
UV zero e usa atributos GL persistentes para as demais. MarkerSet pode
ignorar essas texturas quando seu scope nativo as desativa.

Limites existentes de qualidade/filtros, formatos, RTT, alpha e orçamento
continuam aplicáveis. Bounds, centro calculado e extensão não finitos retornam
INVALID_SCENE antes da publicação. Caixas vazias não alocam geometria,
regularizando o cubo nativo sem bounds válidos. Falhas preservam pixels e
serial anteriores; corrigir a cena permite renderizar novamente.
Diffuse RGBA fora de [0,1] também é inválido para o empacotamento da caixa.
Offset com fator de slope não zero e área projetada zero permanece UNSUPPORTED.

## Sombras

Em SHADOWMAP, shouldGLRender retorna antes de BOUNDING_BOX: o desenho principal
usa a caixa e o mapa conserva a shape original. Grupos com Complexity, caixa
herdada ou callbacks customizados têm captura separada do mapa. A fixture
confirma 12 triângulos no desenho principal e 390 no caster Sphere original,
com mutação/restauração do raio.

Texto, imagem e marcadores em caixas dentro de grupos com mapas dependem da
captura original desses rasters para o mapa. Essa combinação permanece
UNSUPPORTED no perfil atual. O gate usa um caster primitivo e um mapa spot
real com intensidade zero/material emissivo, isolando geometria e UV.

## Validação

O [gate independente](../testsuite/coinrender/CoinRenderBoundingBoxTest.cpp)
tem modos --capture e --gpu. O segundo exige GPU real e CoinGL válido. Mede
RGB com MAE da região ativa <=1 e outliers acima de três níveis <=max(4,2%
da região). O alpha geral do framebuffer não é qualificado.

Linux/NVIDIA: wgpu/Vulkan e BGFX/Vulkan/OpenGL passaram 137 comparações nativas
por variante, 411 no total, além de duas execuções de captura. Shapes
nativas/custom, centro falso, transforms, styles, materiais, UV/matriz, VP,
scopes, callbacks, degenerados, texto/imagem, ambos MarkerSet e mapas com
geometria original estão cobertos. Bounds NaN/infinito e overflow de extensão
verificam INVALID_SCENE, preservação de pixels/serial e recuperação. Logs e
regressões ficam em
[validation/bounding-box-linux](validation/bounding-box-linux).

FILLED usa bounds sem ambiguidade de cobertura nos centros de pixels;
LINES/POINTS usam centros para testar endpoints. As tolerâncias mantêm o
padrão anterior. Pilotos preservam a falha de cobertura da fixture e o erro
CPU de faces coplanares corrigido nesta implementação.

Windows e outros drivers precisam executar seus gates. Não foi executado
benchmark de desempenho nesta campanha semântica.
