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

## Correção dos caminhos wgpu para cenas grandes

Esta rodada parte de `2ff5166496`, depois da correção comum de captura.
Ela resolve a falha D3D12 e o custo elevado de submissão wgpu observado nas
rodadas anteriores, sem mudanças no backend BGFX nem no OpenGL tradicional.

Preservar o primeiro erro assíncrono revelou `Not enough memory left` em
`Device::create_bind_group`. O erro posterior de bind group inválido escondia
a causa. O encoder criava um buffer de uniforms e uma tabela de oito samplers
por desenho, inclusive quando todos usavam os mesmos recursos padrão.
O heap de samplers do wgpu-hal/D3D12 tem 2.048 descritores; esse padrão esgotava
o heap antes de terminar a cena grande.

O encoder compartilhado por alvos de janela e offscreen agora coloca os
uniforms em buffers com offsets dinâmicos alinhados. Cada binding cobre um
registro, e o buffer é dividido conforme os limites do dispositivo e um teto
de 64 MiB por bloco. Uma tabela de bindings serve aos desenhos que usam os
mesmos recursos naquele bloco/passe. O caminho experimental
`COIN_WGPU_CAMERA_BINDINGS=1` também usa essa arena; ele retém o buffer de
materiais, sem voltar à alocação de uma tabela por desenho.

O dispositivo retém vértices, índices e materiais de um quadro privado
imutável com revisão não nula, até 256 MiB. Uma nova revisão invalida os
buffers; o cache não substitui a validação dos dados recebidos. Os caminhos
com revisão zero e cache por nó continuam funcionando como antes.
Na cidade estática de 40 mil, a geometria retida soma 149.763.744 bytes;
os quadros seguintes registram zero uploads de geometria e um cache hit.

O transporte privado C++ agrupa a sequência completa de triângulos opacos
compatíveis em um desenho. Ele exige pelo menos 256 desenhos, sem texturas,
sombras, flags de composição, camadas especiais, fog, clipping ou polygon
offset; os intervalos devem ser contíguos, disjuntos e cobrir os arrays.
A ordem dos índices e os materiais por vértice são preservados. Posições e
normais são transformadas para o espaço da câmera, sem normalizar as normais
na CPU. Qualquer diferença no estado efetivo mantém a sequência original.
O plano do Core permanece intacto. Alterar a câmera exige reconstruir os
vértices transformados, em vez de aplicar o patch a vértices já transformados.
`COIN_WGPU_DISABLE_OPAQUE_BATCHING=1` permite um controle no mesmo binário.

Os pipelines sem sombras removem o trabalho de shadow receiver inativo.
A presença de textura é uma constante de compilação, com variantes separadas
no cache de pipelines; mudar apenas a textura continua reutilizando a variante.
O cache de programas OpenGL do wgpu-hal 24 omite as constantes de especialização
na chave. O módulo usa IDs de shader distintos para variantes com/sem textura
nesse backend, evitando reutilizar um programa compilado com o valor anterior.
Os perfis reais de quatro/oito sombras permanecem completos. O protocolo
privado continua na revisão 42 e a ABI pública não muda. A elegibilidade do
snapshot CPU de câmera continua restrita a BASE_COLOR/32 MiB; a cidade PHONG
grande não depende dessa elegibilidade.

Medição final, 40.000 edifícios, 1024 × 1024, renderização e readback RGBA:

| Caminho | Primeiro antes | Primeiro depois | Mediana aquecida depois |
|---|---:|---:|---:|
| wgpu/D3D12 | Falha no quadro grande | **1.700,99 ms** | **32,95 ms** |
| wgpu/Vulkan | 2.479,27 ms | **1.486,41 ms** | **31,64 ms** |
| wgpu/OpenGL | 31.908,80 ms | **1.721,43 ms** | **35,47 ms** |

Vulkan e OpenGL usam três pares antes/depois alternados, cada amostra em um
processo novo, com um aquecimento e um quadro medido; o primeiro quadro é
informado separadamente. D3D12 tem um controle anterior que falha e três
processos novos corrigidos. As colunas de primeiro quadro são medianas de
três amostras, sem remoção de resultados. O controle aquecido corrigido usa
quatro aquecimentos e oito quadros medidos por API.

Os três controles anteriores de um quadro aquecido tiveram medianas de
1.393,37 ms em Vulkan e 24.892,90 ms em OpenGL.
Essas amostragens aquecidas têm contagens diferentes das corrigidas; os logs
preservam cada valor, sem tratá-las como uma série de oito quadros antes/depois.
As medições excluem carga do arquivo e consulta inicial de capacidades,
mantêm caches do driver/sistema e não representam inicialização após reboot.
O primeiro quadro ainda precisa capturar, validar, transformar e carregar
a geometria, além de compilar o pipeline e esperar pelo readback.

Todos os seis pares de imagens estáticas antes/depois são idênticos por pixel.
As imagens corrigidas são também idênticas entre D3D12, Vulkan e OpenGL:
checksum RGBA `0x6714299260985122`. Um controle separado com câmera em movimento
confirma imagens idênticas entre as três APIs corrigidas. Na comparação Vulkan
antes/depois, dois dos 1.048.576 pixels diferem (máximo de 140 por canal,
diferença absoluta média de 0,000143 por canal). Portanto, o controle com câmera
em movimento não é uma equivalência estrita por pixel com o caminho anterior;
a geometria é reconstruída e a pequena diferença está registrada.
Um controle no mesmo binário com `COIN_WGPU_DISABLE_OPAQUE_BATCHING=1`
reproduz a imagem anterior exatamente, isolando os dois pixels no caminho
de agrupamento/transformação dos vértices.

Os tempos aquecidos da tabela correspondem à cena estática. Com mudança de
câmera a cada quadro, as medianas de três quadros foram 1.467,71 ms
em D3D12, 1.500,81 ms em Vulkan e 1.655,48 ms em OpenGL.
Cada alteração exige nova captura/reconstrução e upload de geometria nesse
caso PHONG grande; não se atribui a esses controles o ganho estático de 32–35 ms.

