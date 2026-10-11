# Frente de fechamento neste Linux

Iniciada em 2026-10-10 na branch `codex/coin-render`. O
[checklist](coin-render-next-fronts-checklist.md) tinha 27 frentes abertas e
cinco critérios recorrentes de entrega no início desta rodada; o estudo da
linha 388 foi concluído abaixo. A classificação indica o que
este computador permite **executar**, não promete que uma investigação terá
resultado suficiente para marcar a caixa. Nenhuma falha visual ou gate será
reclassificado por mera ausência de reprodução.

| Linha do checklist | Alcance neste sistema | Próximo resultado verificável |
| --- | --- | --- |
| 58 — NPOT BGFX | Parcial | Repetir HDR/2047² e combinações Linux; D3D11 precisa de Windows. |
| 76 — rescaling, formatos | Parcial | Ampliar estado/formatos e FreeCAD Linux; Windows/Android em campanhas próprias. |
| 240 — borda fria FreeCAD | Local | Isolar a transição inicial sem alterar o critério de captura. |
| 260 — variação NVIDIA/wgpu | Local em curso | 16/16 A/B com clocks; associar amostras ao processo/frame antes de atribuir o outlier. |
| 270 — sampling API | Parcial | AVD e consumidores Linux; Windows e outros dispositivos seguem separados. |
| 331 — junções e endpoints | Concluído nesta rodada | Reprodução física, custo mínimo das alternativas e decisão de preservar o contrato portátil; melhorias futuras continuam separadas. |
| 346 — bindings/shapes | Concluído nesta rodada | 64 casos côncavos com subclasses/UV e 441 pares de binding em nove formas; correção de TriangleStripSet; 1.795 casos por rota. |
| 348 — depth clamp/formatos | Local | Separar clipping geométrico, range, precisão e formatos. |
| 351 — flat/sombras | Local em curso | QuadMesh preciso agora é recusado transacionalmente; implementar o fan central e qualificar flat com sombras/iluminação por fragmento. |
| 353 — outros drivers/FreeCAD | Parcial | AMD/NVIDIA e FreeCAD Linux; Windows e outros drivers ficam externos. |
| 363 — workbenches | Parcial | Consumidores reais Linux; matriz Windows externa. |
| 368 — arraste/workbenches | Parcial | Input e workbenches Linux; repetir adaptadores no Windows depois. |
| 388 — bbox/depth clamp | Concluído nesta rodada | Clipping e clamp distinguidos em sonda CoinGL; contrato portátil continua no item 348. |
| 407 — mips/recursos | Concluído em 2026-10-11 | Mips GPU diretos RGBA8/RGBA16F e 4096×1024 sob 64 MiB; seis perfis Linux e recuperação após rejeição. [Recibo](validation/rtt-direct-mips-wide-20261011/summary.md). |
| 452 — desempenho Windows | Externo | Exige host Windows e controle de clocks/latência. |
| 485 — igualdade janela/offscreen | Parcial em curso | 40/40 cidade/terreno passaram; capturar recorrência preta espontânea com leituras X11 repetidas e repetir os gates Windows. |
| 550 — bordas AMD/Vulkan | Local | Snap comum e testemunhos P20/câmera com alpha/ownership isolados. |
| 560 — fontes GL no Coin base | Local | Inventariar símbolos/objetos restantes e remover em etapas com ABI verificada. |
| 562 — Vulkan no AVD | Condicionado | Verificar imagem/driver; Goldfish atual falha no submit. |
| 564 — Intel física | Externo | Este host não oferece essa GPU. |
| 565 — CoinGL, nove unidades | Externo | GPUs locais anunciam oito unidades de coordenadas. |
| 589 — DPI/TDR Windows | Externo | Exige monitores com DPI distinto e host Windows. |
| 592 — Metal/Wayland físico | Externo | Metal indisponível; compositor físico/escala fracionária requer outro perfil. |
| 603 — Android duas janelas | Local | Ampliar o AVD x86_64 e preservar zoom/RTT. |
| 611 — milhão no AVD | Concluído como estudo em 2026-10-11 | Estado CPU 1.648→496 B; AVD 8 GiB passou interação/retomada, 4 GiB sofreu encerramento por falta de memória; 2 GiB não qualificado. [Recibo](validation/sparse-state-million-20261011/summary.md). |
| 614 — layout GPU compacto | Parcial | Diferença AMD/Vulkan/CoinGL aqui; Metal/D3D12 fora. |
| 616 — Android físico arm64 | Externo | Emulação x86_64 não substitui o dispositivo. |

