# Benchmark direto em janela, sem readback

`coin_render_window_benchmark` mede apresentação direta numa janela X11. Ao contrário
de `coin_render_gl_benchmark`, ele não cria target offscreen, não chama
`readbackRGBA()` e não transporta pixels para a CPU.

O executável usa a mesma cena e ordem de travessia em todos os ensaios: um
fundo opaco e 48 objetos transparentes sobrepostos. Cada processo executa um
único backend/modo, evitando a restrição global do BGFX. `--dynamic` altera a
transformação a cada quadro; sem essa opção, a cena permanece estática.

## Matriz

Compile uma configuração Release com `COIN_BUILD_RENDER_WINDOW_EXAMPLE=ON`,
`COIN_BUILD_LEGACY_GL_RENDERER=ON` e `COIN_RENDER_BACKEND=BGFX`. Execute cada
linha em um processo novo, na mesma sessão gráfica e sem outras cargas na GPU:

```sh
coin_render_window_benchmark --backend coin-gl --transparency object
coin_render_window_benchmark --backend coin-gl --transparency sorted_layers

coin_render_window_benchmark --backend bgfx-opengl --transparency object
coin_render_window_benchmark --backend bgfx-opengl --transparency weighted_oit
coin_render_window_benchmark --backend bgfx-opengl --transparency sorted_layers

coin_render_window_benchmark --backend bgfx-vulkan --transparency object
coin_render_window_benchmark --backend bgfx-vulkan --transparency weighted_oit
coin_render_window_benchmark --backend bgfx-vulkan --transparency sorted_layers
```

`weighted_oit` não tem equivalente no Coin/GL e, por isso, essa combinação é
rejeitada. Os padrões são 60 quadros de aquecimento e 600 medidos a 960x540.
Use `--width`, `--height`, `--warmup` e `--frames` para campanhas maiores.
Repita a matriz com `--dynamic` para separar o perfil estático daquele com
mudança de transformação.

## Interpretação

A linha de saída registra explicitamente `readback=none`, backend, modo,
resolução, atualização da cena e adaptador. `cpu_frame_*` mede o tempo de
parede ao redor de render+present no thread chamador. `throughput_fps` usa o
intervalo completo dos quadros medidos; no Coin/GL, um `glFinish()` final inclui
a cauda ainda enfileirada na GPU.

Esses tempos não substituem timestamps de GPU. Swapchain, fila do driver e
compositor podem fazer o tempo CPU divergir do trabalho da GPU. Compare apenas
runs pareados na mesma máquina, sessão, resolução e política de atualização;
registre mediana/p95 e throughput, e não derive tempo GPU por subtração.

VSync é solicitado como desligado no caminho GLX quando uma das extensões de
swap interval está disponível. O benchmark não afirma que o compositor ou o
driver aceitaram a solicitação; resultados limitados exatamente à taxa do
monitor devem ser marcados como inconclusivos.

## Timestamps por passagem e memória GPU

As sondas são opt-in e intrusivas. Ative-as apenas em uma campanha separada:

```sh
COIN_RENDER_TRACE_PHASES=1 COIN_WGPU_GPU_TIMESTAMPS=1 \
  coin_render_window_benchmark --backend bgfx-vulkan --transparency weighted_oit
```

O registro `COIN_RENDER_PHASE bgfx` separa `gpu_opaque_ms`,
`gpu_transparent_ms`, `gpu_composite_ms` e `gpu_wait_ms`. O modo `object`
usa views opaca e transparente distintas. Em `weighted_oit` e
`sorted_layers`, a acumulação inclui a reconstrução da profundidade opaca no
framebuffer auxiliar. `gpu_pass_frame` identifica a geração da amostra, pois
o BGFX publica queries por view com atraso.

