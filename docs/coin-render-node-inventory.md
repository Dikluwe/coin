# CoinRender — inventário de nós e workbenches (P15)

P15 fecha a descoberta e a classificação do checkout local; suporte funcional
F19/F20 e qualificação de interação P16 permanecem separados. Percorrer um nó,
aceitar uma cena ou gerar primitivas de picking não certifica o resultado GL.

## Texto e imagem implementados em Linux

Em 2026-10-05, os tipos nativos exatos `SoText2` e `SoImage` passaram a ter
captura explícita na camada comum. `CoinRenderAction` decide a captura depois
dos callbacks anteriores do usuário; subclasses conservam sua callback virtual.
O serviço privado de fontes do Coin fornece os mesmos glifos e medidas usados
pelo CoinGL. Nenhuma classe pública do Coin foi alterada. A ponte privada de
fontes só é compilada com `COIN_BUILD_RENDER`; a ABI C wgpu permanece na revisão 43.
CoinRender precisa da biblioteca Coin produzida por esse mesmo build, que inclui
o serviço privado de glifos; uma Coin anterior não oferece esses símbolos.

- `SoText2`: UTF8, fontes mono/gray, kerning, linhas, espaçamento, justificação,
  cor/alpha do material e máscara de cobertura. Glifos sem cobertura não
  escrevem profundidade; gray conserva o corte de alpha e o blend do CoinGL.
- `SoImage`: L/LA/RGB/RGBA, alinhamento, dimensões solicitadas, crop e zoom
  nearest dos pixels originais, preservando orientação e alpha da imagem.
- Os dois capturam coordenadas de tela na origem do objeto. Câmera, modelo e
  viewport são recalculados em cada captura; os atalhos de atualização de
  geometria em espaço de objeto ficam desativados em frames com esses nós.
- Composição comum distingue o alpha dos pixels da classificação transparente
  que determina a travessia diferida. BGFX e wgpu usam o transporte existente de
  geometria/textura; não há implementação de fontes específica por backend.

O gate `CoinRenderScreenContentTest --gpu` usa pixels do Coin/OpenGL como
oráculo, incluindo uma cópia da cena sem o conteúdo para impedir comparações
vazias. São 93 cenas por executor, com fontes `defaultFont`/DejaVu Sans,
âncoras fracionárias, nove alinhamentos, zoom anisotrópico, mutações, resize,
perspectiva, depth test/write, clip planes, fog, políticas de transparência,
callbacks externos e recuperação após entradas recusadas. Logs em
[validation/screen-raster-linux](validation/screen-raster-linux).
As 279 comparações GPU passaram, com erro RGB máximo de 1/255 e MAE máximo
de 0,366667 por canal na região avaliada. Também passaram 27 gates de regressão
de action, captura, composição, textura, multitextura, depth, reuse e RTT.
O snapshot de estado continua com 1.640 bytes neste build; as novas flags usam
o padding existente. A configuração `COIN_BUILD_RENDER=OFF` foi verificada
sem a ponte de fontes no grafo de build, sem uma compilação completa OFF.
Essa qualificação cobre Linux/NVIDIA, wgpu/Vulkan e BGFX/Vulkan/OpenGL;
não certifica Windows, outras GPUs ou todos os consumidores FreeCAD.

Limites explícitos: 4.096 glifos, 256 linhas, 1 MiB de strings, fonte até
1.024 pixels, 16 MiB de payload/layout capturado, texturas até 8.192 por eixo
e 65.536 runs de cobertura por nó. Frames recusados preservam o último frame
válido. Imagem/texto vazios são inertes.

`SoImage` com texturas herdadas ativas é recusado: o CoinGL aplica texturas
usando coordenadas raster persistentes que o estado da callback não fornece.
Texto desativa essas texturas no CoinGL. Nós de raster dentro de um
`SoShadowGroup` ativo e raster transparente interceptado pela ordenação de
triângulos também são recusados até existir um contrato comum para esses casos.
O fog usa a aproximação planar permitida pelo GL, qualificada no driver desta
campanha. A comparação de imagem mede RGB; não é uma certificação geral de
alpha de framebuffer/RTT. Os testes negativos não contam como paridade visual.