## Primeiro controle executado: bounding box near/far

`CoinRenderBoundingBoxTest` agora inclui uma caixa girada com planos near/far
atravessando o centro, rejeição integral de cada lado e restauração A/B/A. O
controle compara CPU e GPU com CoinGL; as mudanças de imagem impedem que uma
cena constante aprove o ensaio. Em 96×80, a área visível foi 1728 pixels na
base, 1536 em cada corte e zero quando toda a caixa foi rejeitada. Máximo RGB
e MAE foram zero nas 12 comparações por backend. A regressão completa passou
com 143 comparações CoinGL em BGFX/OpenGL, BGFX/Vulkan e wgpu/Vulkan. Os
[recibos](validation/bbox-depth-20261010/) registram as saídas.

Este ensaio cobre clipping geométrico normal. A sonda separada
`--probe-gl-depth-clamp` habilitou `GL_DEPTH_CLAMP` apenas no CoinGL via
`SoCallback`, restaurou o estado após a caixa e comparou com a mesma cena sem
clamp. Near e far passaram de 1536 pixels sob clipping a 1728 sob clamp:
**192 pixels alterados em cada plano**
([log](validation/bbox-depth-20261010/coingl-depth-clamp.log)). Assim, o
clamp não pode ser inferido de um `SoDepthBuffer` com range limitado ou de
uma caixa que cruza o plano. O perfil atual de bounding box continua a usar
clipping geométrico; não há estado CoinRender portátil que solicite
`GL_DEPTH_CLAMP`. A implementação de tal estado, suas regras de rejeição e
a qualificação em formatos/drivers permanecem no item 348. Isso encerra o
**estudo específico da caixa**, não a funcionalidade geral de depth clamp.

Para reproduzir a sonda curta, compilar `CoinRenderBoundingBoxTest` em cada
build e executar `--capture --probe-depth-clip` e `--gpu --probe-depth-clip`.
As execuções GPU desta rodada usaram X11 `:0`, oráculo GLX NVIDIA com
`__NV_PRIME_RENDER_OFFLOAD=1`, `__GLX_VENDOR_LIBRARY_NAME=nvidia` e
`COIN_GLX_PIXMAP_DIRECT_RENDERING=1`; BGFX/OpenGL usou EGL Mesa AMD e as
rotas Vulkan usaram o ICD Radeon. A regressão completa foi executada sem a
opção de sonda; a sonda `--probe-gl-depth-clamp` usa somente CoinGL e não é um
gate de equivalência portátil.

## Segunda frente iniciada: primeira captura wgpu/OpenGL

O SDK wgpu e o consumidor visual instalados foram reconstruídos nesta branch.
No X11 `:0`, a AMD Renoir física executou doze processos da cidade estática
40 mil e um do terreno com um milhão de triângulos, todos native, 1280×960,
um warmup e dois frames. As **13 capturas iniciais e finais** tiveram RGB
máximo zero contra o offscreen e cobertura visível não nula
([CSV](validation/wgpu-gl-first-capture-20261010/summary.csv)). Isto não
reproduz a [imagem inicial preta histórica](coin-render-wgpu-opengl-first-present-20261010.md)
e não demonstra sua correção. A frente 485 segue aberta.

O fixture agora preserva o primeiro resultado e, somente se uma leitura X11
vier sem cobertura, registra quatro novas leituras e suas imagens sem render
adicional. Uma injeção limitada ao fixture (`COIN_VISUAL_TEST_EMPTY_XGETIMAGE=1`)
zerou a cópia da primeira leitura de propósito: as quatro leituras reais
seguintes viram 250473 pixels, enquanto o gate continuou a reprovar a primeira
captura. A execução sem injeção passou RGB zero nas duas capturas. O
[diagnóstico controlado](validation/wgpu-gl-first-capture-20261010/diagnostic.log)
valida a instrumentação, **não** reproduz a falha histórica. Na próxima
ocorrência real, os registros distinguirão uma janela que se preenche sem
render novo de uma falha persistente, e então poderemos direcionar o trace de
apresentação do wgpu.