`gpu_blit_readback_ms` só se aplica ao target offscreen e atualmente fica
`unavailable`: o BGFX executa o blit antes de iniciar o profiler da view. Não
é estimado pela diferença do timestamp total. A espera CPU observada dentro
do BGFX aparece em `gpu_wait_ms`; ela não é apresentada como execução GPU.

O mesmo registro fornece `gpu_memory_used_bytes`,
`texture_memory_used_bytes`, `render_target_memory_used_bytes` e contagens de
vertex buffers, index buffers, texturas, framebuffers e programas. Valores
não oferecidos pelo driver permanecem `-1`. `gpu_draw_submits` é o número de
draws efetivamente processados pelo BGFX, não apenas o número de objetos do
CoinRenderFramePlan. Para comparar pico entre modos e resoluções, agregue o máximo de
cada campo depois do aquecimento.

## Agrupamento de draws

O backend agrupa por padrão somente draws opacos. A chave ordena estado de
pipeline/viewport, assinatura do material, textura/sampler e iluminação. O
material continua armazenado nos vértices: sua assinatura serve para formar
lotes, não introduz um novo uniform. Draws transparentes nunca são ordenados
por essa etapa e mantêm sua ordem relativa original em `object`,
`weighted_oit` e `sorted_layers`.

O trace registra `opaque_draws`, `transparent_draws`, `opaque_grouping`,
`opaque_reordered` e as transições lógicas de pipeline, material, textura e
iluminação. Essas transições descrevem a sequência pedida pelo backend;
`gpu_draw_submits` continua sendo a contagem efetivamente publicada pelo
BGFX. Para um A/B, desative apenas o agrupamento:

```sh
COIN_BGFX_DISABLE_DRAW_GROUPING=1 COIN_RENDER_TRACE_PHASES=1 \
  coin_render_window_benchmark --backend bgfx-vulkan --transparency object
```

A cena padrão deste benchmark tem um único draw opaco e 48 transparentes;
portanto valida que a ordem transparente permanece intacta, mas não é uma
carga representativa para medir ganho de agrupamento. Uma campanha de batching
deve usar uma cena opaca com estados intercalados e comparar as transições e o
tempo sem alterar geometria, câmera ou resolução.

Geometria usa vertex/index buffers dinâmicos persistentes, com capacidade em
potências de dois e crescimento apenas quando a cena ultrapassa a classe atual.
Isso também evita criação/destruição quando o produtor usa `revision == 0`.
`geometry_buffer_reused`, `vertex_buffer_capacity` e `index_buffer_capacity`
expõem o comportamento do pool. Planos com revisão ainda evitam também o
upload; `resource_cache_hit` distingue esse caso.

Texturas imutáveis acompanham a revisão do CoinRenderFramePlan e permanecem alocadas
entre frames; `texture_cache_hit` confirma seu reuso. Uniforms e framebuffers
temporários são criados uma vez. Uma mudança de revisão invalida as texturas,
enquanto um camera patch conserva os recursos e altera somente os transforms.

Mudanças exclusivamente de material são verificadas conservadoramente depois
do lowering. Se geometria, índices, transforms, textura, iluminação, pipeline
e ordem permanecerem idênticos, somente os intervalos de vértices alterados
são enviados ao buffer dinâmico. `material_patch`, `material_patch_ranges` e
`material_patch_vertices` medem esse caminho. Qualquer diferença fora dos
campos de material força o rebuild normal.

## Readback em pipeline

O target offscreen BGFX mantém o caminho síncrono de profundidade 1 como
referência. `coin_render_gl_benchmark --backend bgfx --async-depth 2` habilita staging
duplo e `--async-depth 3`, staging triplo. O backend publica o frame concluído
mais antigo; portanto, após o bootstrap, o consumidor recebe deliberadamente
o frame N-1 ou N-2. Se o produtor alcançar todos os slots ainda pendentes, há
backpressure sobre o slot mais antigo, sem sobrescrever dados em voo.

