# Cenas grandes no Windows — 2026-10-02

Branch local: `codex/coin-render-large-scenes`, baseada em `4e72bfa2ba`.
Windows 10, i5-4670K, 16 GiB RAM, GTX 1060 6 GB, driver 581.08.

## Cena reproduzível

`examples/coinrender/generate_large_scene.py` cria uma cidade Open Inventor
determinística, com seed 136, oito materiais, duas luzes direcionais, ruas entre
quarteirões, alturas variadas e uma base. Cada edifício referencia o mesmo nó
Cube, com transformação e material próprios. Compartilhar esse nó não implica
instancing na GPU. O arquivo não contém câmera: o benchmark enquadra o conjunto
com a mesma câmera perspectiva para todos os renderizadores.

| Grade | Edifícios | Triângulos na cena, incluindo a base | Arquivo |
|---|---:|---:|---:|
| 10 × 10 | 100 | 1.212 | 13.798 bytes |
| 50 × 50 | 2.500 | 30.012 | 327.589 bytes |
| 100 × 100 | 10.000 | 120.012 | 1.313.169 bytes |
| 200 × 200 | 40.000 | 480.012 | 5.285.711 bytes |

Os arquivos gerados e capturas ficam em `H:/Git/coin/build/large-scenes` neste
computador. O gerador é versionado; os arquivos grandes são regeneráveis.

## Resultados iniciais

Renderização offscreen a 1024 × 1024, incluindo RGBA readback síncrono.
São medidas de cena estática, não FPS de uma janela interativa. Os logs completos
estão em [validation/large-scenes-windows](validation/large-scenes-windows/).

| Caminho | Edifícios | Aquecimento / quadros medidos | Mediana | P95 | Resultado |
|---|---:|---:|---:|---:|---|
| wgpu/D3D12 | 100 | 2 / 8 | 17,08 ms | 17,94 ms | Passou |
| wgpu/D3D12 | 2.500 | 2 / 8 solicitados | — | — | Erro de bind group no primeiro quadro |
| wgpu/D3D12 | 10.000 | 3 / 12 solicitados | — | — | Mesmo erro |
| wgpu/Vulkan | 10.000 | 3 / 12 | 387,58 ms | 467,67 ms | Passou |
| wgpu/Vulkan | 40.000 | 2 / 8 | 1.340,99 ms | 1.371,11 ms | Passou |
| Coin/OpenGL | 10.000 | 3 / 12 | 17,67 ms | 22,62 ms | Passou |
| Coin/OpenGL | 40.000 | 8 / 20 | 35,55 ms | 36,99 ms | Passou |

No ensaio OpenGL de 40 mil com apenas dois quadros de aquecimento, o máximo foi
540,80 ms. Com oito quadros de aquecimento, o máximo foi 37,22 ms; ambos os logs
são preservados. Uma comparação visual das capturas de 10 mil objetos mostrou
diferença média absoluta RGB de 0,0145 / 0,0380 / 0,0232 em canais de 0 a 255.
Isso é uma observação da imagem completa, não uma tolerância de regressão.

O D3D12 reportou `BindGroup with 'Draw eight-unit texture bindings' label is
invalid` em `RenderPass::set_bind_group`. O mesmo gerador funciona no controle
pequeno e nos outros caminhos. A causa do erro de recursos ainda exige
diagnóstico; não foi corrigida nem convertida em aprovação.

O ensaio Vulkan de 40 mil demorou mais de nove minutos para encerrar, apesar
de os oito quadros medidos totalizarem 10,65 s. A mediana aquecida não representa
esse custo total. A maior amostra de memória do processo observada durante
a execução foi 553.070.592 bytes de peak working set; não é uma medida de VRAM.
As capturas de 40 mil em GL/Vulkan tiveram diferença média RGB de
0,0145 / 0,0304 / 0,0229 por canal.
Carregamento, primeiro quadro e cleanup passaram a ter marcas de tempo no
benchmark para localizar esse custo em uma próxima execução. As marcas e
a exportação PPM foram verificadas em um controle D3D12/OpenGL de 100 objetos
a 256 × 256, no binário final; esse controle não substitui a medição grande.

A suíte anterior 99/99 usa outras fixtures e não certifica essa carga de cenas
grandes. Esses ensaios iniciais foram feitos com wgpu. O conector BGFX/Windows
foi implementado em seguida, com resultados próprios registrados abaixo.

