# CoinRender: geometria e viewport — P02/P04/P05/P06

Rodada de 2026-10-06 em `codex/coin-render`, após P07 (`1158dd7c1f`).
Este documento conserva a rodada inicial e a ampliação portátil abaixo.
A matriz universal por shape, estado, plataforma e driver permanece aberta.

## Viewport externo no Core — P04

O retângulo Coin mantém origem inferior esquerda e dimensões positivas. A
interseção com o alvo limita viewport/scissor do executor, sem redimensionar
a projeção original. `CoinRenderTransformCore::clippedViewportTransform`
compensa a projeção para executores cujo viewport precisa estar dentro do alvo.
Para o intervalo original `(x,w)` e interseção `(c,d)`, o NDC compensado usa
`scale=w/d` e `translation=(2*(x-c)+w)/d-1`. O mesmo vale para Y; Z/W permanecem
inalterados. BGFX conserva seu mapeamento para o alvo inteiro com scissor comum.

Wgpu usa essa transformação em todos os caminhos de packing: estados normais,
batching tardio/antecipado/incremental, instancing e patches de câmera. A
conversão de profundidade Coin `[-1,1]` para wgpu `[0,1]` ocorre uma única vez.
Luzes, normais, fog e coordenadas de textura mantêm seus espaços originais.
O snapshot Coin e o FramePlan não são modificados.

Uma interseção vazia é uma submissão válida que mantém o clear do frame e não
emite geometria nem clear de profundidade da annotation. O protocolo privado
Rust é **46**, com os mesmos tamanhos/offsets de structs. O campo viewport já
existente transporta `[0,0,0,1]` para a interseção vazia; o valor legado
`[0,0,0,0]` continua significando alvo inteiro. A revisão impede misturar essa
codificação com uma bridge antiga. A bridge continua rejeitando retângulos
externos brutos: é responsabilidade do Core convertê-los antes do transporte.

O preflight RTT wgpu deixa de rejeitar a origem/extensão externa. Cada produtor
é convertido usando seu próprio extent. O gate de ownership verifica aceitação
nos modos staged/direto; isso não qualifica todas as combinações de viewport
externo com formatos, transparência ou sombras em RTT.

## Bindings de normais — P05

A nova fixture revelou dois erros no fast path de `SoIndexedFaceSet`:

- `PER_FACE_INDEXED`/`PER_PART_INDEXED` não selecionavam a normal indexada.
- `PER_FACE`/`PER_PART` e `PER_VERTEX` podiam consumir `normalIndex`, embora
  esses bindings usem a ordem das faces/ocorrências.

O Core distingue agora bindings indexados e não indexados; `PER_VERTEX` usa
ocorrências, inclusive quando a ordem das coordenadas difere da ordem de
emissão. Normais indexadas ausentes/fora do array retornam fallback atômico
para a geração nativa Coin. A geração por crease angle mantém seu caminho
indexado explícito. Materiais, alpha e índices de textura não são alterados.

`CoinRenderIndexedGeometryCoreTest` verifica vetores esperados independentes,
ordem reversa, índices ignorados/aplicados e fallback sem geometria parcial.

## Fixtures compartilhadas

`CoinRenderGeometryViewportTest` executa **214 cenas por backend**:

- 118 cenas de retângulos nas quatro bordas, cantos e completamente externos,
  resize 64 → 80 → 64, estilos FILLED/LINES/POINTS/INVISIBLE e restauração.
- 80 cenas de FaceSet/IndexedFaceSet, bindings OVERALL/PER_FACE/
  PER_FACE_INDEXED/PER_VERTEX/PER_VERTEX_INDEXED, normais fornecidas/geradas,
  alpha heterogêneo e fast path ligado/desligado.
- 16 cenas com luzes directional/point/spot simultâneas, fog NONE/HAZE/FOG/SMOKE,
  range `[.2,.8]`, viewport normal/externo e fast path ligado/desligado.

A execução GPU compara CPU/GPU/CoinGL em cada cena. Nos interiores de faces,
compara também pixels sem cobertura, para detectar deslocamento/geometria
faltante; exclui somente a faixa de um pixel das bordas de raster. Nos strokes,
compara pixels comuns com cobertura e exige pelo menos quatro amostras.
O limite é quatro níveis RGB, sem ampliar tolerâncias após falhas. Interseções
vazias/INVISIBLE exigem ausência integral de cor nos três renderizadores.
Isso não certifica equivalência de cobertura em todas as junções de strokes.