A qualificação final aprovou **100/100 CTests em Vulkan**, em uma execução
integral, com testes GPU de sombras e referências OpenGL obrigatórios,
sem falhas nem casos pulados. Outros **29/29 casos em D3D12** passaram com o
experimento de câmera habilitado. Eles incluem materiais,
iluminação Gouraud, texturas/multitexturas, fog/clipping, estilos de desenho,
peeling/OIT, transparência, composição, RTT, múltiplos dispositivos e execução
assíncrona. Os **24 testes Rust** também passaram, incluindo validação Naga
dos cinco perfis de shader e seus entry points.

Em OpenGL, **10/29 casos passaram**, incluindo a nova regressão de bindings,
fog, texturas, Gouraud, RTT direto e múltiplos dispositivos. Os **19 casos
restantes falham também com as DLLs anteriores**: o controle usa os mesmos
executáveis e argumentos, sem a nova regressão, e aprova 9/28 casos.
O conjunto de nomes que falham é idêntico antes/depois. Eles expõem limitações
já existentes de cópia de profundidade, `textureLoad` de depth em GLSL/peeling,
strokes e readback assíncrono. Essa rodada qualifica a cidade opaca com readback
de cor em OpenGL, sem afirmar paridade funcional completa desse backend.
O contorno do cache de programas corrigiu duas regressões de especialização
(fog após textura e cor/orientação de RTT), que passaram no controle final.

A nova regressão GPU mantém 25.600 desenhos sem agrupamento, atravessa o
limite de um bloco de uniforms e verifica a cor de cada célula após reuse,
mudança de material e mudança de câmera. A regressão CPU cobre agrupamento,
transformação não uniforme, materiais por vértice, reconstrução de câmera,
imutabilidade do plano original e fallback para estados diferentes ou matrizes
projetivas. O teste existente de desempenho passa a exigir uma variante nova
ao remover textura, em vez de exigir a reutilização de um pipeline incompatível.

A execução inicial tinha 65 aprovados, 32 sombras puladas e três falhas.
Dois fixtures novos usavam defaults inadequados (screen-door e BACK culling);
o terceiro tinha a expectativa anterior à especialização por textura.
As correções e a execução final integral estão registradas separadamente.
A primeira execução OpenGL teve 21 falhas, antes do contorno do cache de
programas. Ela, os controles anteriores e as 19 falhas preexistentes finais
estão preservados separadamente.

`build/coin-render-install` recebeu as DLLs qualificadas; os hashes instalados
coincidem com os do build. `Coin4.dll` permaneceu idêntica à baseline desta
rodada e `CoinRender4.dll` contém as correções. A reutilização de bindings reduz
a pressão de descritores para recursos iguais; cenas com milhares de tabelas
de texturas distintas continuam sujeitas aos limites do dispositivo.

Logs, amostras, hashes, testes e diferenças de imagem estão em
[wgpu-large-scenes-summary.json](validation/bgfx-windows/first-frame/wgpu-large-scenes/wgpu-large-scenes-summary.json).
Os controles intermediários de arena, agrupamento e shader são diagnósticos
de binários anteriores ao build final e estão identificados como tais.

## Profundidade e transparência wgpu/OpenGL (02/10/2026)

A investigação seguinte parte de `827603713e`, com as 19 falhas OpenGL
preservadas na rodada anterior. As falhas de linhas, multitextura, materiais,
composição e leitura assíncrona compartilhavam uma causa: a cópia de
`Depth32Float` para buffer exige `DEPTH_TEXTURE_AND_BUFFER_COPIES`, que o
adaptador wgpu/OpenGL não oferece. A validação invalidava o encoder de cópia,
afetando também a publicação da cor daquele quadro.

O módulo agora usa um passe fullscreen para transportar a profundidade para
uma textura de cor `R32Float` antes da leitura quando aquela capability falta.
O passe faz `textureLoad` por coordenada inteira, sem sampler, filtragem ou
quantização. O staging, seu padding e os tickets síncronos/assíncronos mantêm
o mesmo formato de floats de 32 bits. A textura intermediária custa quatro
bytes por pixel e só existe quando há pedido de profundidade nesse fallback;
o pipeline fica retido por dispositivo. A cópia direta permanece nos
adaptadores que oferecem a capability.

O peeling tinha duas causas adicionais. O Naga 24 rejeita `textureLoad` de
`texture_depth_2d` ao gerar GLSL. Nesse backend, os bindings de profundidade
passam a floats não filtráveis, um tipo que o wgpu aceita para views de depth.
O shader lê o canal `r`, preservando o valor. Isso cobre os perfis padrão,
linhas, pontos, quatro/oito sombras e o compositor de camadas.

Além disso, o caminho `copy_texture_to_texture` do wgpu-hal/GLES 24 prende a
origem em `COLOR_ATTACHMENT0`, inclusive para o snapshot de profundidade.
Mesmo após o shader compilar, o snapshot opaco não chegava corretamente às
camadas. O OpenGL agora copia esse snapshot com outro passe fullscreen,
gravando `frag_depth` em uma textura `Depth32Float`. Ele usa a mesma alocação
de snapshot prevista no orçamento de peeling; os limites de camadas e bytes
continuam iguais. As texturas de profundidade dos alvos offscreen e de janela
recebem uso de sampling para permitir esses passes.

A nova regressão GPU desenha um padrão assimétrico de seis valores, incluindo
floats adjacentes, em 67 × 5 pixels. Ela faz o snapshot e a conversão para
`R32Float`, lê linhas de 512 bytes e compara todos os bits com o padrão
esperado. As regressões de shader geram GLSL de fato, além de validar WGSL,
para os cinco perfis de peeling e os passes de transferência/composição.
Nenhuma tolerância dos testes funcionais existentes foi aumentada. A ABI
pública e o protocolo privado 42 permanecem iguais.