## BGFX/Direct3D12 no Windows

As três cargas grandes passaram em 1024 × 1024, com quatro quadros de
aquecimento e oito medidos, incluindo readback síncrono:

| Edifícios | Mediana | P95 |
|---|---:|---:|
| 2.500 | 20,75 ms | 22,59 ms |
| 10.000 | 423,81 ms | 451,76 ms |
| 40.000 | 1.630,15 ms | 1.672,61 ms |

O novo backend renderizou a cidade de 40.000 edifícios em 1024 × 1024, com
quatro quadros de aquecimento e oito medidos: mediana 1.630,15 ms, P95
1.672,61 ms. O primeiro quadro levou 4.023,4 ms e o cleanup, 132,71 ms.
A imagem é pixel a pixel idêntica à captura wgpu/Vulkan da mesma cidade.

Essa execução já usa um índice por matriz para a preparação dos estados de
desenho. A implementação anterior fazia uma busca quadrática e o primeiro
teste BGFX de 40 mil foi interrompido ainda durante essa preparação.
O orçamento da biblioteca BGFX foi compilado para 131.072 chamadas de desenho.
Esse resultado confirma funcionamento de D3D12 nessa carga; o tempo por
quadro dessa implementação motivou a correção descrita a seguir.

O diagnóstico encontrou duas causas no nosso backend: o limite de cache
de 32 MiB fazia repetir lowering/upload, e cada edifício gerava um draw.
A correção mantém os buffers GPU mesmo quando libera a cópia CPU grande
e junta geometria opaca PHONG compatível. Com tracing de fases/timestamps,
as medianas caíram para 12,34 ms em 10 mil e 14,19 ms em 40 mil. As duas
capturas continuam pixel a pixel idênticas às anteriores. O primeiro
quadro ainda precisa de captura, conversão e upload; os ganhos representam
quadros estáticos aquecidos.

Na repetição final sem diagnóstico, usando 4/8 quadros em 1024 × 1024:

| Edifícios | BGFX/D3D12 antes | BGFX/D3D12 corrigido | Ganho |
|---|---:|---:|---:|
| 2.500 | 20,75 ms | 11,86 ms | 1,75× |
| 10.000 | 423,81 ms | 12,20 ms | 34,73× |
| 40.000 | 1.630,15 ms | 14,36 ms | 113,51× |

As três capturas são idênticas às anteriores. O primeiro quadro de 40 mil
ainda levou 4.184,59 ms. O ganho não inclui esse custo inicial nem certifica
mudanças de câmera ou geometria. A nova validação dirigida passou em 47
testes distintos, contando a reexecução das fixtures finais.

O build, os testes Win32 e a qualificação dirigida estão documentados em
[CoinRender BGFX no Windows](coin-render-bgfx-windows.md).

## Primeiro quadro: diagnóstico e correção

Em 2026-10-02, a investigação do primeiro quadro encontrou custo de captura
e cópias de geometria na CPU, hashing repetido de materiais, crescimento dos
vetores de conversão e reserva excessiva dos buffers GPU. A correção transfere
o plano capturado sem copiar seus vetores, reaproveita a captura de materiais
iguais no triângulo, evita procurar coordenadas de unidades de textura
desativadas, reserva a conversão de uma vez e memoiza a assinatura de materiais
uniformes preservando a sequência FNV anterior. A composição calcula apenas
a profundidade necessária, mantendo a divisão homogênea e suas validações.
Programas de peeling/OIT passam a ser criados quando a estratégia os exige.

Três pares alternados de processos, com a versão anterior `5f30dee635` e a
corrigida, mediram a cidade de 40.000 edifícios em BGFX/D3D12, 1024 × 1024:

| Execução | Primeiro quadro anterior | Primeiro quadro corrigido |
|---|---:|---:|
| 1 | 3.572,06 ms | 2.920,06 ms |
| 2 | 3.544,37 ms | 2.884,36 ms |
| 3 | 3.485,52 ms | 2.877,66 ms |
| Mediana | **3.544,37 ms** | **2.884,36 ms** |

A redução foi de **18,62%**, com o mesmo checksum RGBA em todas as execuções.
Esse ensaio mede o primeiro `apply`, incluindo captura e readback síncrono.
Carregamento do arquivo e a consulta inicial de disponibilidade do backend
ficam fora desse intervalo. Cada processo usou um quadro de aquecimento e um
medido depois do primeiro; os contadores de diagnóstico estavam desativados.
Caches do sistema operacional e do driver foram preservados, portanto o
ensaio representa processos novos na máquina em uso, sem simular um reboot.

