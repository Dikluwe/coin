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
inclinação usa a ABI privada Rust 25, descrita abaixo.

A expansão existente aplica largura/tamanho, preservando depth e alpha no
perfil validado. Linhas/pontos nativos mantêm sua topologia sob LINES/POINTS.
Notificações do Coin invalidam o plano; o reuso de câmera/material existente
não aceita strokes preparados em coordenadas de tela.

### Limites explícitos

- Contornos côncavos, coordenadas repetidas ambíguas, montagem incompleta ou
  atributos inconsistentes retornam UNSUPPORTED. Esses casos não geram uma
  aproximação com diagonais. A triangulação de faces côncavas do GL exige estudo
  próprio para definir bordas efetivamente rasterizadas em cada shape.
- Padrão de linha de 16 bits tem fase contínua por polígono, com repetição
  limitada a 1..256 e contagem por fragmentos unitários. A primeira aresta
  segue o perfil de montagem documentado abaixo; bordas e recortes gerais ainda
  precisam de qualificação de raster ampliada.
- Offset por inclinação está implementado para contornos planos com área
  projetada não zero. No wgpu, inclinação e units são combinados na Infra usando a precisão
  D32Float do maior depth original, preservado antes do padrão; FILLED conserva o bias nativo.
  Precisão de units e qualificação de depth/raster continuam vinculadas a
  P04/F12. Faces não planas e faces de lado com factor ativo têm diagnóstico
  explícito; com factor zero, conservam o perfil anterior.
- Textura na unidade 0 com UV explícito, quatro modelos e fog por fragmento
  estão implementados em ambos os backends. Multitextura no wgpu e UV
  procedural/default permanecem no escopo de P07/P08. A matriz de
  transparência e de outras plataformas ainda exige qualificação.
- Não declarar equivalência visual Coin/GL, cobertura de subclasses/custom
  GLRender, todas as modalidades de transparência ou qualificação FreeCAD.
  A referência Coin/GL foi recuperada posteriormente com Mesa/GLX direto.
  As matrizes de amostras qualificadas estão registradas abaixo.

### Evidência desta ampliação

O mesmo CoinRenderDrawStyleTest agora verifica os contornos de IndexedFaceSet,
FaceSet, triângulos, pentágonos convexos, Cube, Sphere, Cylinder e Cone; bordas
sem diagonais, quatro pontos por quad, culling, espelhamento, cortes por planos
de usuário e volume de visão, passagem pelo near plane, interpolação de UV/alpha
e iluminação anterior ao clipping. Testes de pixels com fast path ligado e
desligado cobrem LINES/POINTS, BASE_COLOR/PHONG, cantos e bordas de corte, ausência
de diagonal e depth contra um fundo preenchido que também é desenhado
depois dos strokes. A rejeição de contornos fora do perfil preserva
imagem/serial e o próximo pedido válido recupera a publicação.

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
**25**, com vértice de **44 bytes**, estado de **1096 bytes** e draw de
56 bytes. A revisão 25 acrescenta o máximo de depth da face original. O valor zero dos novos campos conserva a geometria comum de clientes
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

## Padrão contínuo e matriz explícita de contornos (2026-09-28)

`CoinRenderLineStippleCore.h` possui a contagem mecânica do padrão: fragmentos
unitários pelo teste de saída do diamante, bits menos significativos primeiro,
repetição 1..256 e fase contínua entre as arestas. `CoinRenderStrokeCore.h`
expande apenas os intervalos visíveis; largura replica o bit no eixo menor.
O contador reinicia em cada contorno original, mesmo quando dois polígonos
compartilham estado. O perfil atual limita cada segmento a 65.536 células e
retorna diagnóstico quando o orçamento é ultrapassado. A continuidade de linhas
nativas em P08 foi ampliada no perfil descrito abaixo.