O renderer offscreen nativo reposiciona o viewport antes da travessia. Uma
fixture de estado instala o retângulo antes da câmera para comparar a mesma
entrada em CoinGL, CPU, BGFX e wgpu, preservando o tamanho integral do alvo.

`CoinRenderCompositionTest --annotations` em CPU/wgpu verifica barreira de depth no viewport
interno, parcialmente externo e vazio, restauração e rejeição sem publicação.
O gate FFI reconstrói numericamente o centro original após batching/resize.
O teste Rust verifica full-target legado, interseção vazia e limites inválidos.
Os gates existentes de clipping, offset, Gouraud, fog, múltiplas regiões,
shadows, RTT, falha/recuperação continuam na campanha integrada.

## UV procedural e estilos — P02/P07

A fixture P07 adiciona 80 cenas CPU de LINES/POINTS em IndexedFaceSet,
Cube/Sphere/Cone/Cylinder com DEFAULT/Plane, uma/duas unidades e fast path
ligado/desligado. O perfil GPU aprovado adiciona 48 dessas cenas em
IndexedFaceSet/Cube/Sphere. As 32 cenas de IndexedFaceSet/Cube também usam
CoinGL obrigatório. Os limites originais P07 (MAE ≤ 1 e máximo ≤ 3) permanecem.

Dois reproduceres opt-in preservam diferenças conhecidas, fora dos gates
aprovados; eles retornam erro enquanto a pendência existir:

```sh
CoinRenderProceduralTextureTest --probe-sphere
CoinRenderProceduralTextureTest --probe-cone
```

Em NVIDIA/wgpu, esfera LINES/DEFAULT tem máximo 5 contra CoinGL nas junções
texturizadas, embora CPU/GPU tenham máximo 1. Cone LINES/DEFAULT tem MAE 2.94,
máximo 120 entre CPU/GPU nos contornos coincidentes da lateral/tampa. Esses
resultados delimitam a qualificação; não identificam isoladamente qual raster
é incorreto. Cone/Cylinder texturizados em estilos e junções curvas CoinGL
continuam abertos, com logs e comandos reproduzíveis.

## Limites e evidência

Esta rodada não amplia depth clamp, range invertido/clamp de entrada, precisão
de units/offset fora do perfil existente, concavidade de contornos, bindings de
todos os shapes, formatos/filtros de textura ou qualificação Windows/FreeCAD.
PHONG clássico mantém iluminação Gouraud; iluminação por fragmento continua
uma extensão distinta. Não há nova promessa de paridade universal.

Resultados, ambiente NVIDIA, hashes e logs estão em
[validation/geometry-viewport-20261006/summary.json](validation/geometry-viewport-20261006/summary.json).

A rodada aprova 189 CTests wgpu (191 agendados, dois skips conhecidos) e 252
CTests BGFX distintos (254 agendados, dois skips conhecidos). O primeiro run
BGFX teve um timeout de fog durante a execução concorrente; a repetição isolada
passou em 6,23 s com o mesmo limite de 30 s, sem mudança de fonte/tolerância.
Os 39 testes Rust/shaders passaram. O log da falha inicial permanece na evidência.

## Ampliação portátil de fechamento — 2026-10-06

O critério selecionado é CPU/BGFX/wgpu, com as diferenças de raster CoinGL
registradas como [estudo futuro](coin-render-raster-junctions-study.md).
A ampliação substitui os limites históricos acima somente nas células descritas:

- Conserva a matriz anterior de cinco bindings FaceSet/IndexedFaceSet e amplia
  dez tipos exatos: Cube, Sphere, Cone, Cylinder, QuadMesh, TriangleStripSet,
  IndexedTriangleStripSet, LineSet, IndexedLineSet e PointSet. Nesses dez tipos,
  verifica sete bindings **pareados** de material/normal, normais fornecidas/
  geradas, alpha heterogêneo e fast path ligado/desligado. A matriz adicional
  de estilos também inclui IndexedFaceSet com os sete pares. Os 49 cruzamentos
  entre bindings diferentes não estão todos qualificados.
- Gouraud clássico com luzes directional/point/spot assimétricas, quatro modos
  de fog e viewport interno/externo. Linhas/pontos sem normais fornecidas usam
  BASE_COLOR, como os nós nativos. Endpoints com normais recebem iluminação
  antes do clipping/expansão. QuadMesh e strips com bindings de face preservam
  a cor primária flat do vértice provocador do primitivo original.
