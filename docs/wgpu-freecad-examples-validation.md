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

#### Reutilização tipada de câmera — campanha de 2026-09-24

Esta campanha pertence ao Prompt 005 da arquitetura Tekt e não substitui nem
é combinada com as tabelas históricas acima. O Core classificou cada FramePlan
dinâmico e a Infra atualizou somente os estados dependentes da câmera quando
a revisão-base ainda era a que o empacotador possuía.

O executável não-gate `WgpuFrameReuseBenchmark`, na build Release, reproduz a
comparação isolada usando os tamanhos capturados do Assembly:

```sh
"$COIN_WGPU_BUILD_DIR/bin/WgpuFrameReuseBenchmark"
```

Em 250 amostras, 22.005 vértices e 131.169 índices, o empacotamento completo
teve mediana/p95 de 0,051676/0,055704 ms; o `camera_patch`,
0,000240/0,000250 ms. A redução de 99,54% vale apenas para essa fase. No traço
real com dois frames de aquecimento e cinco medidos, todos os medidos foram
`plan_reuse=camera_patch` e `pack_mode=camera_patch`, com mediana/p95 de
0,003917/0,004568 ms no empacotamento.

Na comparação integral, AMD RADV RENOIR, 512×512, `BASE_COLOR`, oito frames de
aquecimento e 30 medidos, câmera alterada e readback RGBA incluído, WebGPU
teve mediana/p95 de 22,3452/24,1579 ms e Coin/GL 0,439217/0,792696 ms. Portanto
o ganho isolado não se converteu em superioridade de frame: traversal e
construção do FramePlan continuam sendo a prioridade. O gate visual alinhado
do primeiro frame ficou em MAE RGB 0 e IoU 1. Um processo WebGPU isolado para
a mesma carga registrou pico RSS de 144496 KiB; por ser outra execução, não é
tratado como redução de memória.

#### Overlay de câmera conservador — campanha de 2026-09-24

O Prompt 006 reaproveita o FramePlan sem percorrer a geometria somente para
uma raiz `SoSeparator` com câmera direta e nós de tipos conhecidos estáticos,
`BASE_COLOR` e fog desligado. Um `SoNodeSensor` imediato invalida o atalho
quando qualquer outro nó ou a estrutura muda. As demais cenas continuam com
traversal Coin. RTT direto e a viewport real do FreeCAD não foram avaliados.

AMD RADV RENOIR, Release, Assembly exportado, 512×512, câmera deslocada a
cada frame, oito warmup e 30 amostras, render mais readback RGBA:

| Execução | WebGPU mediana / p95 (ms) | Coin/GL mediana / p95 (ms) |
| --- | ---: | ---: |
| Ambos no mesmo processo | 4,62694 / 6,20546 | 0,46698 / 0,935525 |
| Processos isolados | 4,55145 / 5,36009 | 0,398563 / 0,679306 |

O WebGPU anterior, nas mesmas condições de cena e GPU, tinha mediana/p95
22,3452/24,1579 ms. A nova mediana é cerca de 79,3% menor, mas ainda fica
~9,9 vezes acima do GL no mesmo processo. A medição isolada registrou pico
RSS de 141972 KiB para WebGPU e 96440 KiB para GL; os processos não medem
delta de memória atribuível somente ao overlay. O trace opt-in mostrou
`plan_cache_hit=1`, `plan_reuse=camera_patch`, `pack_mode=camera_patch` e
travessia de cerca de 0,002–0,004 ms nos frames subsequentes. Trace altera
o tempo total e não foi usado nos números da tabela.

O teste `WgpuRenderActionTest` compara o log Recording e pixels RGBA da GPU
para overlay e travessia nova; também cobre mutação de geometria e inserção
de filho. Uma comparação do primeiro frame exportado com GL, após inverter
a origem vertical, conservou MAE RGB 0 e IoU de silhueta 1. Esse primeiro
frame não exercita o overlay; o teste de pixels cobre a câmera em movimento.

Reprodução com o Assembly exportado deste estudo:

```sh
VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/radeon_icd.json \
COIN_GLX_PIXMAP_DIRECT_RENDERING=1 \
  "$COIN_WGPU_BUILD_DIR/bin/wgpu_gl_benchmark" \
  --backend both --dynamic --frames 30 --warmup 8 --size 512 \
  --scene "/home/dikluwe/Área de trabalho/Estudo coin/estudos/So/SoWgpu-FreeCAD-Exemplos/cenas/Assembly.iv"

VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/radeon_icd.json \
COIN_GLX_PIXMAP_DIRECT_RENDERING=1 \
  "$COIN_WGPU_BUILD_DIR/bin/wgpu_freecad_compare" \
  "/home/dikluwe/Área de trabalho/Estudo coin/estudos/So/SoWgpu-FreeCAD-Exemplos/cenas/Assembly.iv" \
  /tmp/coin-wgpu-assembly-visual 512
```

Para RSS isolado, repetir o benchmark com `--backend wgpu` e `--backend gl`
em processos separados sob `/usr/bin/time -v`.

#### Overlay transacional e validação do target — campanha de 2026-09-24

O Prompt 007 aplica a mudança de câmera diretamente ao FramePlan privado já
validado, mantendo os vetores de geometria no lugar. Se `apply()` ou
`applyAsync()` falhar, o Wiring restaura câmera, estados derivados e revisão.
O target C++ reaproveita sua validação de perfil somente quando a revisão-base
do `camera_patch` coincide com sua última revisão validada; resize ou base
obsoleta exigem validação completa. A ponte Rust continua fazendo sua própria
validação de entrada para cada nova revisão de câmera.

No Assembly exportado, Release/AMD RADV RENOIR, `BASE_COLOR`, 512×512, oito
warmup e 30 frames com câmera em movimento e readback RGBA, medimos:

| Implementação | WebGPU mediana / p95 (ms) | Coin/GL mediana / p95 (ms) |
| --- | ---: | ---: |
| Prompt 006, campanha anterior | 4,62694 / 6,20546 | 0,46698 / 0,935525 |
| Overlay transacional, comparação pareada | 4,40449 / 5,58902 | 0,514238 / 0,883425 |
| Overlay + validação C++ reaproveitada, comparação pareada | 3,92649 / 6,30354 | 0,683714 / 1,04536 |
| Build final, repetição pareada | 3,15660 / 4,32228 | 0,599847 / 1,01720 |

As campanhas não são amostras pareadas entre versões e há variação visível no
GL e no p95 WebGPU; por isso a tabela demonstra oportunidade e ausência de
regressão grande, não estabelece um ganho estatístico fechado de frame. Mesmo
na repetição final, WebGPU ficou cerca de 5,3 vezes mais lento que GL na
mediana. O traço opt-in do overlay mostra `frame_plan_ms` de ~0,015–0,030 ms,
ante ~0,4–0,5 ms no plano copiado. O traço não integra a tabela de latência.
A validação Rust remanescente consumiu cerca de 0,57–0,95 ms em frames
observados; o custo de backend completo ficou na faixa ~3,2–4,0 ms.
Um processo WebGPU isolado com apenas o overlay transacional teve pico RSS
140768 KiB, contra 141972 KiB na campanha anterior com cópia; processos
distintos não permitem atribuir essa diferença à mudança. RTT direto e
readback assíncrono não foram medidos como ganhos.

No build final, processos isolados com o mesmo comando `--dynamic` mediram
WebGPU 3,32241/3,72706 ms, pico RSS 140632 KiB, e GL
0,644591/0,953686 ms, pico RSS 96552 KiB. São comparações de processos,
não um delta de memória causal. O trace de quatro frames medidos, após oito
warmup, separou por frame de câmera: traversal ~0,003–0,005 ms,
`frame_plan_ms` ~0,015–0,025 ms, pack ~0,003–0,008 ms, validação Rust
~0,57–0,79 ms, preparação/encode ~0,17–0,25 ms, submit ~0,10–0,11 ms,
espera GPU ~0,35–1,39 ms e publicação do readback ~0,76–1,07 ms.
Esses intervalos são diagnósticos com tracing ligado, não substituem a
mediana/p95 sem tracing da tabela. A repetição pareada teve 0 uploads de
geometria no último frame; o primeiro enviou 1316856 bytes.