O restante deste inventário registra a descoberta histórica P15. As referências
a texto/imagem ausentes abaixo descrevem aquela base; os workbenches ainda
precisam de fixtures no host, mesmo com os dois nós implementados.

## Fontes e reprodução

FreeCAD base `228c679d78845c3fb6f5eb3d1a27f48aceab68b5`, com patches locais;
CoinRender base P14 `9b474f4639`. Foram examinados **5.123 arquivos** C++/headers/Python
em `src/Gui` e `src/Mod`: **33 classes, 47 overrides GL**, **33 criações especiais**
(19 SoText2, 13 SoImage, 1 SoTexture3) e 25 candidatos de mecanismos Qt/GL externos.
O hash do conteúdo auditado, incluindo patches locais, está no inventário; o SHA
Git sozinho não identifica esse conteúdo.

[Inventário com fontes/linhas/hashes](inventories/freecad-node-inventory.json) e
[revisão por classe: dono, motivo e fechamento seguinte](inventories/freecad-node-review.json).
Descoberta: [audit_freecad_nodes.py](../scripts/coinrender/audit_freecad_nodes.py).

```sh
python3 scripts/coinrender/audit_freecad_nodes.py /mnt/Laranja/Git/externos/freecad-source \
  --check docs/inventories/freecad-node-inventory.json
```

`--check` falha se conteúdo, revisão ou candidatos mudarem. Para atualizar, revisar
as diferenças e só depois gerar com `--output`; toda classe GL descoberta precisa
de revisão explícita, e uma revisão órfã também falha. O digest cobre todos os
arquivos no escopo, incluindo mudanças que o detector lexical não classifique.

A descoberta é lexical, não um parser C++ nem análise de binário: remove strings
e comentários, encontra definições qualificadas GLRender/BelowPath/InPath/OffPath,
callbacks/primitivas/doAction e criações C++/Pivy explícitas. Alias, macros,
definições inline sem qualificação, nós em arquivos Inventor e criação dinâmica
exigem inspeção complementar quando o escopo mudar. Terceiros, addons externos
instalados e workbenches fora destas fontes não são certificados. Candidatos
QOpenGLWidget não são automaticamente nós nem programas de shader.

## O que o Coin oferece e o que o GL faz

CoinRender deriva de SoCallbackAction: Wiring usa callbacks/primitivas e captura
SoState; Core recebe dados; Infra recebe planos. Não invoca o GLRender customizado
para extrair decisões de outro backend. Os atalhos de IndexedFaceSet/LineSet
limitam-se aos tipos exatos, preservando callbacks das subclasses FreeCAD.

No GL, um nó pode preparar geometria, rasterizar texto/imagem, executar shaders,
alterar seleção/cursor ou percorrer uma raiz privada durante GLRender. Essa
semântica só chega ao contrato comum se existir callback/estado equivalente ou
uma adaptação explícita no host. Não deve ser reinterpretada em CoinBgfx e CoinWgpu.

- **SoImage:** `src/shapenodes/SoImage.cpp`, generatePrimitives, publica imagem no
  SoMultiTextureImageElement e emite um quad em espaço de viewport. GLRender usa
  rasterização de imagem. O builder exige unidade habilitada; o SoImage isolado
  não a habilita. O ensaio confirma quad capturado sem textura, com SUCCESS.
  A geometria existente não encerra o contrato de imagem, alpha e alinhamento.
- **SoText2:** `src/shapenodes/SoText2.cpp`, generatePrimitives, é vazio por projeto;
  texto GL é rasterizado por outro caminho. O ensaio confirma SUCCESS sem draws.
- **SoShaderProgram/SoFragmentShader:** o GL instala/executa um programa próprio;
  o plano CoinRender não captura esse programa. P25 passou a rejeitar programas
  ativos antes da publicação; o contrato de shader portátil segue aberto.
