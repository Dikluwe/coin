# Overlays FreeCAD: progresso da paridade GL/BGFX

## Implementado nesta etapa

O retângulo de seleção do `MouseSelection` e a cruz de eixos de canto agora
participam do scene graph fornecido pelo viewer ao adaptador BGFX. Não são
desenhados por uma ação GL auxiliar nem por captura/copiação da viewport GL.
O caminho Coin/GL existente permanece disponível.

- `SoFCScreenSpaceGroup::callback()` publica matrizes, view volume, iluminação
  BASE_COLOR e estado de profundidade para a travessia independente de backend.
  Não chama o elemento GL de habilitação de texturas.
- O rubber-band mantém preenchimento transparente, borda e padrão de linha
  existentes, com conversão de coordenadas lógicas para pixels físicos.
- A cruz de canto reutiliza as mesmas setas e texturas X/Y/Z do caminho GL,
  dentro de sua subviewport. O estado é por viewer, não global.
- Os dois overlays são camadas `SoAnnotation`, compostas depois da cena.
- Geometria só é atualizada quando retângulo, câmera, cores, tamanho ou DPI
  mudam; não há redraw contínuo em repouso.
- Overlays interativos são omitidos quando a intenção não é LiveInteractive.
  O adaptador continua opt-in e restrito ao build experimental.

## Validação

Build FreeCADGui/FreeCADMain: `/tmp/freecad-wgpu-build2`.
Biblioteca Coin/BGFX corrigida: `/tmp/coin-64-viewports-install`.

O caso `freecad-overlays` do runner executa o FreeCAD real em oito células:
Vulkan/OpenGL, object/weighted OIT e DPI 1x/2x. Todas passaram, na GPU física,
sem fallback e sem skips. Verifica pixels coloridos da cruz de canto, resposta
à orientação da câmera, preenchimento durante drag Qt, remoção ao soltar e
contagem de frames estável em idle.

Artefatos: `/tmp/freecad-overlays-matched-background/results.json` e capturas por célula.
`freecad_overlays.FCMacro` é o teste real; o harness Quarter isolado não tem o
viewer completo e continua declarando seus casos sintéticos privados como
UNSUPPORTED. Isso não deve ser confundido com a validação integrada acima.

`compare_freecad_overlays.py` compara as capturas com uma execução separada do
Coin/GL, usando o canto para eixos e a contribuição do preenchimento para
rubber-band e suas quatro bordas. O teste fixa um background sólido comum
(não declara paridade de gradientes). Limite RGB MAE: 6, sem relaxamento.

As oito comparações passaram: eixos até 3,7163/255; preenchimento 0; bordas
até 0,0677/255. Referências Coin/GL: /tmp/freecad-overlays-solid-coin-gl-{1,2}x.
Métricas: /tmp/freecad-overlays-comparison.log.

A regressão real freecad-multi passou 2/2 em Vulkan (object/weighted OIT):
câmeras e preseleção independentes, resize, fechamento do primeiro alvo e
continuidade do sobrevivente. Artefatos: /tmp/freecad-overlays-multi-regression.

## Continuação: itens legados

`Gui::Rubberband` e `Gui::Polyline` agora oferecem geometria Coin retida
pelo contrato `GLGraphicsItem::overlayScene(width, height)`. O viewer BGFX
inclui esses itens depois do eixo de canto e antes do rubber-band moderno,
mantendo a ordem do caminho GL. Cores, alpha, espessura, fechamento e
fechamento pontilhado continuam usando a mesma geometria de `paintGL()`.
O cache é invalidado pelos setters e pelo resize, não em todos os frames.
O caminho GL usa o mesmo cache, mas continua aplicando SoGLRenderAction.

O contrato padrão retorna nullptr para itens de terceiros que só implementam
`paintGL()`; não executamos código GL arbitrário no caminho nativo BGFX.
O GLPainter e o conector de Flag foram portados na continuação descrita abaixo.

`FreeCADLegacyOverlayTest.cpp` valida extração para o plano, preenchimento,
contorno, padrões, fechamento, cache, resize, pop/clear e ativação/desativação.
É um teste C++ que requer headers e biblioteca FreeCADGui do build experimental;
não é um teste isolado do Coin nem prova por si só submissão na GPU.

`freecad_legacy_polyline.FCMacro` usa o comando real `Mesh_PolyCut` e
verifica a seleção poligonal azul, atualização pelo mouse, cancelamento sem
modificar a malha, remoção do overlay e idle. O runner oferece o caso
`freecad-legacy-polyline`, que requer os targets MeshGui e MeshScripts compilados.

Teste C++: PASS, incluindo cópia/atribuição sem compartilhar o cache mutável.
Regressão dos overlays modernos após a integração: 8/8 PASS, GPU física,
sem fallback, em /tmp/freecad-legacy-overlay-modern-regression{,-gl}.
Seleção poligonal real: 4/4 PASS, Vulkan/OpenGL e object/weighted OIT em
DPI 1x, GPU física, sem fallback. Artefatos:
/tmp/freecad-legacy-polyline-final/results.json.
A referência Coin/GL também passou em /tmp/freecad-legacy-polyline-coin-gl.
As quatro comparações por compare_freecad_legacy_polyline.py passaram:
RGB MAE na região até 0,1724/255; nos pixels afetados pela linha até
12,495/255 (limites separados 6 e 16). A máscara evita que o fundo amplo
esconda uma linha ausente; o teste unitário de linha ausente falha como esperado.
Após cancelamento, ambas as métricas são zero. Runner/comparador: 10/10 testes.
Não foi validado o Rubberband legado dentro da edição de Sketcher nesta
etapa; sua geometria e ciclo de vida foram cobertos no teste C++.

