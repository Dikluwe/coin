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
nova interpretação de SoDrawStyle nos executores, nem mudança de ABI Rust.

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
- Offset ativado para o estilo admite factor zero. Factor diferente de zero é
  rejeitado porque exige o gradiente de depth do polígono original. Precisão de
  units e qualificação de depth/raster continuam vinculadas a P04/F12.
- Texturas, inclusive unidades adicionais, e fog de polígonos estilizados no
  wgpu são rejeitados. BGFX usa o caminho existente de atributos homogêneos,
  texturas e fog por fragmento; sua matriz de combinações ainda precisa ser
  qualificada para esses novos estilos, em conjunto com P07/P08.
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

## Checklist de P02

- [x] INVISIBLE com estado efetivo Coin e supressão comum de captura.
- [x] Publicação e invalidação de cache verificadas nos dois backends.
- [x] Contornos de faces recuperáveis e tipos procedurais exatos documentados.
- [x] LINES/POINTS no Core, sem diagonais de quads ou duplicação por triangulação.
- [x] Culling e clipping de contornos convexos, com bordas e pontos novos de corte.
- [x] Gouraud antes de clipping, com UV/alpha interpolados e depth no perfil.
- [ ] Contornos fora do perfil, subclasses e shapes customizados; definir sua matriz.
- [ ] Padrão contínuo entre arestas e offset pelo gradiente do polígono original.
- [ ] Fog e texturas de strokes no wgpu; matriz ampliada BGFX conforme P07/P08.
- [ ] Sorting/transparência, precisão de raster e comparação Coin/GL/FreeCAD.

O suporte de LineSet/PointSet continua distinto do estilo aplicado a polígonos.
P02/F04 não deve ser marcado como integralmente fechado por este perfil.
