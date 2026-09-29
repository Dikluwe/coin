# P18 — passagens, espera CPU e recursos

P18 perfila a baseline P17 sem trocar cena, câmera por alvo, transparência ou
resolução. A [matriz de 56 execuções](inventories/coin-render-p18-profile.csv)
reúne 512² estático/dinâmico nas duas cenas e modos suportados e 1024²
estático em `object`. Cada processo faz 10 quadros de aquecimento e 40 medidos.
Os 56 casos passaram; BGFX/OpenGL, BGFX/Vulkan e wgpu/Vulkan usaram o mesmo
AMD Renoir `1002:1638` da [campanha P17](coin-render-p17-campaign.md).
Coin/GL permanece a referência de tempo de parede de P17: não dispõe destes
marcadores internos e nenhum campo GPU foi inferido para ele.

A coleta usa `COIN_RENDER_TRACE_PHASES=1` e
`COIN_WGPU_GPU_TIMESTAMPS=1`. Ambas são sondas intrusivas. Os tempos daqui
não devem ser comparados diretamente às latências sem trace do P17. O runner
valida por processo os 40 registros medidos da action e da Infra, os campos
backend/alvo/cena, a reconciliação das subfases CPU do Rust e ao menos 20
amostras GPU válidas quando a API as oferece. Nesta GPU, todas as 40 amostras
GPU por célula BGFX e wgpu offscreen foram válidas. Cada valor de tempo no CSV
é mediana das amostras medidas; contagens e bytes são máximos após o warmup.

| Mecanismo | Tempos GPU | Espera CPU | Recursos |
|---|---|---|---|
| BGFX/OpenGL e Vulkan | `gpu_frame_ms` e views opaca/transparente/composição; `gpu_pass_frame` identifica a geração da view | `read_wait_ms`, `frame_wait_ms` e espera informada pelo profiler BGFX | Stats BGFX: draws, buffers, texturas, framebuffers; bytes de memória quando o driver os informa; staging do readback |
| wgpu/Vulkan offscreen síncrono | timestamps do render e da cópia para staging (`rust_gpu status=ok`) | `rust_cpu_detail gpu_wait_ms` mede `device.poll`, além de encode, map e cópias CPU | `rust_resources`: buffers e tamanhos nominais mantidos pela ponte, attachments e staging |
| wgpu/Vulkan janela | timestamp GPU `unavailable` | `rust_surface_cpu` divide validação, acquire, encode e submit/present; nenhum desses spans é tempo GPU | Mesmos contadores próprios; staging de readback igual a zero |

A API wgpu não publica aqui memória total do driver nem quantidade de
framebuffers: `unavailable` é um resultado, não zero. `attachment_nominal_bytes`
é área vezes bytes por pixel das texturas de attachment controladas pela ponte;
`texture_payload_bytes` é carga de textura em cache. Nenhuma dessas grandezas
inclui overhead de alinhamento, heaps do driver, swapchain ou outros processos.
`vertex_buffers` e `index_buffers` do wgpu contam apenas o cache de
geometria e seus buffers aposentados; não cobrem bindings de câmera nem
alocações transitórias. `attachment_nominal_bytes` conta somente cor/depth do
alvo principal, sem texturas temporárias de peeling, swapchain ou RTT externo.
BGFX/OpenGL retornou `-1` para memória GPU total nesta sessão; o CSV deixa o
campo vazio. Não usamos RSS como memória GPU.

## Amostras que orientam P19

Cena transparente estática, 512²; valores em ms. GPU BGFX é o frame reportado
pelo profiler, enquanto GPU wgpu cobre render e cópia offscreen. A espera é
`read_wait_ms` no BGFX offscreen e `device.poll` no wgpu offscreen.