Validação final nesta máquina, MSVC Release, wgpu 24 e GTX 1060/driver 581.08:

| Execução | Resultado |
|---|---:|
| wgpu/OpenGL, bateria integral | **100/100**, zero falhas e zero skips no JUnit |
| wgpu/Vulkan, regressões entre APIs | **29/29** |
| wgpu/D3D12, regressões entre APIs | **29/29** |
| Rust Release/offline, testes em série | **27/27** |

Os **19 nomes que falhavam na baseline agora passam**. As execuções CTest
exigem `COIN_RENDER_REQUIRE_GL_REFERENCE=1`,
`COIN_WGPU_REQUIRE_GL_REFERENCE=1` e
`COIN_RENDER_REQUIRE_WGPU_SHADOW_GPU=1`. Vulkan e D3D12 também habilitam
`COIN_WGPU_CAMERA_BINDINGS=1`. A regressão de preservação dos bits de
profundidade foi executada explicitamente nas três APIs.
O OpenGL original do Coin participa como referência; a implementação desse
backend não foi alterada.

O controle da cidade estática de 40 mil edifícios manteve **zero pixels
diferentes** em relação às imagens de `827603713e`, nas três APIs.
Todas as imagens têm checksum RGBA `0x6714299260985122` e SHA-256 PPM
`77afc06bc324407024bc0168d99d5c6703ab81f064ee4c4f33e1235d39e60b0e`.
Este controle usa um processo por API, 1024 × 1024, quatro aquecimentos e
oito quadros medidos, com leitura de cor habilitada e de profundidade desabilitada:

| API | Primeiro quadro | Mediana aquecida |
|---|---:|---:|
| OpenGL | 1.654,02 ms | 34,64 ms |
| Vulkan | 1.479,55 ms | 30,79 ms |
| D3D12 | 1.685,98 ms | 33,76 ms |

Os caches do sistema/driver permanecem habilitados. Esses valores são um
controle de preservação do caminho opaco, não uma medição do custo adicional
do novo readback de profundidade.

As DLLs qualificadas foram instaladas em `build/coin-render-install`, com
hashes iguais aos do build. `Coin4.dll` continua idêntica à baseline;
`CoinRender4.dll` tem SHA-256
`ccf6137d5462acee8b72f38943633007e8dd420aa796e4543c0a4bea70615575`.
Logs, lista das 19 falhas resolvidas, hashes e controles de imagem estão em
[wgpu-gl-errors-summary.json](validation/bgfx-windows/first-frame/wgpu-gl-errors/wgpu-gl-errors-summary.json).
A rodada parcial de diagnóstico, que isolou a falha do snapshot depois da
correção inicial de readback/shader, está identificada separadamente; a
qualificação integral é a execução final de 100 testes.

## Detalhes da camada comum do CoinRender — 2026-10-02

Esta investigação concentra-se em `CoinRenderAction`, `CoinRenderFramePlanBuilder`,
`CoinRenderFramePlan::isValid` e `coin_render_composition_order`, compartilhados
pelos caminhos wgpu e BGFX. A instrumentação separa custos CPU aninhados nos
registros existentes; não altera geometria, política de validação ou API pública.

Foram executados 48 processos: três amostras sem tracing por variante e API,
uma execução separada com tracing nas seis combinações, e controles com 100 e
10.000 prédios e resolução 256×256 no Vulkan de cada backend. A cidade de
40.000 prédios usa 1.440.036 vértices/índices e 40.001 desenhos. A tabela traz
intervalos CPU de uma execução com tracing por API, em milissegundos. Somente
as colunas de primeiro quadro usam a mediana das três amostras sem tracing.

| Caminho | Captura/travessia | Validações completas | Total das validações | Três classificações/composições | Primeiro quadro baseline / instrumentado |
| --- | ---: | ---: | ---: | ---: | ---: |
| wgpu D3D12 | 448,98 | 2 | 392,75 | 90,16 | 1.662,28 / 1.683,94 |
| wgpu Vulkan | 438,26 | 2 | 387,78 | 88,10 | 1.478,52 / 1.456,46 |
| wgpu OpenGL | 450,20 | 2 | 402,53 | 91,20 | 1.669,65 / 1.627,16 |
| BGFX D3D12 | 451,54 | 3 | 608,30 | 94,94 | 2.342,79 / 2.379,34 |
| BGFX Vulkan | 469,37 | 3 | 589,38 | 84,64 | 2.407,47 / 2.483,60 |
| BGFX OpenGL | 461,47 | 3 | 590,84 | 86,87 | 2.181,46 / 2.195,17 |

As variações entre baseline e instrumentado são controles da medição, não
ganhos de uma otimização. Caches de sistema/driver foram mantidos; não houve
reinicialização ou limpeza de cache de shaders. Os números dependem desta
máquina, driver e cena. Tempos filhos já pertencem aos tempos de seus pais:
por exemplo, não se deve somar `builder_detail.validation_ms` com sua respectiva
linha `validation_detail`, nem somar ambos novamente a `action.frame_plan_ms`.

Os gargalos comuns encontrados são:

1. **Validação repetida do mesmo plano.** O builder valida antes de entregar;
   o target valida antes de preparar/submeter; o lowering BGFX valida uma
   terceira vez. Os registros `validation_detail` aparecem nessa ordem.
   A conferência de vértices custa aproximadamente 154–162 ms por passagem,
   estados aproximadamente 29–31 ms e desenhos aproximadamente 10–13 ms.
   Otimizar a função beneficia os dois backends. Reduzir passagens exige uma
   garantia privada de que o conteúdo validado permanece imutável; somente
   confiar em `revision` não protege contra alterações de um plano mutável.
