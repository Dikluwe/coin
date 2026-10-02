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