Uma execução separada com diagnóstico levou 3.050,86 ms e decompôs o custo:

| Fase | Tempo |
|---|---:|
| Captura da cena | 1.077,57 ms |
| Montagem/validação do plano | 230,52 ms |
| Validação no alvo | 239,48 ms |
| Preparação do alvo/backend | 292,72 ms |
| Conversão para BGFX | 437,97 ms |
| Upload | 99,76 ms |
| Espera até o readback | 667,19 ms |

Conversão, upload e espera são partes da submissão; os totais de backend nos
logs já incluem essas fases. O contador BGFX reportou 19,45 ms de GPU no primeiro
quadro. A espera inclui processamento de recursos, trabalho da thread de
render/driver, sincronização e leitura; a compilação de pipelines não foi
medida isoladamente. Captura e inicialização continuam sendo alvos de trabalho.
A geometria ainda está expandida em 1.440.036 vértices, sem instâncias GPU.

A capacidade dos buffers caiu de 2.097.152 para 1.441.792 elementos. A memória
GPU reportada caiu de 455.135.232 para 329.306.112 bytes, economizando **120 MiB**.
No controle estático final, com 4/8 quadros, 40 mil teve mediana **12,08 ms** e
P95 12,32 ms; 10 mil teve mediana 11,04 ms e P95 11,76 ms. As duas capturas RGB
são pixel a pixel idênticas às anteriores.

Os resultados finais combinam **152 testes distintos aprovados**, incluindo
sombras GPU obrigatórias, D3D12/Vulkan/OpenGL, transparência e RTT. A execução
inicial foi interrompida após timeouts; os casos foram reexecutados e os logs
originais preservados. A fixture de estilos percorreu mais de 15 mil capturas
e seu prazo no Windows passou de 60 para 120 segundos, mantendo as verificações.
Um controle com as DLLs anteriores também mostrou atraso de inicialização:
a fixture de iluminação passou em 90,11 s, contra 20,24 s na repetição corrigida.
Esses dois tempos registram a variação do ambiente e não são uma comparação
de desempenho entre versões.

Logs, XMLs, resultados por teste, hashes e o método estão em
[first-frame-summary.json](validation/bgfx-windows/first-frame/first-frame-summary.json).

### Validação do primeiro quadro em Vulkan e OpenGL

O mesmo código `cad421ab74` foi testado em BGFX/Vulkan e BGFX/OpenGL no
Windows, na GPU NVIDIA. Cada API teve três pares alternados com as DLLs
anteriores `5f30dee635`, em processos novos, sem tracing nem testes GPU
concorrentes. O método e a exclusão do carregamento do arquivo são os mesmos
do ensaio D3D12. Para 40.000 edifícios em 1024 × 1024:

| API | Primeiro anterior, mediana | Primeiro corrigido, mediana | Redução | Aquecido, mediana | Aquecido, P95 |
|---|---:|---:|---:|---:|---:|
| BGFX/Vulkan | 3.624,71 ms | **2.902,66 ms** | **19,92%** | **10,87 ms** | 11,11 ms |
| BGFX/OpenGL | 3.406,56 ms | **2.753,89 ms** | **19,16%** | **13,32 ms** | 20,59 ms |

Os controles aquecidos de 10.000 edifícios mediram 8,87 ms de mediana / 13,12 ms
de P95 em Vulkan e 13,91 ms / 16,68 ms em OpenGL. Os controles aquecidos usam
quatro quadros de aquecimento e oito medidos, incluindo readback RGBA síncrono.

Nas duas APIs e nas duas cidades, a captura corrigida é pixel a pixel idêntica
à captura anterior da mesma API. Os checksums RGBA também se mantiveram iguais.
Vulkan é idêntico a D3D12 nas duas cidades. OpenGL difere de D3D12 em apenas
dois pixels de 1.048.576 (0,00019%) em cada cidade; essas diferenças também
estão presentes na versão anterior. O maior delta de canal foi 119 em 10.000
e 88 em 40.000. A causa desses dois pixels entre APIs não foi isolada.

Esta rodada acrescenta benchmarks e controles de imagem, sem alterar o código
de renderização ou repetir a qualificação anterior de 152 testes. Os logs,
amostras individuais, hashes e diferenças de imagem estão em
[cross-api-summary.json](validation/bgfx-windows/first-frame/cross-api/cross-api-summary.json).
Para reproduzir com o comando abaixo, altere `COIN_BGFX_RENDERER` para
`vulkan` ou `opengl`.

