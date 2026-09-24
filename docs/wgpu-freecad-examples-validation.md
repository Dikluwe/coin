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

| Documento | Formas exportadas | Triângulos | BASE_COLOR: MAE RGB / IoU de silhueta | PHONG antes da correção: MAE RGB / IoU de silhueta |
| --- | --- | ---: | ---: | ---: |
| `PartDesignExample.FCStd` | `Body` | 942 | 0,00136 / 0,999978 | 10,858 / 0,967244 |
| `FEMExample.FCStd` | `Box`, `Plane` | 14 | 0,00068 / 0,999974 | 12,823 / 0,999974 |
| `EngineBlock.FCStd` | `Cut003`, `Common` | 9.212 | 0,00180 / 0,999968 | 9,246 / 0,945802 |
| `draft_test_objects.FCStd` | `ShapeString` | 2.294 | 0,00136 / 0,999860 | 0,037 / 0,999860 |
| `AssemblyExample.FCStd` | `Assembly` | 43.723 | 0 / 1,000000 | 3,352 / 0,957879 |
| `BIMExample.FCStd` | `BuildingPart005` | 12.772 | 0,00068 / 0,999988 | 30,002 / 0,910027 |
| `ArchDetail.FCStd` | `BuildingPart007` | 10.232 | 0,00136 / 0,999959 | 12,838 / 0,953873 |

Esta tabela preserva a linha de base **anterior à correção**. Os 14 pares de
renders WebGPU e Coin/GL concluíram e publicaram imagens RGBA
de 512×512. O buffer GL sai com origem vertical oposta à do WebGPU; as
métricas da tabela invertem as linhas GL antes de comparar. Sem essa
normalização, as métricas seriam enganosas. `BASE_COLOR` mostrou paridade
geométrica/pixel praticamente exata. Com luz direcional e `PHONG`, as
silhuetas aparentes continuam próximas, mas há diferença visível de
sombreamento, sobretudo no BIM e no ArchDetail. Isto era **falha de paridade de
aparência nos arquivos sem normais explícitas**; o diagnóstico e a correção
estão abaixo.
A tabela usa WebGPU/RADV e Coin/GL na mesma GPU AMD; PartDesign, EngineBlock,
Assembly e BIM também deram resultados quase idênticos na comparação WebGPU/NVIDIA ×
GL/AMD. Logo, a diferença não se explica apenas pela seleção de GPUs
distintas. A IoU é derivada de
pixels que diferem do fundo por um limiar de cor; superfícies escuras podem
reduzir essa métrica sem alterar a geometria.

O comparador falha automaticamente em `BASE_COLOR` se a MAE alinhada exceder
1 ou a IoU ficar abaixo de 0,99; o modo `lit` é diagnóstico, sem gate de
equivalência visual. Os sete gates básicos passaram na AMD.

## Diagnóstico das bordas e da iluminação (teste A/B)

As cenas originais têm `Coordinate3` e `IndexedFaceSet`, mas nenhum nó
`Normal`. Seu `ShapeHints` não define `creaseAngle`, cujo padrão no Coin
é zero. O caminho GL gera normais pela cache de `SoIndexedFaceSet`, respeitando
esse ângulo. No caminho rápido WebGPU, quando não há normais explícitas,
`SoWgpuFramePlanBuilder` calcula uma normal geométrica por face, mas no
binding padrão `PER_VERTEX_INDEXED` deduplica o vértice por índice de
coordenada (`nKey = cIdx`). Faces de um canto que compartilham coordenada
podem então reutilizar a normal da primeira face. O shader interpola esses
valores errados, produzindo o aspecto desfocado e escondendo visualmente
cantos, embora a geometria e a silhueta permaneçam.

Para isolar a causa, mantivemos as mesmas posições, índices, câmera, material
e luz e adicionamos somente `NormalBinding PER_FACE` e normais geométricas
explícitas por triângulo em cópias das cenas. Erro MAE RGB de `PHONG`,
WebGPU contra GL alinhado verticalmente, 512×512 na mesma GPU AMD:

| Exemplo | Sem normais explícitas | Com normais por face |
| --- | ---: | ---: |
| PartDesign | 10,858 | 0,163 |
| FEM | 12,823 | 0,098 |
| EngineBlock | 9,246 | 0,070 |
| Draft | 0,037 | 0,037 |
| Assembly | 3,352 | 0,019 |
| BIM | 30,002 | 0,207 |
| ArchDetail | 12,838 | 0,088 |

A imagem GL original e a GL com normais explícitas ficaram praticamente
idênticas (MAE de até 0,009 nível RGB). Assim, a queda do erro não se deve a
termos mudado a referência GL nem a uma luz extra no WebGPU. PartDesign e
EngineBlock tornam os cantos e furos mais evidentes, mas não são exceções
estruturais: 72,4% e 73,0% de seus triângulos, respectivamente, usam ao
menos um índice de vértice cuja normal de face difere mais de 30° da primeira
face incidente; BIM chega a 79,1%, Assembly a 61,4%. Draft é quase coplanar
nesse teste (0%) e já coincidia. A perda observada é de **aparência das
bordas**, não de triângulos.

A correção foi feita no `SoWgpuFramePlanBuilder`: na ausência de normais
explícitas, o caminho rápido iluminado usa o mesmo algoritmo
`SoNormalCache::generatePerVertex` empregado pelo `SoIndexedFaceSet` no GL,
com `creaseAngle`, orientação e binding do estado da cena. Os índices de
normal gerados passam a integrar a chave de deduplicação, separando cantos
duros e preservando a suavização intencional. Índices de coordenada são
validados antes da geração; bindings sem suporte nesse caminho seguem pelo
fallback. `BASE_COLOR` não gera normais desnecessárias. Nenhuma ABI pública
do Coin 4 foi alterada. As variantes com `PER_FACE` acima continuam sendo
apenas a prova diagnóstica: os resultados abaixo usam os arquivos `.iv`
**originais**, sem inserir nós `Normal`.

| Exemplo original, PHONG 512×512 | MAE RGB antes → depois | IoU depois |
| --- | ---: | ---: |
| PartDesign | 10,858 → 0,163 | 0,999978 |
| FEM | 12,823 → 0,098 | 0,999974 |
| EngineBlock | 9,246 → 0,070 | 0,999945 |
| Draft | 0,037 → 0,037 | 0,999860 |
| Assembly | 3,352 → 0,019 | 1,000000 |
| BIM | 30,002 → 0,207 | 0,999988 |
| ArchDetail | 12,838 → 0,088 | 0,999959 |

Os sete pares `BASE_COLOR` também continuam dentro do gate de paridade
(MAE RGB até 0,0018; IoU pelo menos 0,99986). O teste de regressão
`WgpuIndexedFastPathTest` agora compara o caminho rápido ao fallback em
uma aresta de 90° sem normais explícitas, tanto com `creaseAngle = 0`
quanto com `creaseAngle = 1,6`. Todos os 26 testes WebGPU do backend Rust
e os 15 do backend Recording passaram nesta revisão.

No estudo persistem sete cenas de diagnóstico em `cenas/diagnostico-normais/`
e sete trípticos em `imagens/diagnostico-normais/`. Cada imagem mostra, da
esquerda para a direita: **WebGPU original | WebGPU com normais por face | GL**.
As imagens da correção ficam em `imagens/correcao-coin/`: render WebGPU
corrigido e tríptico **WebGPU antes | WebGPU corrigido | GL**, para cada um
dos sete exemplos. As imagens anteriores foram preservadas.


## Perfil e otimização de desempenho

A linha de base anterior foi medida em Debug, 512×512, `BASE_COLOR`, com
render e leitura RGBA incluídos. WebGPU marcava 8,629 ms/frame no PartDesign e
193,640 ms/frame no Assembly, contra 0,566 e 0,569 ms/frame no Coin/GL. O
perfil por fase mostrou que o Assembly não estava limitado pela GPU: a maior
parte do tempo era CPU reconstruindo e validando a mesma cena estática.

Três custos foram removidos sem alterar a ABI pública de `libCoin`:

1. O caminho GPU serializava todo o `FramePlan`, inclusive vértices e índices,
   em texto após cada frame. O log continua com a mesma semântica, mas agora é
   materializado somente quando `getRecordingLog()` é consultado.
2. Cada `SoWgpuRenderAction` mantém um único `FramePlan` privado para
   `apply(SoNode *)` quando raiz e `SoNode::getNodeId()` não mudam. Notificações
   de descendentes alteram esse ID; viewport, cor de fundo e fast path também
   invalidam o plano. A rota RTT GPU→GPU direta não usa o cache, pois seus
   tokens têm vida por apply.
3. Cada plano construído recebe uma revisão privada. Validação estrutural e
   ordem de composição já aprovadas são reutilizadas no target C++ e na ponte
   Rust. Planos manuais, com revisão zero, continuam sendo validados sempre.

No mesmo build Debug, após essas mudanças, PartDesign caiu para mediana
5,578 ms e Assembly para 4,843 ms: reduções de 35,4% e 97,5% contra as
respectivas linhas de base WebGPU. O GL observado nessa execução foi 1,035 e
4,393 ms; a diferença para os valores GL históricos confirma que resultados
de campanhas distintas não devem ser comparados como se fossem uma única
amostra controlada.

### Decomposição por fase

`COIN_WGPU_TRACE_PHASES=1` habilita a instrumentação opt-in. No Assembly
Release, após dois frames de aquecimento, as medianas de cinco frames foram:

| Fase (ms) | Cena estática | Câmera alterada a cada frame |
| --- | ---: | ---: |
| traversal Coin | 0,003 | 14,392 |
| construção do FramePlan | 0,001 | 1,372 |
| empacotamento C++→FFI | 0,162 | 0,360 |
| validação Rust | 0,004 | 0,363 |
| preparação/encode | 0,158 | 0,172 |
| `queue.submit()` no CPU | 0,087 | 0,092 |
| espera GPU | 0,335 | 0,327 |
| publicação do readback | 0,837 | 1,627 |

As fases da ponte ficam aninhadas no tempo total do backend e não devem ser
somadas à linha de action. Em particular, “espera GPU” é o intervalo de
`device.poll(Maintain::Wait)`: inclui execução de render e cópias GPU para
staging, não é timestamp de hardware isolado. “Publicação” inclui map/unpack e
cópia para os vetores do target. A cópia pública posterior de RGBA teve
mediana de 0,078 ms no Assembly estático; portanto o readback interno, não
essa cópia final, é o componente mais relevante depois do cache hit.

### Comparação Release reproduzível

AMD Radeon Graphics (RADV RENOIR), Vulkan/GL na mesma GPU, Release/C++11,
512×512, `BASE_COLOR`, 8 frames de aquecimento e 30 medidos, render e readback
RGBA incluídos:

| Cena estática | WebGPU mediana / p95 (ms) | Coin/GL mediana / p95 (ms) |
| --- | ---: | ---: |
| PartDesign | 3,056 / 3,765 | 1,057 / 1,377 |
| EngineBlock | 2,842 / 3,274 | 2,175 / 2,317 |
| Assembly | **1,977 / 2,283** | 4,221 / 4,826 |
| BIM | 2,725 / 3,141 | 2,017 / 2,329 |

Há uma melhoria mensurável sobre GL no Assembly estático: 53,2% na mediana e
52,7% no p95. PartDesign, EngineBlock e BIM ainda ficam atrás; portanto esta
campanha **não demonstra superioridade geral**. A meta proposta de p95 20%
menor nos modelos pesados foi atingida somente no Assembly.

Para não esconder invalidação do plano, `--dynamic` translada a câmera antes
de cada frame com a mesma sequência nos dois backends:

| Cena dinâmica | WebGPU mediana / p95 (ms) | Coin/GL mediana / p95 (ms) |
| --- | ---: | ---: |
| PartDesign | 3,087 / 3,465 | 1,018 / 1,199 |
| Assembly | 20,387 / 21,096 | 4,006 / 4,727 |