## Continuação: GLPainter

`GLPainter::begin(SoGroup*, pixelWidth, pixelHeight)` grava os mesmos nós
Coin de drawRect/drawLine/drawPoint numa cena fornecida pelo chamador, sem
QOpenGLWidget, makeCurrent ou aplicação de ação GL. A sobrecarga
`begin(QPaintDevice*, SoGroup*)` usa as dimensões/DPR do dispositivo.
A cena é mantida viva durante a sessão; end/destrutor liberam essa referência,
sem apagar as primitivas anexadas. O dono da cena controla sua substituição e
remoção. Sessões não podem ser copiadas ou iniciadas de maneira aninhada.

O antigo begin(device) permanece o caminho GL. Clientes nativos precisam usar
a sessão retida; não há execução automática de paintGL arbitrário.

`GLFlagWindow::overlayScene()` projeta as origens e grava conectores/pontos
com GLPainter. A assinatura das coordenadas invalida o cache quando câmera,
layout, posição ou visibilidade alteram o desenho; o tamanho físico também
participa do cache. O rótulo Flag agora é QWidget raster, com o mesmo QPainter,
em vez de uma superfície QOpenGLWidget desnecessária.

`setLogicOp` e `setDrawBuffer` já eram no-ops e continuam assim. Não declarar
paridade de XOR/operações lógicas ou seleção de buffers GL.

Build FreeCADGui/FreeCADMain/MeshGui: PASS.
`FreeCADGLPainterTest.cpp`: sessão sem GL, estados por primitiva, alpha,
padrão, ponto, ordem e ciclo de vida; pixels offscreen BGFX passaram 4/4 em
Vulkan/OpenGL e object/weighted OIT. O teste comprova ausência de contexto
QOpenGLContext do Qt, não ausência de GL interno no renderer OpenGL do BGFX.
Logs finais: /tmp/freecad-glpainter-final-{vulkan,opengl}-{object,weighted_oit}.log.
A validação também rejeita pontilhado que vira linha sólida e verifica pixels
limpos após removeAllChildren. FreeCADFlagRasterTest valida texto e origem
em QWidget sem contexto GL do Qt.
Regressão de viewer real: 12/12 PASS, sem fallback, em
/tmp/freecad-glpainter-real-regression/results.json.
A repetição após o build raster final capturou a tela de bloqueio Cinnamon
(papel de parede e relógio), não a viewport: os FAILs ficaram preservados em
/tmp/freecad-glpainter-final-real-regression e nas tentativas de diagnóstico.
Ativar a janela e capturar seu winId não contornou o bloqueio; os macros foram
restaurados ao método de captura anterior. Essa repetição não é uma aprovação
nem evidência de regressão do renderer. Reexecutar em sessão desbloqueada.
CTest final: 7/7 PASS em /tmp/freecad-glpainter-ctest.log (três testes diretos e
quatro combinações de pixels GPU); o bloqueio não interfere nos readbacks.
O teste de widget raster é offscreen e não equivale à interação no viewer.
Os três testes C++ têm um projeto CMake reproduzível em
testsuite/qt-quarter/freecad-overlay-tests. Configure FREECAD_SOURCE_DIR,
FREECAD_BUILD_DIR e COIN_PREFIX; habilite FREECAD_OVERLAY_GPU_TESTS para as
quatro combinações GPU. Use o runtime Qt/LD_LIBRARY_PATH correspondente ao
build FreeCAD. GPU indisponível é falha, não aprovação ou skip.

## Continuação: validação integrada de Flag

A matriz final anterior foi repetida em sessão desbloqueada: 12/12 PASS,
sem fallback, em /tmp/freecad-glpainter-unlocked/results.json.

O caso freecad-flags e o helper de teste FreeCADFlagTestBridge exercitam dois
rótulos/conectores, posição/origem, câmera, ocultação, remoção e idle, em
Vulkan/OpenGL, object/weighted OIT e DPI 1x/2x. O helper é uma biblioteca
exclusiva dos testes; não adiciona API Python de produção nem habilita o
adaptador. Configure COIN_TEST_FLAG_HELPER com o .so gerado pelo projeto
freecad-overlay-tests. Ausência do helper é UNSUPPORTED, não aprovação.

O teste encontrou um crash confirmado em QWidget::mapToParent, chamado por
GLFlagWindow::overlayScene: viewport e rótulo são irmãos, mas mapTo exige
ancestral. Backtrace: /tmp/freecad-flags-debug/gdb.log. Corrigido usando mapToGlobal/mapFromGlobal. O conector GL legado
também usa esse mapeamento e o DPR físico. Flag agora solicita redraw quando
origem, posição, tamanho ou visibilidade mudam; setText atualiza o raster/layout.
O teste de atualização não chama redraw artificialmente.

