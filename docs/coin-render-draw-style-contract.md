# CoinRender: contrato e fechamento de SoDrawStyle

P02/F04 continua aberto. Este registro distingue o fechamento de `INVISIBLE`
da conversão de polígonos para `LINES` e `POINTS`, ainda pendente.

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

## Checklist restante de P02

- [x] INVISIBLE com estado efetivo Coin e supressão comum de captura.
- [x] Publicação e invalidação de cache verificadas nos dois backends.
- [ ] Capturar contornos originais para faces, quads e faixas, inclusive shapes
  procedurais; declarar o comportamento de shapes customizados.
- [ ] Resolver LINES/POINTS no Core sobre esses contornos, preservando bordas,
  atributos, materiais e multiplicidade de rasterização entre polígonos.
- [ ] Aplicar culling e clipping de polígonos, incluindo bordas novas de corte,
  antes da expansão de largura/tamanho na geometria comum.
- [ ] Preservar iluminação por vértice, fog, alpha, depth e polygon offset.
- [ ] Validar tamanho, largura e padrão; integrar texturas conforme P07/P08,
  mantendo explícito o limite atual de strokes texturizados no wgpu.
- [ ] Matriz por shape em CPU/BGFX/wgpu e comparação Coin/GL disponível.

O suporte existente de LineSet/PointSet não comprova LINES/POINTS aplicados
sobre polígonos. Não anunciar P02/F04 como fechado por esta entrega.
