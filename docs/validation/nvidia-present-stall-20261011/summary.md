# Pausas de apresentação NVIDIA no X11 atual — 2026-10-11

Uma nova campanha A/B/B/A de `city-1000000.iv` foi interrompida após o
primeiro processo: ele era válido quanto a GPU física, cena e 120 amostras,
mas a mediana de `render_present_ms` foi 499,686 ms. A segunda repetição
foi interrompida, portanto **não há A/B válido** nesta sessão e nenhuma
comparação native/portable deve usar esses números.

Traces curtos com `COIN_RENDER_TRACE_PHASES=1`, janela X11 `:0`,
1280×720, sem readback e política native localizaram as pausas:

| GPU/backend/cena | Mediana de chamada, 8 quadros (ms) | Backend >500 ms, 10 quadros |
| --- | ---: | ---: |
| NVIDIA/wgpu Vulkan, milhão | 389,221 | 5 |
| NVIDIA/wgpu Vulkan, texture-8 | 464,949 | 4 |
| NVIDIA/BGFX Vulkan, texture-8 | 1,005 | 3 |
| AMD/wgpu Vulkan, texture-8 | 0,847 | 0 |

No wgpu/NVIDIA, quatro de dez chamadas `present()` duraram mais de
500 ms, chegando a 977–997 ms, em ambas as cenas. Os demais quadros do
milhão reutilizaram o plano, sem reconstrução (`plan_cache_hit=1`), e
gastaram cerca de 9–15 ms na validação Rust. No BGFX/NVIDIA, as pausas
ficaram em `submit_frame_ms` (750–1000 ms), enquanto lowering e upload
permaneceram abaixo de 0,02 ms no controle pequeno. Assim, a pausa observada
não nasce do payload disperso nem é específica do sampling ou da cena do
milhão. O controle AMD não mostrou o mesmo padrão.

Isso localiza a **pausa na submissão/apresentação NVIDIA neste X11**.
Ainda não distingue driver, PRIME,
compositor ou estado da janela, nem explica causalmente o outlier histórico
de 25%. A frente permanece aberta; um novo A/B de política precisará de
um caminho de apresentação estável ou de medição GPU separada.

Logs, CSV da repetição longa válida, relatório parcial da coorte e hashes
SHA-256: `/mnt/Laranja/Git/externos/coin-render-artifacts/nvidia-variance-current-20261011/`.
Os quatro traces curtos estão resumidos em `short-trace-summary.json`.