O Coin envia faces de três/quatro vértices como TRIANGLES/QUADS e faces maiores
como POLYGON. A referência Mesa consultada começa pela aresta de fechamento nos
primeiros e pela primeira aresta fornecida em POLYGON. O Core conserva essa
ordem no perfil comum; a associação com a convenção de provoking vertex é uma
inferência da montagem e da referência, não uma promessa para todos os drivers.
Fontes: `SoFaceSet.cpp`, `SoIndexedFaceSet.cpp`, `SoGL.cpp` e
[OpenGL 2.1, §§2.6.1, 3.4 e 3.5.4](https://registry.khronos.org/OpenGL/specs/gl/glspec21.pdf).

O máximo de profundidade da face recortada é calculado antes da máscara.
Isso evita alterar o quantum D32Float quando o padrão elimina o vértice mais
profundo. Na ABI privada 25, `polygon_offset_max_depth_bits` ocupa offset 1092:
zero significa ausência; os demais valores transportam os bits IEEE-754 + 1.
O transporte preserva exatamente o expoente perto de potências de dois.
Core fornece a medida geométrica; Infra wgpu decide o quantum do formato.
Offsets anteriores de clipping e bias, vértices e draws permanecem iguais.

| Entrada | Resultado de LINES/POINTS no perfil | Evidência |
|---|---|---|
| FaceSet/IndexedFaceSet convexos com detalhe recuperável | Contorno original, sem diagonais internas | Captura, Core, CPU, GPU; padrão GL em triângulos/quads/pentágonos |
| Cube/Sphere/Cylinder/Cone de tipo exato | Montagem conhecida dos callbacks | Captura e multiplicidade dos contornos |
| Subclasse sem detalhe de face recuperável | UNSUPPORTED | Action CPU/GPU preserva imagem e serial; FILLED/INVISIBLE recuperam |
| Contorno côncavo | UNSUPPORTED | Core e action CPU/GPU preservam publicação |
| Montagem incompleta ou atributos inconsistentes | UNSUPPORTED | Validação do builder; sem aproximação por triangulação |
| Face não plana ou de área projetada zero com factor ativo | UNSUPPORTED | Core e publicação; factor zero conserva o perfil existente |

A matriz de padrões verifica duas faces consecutivas, três quantidades de
vértices, faces indexadas/não indexadas, fast path ligado/desligado, larguras
1/3/6, repetição 1/2/256 e máscaras 0/ffff/000f/aaaa/9249. Amostras interiores
evitam as variações de cobertura de cantos permitidas pelo raster GL. O teste
numérico independente cobre também diagonal, terminal aberto, repetição
acima do limite e orçamento. A comparação GL obrigatória usa o comando acima.
Essas amostras não encerram raster de todas as bordas, recortes fora da matriz
ampliada abaixo, variações de primitivas procedurais, outros drivers ou FreeCAD.

A rodada final passou **19/19 CTests wgpu**, **28/28 CTests BGFX** e
**11/11 testes Rust/WGSL**, com execução GPU serial entre os perfis. A matriz
obrigatória Coin/GL passou com referência CPU e wgpu/Vulkan, BGFX/Vulkan e
BGFX/OpenGL. Logs locais: `/tmp/coin-stipple-wgpu-ctest.log`,
`/tmp/coin-stipple-bgfx-ctest.log`, `/tmp/coin-stipple-rust.log`,
`/tmp/coin-stipple-wgpu-gl.log`, `/tmp/coin-stipple-bgfx-gl.log` e
`/tmp/coin-stipple-bgfx-opengl-gl.log`.

## Recortes com padrão e cobertura dos extremos (2026-09-28)

O teste dos cantos encontrou perda de fragmentos iniciais: o intervalo visível
começava exatamente no extremo da linha, que podia ficar na borda excluída de
um triângulo expandido. O Core agora cobre a célula de raster inteira. A expansão
divide o intervalo nos extremos originais: o corpo conserva sua interpolação
homogênea, e as pequenas extensões conservam W, UV, material/alpha, fog e depth
do extremo. A referência numérica verifica esses atributos com W=1,25 e 1,75.
As arestas internas dos quads têm cobertura de um só triângulo; os testes de
alpha detectam dupla aplicação da cor. A decisão continua exclusivamente no
Core, sem interpretar elementos Coin dentro de BGFX/wgpu. A ABI permanece 25.

A nova matriz usa quads planos 64×64, fast path ligado/desligado, larguras 1/3/6,
repetição 1/2/5 e máscaras 0/ffff/000f/aaaa/9249. Ela inclui quatro cortes
laterais por SoClipPlane, dois planos simultâneos e cortes near/far ortográficos.
Cada configuração roda opaca e com alpha 50% em BLEND, DELAYED_BLEND e
SORTED_OBJECT_BLEND. Expectativas independentes verificam fase contínua,
extremos de cada aresta unitária e source-over único. Recorte total publica
um novo clear e o pedido seguinte recupera a geometria. Os modos ensaiados têm
uma face por cena; isso não qualifica ordenação entre objetos sobrepostos.
Near/far usa LEQUAL para tornar visível a borda situada exatamente no far plane.

CoinRender define uma fase determinística a partir do contorno recortado. GL
permite fase inicial indeterminada em segmentos recortados (§3.4); a referência
Mesa medida apresentou deslocamentos diferentes entre arestas. A comparação
qualifica periodicidade nas arestas preservadas com um deslocamento por aresta
que deve explicar todos os padrões, repetições, larguras e modos da matriz.
A comparação exata de padrões sem recorte permanece obrigatória. Com máscara
sólida/vazia, a presença de fragmentos nas arestas preservadas e nas bordas
criadas por planos de usuário é comparada diretamente. Não se exige fase idêntica nos novos cortes.

A referência Mesa desta execução também omitiu a nova borda near/far em
amostras onde o Core, CPU e GPU a produzem. A especificação determina que bordas
novas do polígono recortado sejam marcadas como boundary (§2.12). CoinRender
preserva esse comportamento; a causa da divergência GL local não foi determinada.
O teste registra o número de amostras diferentes nas bordas de profundidade,
sem contabilizá-las como equivalência visual GL. Não existe SKIP quando a
referência obrigatória não renderiza. Fonte:
[OpenGL 2.1, §§2.12, 3.4 e 3.5.4](https://registry.khronos.org/OpenGL/specs/gl/glspec21.pdf).

Esse fechamento cobre fase determinística e extremos em recortes do perfil
descrito. Raster de diagonais/cantos fracionários, interpolação fora do intervalo
original, viewport/scissor externos, MSAA e drivers físicos continuam em P04/P08.
Não declarar comparação pixel a pixel de todos os recortes com Coin/GL.

Nesta ampliação, passaram **19/19 CTests wgpu** e **28/28 CTests BGFX**.
O teste de estilo final passou com referência CPU e GPU em wgpu/Vulkan,
BGFX/Vulkan e BGFX/OpenGL. A referência GL obrigatória qualificou as comparações
descritas acima; diferenças de fase e bordas near/far não foram contadas como
igualdade pixel a pixel. Logs: `/tmp/coin-clip-stipple-wgpu-ctest.log`,
`/tmp/coin-clip-stipple-bgfx-ctest.log`, `/tmp/coin-clip-stipple-wgpu-gl.log`,
`/tmp/coin-clip-stipple-bgfx-gl.log`, `/tmp/coin-clip-stipple-bgfx-opengl-gl.log`
e `/tmp/coin-clip-stipple-wgpu-final-test.log`.

## Continuidade de linhas nativas em P08 (2026-09-28)

- [x] Contador de fragmentos único para linhas nativas e contornos de polígonos.
- [x] Continuidade de SoLineSet/SoIndexedLineSet e reset por polilinha/nó.
- [x] Reset por segmento com material PER_PART/PER_PART_INDEXED ou normais
  por segmento quando PHONG usa normais fornecidas.
- [x] Continuidade entre packets com materiais diferentes, vértices repetidos
  e polilinhas com um extremo compartilhado.
- [ ] Qualificação ampliada de diagonais, extremos fracionários, alpha,
  clipping de polilinhas, geometria ampliada/vertex arrays GL, MSAA e drivers físicos.

Wiring captura os limites a partir de SoLineDetail e dos bindings efetivos,
seguindo SoLineSet.cpp e SoIndexedLineSet.cpp. Normais por segmento não alteram
a montagem GL quando o modelo é BASE_COLOR ou não há normais fornecidas.
Uma identidade temporária de strip permite ao Core conservar a fase mesmo
quando uma troca de material divide a captura em vários draws. O Core consome
e remove essa identidade antes da submissão: não há mudança na ABI GPU 25.
O Core conta fragmentos pelo mesmo teste de saída do diamante já usado nos
contornos; não resta um algoritmo de comprimento euclidiano por backend.

O caminho rápido IndexedLineSet com padrão usa fallback para os callbacks do
Coin: o payload direto atual contém pares sem limites de polilinha. Linhas
sólidas conservam o caminho direto. Customizações/subclasses e primitivas sem
identificação nativa continuam como segmentos independentes; a continuidade
dessas extensões não está qualificada. Não inferir ligação pela igualdade das
coordenadas. Em recortes, o perfil continua contando os fragmentos visíveis;
equivalência de fase com todos os drivers GL não foi estabelecida.

A matriz usa os dois tipos de nó, uma ou duas instâncias, callbacks e fast path,
oito combinações de bindings/normais/iluminação, larguras 1/3, repetições
1/2/5/256 e cinco máscaras. Amostras internas têm expectativas independentes
de fase e comparação obrigatória com Coin/GL Mesa/llvmpipe. As cores diferentes
testam a continuidade entre packets; esse teste não qualifica toda a
interpolação de cor/alpha. A cena tem sete coordenadas; a referência GL com
vertex arrays em cargas maiores não foi qualificada. O helper mantém a verificação existente de limite
256 para repetição fora do intervalo. Esse fechamento é parcial de P08/F08.

Validação final: **19/19 CTests wgpu** e **28/28 CTests BGFX**, além da
referência GL obrigatória CPU/GPU nos perfis wgpu, BGFX Vulkan e BGFX OpenGL.
Logs: `/tmp/coin-native-stipple-wgpu-ctest.log`,
`/tmp/coin-native-stipple-bgfx-ctest-final.log`,
`/tmp/coin-native-stipple-wgpu-gl.log`, `/tmp/coin-native-stipple-bgfx-gl.log`
e `/tmp/coin-native-stipple-bgfx-opengl-gl.log`.
A primeira suíte BGFX registrou uma falha em `Device-loss setup failed` de
CoinBgfxReadbackModes_opengl; o teste passou isoladamente e na repetição completa,
sem alteração de código. A causa não foi determinada. Evidências preservadas em
`/tmp/coin-native-stipple-bgfx-ctest.log` e
`/tmp/coin-native-stipple-bgfx-readback-recheck.log`. As diferenças de clipping
de polígonos previamente documentadas continuam fora da equivalência pixel a pixel.

## Checklist de P02

- [x] INVISIBLE com estado efetivo Coin e supressão comum de captura.
- [x] Publicação e invalidação de cache verificadas nos dois backends.
- [x] Contornos de faces recuperáveis e tipos procedurais exatos documentados.
- [x] LINES/POINTS no Core, sem diagonais de quads ou duplicação por triangulação.
- [x] Culling e clipping de contornos convexos, com bordas e pontos novos de corte.
- [x] Gouraud antes de clipping, com UV/alpha interpolados e depth no perfil.
- [x] Matriz explícita de contornos fora do perfil e subclasses sem detalhe recuperável; rejeição não equivale a suporte.
- [x] Offset pelo gradiente da face plana original em Core, BGFX e wgpu.
- [x] Padrão contínuo entre arestas, contagem por fragmentos e reinício por polígono no perfil documentado.
- [x] Recortes com padrão e cobertura dos extremos em CPU/BGFX/wgpu; alpha simples e publicação vazia no perfil documentado.
- [x] Inclinação + units fracionário no wgpu/D32Float, com readback numérico.
- [ ] Offset fora do perfil plano e qualificação ampliada de precisão P04/F12.
- [x] Fog por fragmento e textura explícita na unidade 0 em strokes wgpu/BGFX.
- [x] Multitextura/combine e raster ampliado no [perfil P08](coin-render-multitexture-contract.md).
- [ ] UV procedural/default, formatos/qualidade e matriz ampliada P07.
- [x] Amostras de textura/fog e padrão contínuo comparadas com Coin/GL Mesa/llvmpipe.
- [ ] Sorting/modalidades de transparência, drivers/MSAA e integração FreeCAD.

O suporte de LineSet/PointSet continua distinto do estilo aplicado a polígonos.
P02/F04 não deve ser marcado como integralmente fechado por este perfil.

## P08 fechado no perfil documentado

A ABI privada atual é 26 (vértice 100 bytes, estado 2280 bytes); os relatos
de ABI 25 acima registram entregas anteriores. Multitextura/combine, raster
aliased diagonal/fracionário e alpha/clipping estão qualificados no
[contrato P08](coin-render-multitexture-contract.md). P02 permanece aberto
para os limites e a qualificação ampliada registrados nesta checklist.

## Ampliação de geometria/viewport em 2026-10-06

Veja o [perfil P02/P04/P05/P06](coin-render-geometry-viewport-contract.md):
viewport externo/vazio no Core, correção de bindings de normais e fixtures
compartilhadas com resize, alpha, luzes/fog e estilos com UV procedural.
Os limites de depth/offset e a matriz completa por shape continuam abertos.

## Ampliação portátil de geometria/viewport

O [perfil de 2026-10-06](coin-render-geometry-viewport-contract.md#ampliação-portátil-de-fechamento--2026-10-06)
amplia LINES/POINTS com bindings, alpha, UV procedural, clipping e viewport
externo. Para LineSet/IndexedLineSet em POINTS, segmentos independentes emitem
os dois endpoints; strips emitem o ponto compartilhado uma vez por ocorrência.
A referência CPU usa cobertura com 1/256 de pixel. Strokes com cache estável
recebem digest dos atributos após expansão, incluindo fog e posições, e não
reutilizam o identificador de conteúdo indexado anterior.

As junções nativas curvas/coincidentes e os endpoints compartilhados transparentes
são [estudo de melhoria futura](coin-render-raster-junctions-study.md). Os gates
portáteis não relaxam tolerâncias; referências CoinGL permanecem nas matrizes
independentes qualificadas. A escolha não encerra concavidade, offset não planar,
subclasses adicionais ou a matriz de todos os cruzamentos de bindings.
