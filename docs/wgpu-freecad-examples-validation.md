# Validação WebGPU com exemplos oficiais do FreeCAD

Data: 24/09/2026. Escopo: interoperabilidade geométrica offscreen, **não** a
viewport do FreeCAD. O build local do FreeCAD usa Coin próprio e seu
`QuarterWidget` usa `SoGLRenderAction`; não se carregou o módulo WebGPU nesse
processo nem se alterou FreeCAD.

O FreeCAD foi executado headless com Python 3.12. Cada documento oficial foi
aberto com `FreeCAD.openDocument()` e as formas finais nomeadas abaixo foram
tesselladas com tolerância `0,5`. O exportador
`examples/wgpu/freecad_export_iv.py` gravou somente nós Inventor padrão
`Coordinate3`, `IndexedFaceSet`, `ShapeHints` e `Material`. Seleção, overlays,
materiais e texturas da viewport, hierarquia de objetos, câmera original e
nós específicos do FreeCAD **não** estão representados neste ensaio. A API
`TopoShape.tessellate()` retorna vértices e índices de faces; a tesselação
headless é deliberada.

| Documento | Formas exportadas | Triângulos | BASE_COLOR: MAE RGB / IoU de silhueta | PHONG: MAE RGB / IoU de silhueta |
| --- | --- | ---: | ---: | ---: |
| `PartDesignExample.FCStd` | `Body` | 942 | 0,00136 / 0,999978 | 10,858 / 0,967244 |
| `FEMExample.FCStd` | `Box`, `Plane` | 14 | 0,00068 / 0,999974 | 12,823 / 0,999974 |
| `EngineBlock.FCStd` | `Cut003`, `Common` | 9.212 | 0,00180 / 0,999968 | 9,246 / 0,945802 |
| `draft_test_objects.FCStd` | `ShapeString` | 2.294 | 0,00136 / 0,999860 | 0,037 / 0,999860 |
| `AssemblyExample.FCStd` | `Assembly` | 43.723 | 0 / 1,000000 | 3,352 / 0,957879 |
| `BIMExample.FCStd` | `BuildingPart005` | 12.772 | 0,00068 / 0,999988 | 30,002 / 0,910027 |
| `ArchDetail.FCStd` | `BuildingPart007` | 10.232 | 0,00136 / 0,999959 | 12,838 / 0,953873 |

Os 14 pares de renders WebGPU e Coin/GL concluíram e publicaram imagens RGBA
de 512×512. O buffer GL sai com origem vertical oposta à do WebGPU; as
métricas da tabela invertem as linhas GL antes de comparar. Sem essa
normalização, as métricas seriam enganosas. `BASE_COLOR` mostrou paridade
geométrica/pixel praticamente exata. Com luz direcional e `PHONG`, as
silhuetas aparentes continuam próximas, mas há diferença visível de
sombreamento, sobretudo no BIM e no ArchDetail. Isto é **falha de paridade de aparência
para investigar**, não aprovação visual da iluminação. A tabela usa
WebGPU/RADV e Coin/GL na mesma GPU AMD; PartDesign, EngineBlock, Assembly e BIM
também deram resultados quase idênticos na comparação WebGPU/NVIDIA ×
GL/AMD. Logo, a diferença não se explica apenas pela seleção de GPUs
distintas; a causa específica ainda não foi isolada. A IoU é derivada de
pixels que diferem do fundo por um limiar de cor; superfícies escuras podem
reduzir essa métrica sem alterar a geometria.

O comparador falha automaticamente em `BASE_COLOR` se a MAE alinhada exceder
1 ou a IoU ficar abaixo de 0,99; o modo `lit` é diagnóstico, sem gate de
equivalência visual. Os sete gates básicos passaram na AMD.

## Linha de base de desempenho — ainda sem ganho sobre GL

O benchmark aceita `--scene arquivo.iv`, além da cena sintética de 36 cubos.
No mesmo adaptador AMD RADV/GL, build **Debug**, 512×512, `BASE_COLOR`,
4 frames de aquecimento e 12 medidos, com renderização e leitura RGBA
incluídas, foram observadas estas medianas:

| Exemplo | WebGPU (ms/frame) | Coin/GL (ms/frame) |
| --- | ---: | ---: |
| PartDesign | 8,629 | 0,566 |
| FEM | 6,398 | 0,450 |
| EngineBlock | 40,933 | 0,412 |
| Draft | 18,851 | 0,490 |
| Assembly | 193,640 | 0,569 |
| BIM | 57,252 | 0,487 |
| ArchDetail | 48,517 | 0,576 |

**WebGPU não apresenta melhoria de desempenho neste ensaio; está
substancialmente atrás do GL.** Os números são uma linha de base diagnóstica,
não uma conclusão para Release, outras GPUs ou a viewport interativa. Uma
segunda execução de Assembly confirmou a ordem de grandeza (191,6 ms
WebGPU; 0,446 ms GL): a chamada `render()` respondeu por 191,5 ms e a cópia
posterior do buffer RGBA por 0,13 ms. A telemetria do último frame mostrou
zero uploads, zero bytes enviados e um cache hit. Isso descarta o upload
repetido e a cópia pública de RGBA como explicação única para esse caso, mas
**não** separa ainda traversal/planejamento, submit, trabalho GPU e readback
interno. É preciso perfilar essas fases antes de alterar o pipeline.