Durante a repetição, a sessão voltou a bloquear: as capturas continham o relógio
Cinnamon, não os rótulos. Não afirmar falha nem paridade de empilhamento a partir
delas. Uma tentativa especulativa de tornar os rótulos nativos foi revertida;
Flag permanece QWidget raster. A confirmação visual daquela etapa ficou
pendente; a continuação abaixo registra a correção validada. O runner consulta GetActive via D-Bus antes e
depois de testes visuais: bloqueio gera SKIP explícito, nunca PASS, e crashes
continuam FAIL. Não desbloqueia nem muda preferências de segurança.

Build final: PASS; CTest 7/7 PASS; runner/comparador 11/11 PASS.
Artefatos: /tmp/freecad-flags-final-ctest.log e
/tmp/freecad-flags-lock-gate-verified (0 PASS, 4 SKIP por bloqueio).
Naquela etapa o caso Flag ainda não estava validado visualmente.

## Ainda pendente — não declarar paridade completa

| Caminho | Dependência restante |
| --- | --- |
| Itens GL de terceiros e operações lógicas | Migrar clientes para sessões retidas; paintGL arbitrário e XOR não são suportados |
| Interação integrada de Flag | Rótulos/conectores, posição/origem, câmera, hide/remove e DPI validados; ainda ampliar drag/context menu e resize integrado |
| Seleção esclarecida (`SoFCPathAnnotation`) | Caminho, detail e bounding box portados; ampliar validação interativa de seleção esclarecida; depth-clamp genérico com depth test/write ainda não implementado |
| Overlays especializados dos workbenches | Inventariar os nós que só implementam GLRender e validar edição, rótulos, draggers e medidas individualmente |
| Background com imagem/gradiente e framebuffer/imagem | Composição GL do viewer; não incluída nesta etapa |

O rubber-band portado aqui é o novo `RubberbandOverlay` do MouseSelection;
a continuação inclui também Gui::Rubberband legado, mas não afirma suporte
universal a itens GLGraphicsItem de terceiros.

### Continuação: empilhamento nativo dos rótulos Flag

A sessão desbloqueada confirmou que o problema não era apenas a captura:
`/tmp/freecad-flags-resolved` registrou conectores visíveis e rótulos raster
ocultos. Tornar somente os rótulos nativos ainda falhou com ancestralidade
alien do Qt; os ensaios `/tmp/freecad-flags-stacking`,
`/tmp/freecad-flags-siblings` e `/tmp/freecad-flags-layout` não passaram.

A correção atual cria uma viewport raster nativa apenas quando o adaptador
experimental está habilitado. A superfície BGFX e os rótulos ficam como
irmãos com o mesmo pai nativo; o layout de Flag pertence à viewport.
Os rótulos não são filhos da superfície de apresentação, que continua
transparente a eventos de mouse. A busca do viewer para redraw percorre
os ancestrais, e uma QPointer protege o layout quando o widget é destruído.
O teste visual agora exige pixels de **cada um** dos dois rótulos.

A compilação final está em `/tmp/freecad-flags-native-parent-build.log`.
A validação visual final está em `/tmp/freecad-flags-isolated/results.json`:
**8/8 PASS**, Vulkan/OpenGL × object/weighted OIT × DPR 1x/2x,
com submissão GPU comprovada e sem fallback. Cada rótulo é verificado
individualmente; ocultar um preserva o outro, remover os dois não deixa
conectores, e o contador de frames permanece estável em idle.

A primeira execução em `/tmp/freecad-flags-native-parent` teve duas diferenças
de captura: notificação externa e pré-seleção incidental do cursor.
O caso Flag agora desabilita pré-seleção apenas no perfil descartável
para isolar o overlay. O caso hover continua verificando pré-seleção.
O runner aceita `--mode` e `--scale` para repetir variantes específicas.
Testes CPU do runner/comparadores: **12/12 PASS**.
`/tmp/freecad-flags-child` foi SKIP por bloqueio, não PASS;
não há alteração na configuração de bloqueio da sessão.

Regressão da viewport nativa: `/tmp/freecad-native-parent-regressions/results.json`
registra **6/6 PASS**, hover, overlays e múltiplas viewports em Vulkan/OpenGL
(object, DPR 1x). CTest final: **7/7 PASS**.
O patch reproduzível `examples/wgpu/freecad_coin_wgpu.patch.gz` foi atualizado
com GLPainter, Flag, screen-space group e rubber-band; a checagem reversa do
patch contra o checkout FreeCAD usado no build passou, sem alterar o checkout.

## So3DAnnotation — passagem adiada com profundidade

O callback experimental do nó usa `SoWgpuRenderAction::deferAnnotation()`.
A ação guarda cópias referenciadas dos caminhos apenas durante o frame,
ordena por prioridade com estabilidade para empates e reproduz os caminhos
com os estados ancestrais. Anotações aninhadas participam do mesmo conjunto.
A passagem compartilha uma única limpeza de depth e mantém depth test/write;
não transforma So3DAnnotation em SoAnnotation nem altera o booleano global
utilizado pelo GL. Callbacks Coin não-WGPU continuam atravessando normalmente.

