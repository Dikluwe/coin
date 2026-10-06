# CoinRender: geometria e viewport — P02/P04/P05/P06

Rodada de 2026-10-06 em `codex/coin-render`, após P07 (`1158dd7c1f`).
Este documento registra o perfil ampliado; P02/P04/P05/P06 continuam abertos
para a matriz completa por shape, estado, plataforma e driver.

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