2. **Classificação repetida da geometria.** Builder, target e empacotamento/
   lowering chamam a mesma classificação. São cerca de 85–95 ms no total.
   A maior parte está em `classify_ms`; `sort_ms` custa cerca de 3 ms por
   passagem. Mesmo com materiais opacos, a função percorre os índices,
   confere materiais dos vértices e calcula profundidades. Há oportunidade
   de reaproveitar a classificação vinculada ao conteúdo validado, preservando
   a política de transparência, profundidade e dados não finitos.
3. **Expansão e armazenamento na captura.** O plano contém 144.003.600 bytes
   de vértices de 100 bytes cada, 5.760.144 bytes de índices e 65.601.640 bytes
   em 40.001 estados de 1.640 bytes. Só esses três payloads somam 205,39 MiB;
   não representam toda a RAM ou memória GPU. A capacidade reservada para
   vértices é 157.480.300 bytes. O mesmo cubo compartilhado na cena acaba
   expandido por ocorrência. Os sete pares de coordenadas adicionais ocupam
   56 bytes por vértice, ou 80.642.016 bytes neste plano sem texturas.
   Uma representação mais compacta ou geometria compartilhada pode beneficiar
   captura e transporte, mas exige tratar os demais tipos de primitivas e
   multitextura, além desta cidade.

A expansão de estilos custa cerca de 1,5–1,8 ms e a publicação por transferência
de propriedade, incluindo o reset do builder, cerca de 4–5 ms. Esses trechos
não são os maiores alvos deste caso. Com plano estático reutilizado, os registros
de validação e composição desaparecem nos quadros seguintes e captura/construção
ficam abaixo de 0,02 ms nos controles; a solução atual já reaproveita esse trabalho.

Os controles de tamanho confirmam a relação com o payload: no wgpu Vulkan,
10.000 prédios custaram 115,07 ms de captura e 98,33 ms nas duas validações;
40.000 custaram 438,26 e 387,78 ms. No BGFX Vulkan, foram 118,50/148,78 ms
e 469,37/589,38 ms, respectivamente. Reduzir a resolução de 1024×1024 para
256×256 manteve as validações em 390,70 ms no wgpu e 593,15 ms no BGFX.
Assim, baixar resolução não resolve estas varreduras CPU. Custos de leitura
da cena e enquadramento da câmera são registrados separadamente pelo benchmark;
não pertencem ao intervalo atual de primeiro quadro do `CoinRenderAction`.

### Diagnóstico isolado da classificação de floats

O objeto MSVC Release de `CoinRenderFramePlan.cpp` referencia `_fdclass` na
classificação de floats. Para separar esse custo do restante do renderer,
um executável de diagnóstico reproduziu a conferência dos 24 floats por
vértice, o passo de 100 bytes e a conferência do material em 1.440.036 vértices.
Em dez amostras alternadas, a mediana foi **150,213 ms com `std::isfinite`**
e **31,6114 ms com a classificação do expoente IEEE binary32**. As duas
classificações concordaram em 65.552 padrões, incluindo zeros com sinal,
subnormais, extremos finitos, infinitos e NaNs. Código, configuração e log
estão junto às evidências.

Esse resultado torna a conferência de floats o primeiro candidato para uma
otimização comum. Ele não é uma melhoria já aplicada à DLL, uma prova de ganho
do quadro inteiro ou uma proposta de retirar validações. A implementação
precisará preservar todas as rejeições, os diagnósticos e portabilidade para
representações de float suportadas, com testes dos campos afetados.

### Verificação e novos registros

As imagens instrumentadas ficaram byte a byte iguais às respectivas baselines
nas seis APIs. Foram aprovados 5/5 testes com tracing ligado: `FrameCore`,
`FrameReuseCore`, `DiagnosticShell`, `IndexedFastPath` e `Composition`, sem skips.
Não houve alteração do shader ou protocolo Rust/BGFX nesta rodada.

`COIN_RENDER_TRACE_PHASES=1` agora emite também:

- `validation_detail`: materiais, vértices, índices, estado da cena,
  estados de renderização e desenhos; falhas mostram os intervalos percorridos.
- `composition_detail`: classificação e ordenação.
- `builder_detail`: expansão de estilos, validação, composição e publicação.
- `plan_storage`: quantidades, tamanhos e capacidades dos vetores do plano.

O benchmark também informa `startup_detail`, `scene_detail` e `*_first_detail`
para distinguir preparação anterior ao quadro e entrega da imagem. Os tempos
`since_main` começam em `main`, excluindo carregamento do processo; não são
medidas do lançamento do programa. `--backend both` ainda executa os caminhos
sequencialmente, portanto seu segundo resultado não representa uma execução
independente desde o início do processo.

Logs, hashes, controles e método estão em
[common-details-summary.json](validation/bgfx-windows/first-frame/common-details/common-details-summary.json).
A prioridade indicada pela medição é otimizar a conferência de floats, depois
examinar reaproveitamento de validação/composição e expansão da geometria.

## Otimizações da camada comum do CoinRender — 2026-10-03

As melhorias indicadas pela investigação anterior foram aplicadas à DLL
`CoinRender4`, compartilhada pelos caminhos wgpu e BGFX. O `Coin4.dll` e seu
OpenGL tradicional permanecem com os mesmos hashes da baseline `3633e6a5eb`.

### Implementação e preservação do contrato

- A classificação dos floats usa o expoente IEEE binary32 quando o tipo tem
  essa representação, com `memcpy` para respeitar aliasing. Outras representações
  usam `std::isfinite`. Todos os campos continuam validados, com os mesmos
  diagnósticos. Infinitos e NaNs continuam rejeitados; zeros com sinal,
  subnormais e extremos finitos continuam aceitos.
- Unidades de textura desabilitadas deixam de construir snapshots e matrizes
  temporárias na validação e composição. Seus programas de combinação continuam
  sendo verificados, inclusive os floats das unidades desabilitadas.
- A composição deixa de ordenar vetores que já estão ordenados. A comparação
  e a ordenação estável para transparência, camadas e empates são preservadas.