## OpenGL tradicional e wgpu após a otimização BGFX

O build wgpu foi recompilado com o código comum atual (`cad421ab74`, fonte
`fc8bad6a21`). Esta rodada separa o OpenGL tradicional do Coin, o OpenGL do
wgpu e o OpenGL do BGFX. Os ensaios são sequenciais, sem tracing, em
1024 × 1024, com readback RGBA síncrono. As medianas aquecidas usam quatro
quadros de aquecimento e oito medidos. Carregamento do arquivo e consulta de
capacidade continuam fora do tempo do primeiro quadro.

| Caminho | Edifícios | Primeiro quadro | Aquecido, mediana | Aquecido, P95 | Resultado |
|---|---:|---:|---:|---:|---|
| Coin/OpenGL tradicional | 10.000 | 470,28 ms | 15,32 ms | 16,54 ms | Passou |
| Coin/OpenGL tradicional | 40.000 | **663,67 ms** | **35,36 ms** | 39,01 ms | Passou |
| wgpu/Vulkan | 10.000 | 967,62 ms | 409,53 ms | 595,75 ms | Passou |
| wgpu/Vulkan | 40.000 | **3.346,18 ms** | 1.445,01 ms | 2.085,81 ms | Passou |
| wgpu/OpenGL | 10.000 | 13.603,30 ms | 6.561,96 ms | 16.583,90 ms | Passou |
| wgpu/OpenGL | 40.000 | 43.231,70 ms | 24.935,40 ms | 26.403,40 ms | Passou |
| wgpu/D3D12 | 10.000 e 40.000 | Falhou | — | — | Bind group inválido |

O primeiro quadro do Coin/OpenGL em 40 mil é a mediana de três processos
novos: 663,671 / 664,414 / 655,850 ms. wgpu/Vulkan também teve três processos
novos, com 3.346,18 / 3.410,24 / 3.339,64 ms. Na execução aquecida separada,
seu primeiro quadro levou 4.659,89 ms. Os demais primeiros quadros desta tabela
são amostras individuais. As medianas BGFX da rodada anterior foram 2.884,36 ms
em D3D12, 2.902,66 ms em Vulkan e 2.753,89 ms em OpenGL; os quadros aquecidos
foram 12,08 / 10,87 / 13,32 ms, respectivamente. O Coin/OpenGL começa mais
rápido nesta cena; o BGFX otimizado apresenta menor tempo por quadro estático.
Os ensaios não medem câmera em movimento nem uma janela interativa.

O controle de 100 edifícios passou nos três backends wgpu, com adaptadores
NVIDIA identificados como `Dx12`, `Gl` e `Vulkan`, sem fallback para outra API.
D3D12 continua falhando no primeiro quadro das duas cidades grandes com
`BindGroup with 'Draw eight-unit texture bindings' label is invalid` em
`RenderPass::set_bind_group`. Nenhum resultado de desempenho ou imagem grande
foi atribuído a esse backend.

O encerramento wgpu/Vulkan levou 14,25 ms em 10 mil e 49,13 ms em 40 mil.
O processo completo de 40 mil terminou em 24,52 s; a demora de mais de nove
minutos do ensaio inicial não se repetiu. Isso registra esta execução, sem
atribuir uma causa ou declarar corrigida aquela demora. wgpu/OpenGL em 40 mil
levou 324,05 s para o processo completo (5 min 24 s), com cleanup de 48,03 ms.
Nesse caminho, o tempo foi consumido antes do cleanup: somente os oito
quadros medidos totalizaram 201,42 s.

As imagens atuais do Coin/OpenGL e do wgpu/Vulkan são pixel a pixel idênticas
às imagens anteriores do mesmo caminho, em ambas as cidades. wgpu/Vulkan é
idêntico ao BGFX/D3D12. wgpu/OpenGL também é idêntico ao BGFX/D3D12 em ambas
as cidades. Coin/OpenGL difere em valores RGB pequenos já observados antes:
em 40 mil, a diferença média absoluta por canal é 0,0145 / 0,0304 / 0,0229
na escala 0–255, com 63.723 pixels distintos e maior delta de canal 31.
As métricas registram diferenças entre renderizadores, sem definir tolerância.

