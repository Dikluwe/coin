# P17 — campanha comparável em uma GPU física

A campanha mediu 112 execuções válidas: 56 células (janela e offscreen,
duas cenas, estática/dinâmica e modos suportados), repetidas duas vezes.
Cada célula usa a mesma cena Coin e ordem de travessia entre backends. Os 112
resultados individuais estão em [coin-render-p17-runs.csv](inventories/coin-render-p17-runs.csv).
Nenhuma célula falhou na execução final. A tentativa anterior com seleção de
driver incorreta foi descartada; seus dados não entram no CSV.

## Contrato e reprodução

- Cena opaca: 36 cubos com quatro estados de material intercalados, determinística.
  Cena transparente: um fundo opaco e 48 quads translúcidos sobrepostos. Ambas
  são geradas por `scripts/coinrender/generate_p17_scenes.py`; o manifesto SHA-256
  da execução foi `32854a58541b03ad07bbf03f8cace6e8b394b456d83d6e9be0e20e6d5473c072`
  (opaca) e `7fc70de5b45323521ff68714298049625b1f66ba1248a42c3952879d895a4caf`
  (transparente).
- 512×512, 30 quadros de warmup, 180 quadros medidos por processo, duas
  repetições por célula, sem trace/profiling. `static` conserva a cena;
  `dynamic` muda o transform na janela e a câmera no offscreen a cada frame.
  Os resultados dinâmicos completos constam no CSV.
- Janela: apresentação Xlib direta, **sem readback**. Offscreen: renderização
  seguida de readback RGBA e cópia para o consumidor, profundidade de pipeline
  síncrona; os dois alvos têm câmeras diferentes e **não são uma razão A/B**.
- Transparência `object` e `sorted_layers` usam a política da action do Coin.
  `weighted_oit` é extensão BGFX, sem equivalente GL; o wgpu rejeita
  explicitamente `weighted_oit` quando há geometria translúcida. Os modos
  indisponíveis foram excluídos, não contados como PASS.
- A janela solicita VSync desligado no GLX/BGFX. A ponte wgpu exige
  `Immediate` ou `Mailbox` para este benchmark e falha se ambos faltarem;
  a superfície AMD testada aceitou. Não há confirmação externa de que o
  compositor/driver obedeça à solicitação de GLX/BGFX.

A execução final ocorreu em 2026-09-29, Linux 6.17.0-42-generic, Xwayland
privado com Weston/Metacity, AMD Radeon Graphics Cezanne/Renoir `1002:1638`.
GLX foi `radeonsi` acelerado, Mesa 25.2.8; BGFX/Vulkan e wgpu/Vulkan usaram
RADV fixado em `/usr/share/vulkan/icd.d/radeon_icd.json`. O BGFX/OpenGL não
publica IDs PCI nas capabilities (`0x0/0x0`), então a identidade física desse
caminho é corroborada pelo GLX da sessão, não pelo próprio BGFX. O cabeçalho
do Coin/GL offscreen também não consulta adapter; GLX acelerado foi validado
na mesma sessão. Coin/GL inclui uma cópia para um vetor RGBA após
`SoOffscreenRenderer::getBuffer()`, equivalente à cópia solicitada pela API
nativa ao consumidor. Builds CMake `Release`, BGFX e Rust bridge em diretórios
independentes. SHA-256 dos executáveis (janela, offscreen): BGFX
`19bd76037f02ee4d3d92e341cffe47a2d1c1a77bbdfb785a36a726a4d4ac1259`,
`d7382c1891efc9d1a3672bfc4855b3e8efdfd75cb16c49176600146eeef9928c`;
wgpu `0d7722298478b201078c089809f4f4f2f3562a9cf9d64eb63a95955a10d323aa`,
`abfdbf4cd5c49e0a18a2ad47c057a7b79b32770e88ea53b3a33c2415bb9c3fef`.
A base da árvore era `c752e3502d8d91eead28b55eef448118cf11e9f2`, com
as mudanças P17 deste commit aplicadas antes da compilação.

```sh
python3 scripts/coinrender/generate_p17_scenes.py --output-dir /tmp/coin-p17-scenes
cmake -S . -B build-bgfx-recovery/coin-build -DCMAKE_BUILD_TYPE=Release \
  -DCOIN_BUILD_RENDER=ON -DCOIN_RENDER_BACKEND=BGFX \
  -DCOIN_BUILD_LEGACY_GL_RENDERER=ON -DCOIN_BUILD_RENDER_WINDOW_EXAMPLE=ON \
  -DCOIN_INSTALL_RENDER_EXPERIMENTAL=ON \
  -DCMAKE_PREFIX_PATH="$PWD/build-bgfx-recovery/bgfx-prefix"
cmake -S . -B /tmp/coin-p17-wgpu-release -DCMAKE_BUILD_TYPE=Release \
  -DCOIN_BUILD_RENDER=ON -DCOIN_RENDER_BACKEND=RUST_BRIDGE \
  -DCOIN_BUILD_LEGACY_GL_RENDERER=ON -DCOIN_BUILD_RENDER_WINDOW_EXAMPLE=ON \
  -DCOIN_INSTALL_RENDER_EXPERIMENTAL=ON
cmake --build build-bgfx-recovery/coin-build --target coin_render_window_benchmark coin_render_gl_benchmark -j8
cmake --build /tmp/coin-p17-wgpu-release --target coin_render_window_benchmark coin_render_gl_benchmark -j8
env __GLX_VENDOR_LIBRARY_NAME=mesa \
  __EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/50_mesa.json \
  VK_DRIVER_FILES=/usr/share/vulkan/icd.d/radeon_icd.json \
  python3 testsuite/qt-quarter/run_isolated.py \
  --server xwayland --weston-prefix /tmp/coin-p16-weston \
  --artifacts /tmp/coin-p17-session --exec -- \
  python3 scripts/coinrender/run_p17_campaign.py \
  --output-dir /tmp/coin-p17-campaign \
  --bgfx-build "$PWD/build-bgfx-recovery/coin-build" \
  --wgpu-build /tmp/coin-p17-wgpu-release \
  --scenes-dir /tmp/coin-p17-scenes --size 512 --warmup 30 --frames 180 --repetitions 2
```