- O target entrega aos executores a ordem que acabou de validar, por meio de
  um objeto privado que existe apenas durante a submissão síncrona e corresponde
  ao endereço exato do plano. A revisão não serve como prova e o objeto não é
  armazenado entre quadros. Chamadas independentes e planos diferentes seguem
  o caminho anterior sem esse reaproveitamento. Isso elimina a terceira
  classificação nos dois executores e a terceira validação de plano no BGFX.
  Builder e target ainda validam
  separadamente: a action pode ajustar o plano entre essas etapas.
- Cubos nativos, preenchidos e sem textura reutilizam vértices idênticos dentro
  de cada desenho e ocorrência: 36 passam a 24. Uma tabela fixa de face/canto
  encontra candidatos e a comparação integral dos atributos decide a igualdade.
  Normais de face, coordenadas, materiais, ordem dos triângulos e limites dos
  desenhos são preservados. Subclasses, callbacks observadores, funções de
  textura, linhas e pontos mantêm a captura anterior. Não há compartilhamento
  entre ocorrências transformadas.

O tracing também deixou de depender de um símbolo da DiagnosticShell no header
do timer; os executáveis de teste que compilam o packer isoladamente voltaram
a linkar no build completo. As variáveis de tracing e seu alias mantêm o mesmo
comportamento. Não houve alteração de shaders, protocolo Rust/BGFX ou API pública.

### Primeiro quadro e armazenamento

Cena com 40.000 prédios, 40.001 desenhos e imagem 1024×1024. Cada resultado de
primeiro quadro é a mediana de três processos novos por variante, alternando
antes/depois, sem tracing, com renderização e cópia RGBA síncrona. O controle
aquecido usa um processo adicional, quatro quadros de aquecimento e oito
medidos. A baseline é a DLL anterior preservada, executada nesta mesma rodada.
Os tempos são CPU wall time e incluem espera/readback; não são tempos GPU
isolados. Caches do sistema e do driver foram mantidos, sem reiniciar a máquina.

| Caminho | Primeiro antes (ms) | Primeiro depois (ms) | Redução | Aquecido antes → depois (ms) |
|---|---:|---:|---:|---:|
| wgpu D3D12 | 1725.82 | 1189.00 | 31.1% | 34.71 → 26.57 |
| wgpu Vulkan | 1550.41 | 952.16 | 38.6% | 32.66 → 24.86 |
| wgpu OpenGL | 1692.86 | 1117.85 | 34.0% | 35.59 → 27.66 |
| bgfx D3D12 | 2427.60 | 1807.07 | 25.6% | 12.66 → 12.50 |
| bgfx Vulkan | 2496.31 | 1832.21 | 26.6% | 10.64 → 10.84 |
| bgfx OpenGL | 2264.97 | 1639.10 | 27.6% | 13.34 → 12.74 |

No plano comum, os vértices capturados passaram de **1.440.036 para 960.024**,
mantendo **1.440.036 índices**. O payload de vértices caiu de 144.003.600 para
96.002.400 bytes. Somados aos índices e estados de renderização, esses payloads
passaram de **205,39 para 159,61 MiB**, uma redução de 45,78 MiB (22,3%).
Esses números não incluem toda a RAM do processo ou memória da GPU.

No wgpu, o payload de geometria empacotada caiu de 149.763.744 para 101.762.544
bytes. O lowering BGFX ainda expande os vértices pelos índices e mantém
1.440.036 vértices de saída; não há redução equivalente de upload GPU no BGFX.
A captura e a validação comuns ficam menores nos dois caminhos.

Os processos separados com tracing mostram o trabalho removido no Vulkan:

| Caminho | Captura (ms) | Passagens de validação / soma (ms) | Passagens de composição / soma (ms) |
|---|---:|---:|---:|
| wgpu antes | 475.79 | 2 / 429.49 | 3 / 99.21 |
| wgpu depois | 430.51 | 2 / 81.81 | 2 / 42.76 |
| bgfx antes | 456.00 | 3 / 613.33 | 3 / 91.45 |
| bgfx depois | 444.42 | 2 / 83.25 | 2 / 45.44 |

Essas somas consideram apenas os intervalos internos de cada passagem;
não devem ser adicionadas aos timers pais do builder ou target. As faixas das
três amostras e os controles aquecidos estão no JSON. Os custos que restam na
preparação, pipelines e execução de cada backend continuam medidos separadamente.

### Qualificação e equivalência

As seis imagens estáticas ficaram byte a byte iguais às respectivas baselines,
incluindo SHA-256 do PPM e checksum da saída RGBA. Quatro controles adicionais
com 10.000 prédios, câmera e material alterados a cada quadro, também produziram
imagens iguais no Vulkan de wgpu e BGFX. São controles de equivalência; uma
amostra por variante nesses modos não estabelece uma distribuição de desempenho.

Os testes novos comparam a geometria expandida dos cubos, bit a bit, com a
referência nativa de callbacks, incluindo materiais por face, ocorrências
transformadas e dimensões zero/negativas. Conferem ainda 2.048 padrões de float,
NaN/infinito em todos os 24 campos do vértice, matrizes e combinação de textura,
além da restrição e limpeza do objeto de validação após sucesso e falha.

O build completo passou nos dois backends. A qualificação wgpu terminou com
**100 casos Vulkan**, **32 OpenGL** e **32 D3D12**, sem skips. A execução inicial
do Vulkan teve 99 casos aprovados e uma comparação de logs que pressupunha
36 vértices por cubo. Esse teste passou a comparar a sequência expandida de
primitivas, mantendo a referência com observador sem otimização; a repetição
de `ActionTest` e cinco casos de regressão passou (6/6). O log original da
falha está preservado para tornar essa correção rastreável.