A camada adiada fica depois da cena e antes de foreground/decoration.
Essas duas passagens do viewer têm marcadores explícitos que preservam depth,
sem limpeza artificial. SoAnnotation dentro delas conserva sua própria camada.
As filas são por ação, não globais; replay e retenção são encerrados também
nos caminhos de erro. Nós de seleção esclarecida e grade foram portados na continuação abaixo,
não como consequência automática desta mudança.

`FreeCAD3DAnnotationTest` usa o nó real do FreeCAD em uma cena controlada:
limpeza única, prioridade/empates, nesting, apply(path)/off-path, descarte
entre frames, occlusão mútua das anotações, cena mais próxima, foreground
posterior, SoAnnotation comum e alpha. Readback de pixels em BGFX Vulkan e
OpenGL, modos object e weighted_oit.

Compilações Coin e FreeCAD: PASS. CTest integrado: **12/12 PASS**;
regressões Coin de ação/composição/depth/BGFX core: **4/4 PASS**.
Logs: `/tmp/freecad-3d-annotation-ctest.log`,
`/tmp/coin-3d-annotation-ctest.log`.
O teste de anotações é um readback offscreen com o nó FreeCAD real;
não é uma comparação visual de um workbench com Coin/GL.

Regressão do viewer após os novos marcadores: **6/6 PASS**, hover,
overlays e múltiplas viewports em Vulkan/OpenGL, object/DPR 1x,
sem fallback: `/tmp/freecad-3d-annotation-regressions/results.json`.
O patch FreeCAD reproduzível foi atualizado e sua checagem reversa passou.

## SoFCPathAnnotation e SoDrawingGrid — fila de overlays

A ação experimental agora mantém duas filas por frame: overlays adiados
comuns, depois So3DAnnotation. Ambas precedem foreground/decoration.
Apenas a passagem 3D limpa depth. Os caminhos são copiados/referenciados,
reproduzidos com estados ancestrais e liberados inclusive em falhas.

SoFCPathAnnotation restaura caminhos truncados, exclui irmãos fora do path
e reproduz seleção/detail sem teste ou escrita de depth. As regras existentes
de seleção por bounding box têm geometria Coin retida (12 arestas), sem
chamadas GL; trocar detail invalida o cache. A autoatribuição do path é segura.
O caminho GL existente permanece disponível.

SoDrawingGrid adia a travessia e reutiliza SoFCScreenSpaceGroup, preservando
coordenadas da tela. O resize também reduz explicitamente os campos vertex
e numVertices: setValues sozinho mantinha entradas antigas ao encolher a grade.
O adaptador continua experimental e opt-in.

O helper FreeCADDelayedOverlayTestBridge executa os nós reais dentro do
FreeCAD, mas em alvo BGFX offscreen, sem captura do desktop. Testa ordem das
filas, oclusão, restauração/remoção do path, bounding box, troca/remoção de
detail e redução da grade após resize. Não altera o contexto Qt existente.
Não equivale a comparação interativa de um workbench com Coin/GL.

Matriz GPU física: **4/4 PASS**, Vulkan/OpenGL × object/weighted_oit, sem
fallback, em /tmp/freecad-delayed-overlays-colors/results.json.
CTest integrado: **12/12 PASS**, /tmp/freecad-delayed-overlay-ctest.log.
Regressões Coin ação/composição/depth/core: **4/4 PASS**,
/tmp/coin-delayed-overlay-ctest.log.

A continuação abaixo valida branches tight/projected com um ViewProvider Part
real e resolve o recorte near/far para as bounding boxes sem teste/escrita de
depth. O fluxo interativo de seleção esclarecida ainda precisa ser ampliado.

A grade também passou na viewport nativa real: **8/8 PASS**,
Vulkan/OpenGL × object/weighted_oit × DPR 1x/2x, sem fallback.
O caso freecad-grid verifica aparecimento, invariância em screen-space ao
girar a câmera, resize, remoção e idle. Artefatos:
/tmp/freecad-grid-native-fixed/results.json. Não é uma comparação Coin/GL.
Runner/comparadores CPU: **12/12 PASS**.

Regressões após a integração das duas filas: **6/6 PASS**, hover,
overlays e múltiplas viewports em Vulkan/OpenGL (object, DPR 1x),
sem fallback: /tmp/freecad-delayed-overlay-native-regressions/results.json.
O patch FreeCAD reproduzível foi atualizado e a checagem reversa passou.

## Continuação: bounding boxes com ViewProvider real e recorte near/far

O helper agora cria um Part::Feature real com box 2×1×1, rotação de 30 graus
e translação (1, 0.4, -2). Exercita os quatro modos local/projected ×
tight/loose, tanto para o objeto inteiro quanto Face1. Verifica pixels,
limites XY calculados independentemente da implementação, concordância
tight/loose para o box inteiro e diferença de Face1 em projected/tight.
Isso detecta perda ou aplicação dupla da Placement; não substitui o fluxo
interativo de seleção esclarecida de um workbench.

Para bounding boxes opacas de path-overlay, com depth test e write desligados,
a projeção retida mantém clip X/Y/W e fixa clip Z no interior do frustum.
Nesse caso específico, a visibilidade é equivalente ao GL_DEPTH_CLAMP:
arestas não desaparecem por near/far, e o recorte lateral continua ativo.
O estado fica limitado ao push/pop da bounding box, sem afetar a cena.
Não é um depth-clamp genérico e não deve ser usado para depth test/write ou
composição transparente que dependa da profundidade original.