- **Nós GL-only:** o ensaio sintético SoNode prova que a action pode aceitar um nó
  sem executar GLRender nem emitir draws. Não existe introspecção geral segura
  de overrides C++ para rejeitar automaticamente toda subclasse; declarar
  capacidade/adaptar nós conhecidos é responsabilidade da integração.
- **Subclasses de faces:** ensaio sintético preserva callback virtual e geometria
  com fast path ligado/desligado. Isso prova o mecanismo comum, sem substituir
  teste dos tipos FreeCAD concretos, com seus elementos e contexto de seleção.

## Matriz de classes

**Candidato**: fonte contém caminho plausível de captura; precisa de P16.
**Parcial**: geometria/estado parcial, preparação ou semântica GL pendente.
**Bloqueado**: o caminho visual identificado não chega ao callback equivalente.
**Host**: integração Qt/estado de host, sem geometria a portar. Totais: 12
candidatos, 14 parciais, 6 bloqueados e 1 host. Nenhuma destas marcas certifica
uma comparação visual FreeCAD/BGFX/wgpu. Fonte/linha referem-se ao checkout auditado.

| Classe | Estado | Fonte principal | Próximo fechamento |
| --- | --- | --- | --- |
| `SmSwitchboard` | Candidato | `src/Gui/Inventor/SmSwitchboard.cpp:156` | P16 |
| `So3DAnnotation` | Parcial | `src/Gui/Inventor/So3DAnnotation.cpp:161` | P16 |
| `SoAutoZoomTranslation` | Candidato | `src/Gui/Inventor/SoAutoZoomTranslation.cpp:89` | P16 |
| `SoBrepEdgeSet` | Parcial | `src/Mod/Part/Gui/SoBrepEdgeSet.cpp:323` | P16 |
| `SoBrepFaceSet` | Parcial | `src/Mod/Part/Gui/SoBrepFaceSet.cpp:692` | P16 |
| `SoBrepPointSet` | Parcial | `src/Mod/Part/Gui/SoBrepPointSet.cpp:205` | P16 |
| `SoDatumLabel` | Parcial | `src/Gui/SoDatumLabel.cpp:1661` | P03/F20 antes do P16 Sketcher/Measure |
| `SoDrawingGrid` | Parcial | `src/Gui/Inventor/SoDrawingGrid.cpp:149` | P16 |
| `SoFCBoundingBox` | Bloqueado | `src/Gui/Inventor/SoFCBoundingBox.cpp:129` | F19/P16 |
| `SoFCColorBar` | Parcial | `src/Gui/SoFCColorBar.cpp:522` | P03/P16 |
| `SoFCControlPoints` | Bloqueado | `src/Mod/Part/Gui/SoFCShapeObject.cpp:241` | F19/P16 |
| `SoFCIndexedFaceSet` | Parcial | `src/Mod/Mesh/Gui/SoFCIndexedFaceSet.cpp:487` | P16 |
| `SoFCMeshGridNode` | Bloqueado | `src/Mod/Mesh/Gui/SoFCMeshObject.cpp:445` | F19/P16 Mesh |
| `SoFCMeshObjectBoundary` | Candidato | `src/Mod/Mesh/Gui/SoFCMeshObject.cpp:1665` | P16 |
| `SoFCMeshObjectNode` | Candidato | `src/Mod/Mesh/Gui/SoFCMeshObject.cpp:544` | P16 |
| `SoFCMeshObjectShape` | Candidato | `src/Mod/Mesh/Gui/SoFCMeshObject.cpp:632` | P16 |
| `SoFCMeshSegmentShape` | Candidato | `src/Mod/Mesh/Gui/SoFCMeshObject.cpp:1256` | P16 |
| `SoFCPathAnnotation` | Parcial | `src/Gui/Selection/SoFCUnifiedSelection.cpp:2562` | P16 |
| `SoFCScreenSpaceGroup` | Candidato | `src/Gui/Inventor/SoFCScreenSpaceGroup.cpp:143` | P16 |
| `SoFCSelection` | Parcial | `src/Gui/Selection/SoFCSelection.cpp:586` | P16 |
| `SoFCSelectionRoot` | Parcial | `src/Gui/Selection/SoFCUnifiedSelection.cpp:2025` | P16 |
| `SoFCSeparator` | Candidato | `src/Gui/Selection/SoFCUnifiedSelection.cpp:1321` | P16 |
| `SoFCTransform` | Candidato | `src/Gui/Inventor/SoFCTransform.cpp:49` | P16 |
| `SoFCUnifiedSelection` | Parcial | `src/Gui/Selection/SoFCUnifiedSelection.cpp:1052` | P16 |
| `SoFrameLabel` | Bloqueado | `src/Gui/SoLabelNodes.cpp:502` | F20/P03 |
| `SoGLWidgetNode` | Host | `src/Gui/SoFCInteractiveElement.cpp:205` | P16 |
| `SoNaviCube` | Candidato | `src/Gui/Inventor/SoNaviCube.cpp:1439` | P16 |
| `SoPolygon` | Bloqueado | `src/Mod/Mesh/Gui/SoPolygon.cpp:71` | F19/P16 |
| `SoScreenSpaceScale` | Candidato | `src/Mod/Measure/Gui/SoScreenSpaceScale.cpp:81` | P16 |
| `SoShapeScale` | Parcial | `src/Gui/Inventor/SoAxisCrossKit.cpp:83` | P16 |
| `SoStringLabel` | Bloqueado | `src/Gui/SoLabelNodes.cpp:171` | F20/P03 |
| `SoTransformDragger` | Parcial | `src/Gui/Inventor/Draggers/SoTransformDragger.cpp:527` | P16 |
| `SoZoomTranslation` | Candidato | `src/Mod/Sketcher/Gui/SoZoomTranslation.cpp:83` | P16 |

