# CoinRender: contrato e fechamento de SoDrawStyle

P02/F04 continua aberto na matriz completa. INVISIBLE está implementado;
LINES/POINTS agora têm resolução comum e um perfil de contornos convexos
executado em CPU, BGFX e wgpu. Os limites restantes estão registrados abaixo.

## Fonte do contrato

`src/nodes/SoDrawStyle.cpp` atualiza o estado Coin, respeitando campos ignorados
e overrides independentes de estilo, largura, tamanho e padrão.
`src/elements/GL/SoGLDrawStyleElement.cpp` usa `glPolygonMode` para FILLED,
LINES e POINTS. INVISIBLE é tratado por `SoShape::shouldGLRender()` em
`src/shapenodes/SoShape.cpp`, mediante `SoShapeStyleElement::INVISIBLE`.
Portanto, invisibilidade também impede a emissão de linhas e pontos nativos.

A montagem em `src/shapenodes/soshape_primdata.cpp` transforma QUADS e
QUAD_STRIP em dois callbacks triangulares. Os callbacks não oferecem, em geral,
uma identificação completa do polígono original. `SoFaceDetail` fornece
contornos para alguns shapes; shapes procedurais também usam outros details.
Converter cada triângulo em três segmentos introduziria diagonais nas faces.

O [OpenGL 2.1 Specification, seções 2.12 e 3.5](https://registry.khronos.org/OpenGL/specs/gl/glspec21.pdf)
prevê bordas criadas pelo clipping e a preservação das marcações das bordas
originais remanescentes. A preparação de linhas e pontos de polígonos precisa
considerar esse contrato, culling e interpolação de atributos. A iluminação
por vértice precede o clipping; uma conversão que apenas descarta iluminação
para usar BASE_COLOR não fecha o contrato PHONG.

## INVISIBLE implementado

O dono é Wiring comum: `CoinRenderFramePlanBuilder::isShapeInvisible()` lê o
flag efetivo do Coin. Não interpreta novamente campos de SoDrawStyle e não
introduz uma decisão específica de BGFX ou wgpu.

A action poda somente o shape invisível, antes da validação de unidades de
textura ou da preparação dos caminhos indexados. Como o Coin continua chamando
os demais pre-callbacks mesmo após PRUNE, os callbacks indexados também consultam
o mesmo dono. Os callbacks de primitivas e as entradas diretas do builder
protegem a captura sem depender dessa poda da action.

A travessia de grupos, propriedades e irmãos continua. Um shape invisível não
emite draws, vértices, índices, materiais ou estados de renderização próprios.
Não valida recursos GPU que seriam usados apenas por esse shape. Isso não
isenta nós de estado com callbacks próprios, como SoTextureCombine e RTT,
da validação ou execução que esses nós já possuem durante a travessia.

Um quadro inteiramente invisível publica o clear normalmente. Alterar style
invalida o cache pela notificação Coin existente. Não há alteração de ABI ou
executor GPU para esse fechamento.

## Evidência reproduzível

`testsuite/coinrender/CoinRenderDrawStyleTest.cpp` verifica:

- Cube, IndexedFaceSet, LineSet, IndexedLineSet e PointSet invisíveis;
- Separator, Group, campos ignorados e overrides nos dois sentidos;
- manutenção das propriedades percorridas depois de um shape invisível;
- alternância FILLED/INVISIBLE sem reaproveitar geometria obsoleta;
- action com fast path ligado/desligado, referência CPU e execução GPU;
- quadro vazio substituindo a imagem anterior e avançando serial;
- planos de clipping e unidade de textura além dos limites apenas em shapes
  invisíveis; rejeição sem publicação ao torná-los visíveis e recuperação.

Execução local: wgpu/Rust Vulkan e BGFX Vulkan/OpenGL, RGBA8 64×64,
tolerância de dois níveis por canal nas amostras. Os dois perfis usam o mesmo
teste. Esse teste não compara com o renderer Coin/GL e não qualifica FreeCAD,
outros drivers ou plataformas. Em 2026-09-28, passaram 19 CTests no perfil
wgpu e 26 no perfil BGFX, incluindo o novo teste e regressões de frame, reuso,
clipping, composição e publicação. Logs locais:
`/tmp/coin-drawstyle-wgpu-ctest.log`, `/tmp/coin-drawstyle-bgfx-ctest.log` e
`/tmp/coin-drawstyle-bgfx-core-ctest.log`.

## LINES/POINTS implementados no perfil convexo

Wiring captura `SoDrawStyleElement` efetivo e usa os atributos dos callbacks,
incluindo materiais, normais e UV. Faces com `SoFaceDetail` conservam a ordem do
contorno fornecido pelo Coin; o builder verifica identidade do detalhe,
coordenadas e consistência dos atributos entre callbacks. O fast path de
IndexedFaceSet encaminha esses estilos aos callbacks para conservar os details.

Para os tipos exatos SoCube, SoSphere, SoCylinder e SoCone, a montagem conhecida
de `src/misc/SoGenerate.cpp` identifica quads, faixas de quads e triângulos das
tampas. Os pares de callbacks de quads são verificados antes da reconstrução.
A classificação é Wiring comum. Subclasses não herdam automaticamente essa
suposição sobre generatePrimitives. Outros shapes precisam fornecer um
SoFaceDetail recuperável; caso contrário, o pedido retorna UNSUPPORTED.

`CoinRenderPolygonStyleCore.h` recebe o contorno com snapshots e decide a saída:
iluminação por vértice, clipping homogêneo nos seis planos do volume de visão,
clipping por SoClipPlane, culling, topologia, índices e estado resolvido. O
clipping do contorno completo cria a borda de corte e seus dois extremos, sem
expor a triangulação interna do quad. Culling considera projeção, winding e
espelhamento. Faces projetadas de lado continuam emitindo bordas e pontos se
culling estiver desligado. Não deduplicar pontos/arestas de polígonos distintos:
a multiplicidade pode afetar alpha e depth, como no GL.

`CoinRenderLightingCore.h` contém a fórmula Gouraud antes pertencente ao
rasterizador CPU. A resolução de estilos e o rasterizador CPU usam a mesma
função. Cores iluminadas e limitadas são interpoladas nos vértices novos de
clipping, junto de alpha e UV. Infra consome geometria já resolvida: não há
nova interpretação de SoDrawStyle nos executores. O transporte do bias de
inclinação usa a ABI privada Rust 24, descrita abaixo.

A expansão existente aplica largura/tamanho, preservando depth e alpha no
perfil validado. Linhas/pontos nativos mantêm sua topologia sob LINES/POINTS.
Notificações do Coin invalidam o plano; o reuso de câmera/material existente
não aceita strokes preparados em coordenadas de tela.

### Limites explícitos

- Contornos côncavos, coordenadas repetidas ambíguas, montagem incompleta ou
  atributos inconsistentes retornam UNSUPPORTED. Esses casos não geram uma
  aproximação com diagonais. A triangulação de faces côncavas do GL exige estudo
  próprio para definir bordas efetivamente rasterizadas em cada shape.
- Padrão de linha admite sólido (`0xffff`) e vazio (`0`). Outros padrões são
  rejeitados: continuidade entre arestas e contagem de fragmentos Coin/GL
  permanecem pendentes.
- Offset por inclinação está implementado para contornos planos com área
  projetada não zero. No wgpu, inclinação e units são combinados na Infra usando a precisão
  D32Float do maior depth do contorno; units isolado conserva o caminho existente.
  Precisão de units e qualificação de depth/raster continuam vinculadas a
  P04/F12. Faces não planas e faces de lado com factor ativo têm diagnóstico
  explícito; com factor zero, conservam o perfil anterior.
- Textura na unidade 0 com UV explícito, quatro modelos e fog por fragmento
  estão implementados em ambos os backends. Multitextura no wgpu e UV
  procedural/default permanecem no escopo de P07/P08. A matriz de
  transparência e de outras plataformas ainda exige qualificação.
- Não declarar equivalência visual Coin/GL, cobertura de subclasses/custom
  GLRender, todas as modalidades de transparência ou qualificação FreeCAD.
  Os testes de clipping da rodada final ainda registraram indisponibilidade
  da referência Coin/GL offscreen; a comparação dos novos estilos não foi
  executada nesta entrega.

### Evidência desta ampliação

O mesmo CoinRenderDrawStyleTest agora verifica os contornos de IndexedFaceSet,
FaceSet, triângulos, pentágonos convexos, Cube, Sphere, Cylinder e Cone; bordas
sem diagonais, quatro pontos por quad, culling, espelhamento, cortes por planos
de usuário e volume de visão, passagem pelo near plane, interpolação de UV/alpha
e iluminação anterior ao clipping. Testes de pixels com fast path ligado e
desligado cobrem LINES/POINTS, BASE_COLOR/PHONG, cantos e bordas de corte, ausência
de diagonal e depth contra um fundo preenchido que também é desenhado
depois dos strokes. Rejeição de padrões preserva
imagem/serial e o próximo pedido sólido recupera a publicação.

As rodadas finais passaram 19 CTests no perfil wgpu e 26 no perfil BGFX,
em execução sem concorrência GPU entre os perfis. O teste de estilo foi
reexecutado após fortalecer a verificação de depth e do diagnóstico de textura
não suportada.
Rodadas anteriores apresentaram falhas intermitentes OpenGL: segfault em
CoinBgfxSortedLayersOpenGLTest/CoinBgfxWeightedOitOpenGLTest e SIGPIPE em
CoinBgfxReadbackModes_opengl, após avisos GLX/EGL/DRI3. Os três passaram duas
vezes cada em um novo Xvfb, isoladamente. A causa não foi determinada; não
considerar isso qualificação da estabilidade do ambiente nem ocultar os
resultados anteriores. Logs desta ampliação:
`/tmp/coin-poly-bgfx-ctest.log`, `/tmp/coin-poly-bgfx-isolated.log`,
`/tmp/coin-poly-bgfx-final-ctest.log`, `/tmp/coin-poly-wgpu-final-ctest.log`,
`/tmp/coin-poly-bgfx-final-style.log` e `/tmp/coin-poly-wgpu-final-style.log`.

## Offset por inclinação da face original

O Core recebe também o viewport capturado. Para a face plana original, calcula
`m = max(abs(dz_window/dx_pixel), abs(dz_window/dy_pixel))`, já incluindo o
intervalo de depth. O estado resolvido transporta `factor * m` em
`polygonOffsetSlopeBias` e zera o factor usado pela GPU. Clipping e culling
continuam antes da expansão; o bias não modifica os vértices nem o recorte.
Planaridade é verificada no contorno original, mesmo que um corte esconda parte
da face, com tolerância de `1e-5` da extensão em coordenadas do objeto.

A equação segue o contrato de polygon offset da
[especificação OpenGL 2.1, seção 3.5.5](https://registry.khronos.org/OpenGL/specs/gl/glspec21.pdf).
A inclinação dos triângulos que expandem os strokes não substitui a inclinação
da face que lhes deu origem. Área projetada zero com factor ativo fica fora
deste perfil, pois não oferece um gradiente finito único.

BGFX soma esse bias ao componente constante já usado por `coinWindowDepth`;
range, bias e clamp em `[0,1]` são aplicados no fragmento. O shader conserva o
caminho existente de units. wgpu seleciona `fs_depth_bias` apenas para draws
com bias resolvido ativo, usa viewport de depth `[0,1]` e aplica range Coin,
bias e clamp no shader. A variante usa factor/units GPU zero e soma units em janela, com quantum
D32Float `2^(expoente(maxDepth)-23)`; valores fracionários são preservados.
Esse cálculo da precisão pertence à Infra. O caminho de
pipeline anterior permanece para os demais draws. Essa escolha também evita
que o clamp do viewport reduza o resultado ao intervalo Coin antes do bias.
As regras de depth bias e saída de fragmento estão na
[especificação WebGPU](https://www.w3.org/TR/webgpu/).

A ABI privada wgpu passa para **23**: estado de **1092 bytes**, novo float no
offset **1088**, com os offsets de clipping e o draw conservados. Igualdade,
reuso, agrupamento BGFX, empacotamento e diagnóstico textual incluem o bias.
O rasterizador CPU aplica o bias resolvido após o range, para referência do
perfil testado com units zero.

`CoinRenderDrawStyleTest` cobre fórmula numérica independente, dois tamanhos de
viewport, projeção homogênea, range, sinais, clipping e máscara de estilo.
A cena coplanar inclinada verifica LINES/POINTS, fast path ligado/desligado,
oclusão antes/depois do stroke e bias suficiente para atingir o clamp sem
eliminar geometria pelo clipping. Faces não planas preservam imagem e serial,
e a action recupera após rejeição. O teste de depth valida transporte BGFX e
invalidação por mudança isolada do bias; FFI e Naga validam o transporte Rust
e as três variantes WGSL. Esses testes não encerram comparação visual Coin/GL,
precisão de units nem a matriz de transparência.

Em 2026-09-28, passaram **19 CTests wgpu**, **26 CTests BGFX** e
**10 testes Rust** (oito unitários e dois de validação WGSL). Após a revisão
final de diagnóstico e máscara efetiva, passaram novamente os contratos de
frame e depth nos dois perfis. Logs locais:
`/tmp/coin-slope-wgpu-ctest.log`, `/tmp/coin-slope-bgfx-ctest.log`,
`/tmp/coin-slope-rust.log`, `/tmp/coin-slope-wgpu-review-tests.log` e
`/tmp/coin-slope-bgfx-review-tests.log`.

Em uma segunda rodada, o limite de inclinação + units no wgpu foi removido.
Passaram o teste de estilo wgpu, três variantes BGFX e 11 testes Rust.
Readback D32Float verificou units fracionário; sinais opostos de inclinação e
units verificaram a soma por oclusão. Logs: `/tmp/coin-units-wgpu-tests.log`,
`/tmp/coin-units-bgfx-tests.log` e `/tmp/coin-units-rust-tests.log`.

## Expansão comum, texturas e fog

`CoinRenderStrokeCore.h` é o único dono da expansão mecânica de linhas e
pontos: clipping, largura/tamanho, atributos interpolados, peso homogêneo e
distância de fog. O builder captura estado e chama o Core; a duplicação por
backend foi removida. Linhas indexadas texturizadas usam callbacks Coin para
preservar UV, enquanto o caminho direto continua para linhas sem textura.

wgpu transporta `screen_space_w` e `fog_eye_depth_plus_one` na ABI privada
**24**, com vértice de **44 bytes**. Estado continua com 1092 bytes e draw com
56 bytes. O valor zero dos novos campos conserva a geometria comum de clientes
FFI; fog explícito usa distância + 1, inclusive quando a distância é zero.
O shader multiplica a posição homogênea por W e usa o fog capturado. As mesmas
operações já existiam no BGFX e na referência CPU. Cor e UV têm interpolação
em perspectiva; fog é avaliado no fragmento depois da textura.

A referência analítica varia W de 1 a 4 e verifica clipping, duas matrizes UV,
quatro modelos e três fórmulas de fog em linhas e pontos. A matriz de actions
verifica polígonos LINES/POINTS recortados, UV interpolado na nova borda,
SoTexture2Transform, MODULATE/REPLACE/DECAL/BLEND, HAZE/FOG/SMOKE, PHONG e
fast path ligado/desligado. UV procedural/default e unidades adicionais não
foram promovidos a suporte wgpu por essa entrega.

A regressão desta extração passou 19 CTests wgpu e 28 CTests BGFX, contando
as duas reexecuções de capacidade de textura após ajustar expectativas antigas:
BGFX já implementava SCREEN_DOOR e ambos os backends agora aceitam strokes
texturizados. Nove testes unitários e dois de WGSL também passaram. Logs:
`/tmp/coin-stroke-wgpu-ctest.log`, `/tmp/coin-stroke-bgfx-ctest.log`,
`/tmp/coin-stroke-bgfx-texture-tests.log` e `/tmp/coin-stroke-rust.log`.

A referência Coin/GL foi recuperada usando Mesa/llvmpipe, GL 4.5 Compatibility,
Mesa 25.2.8, pixmap GLX e contexto direto. A matriz acima também compara
amostras RGB com Coin/GL, com tolerância de quatro níveis por canal, na
referência CPU e nas saídas GPU. A execução é obrigatória com
`COIN_RENDER_REQUIRE_GL_REFERENCE=1` e falha se o renderer GL não funcionar.
Comando local reproduzível para wgpu:

```sh
__GLX_VENDOR_LIBRARY_NAME=mesa COIN_GLXGLUE_NO_PBUFFERS=1 \
COIN_GLX_PIXMAP_DIRECT_RENDERING=1 COIN_RENDER_REQUIRE_GL_REFERENCE=1 \
xvfb-run -a /mnt/Laranja/Git/externos/coin-build/bin/CoinRenderDrawStyleTest
```

Para BGFX, usar o executável em `build-bgfx-recovery/coin-build/bin` e
`COIN_BGFX_RENDERER=vulkan` ou `opengl`. Evidências em
`/tmp/coin-stroke-required-gl.log` e `/tmp/coin-stroke-required-gl-bgfx.log`.
Isso fecha a comparação de amostras desse perfil com GL; ainda não fecha
raster de todas as bordas, drivers físicos ou integração FreeCAD.

## Checklist de P02

- [x] INVISIBLE com estado efetivo Coin e supressão comum de captura.
- [x] Publicação e invalidação de cache verificadas nos dois backends.
- [x] Contornos de faces recuperáveis e tipos procedurais exatos documentados.
- [x] LINES/POINTS no Core, sem diagonais de quads ou duplicação por triangulação.
- [x] Culling e clipping de contornos convexos, com bordas e pontos novos de corte.
- [x] Gouraud antes de clipping, com UV/alpha interpolados e depth no perfil.
- [ ] Contornos fora do perfil, subclasses e shapes customizados; definir sua matriz.
- [x] Offset pelo gradiente da face plana original em Core, BGFX e wgpu.
- [ ] Padrão contínuo entre arestas.
- [x] Inclinação + units fracionário no wgpu/D32Float, com readback numérico.
- [ ] Offset fora do perfil plano e qualificação ampliada de precisão P04/F12.
- [x] Fog por fragmento e textura explícita na unidade 0 em strokes wgpu/BGFX.
- [ ] Multitextura, UV procedural/default e matriz ampliada P07/P08.
- [x] Amostras de textura/fog dos estilos comparadas com Coin/GL Mesa/llvmpipe.
- [ ] Sorting/transparência, raster ampliado, drivers e integração FreeCAD.

O suporte de LineSet/PointSet continua distinto do estilo aplicado a polígonos.
P02/F04 não deve ser marcado como integralmente fechado por este perfil.
