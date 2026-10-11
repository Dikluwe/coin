# QuadMesh com iluminação precisa: fan portátil e CoinGL

O modo `COIN_QUADMESH_PRECISE_LIGHTING=1` do CoinGL substitui cada quad 3D
por um `GL_TRIANGLE_FAN` com um vértice central. A captura portátil anterior
recebia os dois triângulos comuns do callback e recusava o modo. O callback
agora emite os mesmos quatro triângulos por quad, com centro geométrico,
normal e UV ponderadas como na rota GL. As combinações que o CoinGL mantém na
rota comum, como material por vértice, continuam nela.

O teste encontrou um segundo defeito no CoinGL texturizado. `get4()` do
elemento de textura converte coordenadas 2D/3D no mesmo objeto temporário;
guardar quatro ponteiros fazia todos os pesos lerem a última UV. A rota GL
agora guarda os quatro **valores**. O reparo anterior só havia inicializado
os ponteiros e eliminado o `SIGSEGV` do primeiro quad
([histórico](../../coin-render-quadmesh-precise-texture-20261011.md)).

Quando material ou normal é por face, o CoinGL ativa `GL_FLAT`. Cada
triângulo do fan toma a cor iluminada do seu último vértice. O builder agora
associa essa cor constante ao triângulo; interpolar a iluminação produzia
RGB máximo 8–9 mesmo com a geometria correta. O modo preciso não oferece
coordenadas centrais independentes para unidades de textura adicionais; a
captura as recusa com `UNSUPPORTED` antes da publicação.

## Gate físico Linux

O executável `CoinRenderGeometryViewportTest --probe-precise-positive`
compara CPU, GPU e CoinGL no interior estável, com gate RGB máximo 4. O
[runner](../../../testsuite/coinrender/run-precise-quadmesh.py) executa cada
combinação em processo novo. Cada linha cruza materiais
OVERALL/PER_PART/PER_FACE, normais OVERALL/PER_PART/PER_FACE/PER_VERTEX e
ausência/presença de UV 2D com textura RGBA 2×2. A malha 4×2 cruza ainda
BASE_COLOR/PHONG; a 3×3 não planar usa PHONG. A contagem de índices verifica
quatro triângulos por quad.

| Backend/API e GPU física | 4×2 | 3×3 não planar | RGB máximo |
| --- | ---: | ---: | ---: |
| BGFX/Vulkan, NVIDIA RTX 3060 Laptop | 48/48 | 24/24 | 2 |
| BGFX/OpenGL, AMD Renoir | 48/48 | 24/24 | 1 |
| wgpu/Vulkan, NVIDIA RTX 3060 Laptop | 48/48 | 24/24 | 2 |
| wgpu/Vulkan, AMD Renoir | 48/48 | 24/24 | 1 |

Total: 288/288 processos. [BGFX/Vulkan 4×2](matrix-bgfx-vulkan-nvidia.json),
[demais perfis 4×2](matrix-other-profiles.json) e
[todos os perfis 3×3](multirow-profiles.json) guardam cada resultado e sua
saída. Para reproduzir, compilar `CoinRenderGeometryViewportTest` nos builds
BGFX e wgpu, usar X11 `:0`, `COIN_QUADMESH_PRECISE_LIGHTING=1`, selecionar o
ICD Vulkan e o fornecedor GLX da mesma GPU e rodar, por exemplo:

```sh
python3 testsuite/coinrender/run-precise-quadmesh.py \
  --exe /tmp/coin-render-closure-bgfx/bin/CoinRenderGeometryViewportTest \
  --out /tmp/precise-quadmesh.json
python3 testsuite/coinrender/run-precise-quadmesh.py \
  --exe /tmp/coin-render-closure-bgfx/bin/CoinRenderGeometryViewportTest \
  --out /tmp/precise-quadmesh-multirow.json --multirow --phong-only
```

O modo `--probe-precise-recovery` passou na NVIDIA/BGFX: cena comum,
fan centrado, retorno à cena comum e fallback por material de vértice com e
sem textura. `--probe-precise-multitexture-rejection` passou em BGFX e wgpu:
pixels/serial preservados na recusa e recuperação do mesmo alvo. Sem o modo
preciso, a regressão completa de geometria BGFX/Vulkan passou 1795/1795
casos. Com o modo ativo, a regressão passou 1795/1795 em BGFX/Vulkan e
wgpu/Vulkan NVIDIA; ela acrescentou normais geradas sem vetor OVERALL e os
estilos LINES/POINTS do fan ao gate CPU/GPU. A comparação CoinGL da matriz
dirigida acima cobre o estilo FILLED. O teste numérico de material por face
usa os índices 1/2/3 da rota centrada do CoinGL, preservando 0/1/2 na rota
comum.

A regressão geral BGFX/OpenGL AMD falhou no mesmo caso de IndexedFaceSet
`bindings/0/7/generated-0/alpha-0/fast-0`, RGB máximo 11 entre CPU e
CoinGL, **com e sem** o modo preciso. É uma divergência pré-existente fora
do QuadMesh; os 48 casos dirigidos BGFX/OpenGL e 24 não planares da tabela
passaram. O gate cobre um único estágio de textura, as duas malhas e as
GPUs/APIs da tabela; sombras, iluminação por fragmento, multi-textura do fan,
Windows e Android permanecem frentes próprias.