O runner grava `results.json`, `glxinfo-B.log` e uma saída bruta por execução
no diretório de saída. Rejeita cena/hash alterado, fallback de
`sorted_layers`, backend/modo/contagem inconsistentes, adaptador Vulkan
errado e métricas ausentes. Para o Coin/GL offscreen, define
`COIN_GLX_PIXMAP_DIRECT_RENDERING=1`; no Xwayland desta máquina o bootstrap
indireto do Coin não criava contexto. Weston extraído em `/tmp` e os diretórios
de build são insumos locais; devem ser substituídos numa reprodução externa.

## Medições estáticas

Valores abaixo são a mediana das duas medianas de frame, a mediana dos dois
p95 e a mediana dos dois throughputs, respectivamente; não são percentis
recalculados sobre 360 frames. Unidade de tempo: ms de parede no thread
chamador. Os resultados dinâmicos e ambas as repetições estão no CSV.

| Alvo | Cena | Backend | Modo | Mediana (ms) | p95 (ms) | Throughput (fps) |
|---|---|---|---|---:|---:|---:|
| Janela | Opaca | coin-gl | `object` | 0.097 | 0.127 | 9659.4 |
| Janela | Opaca | bgfx-opengl | `object` | 0.258 | 0.305 | 3747.1 |
| Janela | Opaca | bgfx-vulkan | `object` | 0.130 | 0.160 | 7414.2 |
| Janela | Opaca | wgpu-vulkan | `object` | 0.527 | 0.570 | 1876.8 |
| Janela | Transparente | coin-gl | `object` | 0.371 | 0.433 | 2639.2 |
| Janela | Transparente | coin-gl | `sorted_layers` | 0.664 | 0.814 | 1453.3 |
| Janela | Transparente | bgfx-opengl | `object` | 0.419 | 0.479 | 2346.5 |
| Janela | Transparente | bgfx-opengl | `weighted_oit` | 0.669 | 0.758 | 1476.3 |
| Janela | Transparente | bgfx-opengl | `sorted_layers` | 1.738 | 1.979 | 573.9 |
| Janela | Transparente | bgfx-vulkan | `object` | 0.268 | 0.341 | 3468.7 |
| Janela | Transparente | bgfx-vulkan | `weighted_oit` | 0.818 | 0.878 | 1216.5 |
| Janela | Transparente | bgfx-vulkan | `sorted_layers` | 2.100 | 2.325 | 472.6 |
| Janela | Transparente | wgpu-vulkan | `object` | 0.657 | 0.711 | 1502.4 |
| Janela | Transparente | wgpu-vulkan | `sorted_layers` | 3.847 | 5.980 | 257.4 |
| Offscreen | Opaca | coin-gl | `object` | 0.379 | 0.419 | 2628.9 |
| Offscreen | Opaca | bgfx-opengl | `object` | 0.637 | 0.799 | 1531.2 |
| Offscreen | Opaca | bgfx-vulkan | `object` | 0.631 | 0.739 | 1539.3 |
| Offscreen | Opaca | wgpu-vulkan | `object` | 0.789 | 0.886 | 1239.9 |
| Offscreen | Transparente | coin-gl | `object` | 0.608 | 0.716 | 1623.1 |
| Offscreen | Transparente | coin-gl | `sorted_layers` | 0.902 | 1.056 | 1089.4 |
| Offscreen | Transparente | bgfx-opengl | `object` | 0.721 | 0.871 | 1360.6 |
| Offscreen | Transparente | bgfx-opengl | `weighted_oit` | 0.992 | 1.173 | 1004.2 |
| Offscreen | Transparente | bgfx-opengl | `sorted_layers` | 1.507 | 1.777 | 657.6 |
| Offscreen | Transparente | bgfx-vulkan | `object` | 0.699 | 0.921 | 1364.6 |
| Offscreen | Transparente | bgfx-vulkan | `weighted_oit` | 1.102 | 1.350 | 894.8 |
| Offscreen | Transparente | bgfx-vulkan | `sorted_layers` | 2.252 | 2.506 | 445.4 |
| Offscreen | Transparente | wgpu-vulkan | `object` | 0.925 | 1.067 | 1056.8 |
| Offscreen | Transparente | wgpu-vulkan | `sorted_layers` | 6.003 | 6.426 | 165.6 |

`cpu_frame_*` inclui travessia, trabalho CPU do backend e chamada de
apresentação ou readback. Throughput usa o intervalo de parede dos quadros
medidos; Coin/GL janela também faz `glFinish()` no final para incluir a cauda
na fila. São **tempos CPU observados**, não timestamps GPU nem uma promessa de
latência da tela. A profundidade de filas e o bloqueio na apresentação podem
variar entre APIs; o offscreen inclui readback de cor e não equivale à janela.
Comparações de custo dentro da mesma combinação de backend/alvo/cena são as
mais diretas; diferenças entre backends indicam perfis a investigar em P18,
sem atribuir toda a diferença à GPU. Tampouco derivamos memória GPU de RSS.

P17 encerra o protocolo e as medições deste perfil físico. P18 mede as
passagens, esperas CPU e memória; P19 avalia persistência/readback em pipeline.
A matriz Intel/NVIDIA/Windows/Wayland nativo continua em P20–P22.