### Diferenças que mudam a prioridade

- Part/BRep e seleção já têm callbacks locais para materiais, contextos e
  primitivas. SoFCSelectionRoot/SoFCSelection, edges e points não são simplesmente
  GL-only. Clarificação, hover, delayed paths e depth ainda exigem P16.
- SoNaviCube tem generatePrimitives vazio, mas callback chama renderCoin, que
  prepara e atravessa callbackRoot com viewport/estado próprios. SoFCScreenSpaceGroup
  também prepara estado no callback: ambos são candidatos a ensaio real.
- So3DAnnotation/SoDrawingGrid/SoFCPathAnnotation têm adaptação de camadas sob
  FREECAD_COIN_WGPU_EXPERIMENTAL, usando o nome legado SoWgpuRenderAction.
  Neste Coin ele encaminha CoinRender; o nome da flag não prova exclusividade
  wgpu. Conferir a definição e o pacote efetivamente ligado em cada build P16;
  não criar uma segunda interpretação de camadas para BGFX.
- SoStringLabel percorre textRoot privado só no GLRender; SoFrameLabel prepara
  imagem ali. SoDatumLabel possui primitivas dependentes de imgWidth/imgHeight e
  preparação raster/cena GL. Um frame aquecido pelo GL pode esconder lacunas.
- SoFCBoundingBox/SoFCControlPoints/SoPolygon têm primitivas vazias; o GL prepara
  geometria ou percorre raiz privada. SoFCMeshGridNode usa glBegin(GL_LINES).
- SoFCColorBar retém geometria e SoText2, mas prepareViewport roda no GLBelowPath.
  SoShapeScale atualiza escala no GL e doAction; o callback herdado SoBaseKit
  chama SoBaseKit::doAction qualificado. SoTransformDragger prepara cache/escala
  no GL. Travessia de descendentes não garante atualização de câmera/resize.