A suíte completa BGFX terminou com **163/163 casos aprovados**, 0 skips,
incluindo parametrizações D3D12/Vulkan/OpenGL, superfícies, transparência,
profundidade, sombras e renderização em textura. As referências OpenGL nativas
foram obrigatórias nesta rodada. A execução inicial BGFX aprovou 131 casos
e pulou 32 casos de sombras GPU que exigem uma flag explícita; esses 32 foram
executados novamente com `COIN_RENDER_REQUIRE_BGFX_SHADOW_GPU=1`, sem skips.
O log inicial também está preservado. No wgpu, as sombras GPU foram
obrigatórias desde a primeira execução Vulkan.

Logs, scripts de reprodução, resultados JUnit e hashes estão em
[common-optimizations-summary.json](validation/bgfx-windows/first-frame/common-optimizations/common-optimizations-summary.json).
As instalações locais `build/coin-render-install` e
`build/coin-render-bgfx-install` foram atualizadas e conferidas contra os builds.

## Reaproveitamento de cubos e comparação com Coin/OpenGL — 2026-10-03

O maior custo comum que restou após `113ee2b4f8` foi a captura, com cerca de
430 ms no Vulkan. Esta rodada acrescenta um template limitado à geometria de
um cubo, aprendido da própria sequência nativa de callbacks. Ele permite
reaproveitar posições, normais, coordenadas e índices em ocorrências seguintes
com dimensões bit a bit iguais e o mesmo binding de normais.

O caminho exige o tipo exato `SoCube`, preenchido, sem unidades de textura
habilitadas, sem função de coordenadas e com material `OVERALL`. Também exige
que não existam callbacks externos de shape/primitivas. Subclasses, materiais
por face/parte, linhas, pontos e observadores continuam no caminho anterior.
O cache é um único template de aproximadamente 2,5 KiB, invalidado a cada
quadro; não retém o nó nem detalhes de primitivas. Mudanças de dimensões ou
binding provocam nova captura nativa. `fastPathEnabled=false` impede o replay.

Material, iluminação, transformações, clipping, câmera, viewport, profundidade,
sombras, camadas e demais estados continuam capturados em cada ocorrência.
Os vértices são copiados para o intervalo próprio do desenho e recebem o slot
atual de material. Não há instancing GPU nem aliasing de geometria entre
ocorrências. A quantidade de vértices, índices e bytes permanece igual à da
rodada anterior; a melhoria elimina geração e processamento repetidos na CPU.

### Resultados pareados

40.000 prédios em 1024×1024, três processos novos antes/depois por API,
alternando as versões, sem tracing. A baseline é o build `113ee2b4f8`
preservado antes desta alteração. Primeiro quadro inclui renderização e cópia
RGBA síncrona; carregamento da cena e consulta de capacidade ficam fora.
O Coin/OpenGL foi medido em três processos novos nesta rodada. O controle
aquecido usa quatro quadros de aquecimento e oito medidos. Caches do driver
e do sistema foram mantidos e os processos GPU foram executados em sequência.

| Caminho | Primeiro antes (ms) | Primeiro depois (ms) | Redução adicional | Aquecido depois (ms) |
|---|---:|---:|---:|---:|
| Coin/OpenGL nativo | — | 630.14 | — | 34.85 |
| wgpu D3D12 | 1191.00 | 1087.34 | 8.7% | 26.01 |
| wgpu Vulkan | 942.06 | 819.15 | 13.0% | 24.56 |
| wgpu OpenGL | 1101.83 | 996.48 | 9.6% | 27.92 |
| bgfx D3D12 | 1784.46 | 1680.07 | 5.8% | 12.27 |
| bgfx Vulkan | 1830.92 | 1745.58 | 4.7% | 10.67 |
| bgfx OpenGL | 1661.26 | 1531.86 | 7.8% | 12.48 |

Os controles separados com tracing mostram a captura e o número de replays:

| Caminho Vulkan | Captura antes → depois (ms) | Cubos reaproveitados |
|---|---:|---:|
| wgpu | 429.27 → 300.75 | 39999 |
| bgfx | 440.18 → 322.74 | 39999 |

O replay reduz o custo comum, mas não torna os primeiros quadros equivalentes
ao Coin/OpenGL. Os intervalos de preparação, empacotamento, pipelines e espera
por readback continuam separados nos logs. No BGFX, a expansão da geometria
e o bootstrap do readback ainda custam mais no primeiro quadro. Nos quadros
estáticos aquecidos, os caminhos otimizados já ficam abaixo do Coin/OpenGL
nesta cena. Estes resultados valem para o adaptador e driver desta máquina;
as faixas das três amostras estão no JSON.

### Verificação

Os builds completos wgpu e BGFX passaram. Seis testes iniciais de Core,
Action, composição, reutilização e packing passaram. 84 casos adicionais
passaram nas seis APIs, sem skips, cobrindo captura, materiais, profundidade,
texturas, iluminação, clipping, estilos, transparência, sombras e RTT.
As referências OpenGL e a execução GPU de sombras foram obrigatórias.

As comparações novas cobrem materiais diferentes em ocorrências transformadas,
profundidade alternada, dimensões alteradas entre ocorrências e quadros,
desenhos que se juntam no mesmo intervalo e observadores que devem receber
todos os 24 triângulos de dois cubos. A expansão da captura continua igual à
referência nativa, incluindo atributos bit a bit e materiais por face.

As seis imagens estáticas e quatro controles de câmera/material em movimento
ficaram byte a byte iguais às respectivas baselines. O Coin/OpenGL manteve
o checksum entre suas três execuções; os hashes de `Coin4.dll` também
permaneceram iguais. As instalações locais dos dois backends foram atualizadas
e verificadas por SHA-256 contra os builds.

Logs, scripts, JUnit, hashes e medições estão em
[cube-replay-summary.json](validation/bgfx-windows/first-frame/cube-replay/cube-replay-summary.json).

## Geometria indexada no BGFX/OpenGL — 2026-10-03