| Alvo | Backend | Modo | CPU backend | GPU reportada | Espera CPU | Staging GPU/ponte |
|---|---|---|---:|---:|---:|---:|
| Janela | BGFX/Vulkan | `weighted_oit` | 1,077 | 0,751 | — | 0 |
| Janela | wgpu/Vulkan | `sorted_layers` | 3,213 | indisponível | — | 0 |
| Offscreen | BGFX/Vulkan | `weighted_oit` | 1,281 | 0,443 | 1,010 | 1 MiB |
| Offscreen | wgpu/Vulkan | `sorted_layers` | 6,778 | render 1,547 + cópia 0,054 | 2,752 | 1 MiB |

Em BGFX/Vulkan `weighted_oit` offscreen, o profiler mediu medianas de
0,064 ms na view opaca, 0,265 ms na acumulação transparente e 0,095 ms na
composição; registrou pico de 54 draws submetidos, 5 vertex buffers,
5 index buffers, 10 texturas, 3 framebuffers e 1 MiB de staging GPU mais
1 MiB de staging CPU. São métricas do BGFX e podem incluir recursos
compartilhados do runtime. Os picos a 1024² subiram para 4 MiB de staging
GPU no BGFX e 4 MiB de buffer de cor no wgpu; os attachments nominais do
wgpu somaram 8 MiB. Em ambas as resoluções, a janela teve staging de readback
zero, como o contrato sem readback exige.

A diferença entre tempo CPU e GPU **não é** tempo de espera por subtração:
filas, `present`, `device.poll`, queries e cópias se sobrepõem. Tampouco se
somam medianas de passagens de quadros diferentes. `gpu_blit_readback_ms`
permanece indisponível no BGFX porque a view de blit não produz timestamp
confiável no ponto atual; não estimamos sua duração pelo residual do frame.
O wgpu não possui neste perfil timestamps GPU por passagem da janela, embora
registre sua preparação CPU. Esses limites ficam visíveis na matriz.

Os testes focados `CoinRenderOffscreenTest`, `CoinRenderDiagnosticShellTest` e
`CoinRenderSurfaceTest` passaram na Radeon; o primeiro passou também com o
trace ligado. O teste offscreen voltou a usar o backend Rust antes da etapa
de fault injection, após os seus checks visuais intencionalmente executados
no backend CPU. A suíte Python de isolamento (77 testes) também passou.

## Reprodução

Use os dois builds Release e as cenas SHA-256 documentados em P17. A
biblioteca `libCoinRender.so` da coleta tinha SHA-256
`5fd1fc244bc4af19551f2053fcc39b13fb7becfe741ccf72a03a07ac352574ec`
(BGFX) e `c2d82c1d93d46df84f81bcdd40a8e3c02b3ce1c9c332394d3b7eb1a404f6c644`
(wgpu). A árvore partiu do commit P17 `3af5632232` com as alterações P18
aplicadas. A sessão foi Xwayland privada com Mesa GLX/EGL e RADV físico:

```sh
env __GLX_VENDOR_LIBRARY_NAME=mesa \
  __EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/50_mesa.json \
  VK_DRIVER_FILES=/usr/share/vulkan/icd.d/radeon_icd.json \
  python3 testsuite/qt-quarter/run_isolated.py \
  --server xwayland --weston-prefix /tmp/coin-p16-weston \
  --artifacts /tmp/coin-p18-session --exec -- \
  python3 scripts/coinrender/run_p18_profile.py \
  --output-dir /tmp/coin-p18-profile \
  --bgfx-build "$PWD/build-bgfx-recovery/coin-build" \
  --wgpu-build /tmp/coin-p17-wgpu-release \
  --scenes-dir /tmp/coin-p17-scenes --warmup 10 --frames 40
```

O runner guarda `results.json`, `glxinfo-B.log` e o trace bruto por célula no
diretório de saída. O CSV versionado conserva os agregados e os campos
indisponíveis. P19 pode usar estes perfis para A/B de reuso, staging de
profundidade 1/2/3 e backpressure, preservando verificação visual e o mesmo
contrato Coin.