O teste de pixels para a câmera em movimento continuou exato frente à
travessia integral; o teste do target cobre base obsoleta e resize. A
comparação visual do primeiro frame com GL permanece um gate separado, não
mede o overlay; na repetição final, MAE RGB 0 e IoU 1 após inverter a origem
vertical. Debug Rust 42/42, Release Rust 42/42 e Debug Recording 28/28
passaram. A viewport real do FreeCAD segue fora do ensaio.

#### Cena validada com dono Rust — campanha de 2026-09-24

O Prompt 008A acrescenta uma revisão-base ao protocolo **privado** C++/Rust
(versão 17). Para quadros opacos, sem textura, o dispositivo Rust guarda uma
cópia validada da geometria, materiais e ordem de composição. Um patch de
câmera usa essa cópia e lê do chamador apenas os estados alterados. Base
obsoleta, outro dispositivo, geração perdida, resize, transparência ou
texturas seguem pelo caminho integral. O cache CPU é limitado a 32 MiB;
cenas maiores continuam na validação integral. Não muda a ABI pública de
`libCoin`.

AMD RADV RENOIR, Release, Assembly exportado, `BASE_COLOR`, 512×512, câmera
movida a cada frame, oito warmup e 30 amostras, render mais readback RGBA,
WebGPU e Coin/GL no mesmo processo:

| Repetição | WebGPU mediana / p95 (ms) | Coin/GL mediana / p95 (ms) |
| --- | ---: | ---: |
| 1 | 2,16270 / 2,55116 | 0,418146 / 0,902754 |
| 2 | 2,05821 / 2,83999 | 0,477274 / 0,862460 |
| 3 | 2,14292 / 2,82476 | 0,404401 / 0,788676 |

A campanha anterior sem cache proprietário registrou 3,15660/4,32228 ms
para WebGPU e 0,599847/1,01720 ms para GL, mas foi executada antes e não é
um A/B intercalado. O ganho de fase é mais bem demonstrado: no trace de
câmera, `validation_ms` caiu de ~0,57–0,79 para ~0,004–0,009 ms. O frame
inteiro continua ~4–5 vezes mais lento que GL nas novas repetições; espera GPU
e readback seguem relevantes. Trace não integra os valores da tabela.

Um processo WebGPU isolado mediu 2,07345/2,30443 ms e pico RSS 142008 KiB;
o anterior mediu 140632 KiB em outro processo. A cópia de geometria possuída
pelo Rust custa memória, mas esses picos não isolam causalmente a diferença.
O último frame teve zero uploads de geometria. O teste FFI usa ponteiros de
geometria nulos no patch para provar que o Rust lê sua própria cópia, compara
pixels com um frame completo de referência e verifica estado divergente,
base obsoleta e novo dispositivo sem publicar saída. No gate visual GL do
primeiro frame, a MAE
RGB alinhada foi 0 e a IoU de silhueta 1. Esse gate não mede a câmera em
movimento nem a viewport real do FreeCAD.

### Qualidade e memória

#### Readback explícito de saídas — campanha de 2026-09-24

O Prompt 008B corrigiu uma assimetria do benchmark anterior: o WebGPU
copiava cor **e profundidade** mesmo quando o programa lia apenas RGBA,
enquanto o GL fornecia seu buffer de cor. O novo padrão do benchmark solicita
apenas RGBA ao target WebGPU; cor+depth permanece o padrão da API experimental
e é medido separadamente com `--backend wgpu --readback color-depth`. Portanto,
os números RGBA de campanhas anteriores não são A/B pareado do novo padrão.

AMD RADV RENOIR e GL radeonsi Renoir, Release, `BASE_COLOR`, 512×512, câmera
alterada em cada frame, 10 warmup e 60 amostras:

| Cena | WebGPU RGBA mediana/p95 (ms) | Coin/GL RGBA mediana/p95 (ms) | WebGPU cor+depth separado (ms) |
| --- | ---: | ---: | ---: |
| PartDesign | 1,281 / 1,621 | 0,364 / 0,568 | 3,083 / 3,567 |
| Assembly | 1,725 / 2,202 | 0,429 / 0,882 | 2,165 / 2,574 |

Em PartDesign 1024×1024, 10 warmup/40 amostras, RGBA mediu 5,187/6,119 ms
contra 1,141/1,822 ms no GL. Em processos WebGPU isolados, RGBA mediu
5,569/6,078 ms e pico RSS 146744 KiB; cor+depth mediu 11,942/13,023 ms e
154864 KiB. O custo do depth readback varia com cena e resolução; não atribuir
essa diferença somente a execução de shader. RTT direto e readback assíncrono
não foram usados para justificar o ganho de latência síncrona.

O comparador visual RGBA-only preservou no Assembly MAE RGB 0 e IoU 1 após
inversão vertical; no PartDesign, MAE RGB 0,001358 e IoU 0,999978. O teste
offscreen também exige cor idêntica ao alternar a política e depth novamente
disponível após restaurá-la; o teste assíncrono exige ticket sem depth mesmo
se o target mudar de política antes do poll. Isso não cobre a viewport real
do FreeCAD.

Reprodução (substituir o diretório de build conforme a máquina):

```sh
VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/radeon_icd.json \
COIN_GLX_PIXMAP_DIRECT_RENDERING=1 \
  "$COIN_WGPU_BUILD_DIR/bin/wgpu_gl_benchmark" --backend both \
  --readback color --dynamic --frames 60 --warmup 10 --size 512 \
  --scene "/home/dikluwe/Área de trabalho/Estudo coin/estudos/So/SoWgpu-FreeCAD-Exemplos/cenas/PartDesign.iv"

VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/radeon_icd.json \
  "$COIN_WGPU_BUILD_DIR/bin/wgpu_gl_benchmark" --backend wgpu \
  --readback color-depth --dynamic --frames 60 --warmup 10 --size 512 \
  --scene "/home/dikluwe/Área de trabalho/Estudo coin/estudos/So/SoWgpu-FreeCAD-Exemplos/cenas/PartDesign.iv"
```

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

### Pool de staging e cópia RGBA direta — Prompt 008C (2026-09-24)

Build Release Rust, AMD RADV RENOIR, `BASE_COLOR`, câmera movida a cada
frame, RGBA-only e readback síncrono incluído. O GL usa o mesmo output de cor.
Cada linha abaixo é um processo pareado `--backend both`, com 10 warmup e
60 frames medidos em 512×512:

| Cena e repetição | WebGPU mediana/p95 (ms) | Coin/GL mediana/p95 (ms) |
| --- | ---: | ---: |
| PartDesign 1 | 0,373 / 0,706 | 0,588 / 1,060 |
| PartDesign 2 | 0,381 / 0,678 | 0,371 / 0,845 |
| PartDesign 3 | 0,389 / 0,677 | 0,400 / 0,896 |
| Assembly 1 | 0,522 / 0,831 | 0,416 / 0,818 |
| Assembly 2 | 0,524 / 0,794 | 0,436 / 0,845 |
| Assembly 3 | 0,501 / 0,757 | 0,415 / 0,895 |

Um único processo anterior à alteração mediu PartDesign 1,829/2,205 ms
contra GL 0,415/0,865. Ele não foi intercalado com a nova implementação;
a variação entre processos e a ausência de A/B entre os binários impedem
atribuir a redução end-to-end especificamente ao pool. O trace
mostra `staging_color_reused=0` no primeiro frame e 1 nos seguintes; trace
não integra a tabela. O pool não atrasa a identidade do frame síncrono.