O Assembly dinâmico confirma a prioridade seguinte: traversal e construção
do plano somam cerca de 15,8 ms, enquanto a espera GPU fica perto de 0,33 ms.
O cache acelera cenas realmente estáticas; não é evidência de ganho na
viewport interativa do FreeCAD.

### Qualidade e memória

Uma nova comparação Release das quatro cenas, no primeiro frame (logo, sem
cache hit de plano), manteve IoU de silhueta 1,000000 após alinhar a origem
vertical. MAE RGB alinhada WebGPU/GL:

| Cena | BASE_COLOR | PHONG |
| --- | ---: | ---: |
| PartDesign | 0,655 | 0,798 |
| EngineBlock | 0,638 | 0,696 |
| Assembly | 0,907 | 0,924 |
| BIM | 0,681 | 0,876 |

Todos os gates permaneceram abaixo de MAE 1 e acima de IoU 0,99. A tabela de
correção de normais acima permanece como o teste A/B histórico; esta campanha
Release usa outro build e serve como gate de não regressão da otimização, não
como substituição daqueles valores.

No Assembly estático, `/usr/bin/time -v` em processos separados mediu pico
RSS de 148.424 KiB no WebGPU e 119.604 KiB no GL: WebGPU consumiu 28.820 KiB
(24,1%) a mais. O número inclui runtime, driver e memória CPU do processo, não
é medida de VRAM. A telemetria WebGPU registrou um único upload de geometria
de 1.316.856 bytes; os 30 frames medidos tiveram zero uploads e um cache hit.

### Próximas otimizações, em ordem

1. Cache incremental por subárvore/estado, começando por câmera, para não
   refazer 14,4 ms de traversal quando apenas a view muda.
2. Reutilizar empacotamento POD e buffers/attachments de staging por revisão,
   reduzindo alocações e cópias sem relaxar validação.
3. Adicionar timestamp queries para separar execução GPU de cópias para
   staging; o marcador atual não permite essa conclusão.
4. Medir RTT direto e readback assíncrono em fluxos de produto. Eles não foram
   usados para justificar os ganhos acima e sua existência não implica menor
   tempo end-to-end.

As sete cenas originais `.iv` e 35 PNGs da primeira campanha persistem em
`/home/dikluwe/Área de trabalho/Estudo coin/estudos/So/SoWgpu-FreeCAD-Exemplos/`.
Cada exemplo tem `base-wgpu`, `base-gl`, `lit-wgpu`, `lit-gl` e
`lit-comparacao`; nos pares GL a origem vertical já foi corrigida. Nas
comparações lado a lado, **esquerda = WebGPU, direita = GL**. Em especial,
`imagens/BIM-lit-comparacao.png` registra o artefato de normais iluminadas.



## Reproduzir no checkout local

```sh
export FREECAD_BUILD_DIR=/mnt/Laranja/Git/externos/freecad-build
export FREECAD_EXAMPLE_DIR=/mnt/Laranja/Git/externos/freecad-source/data/examples
export COIN_WGPU_BUILD_DIR=/dev/shm/coin-wgpu-release
export FREECAD_WGPU_OUT=$(mktemp -d /tmp/coin-freecad-wgpu.XXXXXX)
cmake -S . -B "$COIN_WGPU_BUILD_DIR" -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_STANDARD=11 -DCOIN_BUILD_WGPU=ON \
  -DCOIN_WGPU_BACKEND=RUST_BRIDGE -DCOIN_BUILD_TESTS=ON \
  -DCOIN_TEST_WGPU_GL_REFERENCE=ON
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
  --scene "$FREECAD_WGPU_OUT/PartDesign.iv" --frames 30 --warmup 8 --size 512

# Invalida o plano ao mover a câmera; use --backend wgpu ou gl para RSS isolado.
VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/radeon_icd.json \
COIN_GLX_PIXMAP_DIRECT_RENDERING=1 \
  "$COIN_WGPU_BUILD_DIR/bin/wgpu_gl_benchmark" \
  --scene "$FREECAD_WGPU_OUT/PartDesign.iv" --frames 30 --warmup 8 \
  --size 512 --dynamic
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