Readbacks cobrem near, far, cruzamento do intervalo, projeções ortográfica e
perspectiva, bbox totalmente atrás do olho e recorte lateral. A máscara das
arestas deve ser idêntica à referência sem recorte near/far; a cena permanece
recortada normalmente. Não foi validada uma aresta cruzando o plano do olho.

Validação final: **4/4 PASS** na GPU física em Vulkan/OpenGL ×
object/weighted_oit, sem fallback, /tmp/freecad-bbox-full-final/results.json.
CTest: **12/12 PASS**, /tmp/freecad-bbox-final-ctest.log.
Regressões de viewer real hover/overlays/multi: **6/6 PASS**, sem fallback,
/tmp/freecad-bbox-native-regressions/results.json. Runner CPU: **12/12 PASS**.
Build FreeCADMain passou; patch reproduzível atualizado e checagem reversa
aprovada. A validação dos ViewProviders é offscreen dentro do FreeCAD real,
não uma comparação visual interativa com Coin/GL.

## Continuação: seleção on-top na viewport real

O novo caso freecad-path-selection usa a API Selection de produção e
OnTopWhenSelected=1. Não injeta um nó de teste: procura e exige exatamente
um SoFCPathAnnotation criado pelo viewer ao selecionar Face1, Face2 e o
objeto inteiro. Um objeto opaco envolve a peça, ocultando sua renderização
normal. As alterações de pixels verificam o caminho on-top, incluindo o
material transparente usado pelo próprio viewer. Troca de face, resize,
remoção de pixels/path e idle são verificados sem zoom/redraw artificial.

O fitAll inicial enquadra a fixture completa antes da referência, evitando
comparar câmeras diferentes após a inclusão do objeto oclusor. As capturas
são do desktop: execução com outra janela na frente não comprova regressão.
O caso agora exige que o FreeCAD esteja ativo; perda de foco retorna SKIP,
nunca PASS. Janelas de matrizes diferentes não podem compartilhar a captura.

As tentativas /tmp/freecad-path-selection{,-occluded,-isolated,-fit} ficam
preservadas para diagnóstico; houve concorrência de janelas e capturas de
outra aplicação. Não constituem aprovação integral nem falhas confirmadas
do renderer. A interação deste caso é via API, não hit-testing físico do
mouse nem operação do popup de seleção ambígua.

Resultado final desta repetição: **3 PASS, 5 SKIP, 0 FAIL**,
/tmp/freecad-path-selection-active/results.json. Passaram Vulkan object/DPR 1x,
Vulkan weighted_oit/DPR 2x e OpenGL weighted_oit/DPR 1x. Os cinco SKIPs são
perda de foco/obstrução do desktop, não ausência de GPU nem aprovação.
Todos os PASS comprovaram submissão na GPU física sem fallback.
Runner/comparadores CPU: **12/12 PASS**. Repetir as cinco variantes mantendo
a janela FreeCAD visível; a matriz visual completa continua pendente.

## Repetição da seleção on-top: notificação externa

/tmp/freecad-path-selection-repeat executou todas as oito variantes:
7 PASS e 1 FAIL na comparação de remoção em Vulkan/object/DPR 2x.
A inspeção das PNGs confirmou uma notificação externa no canto superior
direito da referência, ausente na captura final; a diferença ficou restrita
a X=1368..1770, Y=0..152 numa imagem 1772×600. Não é uma falha confirmada
do renderer nem uma aprovação integral dessa execução.

O caso agora compara a faixa central que contém toda a projeção da peça
alvo. A fixture centralizada tem um objeto oclusor três vezes maior que a
peça; a faixa de 25%..75% da largura contém o alvo e exclui notificações
nos cantos e o eixo de canto. Mantém os limites anteriores de mudança e
remoção, contagem de paths, resize e idle. Não altera preferências de
notificações do desktop nem aceita capturas de outra janela.

Repetição final /tmp/freecad-path-selection-region/results.json:
**8/8 PASS, 0 SKIP, 0 FAIL**, Vulkan/OpenGL × object/weighted_oit × DPR 1x/2x.
GPU física e submissão comprovadas, sem fallback. Seleção por caminho
on-top, faces, objeto inteiro, resize, remoção e idle validados na viewport
nativa. Runner/comparadores: **12/12 PASS**. O popup de seleção ambígua
e o hit-testing físico do mouse continuam fora deste caso.

## Continuação após os commits: popup real de seleção ambígua

Commits de integração: Coin c8adf4d3bf e FreeCAD 228c679d78.
As alterações não relacionadas permaneceram fora desses commits.

O helper de testes agora instancia SelectionMenu de produção, chama doPick
com candidatos Face1/Face2 e mantém o popup modal real. Hover usa os handlers
de produção; a confirmação abre o submenu através do menu pai e envia
Right/Return pelo caminho Qt de eventos de teclado. Não substitui o resultado
da escolha nem chama onPicked artificialmente. Cancelamento fecha o popup.
Verifica também limpeza de ClarifySelectionActive e da pré-seleção.