Paridade visual é o piso, não a meta. Para afirmar vantagem sobre GL será
necessário: corrigir a diferença PHONG; repetir o benchmark em Release, na
mesma GPU e com cenas estáticas e dinâmicas; medir mediana, p95, uso de
memória, uploads e latência de interação; e demonstrar ganho reproduzível
sem reduzir a qualidade. Uma meta de produto proposta, ainda **não
atingida**, é p95 pelo menos 20% menor nos modelos pesados (EngineBlock,
Assembly e BIM) e ausência de regressão relevante nos leves. RTT GPU→GPU,
readback assíncrono e recuperação após perda do device também precisam ser
medidos em fluxos reais; sua existência por si só não prova superioridade.

As sete cenas `.iv` e 35 PNGs persistem em
`/home/dikluwe/Área de trabalho/Estudo coin/estudos/So/SoWgpu-FreeCAD-Exemplos/`.
Cada exemplo tem `base-wgpu`, `base-gl`, `lit-wgpu`, `lit-gl` e
`lit-comparacao`; nos pares GL a origem vertical já foi corrigida. Nas
comparações lado a lado, **esquerda = WebGPU, direita = GL**. Em especial,
`imagens/BIM-lit-comparacao.png` registra a divergência de iluminação.



## Reproduzir no checkout local

```sh
export FREECAD_BUILD_DIR=/mnt/Laranja/Git/externos/freecad-build
export FREECAD_EXAMPLE_DIR=/mnt/Laranja/Git/externos/freecad-source/data/examples
export COIN_WGPU_BUILD_DIR=/dev/shm/coin-wave6-rust
export FREECAD_WGPU_OUT=$(mktemp -d /tmp/coin-freecad-wgpu.XXXXXX)
cmake --build "$COIN_WGPU_BUILD_DIR" --target wgpu_freecad_compare wgpu_gl_benchmark -j4

PYTHONPATH="$FREECAD_BUILD_DIR/lib" LD_LIBRARY_PATH="$FREECAD_BUILD_DIR/lib" \
  python3 examples/wgpu/freecad_export_iv.py \
  "$FREECAD_EXAMPLE_DIR/PartDesignExample.FCStd" \
  "$FREECAD_WGPU_OUT/PartDesign.iv" --object Body --deflection 0.5

VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/radeon_icd.json \
COIN_GLX_PIXMAP_DIRECT_RENDERING=1 \
  "$COIN_WGPU_BUILD_DIR/bin/wgpu_freecad_compare" \
  "$FREECAD_WGPU_OUT/PartDesign.iv" \
  "$FREECAD_WGPU_OUT/PartDesign-lit" 512 lit

VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/radeon_icd.json \
COIN_GLX_PIXMAP_DIRECT_RENDERING=1 \
  "$COIN_WGPU_BUILD_DIR/bin/wgpu_gl_benchmark" \
  --scene "$FREECAD_WGPU_OUT/PartDesign.iv" --frames 12 --warmup 4 --size 512
```

Para os outros documentos, usar `--object Box --object Plane` (FEM),
`--object Cut003 --object Common` (EngineBlock), `--object ShapeString`
(Draft), `--object Assembly`, `--object BuildingPart005` (BIM) ou
`--object BuildingPart007` (ArchDetail). O texto do Draft é malha tessellada,
não o pipeline de fontes da viewport. Sem o
argumento `lit`, o comparador usa `BASE_COLOR`. Ele grava PPM WebGPU e GL;
para visualizar o GL na mesma orientação, inverter verticalmente suas linhas
(por exemplo, `convert arquivo-gl.ppm -flip arquivo-gl-aligned.png`).

O backend GLX do Coin exigiu `COIN_GLX_PIXMAP_DIRECT_RENDERING=1` neste
ambiente. O aviso de ausência de framebuffer config para pbuffer não impediu
o fallback para GLX pixmap. A primeira tabela mede erro de imagem e
silhueta; a segunda mede tempo end-to-end. O comparador instalado também passou
em um prefixo temporário, sem `LD_LIBRARY_PATH` da árvore de build.

## Próximo gate necessário para integração real

Fazer um build **isolado** do FreeCAD contra a mesma revisão do Coin/WebGPU,
integrar um target WebGPU ao widget Qt sem substituir o caminho GL atual e
testar nós específicos (`SoBrepFaceSet`, `SoFCSelection`, overlays), picking,
transparência, textura, resize e device loss. Não carregar lado a lado o Coin
embutido do FreeCAD e a biblioteca experimental ligada a outra revisão de
`libCoin.so.80` no mesmo processo.