- LINES/POINTS com bindings, alpha e viewport externo usam o oráculo portátil
  nas junções. As matrizes independentes de bindings/luzes mantêm CoinGL.
  Texturas DEFAULT/Plane/função autoral, unidade 0/multitextura, clipping e alpha
  são verificadas em faces e Cube/Sphere/Cone/Cylinder; contornos convexos
  conservam seus limites de concavidade e offset planar.
- Cada endpoint finito de `SoDepthBuffer.range` é limitado a [0,1], inclusive
  ranges reversos e colapsados. NaN/Inf são rejeitados sem publicação; o pedido
  válido seguinte recupera a mesma action. Isso não introduz GL_DEPTH_CLAMP
  geométrico. O protocolo privado Rust atual é **48**, sem mudança de layout.
- `CoinRenderDepthCore` resolve slope/maximum da face triangular original em
  coordenadas de janela, antes da execução. Ranges reversos usam módulo do
  gradiente; units recebem o quantum do formato no executor. A expansão de
  strokes conserva slope/maximum da face original, não da faixa expandida.
- A referência CPU usa oito bits fracionários em coordenadas de janela
  (1/256 de pixel), evitando ownership diferente nas faixas coincidentes.
  Contornos colapsados e bounding boxes conservam a direção autoral dos
  endpoints, inclusive após clipping. A propriedade é capturada no estado
  privado e resolvida no Core; não há campo novo no transporte Rust.
  A expansão comum desloca faixas um passo subpixel para cima/esquerda, dando
  ownership top/left às amostras de borda; bounding boxes mantêm seu perfil
  nativo. Ranges coplanares de linhas/pontos com teste/escrita de depth ativos
  recebem um intervalo colapsado equivalente, sem alterar o clipping original.
  Essa escolha é qualificada na GPU/API da campanha; não declara igualdade
  com todos os rasterizadores físicos. Os limites RGB dos gates permanecem.
- Strokes indexados com cache estável recalculam o digest do payload após
  expansão (índices locais, posições e atributos). Mantêm reuso em cena estática
  e invalidam o payload quando iluminação/fog/viewport/clipping o alteram.
- Composição em múltiplas regiões externas, sombras com controle crop,
  annotations e RTT capture/FBO/pbuffer/staged/direto usam fixtures próprias.
  O controle de sombras exige mudança visível entre ativo/inativo; a expectativa
  crop vem do quadro maior com a mesma câmera, sem ajustá-la ao resultado.

Corrigidos também dois desalinhamentos de bindings nativos: o callback de
QuadMesh avançava PER_FACE na segunda coluna do primeiro quad, e o GLRender de
TriangleStripSet avançava o binding da primeira face novamente no terceiro
vértice. A rota nativa IndexedLineSet sem índices explícitos de linha/segmento
agora usa a ordem de ocorrência, sem tratar separadores de coordenadas como
índices de atributo. Vetores de slots esperados e pixels verificam essas correções.

Não se qualificam aqui todos os pares de bindings, shapes/subclasses adicionais,
flat com sombras/iluminação por fragmento, `COIN_QUADMESH_PRECISE_LIGHTING`
não padrão, formatos adicionais RTT ou Windows/FreeCAD/outros drivers.
Planos privados construídos à mão com offset não resolvido e range reverso
não fazem parte do perfil capturado: devem passar pela resolução Core antes
que o transporte/executor receba slope/maximum.

A [campanha integrada final](validation/geometry-viewport-closure-20261006/summary.json)
qualifica 204 CTests wgpu (206 agendados) e 270 BGFX (272 agendados), com os
mesmos dois skips conhecidos. Em wgpu, duas verificações estruturais foram
adaptadas para recuperar posições antes do passo raster; os dois retestes passam
com as tolerâncias de atributos preservadas. Um timeout BGFX de sombras a 60 s
passou isolado em 18,74 s, sem mudar fonte/limite. Rust: 40 passes. Recording:
dez passes e dois skips de GPU, depois dos controles CPU executados.

Cada execução GPU de geometria aprova **1.290 cenas**, inclusive OpenGL sem
exclusões. O procedural aprova **311 cenas e 69 referências CoinGL** em cada
rota qualificada. O controle OpenGL de 84 células curvas, antes com 42 falhas,
passa após a política comum de ownership. O reproducer nativo da esfera ainda
mede máximo 9 (CPU/GPU máximo 1), preservando o estudo futuro.