A captura compartilhada já fornecia 24 vértices e 36 índices por cubo,
mas o lowering BGFX expandia novamente um vértice por índice. Esta alteração
mantém um remapeamento local ao item de composição, convertendo cada índice
de origem apenas uma vez. A ordem dos índices continua intacta, assim como
todos os atributos, as transformações e as assinaturas de material. O hash
de material continua calculado por ocorrência no fluxo original de índices.

O remapeamento exige pelo menos 256 desenhos no quadro e ausência de sombras.
O item precisa passar pelas regras existentes de batching opaco PHONG: sem
blend, screen door, overlay, clear-depth ou atributos de strokes, e com
transformações afins. Reutiliza até 16 KiB de scratch
para intervalos de até 4096 vértices. Os demais caminhos mantêm a expansão
anterior. Não há compartilhamento entre itens, desenhos, transformações ou diferentes posições na ordenação
transparente. `firstVertex`/`vertexCount` refletem o intervalo compacto, também
quando os desenhos opacos se juntam em um batch. Os shaders não mudaram.

Na cidade de 40.000 prédios, o backend passa de 1.440.036 para 960.024 vértices:
33,3% menos vértices convertidos e enviados. O payload de vértices BGFX cai de
258,19 para 172,12 MiB; os 1.440.036 índices e o batch opaco único permanecem.
Essa redução afeta a conversão CPU, a cópia para upload e a espera pela GPU.

### Medição pareada

Baseline `30f1f4018f`, três processos novos antes/depois por API, alternando as
versões, 1024×1024, `--warmup 1 --frames 3`. O primeiro quadro inclui renderização
e cópia RGBA síncrona; parsing e capability probe ficam fora. Não há tracing
nas medianas. Os processos foram executados em sequência, sem compilação ou
testes GPU concorrentes. Caches do driver e do sistema foram mantidos.
O controle aquecido separado usa quatro quadros de aquecimento e oito medidos.

| Caminho | Primeiro antes (ms) | Primeiro depois (ms) | Redução | Aquecido depois (ms) |
|---|---:|---:|---:|---:|
| BGFX OpenGL | 1600.43 | 1286.25 | 19.6% | 13.13 |
| BGFX Vulkan | 1761.76 | 1519.78 | 13.7% | 10.09 |
| BGFX D3D12 | 1695.99 | 1440.13 | 15.1% | 11.78 |

O controle OpenGL com tracing separa os intervalos:

| Intervalo | Antes (ms) | Depois (ms) |
|---|---:|---:|
| Lowering | 214.29 | 155.01 |
| Upload | 90.92 | 59.40 |
| Espera pelo readback | 599.62 | 416.28 |

A preparação do renderer permanece incluída no primeiro quadro. Os logs separam
a preparação da submissão. A espera pelo readback inclui trabalho
pendente de criação, upload e renderização no driver; não é uma medida isolada
de custo de cópia dos pixels. As faixas das três amostras estão no JSON.

Após a suíte, novos controles do Coin e wgpu usaram o mesmo tamanho/cena,
três processos por caminho e quatro quadros de aquecimento/oito medidos:

| Controle | Primeiro quadro (ms) | Aquecido (ms) |
|---|---:|---:|
| Coin/OpenGL | 642.63 | 36.46 |
| wgpu OpenGL | 981.47 | 28.08 |
| wgpu Vulkan | 809.41 | 24.65 |

O BGFX/OpenGL se aproxima dos outros caminhos no primeiro quadro, mas ainda
não os iguala nesta máquina. O benefício aquecido é menor, porque os buffers
estáticos já eram reutilizados; a principal redução está no primeiro upload.

No controle OpenGL de 10.000 prédios com material mudando, a mediana aquecida de
três processos caiu de 346,58 para 285,37 ms. Uma amostra depois ficou em 456,12 ms;
ela foi mantida no cálculo e a faixa completa está no JSON. O controle de câmera
teve um processo por versão, de 348,28 para 300,73 ms, sem inferência estatística.

### Verificação e instalação

O build Release e o teste Core passaram. O teste Core adicional compara bit a bit o
fluxo de atributos expandido com a entrada compacta, com materiais diferentes,
300 transformações, batching opaco e os fallbacks para transparência e intervalos
maiores. A suíte final de 50 casos passou, sem falhas nem skips, com renderer padrão
OpenGL. Os três casos adicionais de clipping em Vulkan/camadas e OpenGL/OIT/camadas
também passaram. Inclui janelas,
readback, materiais, Gouraud, texturas/multitextura, clipping, estilos, profundidade,
transparência, sombras GPU, múltiplos targets e RTT. Referência OpenGL e GPU de
sombras foram obrigatórias.

A tentativa inicial aplicava índices também aos desenhos pequenos e transparentes.
A rodada de 152 casos foi interrompida após um erro de acesso à memória em
clipping Vulkan/camadas e dois timeouts em clipping OpenGL. A repetição Vulkan
passou. O teste OpenGL/OIT direto passou, mas custou 81,60 s contra 8,97 s na
baseline. Essa tentativa ampla foi descartada. Na versão final com guard de
batching opaco, os três testes passaram, com 4,81 s em OpenGL/OIT e 2,70 s em
OpenGL/camadas. Os logs da tentativa e das rechecagens foram preservados.

Uma rodada posterior de 34 processos também foi descartada integralmente após
os tempos da própria baseline oscilarem: o primeiro OpenGL chegou a 28.949 ms,
com mediana aquecida de 173,7 ms. Uma nova rodada completa foi executada após
observar baixa carga na GPU. Todas as amostras cronometradas finais ficaram
abaixo de 30 ms na mediana aquecida. Não foram removidas amostras individuais
da rodada final; as duas rodadas descartadas estão em `attempts/`.

As 19 comparações antes/depois de imagem passaram por SHA-256 do PPM e hash RGBA,
incluindo cena estática nas três APIs e mudanças de câmera/material no OpenGL.
A instalação local BGFX foi atualizada e os hashes de `Coin4.dll`,
`CoinRender4.dll` e do benchmark conferem com o build qualificado.
`Coin4.dll` permanece idêntico à baseline. O build wgpu não foi alterado.