## Terceira frente: junções CoinGL

O [estudo das junções](coin-render-raster-junctions-study.md) foi concluído
como decisão de arquitetura, com os testemunhos físicos já preservados e
contagem de geometria por alternativa. A expansão portátil atual usa no
mínimo quatro vértices e seis índices por segmento. Uma junção explícita por
quad soma quatro vértices e seis índices e acrescenta mistura alpha no ponto
compartilhado; inverter o segmento muda qual endpoint cobre o pixel. A decisão
é manter o contrato atual, sem afrouxar gates ou prometer igualdade de alpha
CoinGL. A melhoria de bordas P20 e a qualificação de outras geometrias seguem
em itens próprios.

## Quarta frente: variação NVIDIA/wgpu

O `benchmark_linux.py` repetiu A/B/B/A em NVIDIA/Vulkan, wgpu e BGFX, com
`texture-8` e `city-40000`: 16/16 processos válidos, sem readback. O
[CSV versionado](validation/nvidia-variance-20261010/summary.csv) conserva
as medianas de 90 frames após 30 aquecimentos. Em wgpu/cidade, as duas
execuções **native** foram 0,6561 e 0,5817 ms, amplitude de 12,8% em relação
à menor; portable foi 0,5789 e 0,5826 ms. Textura com oito unidades ficou
entre 0,5770 e 0,5820 ms em wgpu. BGFX ficou entre 0,5809 e 0,5981 ms nas
duas cargas. Amostras `nvidia-smi` a cada 250 ms durante a rodada registraram
clock gráfico de 210–1762 MHz (mediana 1702), memória de 405–7001 MHz e
temperatura de 53–57 °C. A amostragem agregada não associa cada frame ao
clock; não prova que clocks causaram o outlier. A frente 260 fica aberta.

Comando: `python3 testsuite/reproducers/sampling-api/benchmark_linux.py
--artifacts /mnt/Laranja/Git/externos/coin-portable-sampling-artifacts/20261008-sampling-api-linux
--name benchmark-nvidia-next-20261010 --profiles wgpu-nvidia-vulkan,bgfx-nvidia-vulkan
--workloads texture-8,city-40000 --frames 90 --warmup 30`. Foram usados os
binários congelados dessa campanha, hashes `ec0673c5f8f8` (wgpu) e
`5f7d58ffa436` (BGFX); a análise bruta permanece no diretório de artefatos,
`summary.json` SHA-256
`5a6f07a6ec16a1e70bfde0d3f2d2cb5f0ae63b7376fad744590ee1cbf2b72693`.

## Quinta frente: contornos côncavos

`CoinRenderGeometryViewportTest` passou a executar 16 cruzamentos de
`SoFaceSet`/`SoIndexedFaceSet` côncavos, fast path, transparência e viewport
externo como parte da regressão normal. A forma em “L” tem três amostras
independentes para impedir que uma triangulação em leque preencha o recorte.
BGFX/Vulkan, BGFX/OpenGL e wgpu/Vulkan na AMD física passaram 1306 casos por
rota; as 48 comparações côncavas CPU/GPU/CoinGL por rota tiveram máximo RGB 1.
O [recibo](validation/concave-geometry-20261010/summary.log) guarda identidades
e contagens. A expansão posterior com subclasses e UV consta da décima
terceira frente abaixo.

O gate já existente em `CoinRenderDrawStyleTest` passou novamente em
BGFX/Vulkan, BGFX/OpenGL e wgpu/Vulkan. Ele recusa o contorno não planar com
`UNSUPPORTED`, preserva pixels/serial e recupera o mesmo alvo após restaurar a
geometria. Portanto, a semântica **atual de rejeição** de offset não planar está
verificada; o suporte positivo a offset não planar segue fora do contrato
atual. Os cruzamentos com UV foram incluídos posteriormente.

## Sexta frente: iluminação precisa do QuadMesh