```sh
COIN_RENDER_TRACE_PHASES=1 COIN_BGFX_RENDERER=vulkan \
  coin_render_gl_benchmark --backend bgfx --size 512 --warmup 20 --frames 300 --async-depth 2
```

O benchmark informa mediana, p95 e throughput fim a fim, além de
`pipeline_depth`. No trace, `readback_latency_frames` mede a idade do frame
publicado, `read_wait_frames`/`read_wait_ms` medem espera e
`readback_bootstrap` identifica o preenchimento inicial. Memória é registrada
separadamente como `readback_gpu_staging_bytes`,
`readback_cpu_staging_bytes` e `readback_published_bytes`;
`readback_pipeline_bytes` soma apenas os rings GPU e CPU. A janela nunca usa
esse caminho: se a CPU não pede pixels, não há blit nem readback.

## `SoSceneTexture2` GPU→GPU

`COIN_RENDER_RTT_GPU_DIRECT=1` ativa no BGFX o grafo direto de
`SoSceneTexture2`. Cada subcena é renderizada em uma attachment RGBA8 e essa
mesma textura é ligada no draw consumidor, sem cópia GPU→CPU→GPU. Dependências
aninhadas são executadas em ordem topológica e produtores equivalentes dentro
do mesmo `apply()` compartilham um único passe.

Framebuffer, textura de cor e profundidade ficam cacheados pela identidade do
produtor entre frames. Uma mudança de resolução recria somente as attachments
desse produtor; produtores removidos da cena são descartados no fim do
`apply()`. A orientação de UV segue `originBottomLeft`, mantendo o mesmo
resultado em BGFX/OpenGL e BGFX/Vulkan.

Sem a variável, o caminho staged RGBA8 continua sendo a referência e o
fallback selecionável. Ele executa readback, normaliza a orientação das linhas
e reenvia a textura ao backend. O budget por aplicação contabiliza 4 bytes por
pixel no staged e 8 bytes por pixel no direto (cor RGBA8 mais profundidade de
32 bits). O RTT direto usa atualmente transparência `object`; a composição do
frame consumidor continua respeitando o modo configurado.

No RADV testado, fechar a conexão Xlib depois que o BGFX descarrega o driver
Vulkan chama um callback pertencente ao DSO já descarregado. O benchmark
deixa essa conexão aberta no processo Vulkan; o sistema operacional a
recupera imediatamente ao término. O caminho OpenGL continua chamando
`XCloseDisplay()` normalmente.

## Baseline local de 2026-09-26

Release, cena estática, 960×540, 60 warmup e 600 quadros, sem trace e sem
readback:

| Backend | Transparência | Mediana CPU (ms) | p95 (ms) | Throughput (fps) |
|---|---:|---:|---:|---:|
| Coin/GL | `object` | 0,822 | 1,154 | 1.185,6 |
| Coin/GL | `sorted_layers` | 1,516 | 2,124 | 632,4 |
| BGFX/OpenGL | `object` | 0,395 | 0,822 | 2.107,9 |
| BGFX/OpenGL | `weighted_oit` | 1,080 | 2,160 | 818,7 |
| BGFX/OpenGL | `sorted_layers` | 2,871 | 6,006 | 305,5 |
| BGFX/Vulkan | `object` | 0,381 | 0,979 | 2.067,1 |
| BGFX/Vulkan | `weighted_oit` | 0,503 | 1,402 | 1.586,7 |
| BGFX/Vulkan | `sorted_layers` | 0,506 | 0,980 | 1.697,9 |

Coin/GL identificou `AMD Radeon Graphics (radeonsi, renoir)`. BGFX/Vulkan
selecionou `vendor_id=0x10de`, `device_id=0x2560` (NVIDIA); portanto suas
linhas não são um A/B de API na mesma GPU. BGFX/OpenGL não forneceu IDs no
`Caps` (`0x0000/0x0000`), embora use a sessão GLX do mesmo display. Esses
números são uma baseline desta máquina, não uma alegação geral de vantagem.