O caso native freecad-selection-menu usa a peça encoberta para comprovar
o destaque on-top durante hover, mudança de face, seleção confirmada e
remoção. Paths e pixels são verificados, incluindo idle. É uma fixture com
candidatos controlados; não valida ray picking físico nem o atalho de entrada
do popup. Não exige alteração GL/BGFX adicional no menu Qt.

A captura consulta _NET_ACTIVE_WINDOW do X11. isActiveWindow do Qt sozinho
mostrou estado desatualizado durante o loop modal e capturou outra aplicação.
Perda de foco X11 é SKIP. As tentativas smoke/diagnostic tiveram capturas
inválidas; x11 revelou um problema do clique sintético que foi substituído
pela abertura/confirmacão normal do submenu. Nenhuma dessas tentativas é
declarada aprovação. A integração offscreen do popup foi removida da macro
de readback após timeout; não é uma evidência de funcionamento do menu.

Smoke nativo final: /tmp/freecad-selection-menu-keyboard/results.json,
**1/1 PASS**, Vulkan/object/DPR 1x, GPU física sem fallback.
CTest existente: **12/12 PASS**, /tmp/freecad-selection-menu-ctest.log.
Runner/comparadores: **12/12 PASS**.

Matriz inicial do popup: /tmp/freecad-selection-menu-matrix/results.json,
**5 PASS, 2 SKIP, 1 FAIL**. Passaram as quatro variantes Vulkan e OpenGL
weighted_oit/DPR 2x. OpenGL object/DPR 1x teve um frame adicional entre
idle_start=115 e idle_end=116; a repetição reproduziu um único frame.
Isso não comprova redraw contínuo, mas não foi aceito como PASS.

A macro agora espera duas amostras consecutivas quietas, espaçadas 500 ms,
após o fechamento/limpeza do popup, com limite total de 3 segundos. Depois
mede 1200 ms de idle sem aceitar nenhum frame adicional. A fase de
estabilização não solicita redraw. Quatro testes executam as próprias
funções da macro e comprovam estabilidade mínima, reinício por frame tardio,
falha de redraw contínuo e medição final estrita: suíte Python **16/16 PASS**.

A matriz com essa fase final ficou em **0 PASS, 8 SKIP** por foco X11 em
outra aplicação: /tmp/freecad-selection-menu-settled/results.json.
A validação visual completa do popup e da nova estabilização ainda está
pendente. Não alterar esse resultado para PASS nem declarar paridade de
hit-testing físico. A tentativa de integração offscreen do popup foi
removida; os readbacks existentes permanecem independentes do desktop.

Regressão dos readbacks após a continuação: **4/4 PASS**, Vulkan/OpenGL ×
object/weighted_oit, GPU física sem fallback,
/tmp/freecad-selection-menu-readback-regression/results.json.
O helper também exige que os dois callbacks de hover tenham sido executados;
fechar o menu antes deles não pode produzir um PASS artificial.

## Sessão isolada: validação final do popup e seleção on-top

run_isolated.py fornece autenticação X privada, D-Bus privado, Metacity
e diretórios XDG temporários. Não muda HOME nem as preferências de bloqueio
da sessão normal. O probe de bloqueio agora verifica NameHasOwner antes de
GetActive para não iniciar um screensaver por efeito colateral.

Xvfb e Xephyr, neste ambiente, não ofereceram DRI3. GL anunciou llvmpipe,
e a seleção da AMD em Vulkan não produziu pixels na viewport. As tentativas
/tmp/freecad-isolated-xvfb-menu e /tmp/freecad-isolated-xephyr-menu falharam;
não são aprovações nem evidência de apresentação hardware.

A solução usa Xwayland rootful sobre Weston X11/GL, com o Weston extraído
em /tmp/coin-weston.zyfEjc/runtime (weston e libweston-13-0 13.0.0-4build3).
Nenhum pacote foi instalado no sistema. Inventário GL e log do compositor
confirmaram AMD Radeon Graphics (radeonsi, renoir); BGFX Vulkan confirmou
vendor 0x1002/device 0x1638. A ausência de fallback é exigida por célula.

A primeira matriz isolada de popup passou 8/8, mas o wrapper terminou em erro
ao tentar remover um mount GVfs ainda ativo. A ordem de encerramento foi
corrigida: o pai conserva o runtime até o D-Bus privado e seus serviços saírem,
depois limpa somente os recursos temporários dessa execução.

Repetição FINAL: /tmp/freecad-isolated-xwayland-final/results.json,
**16/16 PASS, 0 SKIP, 0 FAIL**, e wrapper com exit 0. Oito células são o
popup real e oito a seleção on-top: Vulkan/OpenGL × object/weighted_oit ×
DPR 1x/2x, GPU física, sem fallback. Inclui hover, confirmação/cancelamento,
limpeza de ClarifySelectionActive/pré-seleção, resize da seleção, remoção
de paths/pixels e idle estrito após estabilização limitada.
A sessão gráfica foi encerrada; logs/PNGs e inventário permanecem nos artefatos.
Testes Python: **22/22 PASS**, incluindo probe sem autoativação e limpeza
restrita aos processos próprios.

Esta validação usa candidatos controlados e eventos Qt, não ray picking
físico nem comparação pixel a pixel com Coin/GL. Paridade universal ainda
não foi atingida: itens GL de terceiros, operações lógicas, overlays
especializados e outros caminhos pendentes continuam fora deste resultado.


