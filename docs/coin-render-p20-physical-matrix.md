# P20 — matriz física Linux (continuação de 2026-10-07)

A [campanha atual](coin-render-hardware-surfaces-linux.md) executou as duas
GPUs físicas deste PC: AMD Renoir `1002:1638`, Mesa radeonsi/RADV 25.2.8,
e NVIDIA RTX 3060 Laptop `10de:2560`, driver 610.57.04. Builds Release,
X11 físico, fixtures P17 verificadas por SHA-256, 128×128, um quadro de
aquecimento e três capturados. O [inventário por célula](inventories/coin-render-p20-physical-matrix.json)
guarda execução e qualificação visual separadamente.

| GPU | CoinGL | BGFX OpenGL | BGFX Vulkan | wgpu Vulkan |
|---|---|---|---|---|
| AMD janela | Referência capturada válida | RGB qualificado | RGB qualificado | RGB qualificado |
| AMD offscreen | Pixmap direto válido | RGB qualificado | Opaca qualificada; transparência com 4 pixels de borda fora do gate | Opaca qualificada; mesma divergência Vulkan |
| NVIDIA janela/offscreen | Referência capturada válida | RGB qualificado, EGL NVIDIA demonstrado | RGB qualificado | RGB qualificado |
| Intel | Sem GPU gráfica física neste PC | Não executado | Não executado | Não executado |

Das 48 células planejadas, **32 executaram e capturaram**, sem falha de
execução; 16 Intel não foram executadas. Os oito pares BGFX/wgpu Vulkan
coincidem exatamente em RGBA dentro da mesma GPU. O gate RGB com CoinGL é
máximo <=2 e MAE <=1,0: **22/24 comparações passaram**. As duas divergências
AMD/Vulkan transparentes offscreen medem MAE 0,134013, máximo 17 e quatro
pixels acima de três níveis. Todos estão em bordas e próximos de cores
vizinhas do oráculo (erro <=1); a causa específica continua como estudo.
Nenhum esperado ou tolerância foi alterado após a campanha.

O benchmark de janela agora captura CoinGL e grava PPM top-down antes de
apresentar, permitindo comparação por pixel. O runner também rejeita
capturas degeneradas e cenas distintas que produzam a mesma imagem. Para
BGFX/OpenGL, GLX sozinho não comprova a GPU EGL: a prova lê vendor/renderer
com o contexto do backend corrente. A falha antiga NVIDIA/EGL em Xwayland
não reapareceu no X11 físico; a combinação antiga continua distinta.

Reproduzir com seleção física explícita de GLX/EGL/ICD, usando
`scripts/coinrender/run_p20_physical_matrix.py --device amd|nvidia
--session physical --bgfx-build BUILD_BGFX --wgpu-build BUILD_WGPU
--scenes-dir SCENES --output-dir EVIDENCE`. Os comandos completos estão nos
scripts e JSONs da [evidência](validation/hardware-surfaces-linux-20261007/).
`--session isolated` continua exigindo o launcher privado; `--targets` permite
campanhas só de janela ou offscreen. Captura não é benchmark de desempenho.

O [relatório anterior](validation/hardware-surfaces-linux-20261007/prior-p20-report.md)
e o [inventário anterior](validation/hardware-surfaces-linux-20261007/prior-p20-matrix.json)
registram os 28 processos, quatro falhas e oráculos inconclusivos daquela
rodada. P20 mantém Intel física e a melhoria de raster AMD como pendências.
Windows, AppKit/Wayland e Android permanecem nas fases P21–P23.