Logs, scripts, resultados JUnit, hashes e medição consolidada:
[`bgfx-gl-indexed-summary.json`](validation/bgfx-windows/first-frame/bgfx-gl-indexed/bgfx-gl-indexed-summary.json).

## Upload por referência no BGFX/OpenGL — 2026-10-03

O upload completo de vértices acima de 32 MiB agora transfere a alocação do
`std::vector` ao BGFX por `makeRef`, com callback de liberação. O dono desse
bloco é independente do target e do plano em cache. O BGFX pode liberá-lo na
thread de renderização ou durante shutdown, depois de consumir os comandos.
Não há espera extra nem uma referência a memória temporária da stack.
Se a alocação do pequeno objeto dono falhar, o caminho anterior com `copy`
continua disponível. Uploads pequenos, patches de material e o caminho direto
de RTT mantêm a cópia anterior. Os índices ainda são enviados com `copy`.

Na cidade de 40.000 prédios, elimina-se uma cópia CPU de 172,12 MiB por upload
completo. O payload GPU, o layout de 188 bytes, os 960.024 vértices e os
1.440.036 índices permanecem iguais. Não é uma medição de redução do pico RSS:
o número se refere ao bloco cuja cópia deixa de existir.

Os contadores do plano sobrevivem à transferência e à retenção repetida.
O cálculo do orçamento inclui os vértices transferidos, para também descartar
os índices CPU dos planos grandes. Planos pequenos continuam com os dados
necessários aos patches de material. Nenhum shader ou código wgpu mudou.

### Medição da versão final

Baseline `b70db88d49`, três processos novos antes/depois por API, alternados,
cidade de 40.000 prédios em 1024×1024, `--warmup 1 --frames 3`. Primeiro quadro
inclui renderização e cópia RGBA síncrona, sem parsing/capability probe.
Sem tracing nas medianas; caches do driver/sistema mantidos. Build, testes e
processos GPU foram serializados. O controle aquecido separado usa quatro
quadros de aquecimento e oito medidos. Todas as medianas aquecidas dos processos
cronometrados estáticos ficaram abaixo de 30 ms. Faixas e amostras estão no JSON.

| Caminho | Primeiro antes (ms) | Primeiro depois (ms) | Redução | Aquecido antes (ms) | Aquecido depois (ms) |
|---|---:|---:|---:|---:|---:|
| BGFX OpenGL | 1273.72 | 1212.01 | 4.8% | 11.89 | 12.76 |
| BGFX Vulkan | 1532.60 | 1412.41 | 7.8% | 10.15 | 10.28 |
| BGFX D3D12 | 1401.40 | 1359.77 | 3.0% | 11.73 | 12.26 |

Não foi observado ganho no quadro estático aquecido. Esse controle variou
menos de 1 ms entre versões; o trabalho GPU e o reuse dos buffers não mudam.

Uma execução separada com tracing no OpenGL:

| Intervalo | Antes (ms) | Depois (ms) |
|---|---:|---:|
| Lowering | 152.22 | 151.44 |
| Upload | 60.65 | 1.88 |
| Espera pelo readback | 431.06 | 407.89 |

O tempo de upload mede o intervalo CPU do backend. O driver ainda precisa
receber a geometria e renderizar; esse trabalho aparece também na espera pelo
readback. Não se trata de upload GPU de custo zero. Na execução com tracing
depois, preparar o target custou 194.42 ms,
e a espera pelo readback custou 407.89 ms.
Essas parcelas e o lowering permanecem como oportunidades de melhoria.

Os controles de atualização OpenGL usam 10.000 prédios, warmup 1 e três quadros:

| Atualização | Processos por versão | Aquecido antes (ms) | Aquecido depois (ms) |
|---|---:|---:|---:|
| camera | 1 | 288.13 | 267.18 |
| material | 3 | 287.48 | 276.17 |

O controle de câmera tem somente um processo por versão. Esses controles
verificam principalmente a correção dos caminhos de reuse/rebuild; não
estabelecem uma distribuição de desempenho para câmera.

### Qualificação

A suíte de 50 casos passou sem falhas nem skips, incluindo OpenGL,
profundidade, transparência, clipping, texturas, janelas, sombras GPU,
múltiplos targets e RTT. Os testes GPU de referência/sombras foram obrigatórios.
O Core verifica contadores e dados depois da transferência, retenção repetida
e descarte dos índices restantes. O teste offscreen excede 32 MiB, verifica
cache estático e rebuild de material, injeta perda de dispositivo depois de
enfileirar um novo upload grande e confirma recuperação com a mesma imagem.
Esse teste de ciclo de vida também passou em Vulkan e D3D12, em processos separados.

Os 38 processos finais geraram 19 pares de imagens
idênticos à baseline pelo SHA-256 do PPM e pelo hash RGBA, incluindo as três
APIs, tracing, controle aquecido, câmera e material. A instalação local BGFX
foi atualizada; hashes do build e da instalação conferem. `Coin4.dll` e o
benchmark permanecem idênticos à baseline.

A rodada parcial inicial foi interrompida pelo tratamento de stderr de diagnóstico
do PowerShell 5. A rodada completa da primeira implementação também foi
preservada; ela precede a correção que descarta os índices CPU restantes.
Os resultados acima pertencem a uma nova rodada completa com a versão final,
sem excluir amostras individuais. Os pilotos estão em `pilots/`.

Evidências, scripts e logs:
[`bgfx-gl-upload-summary.json`](validation/bgfx-windows/first-frame/bgfx-gl-upload/bgfx-gl-upload-summary.json).

## Reproduzir

```powershell
python examples/coinrender/generate_large_scene.py city-40000.iv --grid 200
cmake --build build-win --config Release --target coin_render_gl_benchmark
$env:WGPU_BACKEND = 'vulkan' # trocar por dx12 (D3D12) ou gl (OpenGL)
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