- MeshObjectNode habilita SoFCMeshObjectElement para SoCallbackAction; shapes e
  segmentos emitem primitivas. Verificar bindings, boundary e renderTriangleLimit.
  SoFCIndexedFaceSet tem seleção GL/doAction de ações específicas, não um callback
  próprio equivalente para toda a seleção.
- SoFCUnifiedSelection também altera cursor/bounding-box no GL; SoGLWidgetNode
  transporta QOpenGLWidget por SoGLWidgetElement. Isso pertence ao host, não à GPU.

## Consumidores e casos para P16

| Host/workbench | Caso concreto e evidência | Estado e dependência |
| --- | --- | --- |
| GUI/Part | Faces/edges/points BRep, selection root, hover/clarificação | Captura parcial existente; qualificar seleção, paths, depth e resize. Primeiro caso P16. |
| Mesh | Objeto/segmentos/boundary e SoFCIndexedFaceSet; grid Mesh | Geometria candidata; seleção parcial e grid GL-only. Testar abaixo/acima do renderTriangleLimit. |
| Sketcher | EditModeConstraintCoinManager cria 7 SoImage; EditModeCoinManager e InformationOverlayCoinConverter criam SoText2; SoDatumLabel/SoZoomTranslation | Ícones, texto e rótulos bloqueiam equivalência de edição; transformação candidata. |
| Draft | view_text/view_label/view_dimension: 4 criações Pivy SoText2 | Texto ausente; fechar P03 antes de certificar estes casos. |
| BIM | ArchSite: 2 criações Pivy SoText2 | Texto ausente; SoShadowGroup comentado não é criação ativa. |
| Fem | CreateLabels: SoText2 e SoImage; color bar GUI compartilhada | Texto/imagem e atualização de viewport pendentes. |
| Measure | MassPropertiesResult: SoImage; SoScreenSpaceScale; labels compartilhados | Escala candidata, ícone/rótulo sem qualificação. |
| PartDesign | ViewProviderDatumCS: 3 SoText2 | Eixos geométricos podem capturar; legendas precisam P03. |
| Viewer/GUI | NaviCube, axis cross (SoText2), draggers/FrameLabel; ViewProviderAnnotation (SoText2/SoImage), NavlibPivot, ManualAlignment | NaviCube candidato; texto, imagem e preparação fria dos rótulos pendentes. |
| Texturas de faces | ViewProviderTextureExtension cria SoTexture3 | Fora do contrato de textura 2D; verificar/rejeitar volume e coordenadas antes de declarar suporte. |
| CAM | PathSimulator/AppGL/DlgCAMSimulator + Shader.cpp: glCreateShader/glUseProgram | Widget GL independente do grafo Coin; trocar CoinRender não migra o simulador. Integração/Infra própria se requerida. |
| TechDraw/Quarter/Qt | QOpenGLWidget em QGVPage e componentes GUI | Uso de widget não prova nó GL-only ou shader customizado. Ensaiar host no P16. |

Não foram encontradas criações explícitas ativas de SoShaderProgram, vertex/fragment/
geometry shader Coin, cube maps ou SoShadowGroup no escopo. Isso não certifica
addons, arquivos de cena ou importadores dinâmicos. O shader CAM é um caso real
separado; o contrato Coin de shader também permanece aberto por si só.

## Checklist dos fechamentos derivados

- [ ] **I01 — Texto viewport (P03/F05):** layout, fonte, âncora, tamanho, clipping,
  alpha/camadas e atlas; priorizar Draft/Sketcher/PartDesign/Fem/BIM. Wiring captura,
  Core decide o layout/composição, Infra executa atlas/quad.
- [ ] **I02 — Imagem viewport (F20):** capturar pixels/alpha/UV/DECAL e alinhamento
  de SoImage em um contrato comum; qualificar orientação, tamanho, recorte e
  estado anterior. Não corrigir somente shaders de um backend.
- [ ] **I03 — Preparação fria de rótulos:** StringLabel/FrameLabel/DatumLabel/color
  bar expõem/atualizam geometria comum pelo callback, sem exigir GL prévio.