Em PartDesign 1024×1024, 10 warmup/40 frames, o processo pareado RGBA-only
mediu WebGPU 3,853/4,430 ms e GL 1,394/1,699 ms. Em processos WebGPU
isolados, RGBA-only mediu 3,929/4,845 ms com pico RSS 143136 KiB;
cor+depth mediu 9,666/10,358 ms e 155032 KiB. O RSS inclui runtime e
driver, não é VRAM. Color+depth não é comparação equivalente ao GL RGBA.
O comparador visual, após inverter verticalmente o GL, manteve PartDesign
MAE RGB 0,001358/IoU 0,999978 e Assembly MAE 0/IoU 1. A viewport real do
FreeCAD continua fora do escopo. Release Rust 42/42, Debug Rust 42/42 e
Recording 28/28 passaram.

Reprodução (usar `PartDesign.iv` ou `Assembly.iv` em `SCENE`):

```sh
SCENE='/home/dikluwe/Área de trabalho/Estudo coin/estudos/So/SoWgpu-FreeCAD-Exemplos/cenas/PartDesign.iv'
VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/radeon_icd.json \
COIN_GLX_PIXMAP_DIRECT_RENDERING=1 \
  /tmp/coin-wgpu-tekt-release/bin/wgpu_gl_benchmark --backend both \
  --readback color --dynamic --frames 60 --warmup 10 --size 512 \
  --scene "$SCENE"
```

### Bindings persistentes de câmera — Prompt 008D (2026-09-24)

O encoder Rust pode reter o storage buffer de materiais e um uniform
buffer/bind group por draw para a mesma geometria validada, atualizando só
os uniforms antes do submit. `COIN_WGPU_CAMERA_BINDINGS=1` liga esse teste;
o padrão é **desligado**. O cache privado é limitado a 512 draws e 4 MiB
de payload de materiais+uniforms e é descartado ao trocar a geometria,
device ou sair do patch de câmera. A ABI pública do Coin 4 e o protocolo
privado FFI não mudaram. O trace opt-in mostrou 0 bindings no frame
completo, 1 criado no primeiro patch e 1 reutilizado em cada patch seguinte
do PartDesign. O teste de múltiplos devices compara três patches sucessivos
pixel a pixel contra frames completos; dois patches assíncronos ficam em voo
ao mesmo tempo e cada ticket corresponde à sua câmera. O teste ainda cobre
RTT, base obsoleta, estado não-câmera e recriação do device.

A/B do **binário final Release**, AMD RADV RENOIR, `BASE_COLOR`, câmera
movida por frame, RGBA-only com readback síncrono incluído. Em 512×512,
cada processo mediu 120 frames após 20 warmup, com WebGPU e GL pareados.
Foram seis pares por cena: três na ordem off→on e três na ordem on→off.
A tabela reporta a **mediana das seis medianas por processo** e a
**mediana dos seis p95 por processo**, não um percentil reagrupado. Tempo
em ms; RSS em KiB.

| Cena | Cache | WebGPU mediana/p95 | Coin/GL pareado mediana/p95 | Faixa das medianas WebGPU | Pico RSS dos processos |
| --- | --- | ---: | ---: | ---: | ---: |
| PartDesign | off | 0,487 / 0,831 | 0,403 / 0,897 | 0,415–0,573 | 147096–147484 |
| PartDesign | on | 0,398 / 0,739 | 0,394 / 0,832 | 0,378–0,464 | 146952–147604 |
| Assembly | off | 0,531 / 0,866 | 0,423 / 0,888 | 0,526–0,597 | 157320–158012 |
| Assembly | on | 0,477 / 0,839 | 0,440 / 0,921 | 0,465–0,545 | 157464–157804 |

O cache reduziu a mediana WebGPU em todos os 12 pares de 512²: 18,2% no
PartDesign e 10,1% no Assembly na mediana das medianas. Isso é melhoria
mensurável do WebGPU neste perfil, não prova de vantagem geral sobre GL.
GL também variou entre processos; o Assembly ainda ficou atrás do GL.

Em PartDesign 1024×1024, três pares com ordem off→on, on→off, off→on;
60 frames após 10 warmup, também RGBA-only. Cada célula lista
mediana/p95 dos processos 1 · 2 · 3:

| Cache | WebGPU | Coin/GL pareado | Pico RSS (KiB) |
| --- | --- | --- | ---: |
| off | 3,965/4,593 · 3,974/4,878 · 4,157/4,948 | 1,295/1,785 · 1,122/1,614 · 1,346/1,861 | 159308–159680 |
| on | 3,954/5,214 · 3,990/4,614 · 3,958/5,175 | 1,266/1,707 · 1,128/1,595 · 1,281/1,707 | 159532–159932 |

O RSS é do **processo conjunto** WebGPU+GL, não VRAM nem custo isolado do
cache. Em 1024², a mediana quase não mudou, o p95 piorou em dois dos três
pares e o WebGPU segue bem atrás do GL. Por isso o cache permanece opt-in
até ensaios mais amplos. RTT direto e readback assíncrono não participam
deste A/B nem justificam ganho de latência aqui.

O comparador visual de cenas completas manteve PartDesign MAE RGB
0,001358/IoU 0,999978 e Assembly MAE 0/IoU 1 após inverter verticalmente
o GL. Esse comparador não exercita o cache de patches; o teste de múltiplos
devices é o oráculo desse caminho. Release Rust 42/42 e Debug Rust 42/42
passaram com o cache ligado; Recording 28/28 passou. A viewport real do
FreeCAD continua sem teste.

Reprodução, substituindo `SCENE` por `Assembly.iv` para a outra cena:

```sh
SCENE='/home/dikluwe/Área de trabalho/Estudo coin/estudos/So/SoWgpu-FreeCAD-Exemplos/cenas/PartDesign.iv'
for order in '0 1' '1 0'; do
  for rep in 1 2 3; do
    for mode in $order; do
      COIN_WGPU_CAMERA_BINDINGS="$mode" \
      VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/radeon_icd.json \
      COIN_GLX_PIXMAP_DIRECT_RENDERING=1 \
        /tmp/coin-wgpu-tekt-release/bin/wgpu_gl_benchmark --backend both \
        --readback color --dynamic --frames 120 --warmup 20 --size 512 \
        --scene "$SCENE"
    done
  done
done
```

## Separação de fases CPU/GPU (Prompt 008E, 2026-09-24)

Build Release Rust, AMD Radeon Graphics RADV RENOIR/Vulkan, `BASE_COLOR`,
RGBA-only, `COIN_WGPU_CAMERA_BINDINGS=1`, 40 frames após 10 warmup por
processo. Os campos `rust_cpu_detail` são spans de relógio CPU por frame;
`rust_gpu` vem de queries hardware (quatro timestamps: começo/fim dos
passes e começo/fim da cópia para staging). As queries só existem com
`COIN_WGPU_TRACE_PHASES=1 COIN_WGPU_GPU_TIMESTAMPS=1` e se o adapter
suportar os recursos exigidos. Todos os 40 frames medidos de cada cenário
retornaram `status=ok`. GPU render/copy não incluem criação CPU de recursos,
fila/espera do `device.poll`, map nem cópia para o vetor do chamador. A
query de cópia inclui cor e profundidade se profundidade for pedida.

Mediana/p95 em ms dos **frames traceados**; são percentis por coluna e
portanto não devem ser somados entre colunas:

| Cena/tamanho | Attachments CPU | Encode draws CPU | Staging CPU | Submit CPU | Espera CPU | Cópia cor CPU | Render GPU | Cópia GPU |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| PartDesign 512² | 0,0146/0,0163 | 0,0435/0,0484 | 0,0095/0,0134 | 0,0475/0,0517 | 0,2742/0,6271 | 0,0661/0,1000 | 0,0690/0,0904 | 0,0529/0,0691 |
| Assembly 512² | 0,0163/0,0207 | 0,0711/0,1207 | 0,0096/0,0132 | 0,0524/0,0622 | 0,3006/0,6769 | 0,0761/0,1133 | 0,0964/0,1198 | 0,0563/0,0668 |
| PartDesign 1024² | 1,3597/1,5506 | 0,1026/0,1487 | 0,0171/0,0249 | 0,0855/0,1185 | 0,4716/1,1708 | 1,2136/1,7586 | 0,0650/0,0765 | 0,1516/0,1989 |