`COIN_QUADMESH_PRECISE_LIGHTING=1` reproduziu uma falha real no primeiro
QuadMesh iluminado da matriz, igual em BGFX/Vulkan e wgpu/Vulkan: CPU/GPU
máximo 1, CPU/CoinGL máximo 6 contra limite 4 no pixel (30,28). O CoinGL
subdivide cada quad com um vértice central nesse modo, enquanto a captura
portátil mantém a triangulação comum. O builder agora recusa QuadMesh sob
essa variável com `UNSUPPORTED`, antes de publicar a triangulação diferente.
Pixels e seriais permanecem intactos e a mesma action recupera ao renderizar
uma cena sem QuadMesh: 3/3 rotas físicas passaram o teste de rejeição.
O [recibo](validation/precise-lighting-20261010/summary.log) conserva a falha
anterior e os passes posteriores. O modo positivo não padrão continua aberto
no item 351. Sem essa variável, 1306 casos passaram em cada rota após a
correção.

## Sétima frente: repetição da primeira captura

Mais 40 processos wgpu/OpenGL X11 native alternaram cidade em movimento e
terreno de um milhão. Todos passaram RGB 0 em capturas inicial e final, sem
acionar as leituras diagnósticas extras. O
[recibo](validation/wgpu-gl-first-capture-20261010/stress-40.log) conserva a
contagem e o hash do CSV temporário. A falha intermitente histórica continua
sem reprodução causal; o item 492 não foi fechado.

## Oitava frente: cargas adicionais na NVIDIA

O runner congelado repetiu A/B/B/A em wgpu e BGFX/Vulkan físicos, com oito
texturas POT, oito NPOT, 40.000 e 1.000.000 de instâncias. Os **32/32
processos** passaram a validação de GPU, quantidade de frames e ausência de
readback. O [resumo por processo](validation/nvidia-variance-wide-20261010/summary.csv)
inclui hashes dos binários e cenas; o `summary.json` bruto e os CSV de frames
permanecem em
`/mnt/Laranja/Git/externos/coin-portable-sampling-artifacts/20261008-sampling-api-linux/benchmark-nvidia-wide-20261010`
(SHA-256 do resumo `9261eb57dd1850838249cd8c1f6c9dfcf152fde12f3d3df33c6d227fdf699407`).

As duas medianas BGFX por política diferiram no máximo **0,68%** entre
repetições nas quatro cargas. No wgpu, a maior amplitude foi **3,32%** no
milhão native (9,3107/9,6194 ms); o milhão portable foi 8,9979/8,9887 ms.
As 48 amostras medidas do milhão wgpu e as 48 BGFX não tiveram quadros acima
de 16,67 ms. Isso amplia as cargas sem reproduzir o salto histórico de 25%.
Uma amostragem NVML de 100 ms cobriu somente parte da rodada e não está
alinhada a cada frame; ela não permite atribuir causalidade aos clocks. O
estudo de variação e o trace de replay BGFX por carga seguem abertos no item
260; a ausência de pico nesta repetição não fecha a causa.

## Nona frente: fallback do QuadMesh preciso

O CoinGL só executa o fan central quando a coordenada é 3D, o material não
é por vértice e a textura não é procedural. O builder portátil recusava
todo QuadMesh com a variável ativa, inclusive os casos em que CoinGL usa
triangulação comum. A condição de rejeição agora acompanha a escolha do
CoinGL. O gate direcionado na NVIDIA/BGFX Vulkan confirmou rejeição
transacional no caso centrado, recuperação e aceitação do material por vértice
com CPU/GPU/CoinGL RGB máximo 1. A regressão completa passou 1306/1306 casos
sem a variável. O fan central positivo e os cruzamentos flat/sombras ainda
faltam; item 365 segue aberto.

## Décima frente: zoom Android entre superfícies

O host Android fazia `viewAll` a cada `INIT_WINDOW`, apagando o zoom ao
retornar da HOME. O enquadramento agora ocorre somente na primeira superfície
da Activity. O APK x86_64 foi recompilado, instalado e exercitado no AVD
AMD/GLES: o gesto alterou a distância focal de 1880,807 para 2668,993; três
recriações reais mantiveram 2668,993 no mesmo PID, com seriais e capturas
offscreen crescentes. O [recibo](validation/android-zoom-20261010/summary.md)
preserva APK, hash, gerações e limite. Duas janelas, AUTO e tickets/RTT entre
threads continuam abertos no item 642.

## Décima primeira frente: inventário GL do Coin base Android