- [ ] **I04 — Geometria GL-only (F19):** bbox, control points, Polygon e MeshGrid
  publicam linhas/faces/raiz retida na Wiring; Core aplica geometria/estilo comuns.
- [ ] **I05 — Seleção e camadas (P16/F01/F18):** BRep, roots, paths/clarificação,
  hover/depth, overlays e gates de build; diagnosticar o comportamento por caso.
  Part/BRep, grade, on-top, links/arrays e documentos estão qualificados no
  [perfil P16](coin-render-freecad-viewport.md); clarificação e outros consumidores
  permanecem nesta frente ampliada.
- [ ] **I06 — Kits/draggers/viewport (P16):** atualização por callback de escala,
  cache, resize e câmera; picking/manipulação continuam do Coin/host.
- [ ] **I07 — Shaders Coin (F20):** inventariar consumidores reais adicionais,
  declarar capacidade/diagnóstico e contrato de programa/parâmetros antes da
  implementação específica em BGFX/wgpu; programa GL não é plano portátil.
- [ ] **I08 — Volume/cube maps/sombras (F20):** SoTexture3 tem consumidor real;
  conferir formatos/dimensões e política comum, com rejeição explícita onde faltar
  suporte. Cube maps/sombras precisam ampliar a descoberta de consumidores.

Estas pendências funcionais não são P15 incompleto: P15 identifica o que precisa
ser fechado e seus donos. P16 deve declarar casos utilizáveis e seus bloqueios,
sem certificar uma workbench inteira pela presença de triângulos básicos.

## Evidência e limites

- [x] Todas as 33 classes descobertas têm revisão, dono e fechamento seguinte.
- [x] Métodos GL por paths incluídos; ausência de callback separada de herança.
- [x] SoImage/SoText2/SoTexture3 e usos Pivy vinculados a consumidores concretos.
- [x] Shader/widget CAM separado de nós/shaders Coin e de contexto Quarter/Qt.
- [x] SoImage isolado, SoText2, GL-only e shader caracterizados sem GPU; callbacks
  de subclasse ensaiados com atalhos ligados/desligados.
- [x] Descoberta verifica comentários/strings, revisão nova/órfã, mudanças de
  conteúdo e distinção de shader externo; 3 casos Python aprovados.
- [x] Caracterização C++ aprovada nas builds CPU C++11, BGFX C++11 e wgpu Debug.
- [ ] Qualificação visual/interativa FreeCAD de cada caso: P16.

[CoinRenderNodeInventoryTest](../testsuite/coinrender/CoinRenderNodeInventoryTest.cpp)
é caracterização dos bloqueios atuais; SUCCESS nos casos ainda omitidos não
significa suporte. P25 acrescentou rejeição explícita para programa ativo,
textura 3D, cube map, cube RTT e ShadowGroup ativo, sem submissão. Usa um conector testemunha para observar o plano e não inicializa GPU.
[test_node_inventory.py](../testsuite/coinrender/test_node_inventory.py) testa o
inventário em fontes sintéticas, sem depender de FreeCAD instalado no CI.
Não houve alteração de renderização em produção nem edição das fontes FreeCAD.

Validação final: **5/5 em cada build** (CPU, BGFX e wgpu), total 15 casos CTest
aprovados, incluindo descoberta, caracterização, publicação, reuso e diagnósticos.
O teste Python contém três cenários. `--check` reproduziu o inventário sem
candidatos não revisados ou revisões órfãs. Logs locais em
`/tmp/coin-p15-recording-tests.log`, `/tmp/coin-p15-bgfx-tests.log` e
`/tmp/coin-p15-wgpu-tests.log`.

O P16 atualizou a identidade do inventário após neutralizar a integração
Quarter/CMake para CoinRender. As 33 classes, 47 overrides e classificações
permanecem iguais; a qualificação Part/BRep está registrada separadamente no
[contrato P16](coin-render-freecad-viewport.md).