### Hit-testing nativo do mouse

O caso `freecad-mouse-picking` injeta movimento e clique por XTest no
servidor X11 privado, passando pela viewport e pelo picking de produção.
Não usa `addSelection`, `setPreselection` nem eventos Qt enviados diretamente
para fabricar o resultado. `getObjectInfo` (SoRayPickAction, somente leitura)
localiza dois interiores de faces visíveis e fornece os identificadores
esperados, comparados com a pré-seleção e a seleção criadas pelo mouse.

Exige alteração visual da face, transição entre faces, clique na segunda,
remoção do hover no fundo, limpeza de seleção e repetição depois de resize.
Inclui DPR 1/2, estabilização limitada a 3 segundos e idle estrito de 1,2s.
A seleção é limpa por API somente na etapa de limpeza; sua criação é nativa.
A sessão isolada tem cursor próprio e não move o cursor da sessão principal.

Este cenário cobre uma caixa Part na vista axonométrica. Arestas/vértices,
montagens transformadas, perspectiva, modificadores de seleção e comparação
direta com Coin/GL continuam fora desta cobertura.


Resultado final: `/tmp/freecad-mouse-picking-final/results.json`,
**8/8 PASS, 0 SKIP, 0 FAIL**, wrapper exit 0. BGFX Vulkan/OpenGL ×
object/weighted_oit × DPR 1/2, AMD física e sem fallback.
Testes Python: **29/29 PASS**. A primeira matriz teve 7/8 por dois frames
tardios após a limpeza; a repetição completa com estabilização limitada passou
e continua rejeitando qualquer frame adicional na janela de idle estrito.
O picking de produção existente funcionou; não foi necessário trocar seu
algoritmo nem simular seleção para o adaptador.


### Arestas, vértices, perspectiva e Placement

O novo caso `freecad-mouse-elements` exercita Face/Edge/Vertex com XTest
em três cenas: ortográfica, perspectiva e perspectiva com Placement
(translação 23/-17/11 e rotação 31 graus em torno de 1/2/3).
Projeta pontos conhecidos da topologia, rejeita elementos ocultos com
getObjectInfo e compara o elemento criado pelo mouse com esse resultado.
Cada elemento precisa aparecer em hover, ficar selecionado por clique,
limpar a pré-seleção no fundo e remover seus pixels após a limpeza.

A primeira execução revelou que `SoBrepEdgeSet` identificava Edge1 mas
não desenhava seu destaque (zero pixels). A seleção B-rep só era desenhada
pelo GLRender desses nós; o caminho por callbacks foi acrescentado tanto
a SoBrepEdgeSet como a SoBrepPointSet. Os helpers retidos agora aceitam
SoAction e despacham para GLRender ou callback, compartilhando cores,
índices e regras de profundidade. Hover fica sobre a peça; seleção
confirmada respeita profundidade, sem escrever no depth buffer.

Para vértices, a medição conta todos os pixels numa região pequena ao
redor do ponto. A amostragem de dois em dois perdia pixels de sprites de
4px após rotação. Controles negativos exigem zero quando não há destaque
ou quando os pixels alterados estão fora da região do vértice.

Placement de um Part::Feature não equivale a cobertura completa de
montagens/App::Link. Modificadores de seleção, overrides secundários de
cores/mascaras e popup com candidatos obtidos por mouse real ainda
precisam de casos próprios, assim como comparação com Coin/GL.


Matriz repetida: `/tmp/freecad-elements-final/results.json`,
**8/8 PASS, 0 SKIP, 0 FAIL**, wrapper exit 0, GPU física, sem fallback:
Vulkan/OpenGL × object/weighted_oit × DPR 1/2. São nove combinações
câmera/elemento por processo, 72 verificações de hover/clique na matriz.
O teste registra a posição real do cursor e o SoLocation2Event recebido.

A primeira matriz, com etapas de 350ms, ficou em 7/8: a pré-seleção de uma
aresta transformada não correspondeu ao ray-picking em Vulkan weighted_oit
DPR 2. A repetição direcionada passou, e a matriz completa acima passou
com intervalos de 600ms (como o teste de faces anterior). Isso não prova
a causa daquela falha; seus logs foram preservados em /tmp/freecad-elements-matrix.
O wrapper Xwayland passou a usar Weston --no-input para impedir que o
compositor encaminhe movimento físico do desktop ao X11 privado; XTest
continua operando diretamente dentro da sessão isolada.


A opção --no-input foi validada separadamente em execução única:
`/tmp/freecad-elements-no-host-input-final/results.json`, **1/1 PASS**,
Vulkan weighted_oit DPR 2, GPU física e idle sem frames extras.
Uma tentativa anterior, concorrente com a matriz, acertou todos os picks mas
falhou no idle (164→168 frames); permanece registrada em
/tmp/freecad-elements-no-host-input. A repetição única não demonstra a causa
dos frames anteriores. A matriz 8/8 foi iniciada antes dessa opção do wrapper.
Testes Python finais: **34/34 PASS**. O patch comprimido de integração foi
atualizado a partir do baseline original, preservando as alterações já commitadas.


### App::Link e modificadores: cenário preparado, BGFX pendente