O `rust_cpu_detail` ainda separa validação, setup/leitura da sonda,
snapshot, registro/recebimento do map, verificações, profundidade,
publicação e reciclagem. Em PartDesign 1024², a mediana de setup da
sonda foi 0,0485 ms e a de leitura da sonda 0,0017 ms. Em 512²,
attachments+encode e espera dominam relativamente; em 1024², a criação
dos attachments e a cópia CPU de cor são os maiores alvos isolados. O
tempo de espera CPU não é uma medição isolada de execução GPU.

Comparação **sem trace** em processo pareado WebGPU/CoinGL, mesma cena/GPU,
40 frames após 10 warmup, mediana/p95 do tempo total com readback:

| Cena/tamanho | WebGPU (ms) | CoinGL (ms) | Pico RSS WebGPU isolado (KiB) |
| --- | ---: | ---: | ---: |
| PartDesign 512² | 0,3686/0,6909 | 0,3467/0,8338 | 133976 |
| Assembly 512² | 0,4765/0,6649 | 0,3769/0,7310 | 140072 |
| PartDesign 1024² | 3,8188/4,6253 | 1,1924/1,4471 | 142996 |

RSS veio de execuções WebGPU-only sem trace (`/usr/bin/time -f %M`), não
é memória de GPU nem delta frente ao GL. Em execuções WebGPU-only dessa
medição, PartDesign 512² foi 0,3473/0,4735 ms, Assembly 512²
0,3967/0,4292 ms e PartDesign 1024² 3,8270/4,3839 ms. A variação
entre processos recomenda repetir/contrabalançar um futuro A/B; esta
instrumentação não reivindica aceleração. O comparador visual 512²
manteve, após inverter GL, PartDesign MAE RGB 0,001358/IoU 0,999978 e
Assembly MAE 0/IoU 1. A viewport real do FreeCAD não foi testada.
Release Rust 42/42, Debug Rust 42/42, Recording 28/28 passaram.

A suíte Release com as duas flags de trace passou 42/42 em execução serial.
Uma execução paralela `-j4` com as flags teve falha única no teste de janela
X11; o mesmo teste passou isolado sem trace, com trace CPU e com timestamps,
e passou na suíte serial. Este teste é sensível à concorrência de GPU/X11;
a falha paralela não foi reproduzida isoladamente.

Reproduzir com a cena normalizada existente (trocar por `Assembly.iv` para
a outra cena e usar `--size 1024` para o ensaio maior):

```sh
SCENE='/home/dikluwe/Área de trabalho/Estudo coin/estudos/So/SoWgpu-FreeCAD-Exemplos/cenas/PartDesign.iv'
VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/radeon_icd.json \
COIN_WGPU_CAMERA_BINDINGS=1 COIN_WGPU_TRACE_PHASES=1 \
COIN_WGPU_GPU_TIMESTAMPS=1 \
  /tmp/coin-wgpu-tekt-release/bin/wgpu_gl_benchmark --backend wgpu \
  --readback color --frames 40 --warmup 10 --size 512 --scene "$SCENE"

VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/radeon_icd.json \
COIN_GLX_PIXMAP_DIRECT_RENDERING=1 COIN_WGPU_CAMERA_BINDINGS=1 \
  /tmp/coin-wgpu-tekt-release/bin/wgpu_gl_benchmark --backend both \
  --readback color --frames 40 --warmup 10 --size 512 --scene "$SCENE"
```

Descartar as primeiras dez linhas `rust_cpu_detail`/`rust_gpu` do trace
(warmup). Comparar o tempo total somente com o segundo comando, sem trace.

## Próximo gate necessário para integração real

Fazer um build **isolado** do FreeCAD contra a mesma revisão do Coin/WebGPU,
integrar um target WebGPU ao widget Qt sem substituir o caminho GL atual e
testar nós específicos (`SoBrepFaceSet`, `SoFCSelection`, overlays), picking,
transparência, textura, resize e device loss. Não carregar lado a lado o Coin
embutido do FreeCAD e a biblioteca experimental ligada a outra revisão de
`libCoin.so.80` no mesmo processo.