O build Android x86_64 com `COIN_BUILD_LEGACY_GL_RENDERER=OFF` continua com
**112 símbolos GL indefinidos fracos** em `libCoin.so`; `libCoinRender.so`
não contém nenhum desses símbolos. `libCoin.so` não traz `DT_NEEDED` para
libGL/libGLES. A análise dos 926 objetos de `src` encontrou **81 objetos**
com referências GL; os maiores grupos incluem `SoGLRenderAction` (43 símbolos),
`SoExtSelection` (26), `SoRenderManager` (24) e `SoText2` (16). O cabeçalho
compatível Android declara `GLAPI` como `extern __attribute__((weak))`, o que
explica por que o link funciona sem resolver essas referências; não elimina
o acoplamento residual. Esse inventário delimita o item 599: separar ou
substituir os caminhos GL desses objetos mantendo a ABI e o teste de
rejeição do renderer legado ainda requer trabalho. Não foi removido nenhum
símbolo por exclusão cega de fontes.

## Décima segunda frente: custo do arraste do milhão Android

O trace do AVD de 8 GiB separou o arraste do quadro parado: dez patches de
câmera da versão inicial gastaram mediana de 130,3 ms em `frame_plan_ms` e
63,7 ms no backend; 537 quadros parados do trace reutilizaram o plano. O undo
por estado copiava cerca de um milhão de matrizes de view/projeção e `fogEnd`
para cada gesto. O undo compacto guarda só a câmera anterior e um bitset
dos estados cujo `fogEnd` mudou. O teste de 70 estados verificou a fronteira
de 64 bits e rollback exato; câmera na NVIDIA/BGFX Vulkan permaneceu RGB 0
contra reconstrução e máximo 1 contra CoinGL.

No mesmo AVD, 17 patches da variante nova tiveram `frame_plan_ms` mediano
34,1 ms, com backend ainda em 64,3 ms. O pior grupo de dez frames próximo
ao gesto foi de 10,15 FPS/98,5 ms, frente a 5,52 FPS/181,2 ms na rodada
anterior. É um A/B curto, com contagem de eventos diferente; os traces isolam
o ganho de CPU do plano, mas não fecham a campanha de caudas. Os
[recibos e limites](validation/android-million-camera-20261010/summary.md)
preservam APK, logs, memória e escopo. A frente 650 continua aberta.

## Décima terceira frente: subclasses e UV côncavos

As células côncavas agora incluem `SoFaceSet`, `SoIndexedFaceSet` e subclasses
reais de ambos, cruzados com fast path, alpha, viewport externo e UV com textura
2×2: **64 casos** por rota. Cada caso compara CPU/GPU/CoinGL e a amostra de
notch verifica que o recorte côncavo não foi preenchido. BGFX/Vulkan,
BGFX/OpenGL e wgpu/Vulkan físicos na NVIDIA passaram todos os 64 casos, máximo
RGB 1. A rejeição
transacional já existente de offset não planar passou nas mesmas três rotas.
O [recibo](validation/geometry-subclasses-uv-20261011/summary.md) guarda
comandos, adaptadores e hashes dos logs. A sonda posterior dos 49 pares de
material/normal encontrou RGB 11 contra CoinGL no TriangleStripSet com
material PER_FACE e normal PER_VERTEX. O callback normalizava o material por
face nos três vértices, enquanto GL suavizado conserva o material dos dois
vértices compartilhados. O builder agora reconstrói esses materiais pela
ordem de callbacks de cada strip. Nove formas × 49 pares passaram em todas
as três rotas, máximo RGB 1; a matriz integrada passou **1.795/1.795** por
rota. [Falha anterior e logs finais](validation/geometry-subclasses-uv-20261011/summary.md).
O item 358 está fechado dentro do contrato atual; offset não planar positivo
segue fora do contrato.

## Décima quarta frente: replay BGFX de textura nas duas GPUs

Em AMD e NVIDIA físicas, BGFX/Vulkan passou oito processos de textura-8 e
NPOT-8 sob native/portable, cada um com dois warmups e oito frames medidos.
Todos construíram plano e recursos no primeiro quadro e reutilizaram plano,
textura e buffer de geometria nos nove seguintes. A sonda não faz readback e
usa a mesma fixture congelada do benchmark A/B. O
[recibo](validation/replay-textures-dual-gpu-20261011/summary.md) traz hashes,
adaptadores e logs. Isso amplia o replay a outras GPUs/cargas; a variação
wgpu/NVIDIA observada historicamente ainda não tem causa isolada e o item 260
permanece aberto.