`freecad-mouse-links.FCMacro` usa duas instâncias App::Link da mesma
caixa Part (origem oculta, LinkPlacement de B deslocado 30 unidades).
O mouse deve identificar LinkA/LinkB, não Box. Ctrl adiciona B à seleção,
Ctrl em A remove A, e Shift em A substitui a seleção de B. Cada etapa exige
pixels apenas nas instâncias esperadas; o teste rejeita destaque vazando
para outra instância da malha compartilhada. Inclui limpeza e resize.

O modificador é injetado por XTest e sincronizado com X11/Qt, depois
confirmado no SoMouseButtonEvent recebido pelo Coin. A primeira versão do
teste enviava tecla/clique/liberação sem processar a tecla, e não adicionava
B. Após sincronização, CtrlDown/ShiftDown chegam corretamente. A API de
seleção só é usada para leitura e limpeza, nunca para criar a seleção.

A opção explícita `--reference-gl` produz **REFERENCE_PASS**, não PASS
BGFX, e proíbe --require-hardware. O gate normal rejeita referência GL,
mesmo com linha de submissão BGFX no log. O modo referência usa somente
object e DPR 1/2: weighted_oit do BGFX não é uma variante da referência GL.

Foi possível executar a referência com o build GL persistente antigo em
/mnt/Laranja/Git/externos/freecad-build e Xvfb (não GPU física):
DPR 1 passou em /tmp/freecad-links-gl-instance-isolation; DPR 2 caiu na
inicialização nesse run, mas passou em uma sessão nova em
/tmp/freecad-links-gl-instance-dpr2-retry. Os dois resultados são mantidos,
sem converter crash em aprovação. Todos os controles Python: **43/43 PASS**,
incluindo identidade de instância, vazamento de pixels e modificadores.

Os runtimes BGFX/FreeCAD/Weston de /tmp usados anteriormente foram removidos.
Nenhum outro build BGFX ou pacote bgfx instalado foi localizado. Portanto
não há aprovação BGFX nova para App::Link, nem comparação direta de imagem
Coin/GL × BGFX no mesmo build. O cenário está pronto para a matriz após
restaurar/recriar esses runtimes. Links aninhados, montagens, arestas e
vértices através de Link continuam fora deste cenário de faces.


### Retomada persistente após reboot

O ambiente foi reconstruído em `build-bgfx-recovery/` no Coin, sem instalar
bibliotecas globais. Configuração, revisões fixadas e scripts de repetição:
[bgfx-recovery.md](bgfx-recovery.md). As notas acima sobre perda de /tmp
descrevem o estado anterior à reconstrução.

App::Link passou em 8/8 combinações BGFX, com GPU física obrigatória:
Vulkan/OpenGL × object/weighted_oit × DPR 1/2. A referência Coin/GL do mesmo
build passou em 2/2. Ctrl/Shift são confirmados no evento nativo Coin;
instâncias da mesma malha não compartilham o destaque indevidamente.

A comparação visual detectou uma falha não coberta por esses gates:
o filho Xlib recobria a barra de abas do documento após resize em DPR 2.
O adaptador agora intersecta os limites/máscaras dos ancestrais e subtrai
as barras QTabBar visíveis, em pixels físicos, por XFixes ShapeBounding.
Câmera, tamanho do alvo e picking permanecem inalterados; a região é
atualizada somente quando muda, para não criar um ciclo de exposição.

O novo gate compara pixels da tela com pintura Qt independente. O caso
Vulkan weighted_oit DPR 2 passou com RGB MAE 0 na barra, seleção após
resize e idle estável. Falhas das primeiras versões do teste permanecem
em `artifacts/links-native-clip-probe*`; não contam como aprovação.
Controles Python: 60/60 PASS. O patch comprimido aplica ao baseline
FreeCAD `d8d85f05ff`, preservando o escopo de 31 arquivos.

Ainda não se declara paridade completa: links aninhados/montagens,
arestas/vértices através de Link e diferenças de iluminação precisam
de cenários adicionais.


A matriz final após o recorte passou **8/8 BGFX + 2/2 referência GL**
(`links-bgfx-clipped` e `links-gl-clipped`). Todas as quatro células DPR 2
tiveram 4494 amostras de chrome e RGB MAE 0. Nas 88 comparações finais,
o erro do baseline DPR 2 redimensionado caiu de ~111 para ~1,93 sem excluir
a barra da medição; IoU mínimo do destaque: 0,9944. Permanecem diferenças
de iluminação, sem alegar equivalência pixel a pixel.


### Links aninhados e especular: atualização

A cobertura nova e a correção do vetor especular estão descritas em
[bgfx-linked-topology-lighting.md](bgfx-linked-topology-lighting.md).
As pendências históricas de links acima não descrevem essa etapa nova.
O cenário usa caminhos completos, composição de placements, seleção de
faces/arestas/vértices em duas instâncias e repetição após resize.
A primeira correção alinhou o especular no BGFX, WGSL e CPU. A atualização
seguinte também alinhou a interpolação Gouraud do PHONG do Coin, com
oráculos de normais curvas, luzes pontuais/spot, clipping e materiais:
[bgfx-gouraud-parity.md](bgfx-gouraud-parity.md). Não é equivalência universal
pixel a pixel de toda cena.