O código Rust mantém `camera_scene_eligible` limitado a 32 MiB de payload
CPU e cria bindings/uniforms por draw quando essa retenção não se aplica.
As otimizações de retenção dos buffers GPU e agrupamento de draws implementadas
em `CoinBgfxBackend`/`CoinBgfxLowering` não foram portadas para esse caminho
wgpu. Isso identifica trabalho ainda necessário no backend; esta rodada não
isola quanto cada fase responde pelos tempos altos nem corrige o erro D3D12.

Logs, hashes dos binários, amostras e comparações de imagem estão em
[legacy-wgpu-summary.json](validation/bgfx-windows/first-frame/legacy-wgpu/legacy-wgpu-summary.json).
Não houve alteração no código de renderização nem repetição da suíte completa.
Para reproduzir, use `--backend gl` no benchmark para Coin/OpenGL, ou o build
`RUST_BRIDGE` com `--backend wgpu` e `WGPU_BACKEND=vulkan`, `dx12` ou `gl`.

## Correção comum da captura do primeiro quadro

A captura montava material, iluminação, câmera, viewport e estado de desenho
novamente para cada triângulo. Os 40 mil edifícios usam um material por cubo:
o estado era montado 12 vezes durante a emissão de seus 12 triângulos.
O capturador comum agora reutiliza esse estado por índice de material durante
uma ocorrência de `SoCube`, `SoCone`, `SoCylinder` ou `SoSphere` preenchidos,
sem imagens de textura habilitadas nem função de coordenadas primária.
A geometria e as validações continuam completas; não houve agrupamento ou
instancing novo nesta correção.

A reutilização termina no post-callback da forma e no reset do quadro.
Portanto, reutilizar o mesmo nó sob outra transformação ou material exige
nova captura. Subclasses continuam no caminho completo, pois podem alterar
estado em `generatePrimitives`. O Core oferece uma consulta protegida às
listas reais de callbacks, incluindo registros herdados e callbacks tail;
callbacks adicionais da aplicação também desativam a reutilização, mesmo
quando registrados através de `SoCallbackAction *`. A mudança não altera
layout nem métodos virtuais de `SoCallbackAction`.

Esta lógica fica em `CoinRenderAction`/`CoinRenderFramePlanBuilder`, antes da
seleção de BGFX ou wgpu. Os backends e shaders não foram alterados. O OpenGL
tradicional usa `SoGLRenderAction`, fora desse capturador: seu controle de
40 mil continuou funcionando, com 723,62 ms no primeiro quadro e 35,90 ms de
mediana aquecida, preservando a imagem anterior. Não se atribui a ele um ganho
por esta mudança.

Comparação com as DLLs preservadas de `25cc463e27`, três pares alternados em
processos novos, 40.000 edifícios, 1024 × 1024, incluindo readback RGBA:

| Caminho | Primeiro antes, mediana | Primeiro depois, mediana | Redução observada |
|---|---:|---:|---:|
| BGFX/D3D12 | 3.777,38 ms | **2.777,61 ms** | **26,47%** |
| BGFX/Vulkan | 3.169,95 ms | **2.499,80 ms** | **21,14%** |
| BGFX/OpenGL | 3.258,08 ms | **2.364,33 ms** | **27,43%** |
| wgpu/Vulkan | 3.275,47 ms | **2.703,64 ms** | **17,46%** |

Nenhuma amostra foi removida. O par inicial D3D12 levou 9.941,26 / 7.444,62 ms,
mostrando variação de inicialização maior que nos pares seguintes. Os tempos
excluem carga do arquivo e consulta inicial de capacidades, mantêm caches do
sistema/driver e não representam inicialização após reboot. Os logs individuais
permitem avaliar a variação, sem comparar diretamente essas medianas com as
de outra rodada.

Em um par separado com tracing, a captura BGFX/Vulkan de 40 mil caiu de
**1.029,89 para 473,80 ms**, redução de aproximadamente **54%**. A montagem e
validação do plano continuaram em 231,82 / 267,03 ms, e o trecho de backend em
1.632,43 / 1.719,08 ms. Esses últimos valores não indicam otimização dessas
fases. O primeiro quadro ainda captura 1.440.036 vértices expandidos e precisa
de validação, preparação do alvo, conversão, upload e espera de readback.

Em wgpu/OpenGL, o controle diagnóstico de **10 mil** reduziu a captura de
263,42 para 125,21 ms. A submissão permaneceu dominante, em 9.136,27 /
9.399,48 ms; o primeiro quadro total foi 9.622,93 / 9.655,81 ms. Assim, esse
controle confirma melhora na captura comum, sem afirmar ganho no tempo total
ou substituir a medição anterior de 40 mil. wgpu/D3D12 ainda falha no primeiro
quadro grande com o mesmo bind group inválido.

Os controles aquecidos finais de 40 mil mediram 12,57 ms em BGFX/D3D12,
10,68 ms em BGFX/Vulkan, 12,94 ms em BGFX/OpenGL e 1.426,26 ms em wgpu/Vulkan.
O primeiro controle D3D12 corrigido oscilou (mediana 38,91 ms / P95 118,70 ms).
Uma repetição antes/depois mediu 12,38 / 12,57 ms, com P95 13,02 / 13,17 ms;
o ensaio oscilante permanece registrado. Todos os controles de imagem
antes/depois são idênticos na mesma API, inclusive wgpu/OpenGL em 10 mil.

As regressões comparam a captura otimizada com a captura completa para as
quatro formas, índices de material por parte, nós compartilhados e alterações
entre quadros. Elas verificam ainda mudanças de profundidade entre callbacks
de triângulo e dentro de uma subclasse de `SoCube`.

A qualificação reúne os **163 casos do build BGFX** e **8 fixtures wgpu/Vulkan**.
A execução BGFX inicial teve 129 aprovados, 32 sombras puladas por falta do
opt-in GPU e dois `SEGFAULT` nos testes de transparência. A captura CPU desses
testes passou com as DLLs anteriores e atuais, usando o mesmo executável
diagnóstico, inclusive com a referência OpenGL obrigatória. Os dois testes
completos passaram em uma repetição. A causa das falhas iniciais não foi isolada.

Depois de remover toda a instrumentação temporária e reconstruir o teste
original, uma execução de **49 casos** exigiu GPU de sombras e referências
OpenGL (`COIN_RENDER_REQUIRE_BGFX_SHADOW_GPU=1`,
`COIN_RENDER_REQUIRE_GL_REFERENCE=1` e `COIN_WGPU_REQUIRE_GL_REFERENCE=1`).
Ela aprovou todos os casos, incluindo transparência nos três renderizadores.
Os resultados finais por nome consolidam 163 aprovados, sem casos pulados;
isso combina a suíte inicial com a repetição obrigatória, e não representa
uma única execução integral sem falhas. Os resultados iniciais estão preservados.

Os prefixos locais `build/coin-render-bgfx-install` e `build/coin-render-install`
receberam as duas DLLs e o header do Core atualizado. Os hashes instalados são
iguais aos dos binários testados; `Coin4.dll` e `CoinRender4.dll` devem ser
atualizados juntos por causa da nova consulta protegida no Core.

Logs, hashes, amostras, diferenças de imagem e resultados de testes estão em
[common-capture-summary.json](validation/bgfx-windows/first-frame/common-capture/common-capture-summary.json).

## Reproduzir

```powershell
python examples/coinrender/generate_large_scene.py city-40000.iv --grid 200
cmake --build build-win --config Release --target coin_render_gl_benchmark
$env:WGPU_BACKEND = 'vulkan' # trocar por dx12 para D3D12
& ./build-win/bin/coin_render_gl_benchmark.exe --scene city-40000.iv `
  --backend wgpu --size 1024 --warmup 2 --frames 8 `
  --image-output city-40000-vulkan.ppm
# Para a referência: --backend gl. Aquecimento usado na repetição: 8; quadros: 20.
```

`--image-output` salva PPM fora do intervalo medido, corrigindo a origem vertical
do readback GL. Só é aceito no modo síncrono com cópia RGBA. O benchmark também
registra carregamento, primeiro quadro e cleanup, separadamente da mediana aquecida.

Para repetir o primeiro quadro no build BGFX desta máquina, a partir de
`H:/Git/coin`, execute um processo novo por amostra:

```powershell
$env:COIN_BGFX_RENDERER = 'd3d12'
& ./build/coin-render-bgfx-msvc/bin/coin_render_gl_benchmark.exe `
  --scene ./build/large-scenes/city-40000.iv --backend bgfx `
  --size 1024 --warmup 1 --frames 1
# Para investigar as fases CPU, habilite antes de uma execução separada:
$env:COIN_RENDER_TRACE_PHASES = '1'
```
