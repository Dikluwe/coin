# P20 — matriz física Linux (parcial)

Esta primeira execução de P20 usa as duas GPUs físicas presentes nesta máquina:
AMD Renoir `1002:1638` com Mesa radeonsi/RADV 25.2.8 e NVIDIA RTX 3060
`10de:2560` com driver 610.57.04. A verificação exige o dispositivo PCI, o
renderer GLX acelerado e o ICD Vulkan fixado. Uma sessão Xwayland privada
mantém o display e o gerenciador de janelas isolados. Os dois builds são
Release. As fixtures P17 `opaque-interleaved` e `transparent-overlap` são
verificadas por SHA-256; todas as células usam `object`, 128×128, um quadro de
aquecimento e três quadros medidos. A [matriz por célula](inventories/coin-render-p20-physical-matrix.json)
guarda execução, qualificação, checksum, driver, hashes dos binários Release e
motivo de falha ou skip.

| GPU | Coin/GL janela | Coin/GL offscreen | BGFX OpenGL | BGFX Vulkan | wgpu Vulkan |
|---|---|---|---|---|---|
| AMD | Apresentação nas 2 cenas; sem captura de janela | 2 execuções; oráculo GL inconclusivo | 4 capturas; transparência offscreen diverge | 4 capturas | 4 capturas |
| NVIDIA | Apresentação nas 2 cenas; sem captura de janela | 2 execuções; oráculo GL inconclusivo | 4 falhas | 4 capturas | 4 capturas |
| Intel | Não há GPU Intel física neste host | Não executado | Não executado | Não executado | Não executado |

São 48 células planejadas (2 alvos × 2 cenas × 4 executores × 3 GPUs): **28
executaram**, **4 falharam** e **16 foram puladas** por ausência de hardware
Intel. A contagem de execução não é uma contagem de equivalência visual.
Coin/GL de janela não dispõe de captura no benchmark, portanto suas quatro
células validam apresentação, sem comparação de pixels. Coin/GL offscreen
reportou `glXChooseFBConfig() gave no valid configs` na AMD e o *mesmo*
checksum para cenas distintas; seus quatro hashes foram registrados, mas não
servem como oráculo visual até validar o contexto e os pixels.

Nos executores com captura, a cena opaca teve checksum idêntico entre BGFX
OpenGL, BGFX Vulkan e wgpu Vulkan na AMD, e entre BGFX Vulkan e wgpu Vulkan
na NVIDIA: `0xbd7999730dbe6385` na janela e `0x5094726603f138d3`
offscreen. Na cena transparente, BGFX Vulkan e wgpu Vulkan coincidiram
**dentro de cada GPU** nos dois alvos. As GPUs produziram checksums diferentes
para a transparência; isso não prova defeito nem equivalência, pois o hash não
mede distância de pixels. BGFX OpenGL na AMD coincidiu com Vulkan na janela,
mas divergiu offscreen (`0x6e4c2757ed19133b` contra
`0x394a9e3f904e85b4`). Essa célula requer uma comparação de pixels com
tolerância definida antes de ser qualificada.

Na NVIDIA, BGFX OpenGL de janela falhou em `eglCreateWindowSurface` com
`Failed to create surface` dentro de `glcontext_egl.cpp:185`. O ensaio sem
captura reproduziu a falha, então a captura F15 não é sua causa. O offscreen
rejeitou as capacidades de janela/offscreen dessa seleção de renderer. É uma
falha desta combinação Xwayland/EGL/driver; ainda não caracteriza todas as
sessões Xorg NVIDIA. BGFX e wgpu Vulkan permaneceram funcionais na mesma
sessão e dispositivo.

## Reproduzir

Os arquivos `results.json`, `glxinfo-B.log`, `vulkaninfo-summary.log`,
`lspci.log` e um log por célula ficam no diretório de saída. O executor
[run_p20_physical_matrix.py](../scripts/coinrender/run_p20_physical_matrix.py)
falha se o dispositivo GL/Vulkan não é o esperado ou se alguma célula falha.

```sh
env __GLX_VENDOR_LIBRARY_NAME=mesa \
  __EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/50_mesa.json \
  VK_DRIVER_FILES=/usr/share/vulkan/icd.d/radeon_icd.json \
  python3 testsuite/qt-quarter/run_isolated.py \
  --server xwayland --weston-prefix /tmp/coin-p16-weston \
  --artifacts /tmp/coin-p20-amd-session --exec -- \
  python3 scripts/coinrender/run_p20_physical_matrix.py \
  --device amd --output-dir /tmp/coin-p20-amd \
  --bgfx-build "$PWD/build-bgfx-recovery/coin-build" \
  --wgpu-build /tmp/coin-p17-wgpu-release \
  --scenes-dir /tmp/coin-p17-scenes

env __NV_PRIME_RENDER_OFFLOAD=1 __GLX_VENDOR_LIBRARY_NAME=nvidia \
  __EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/10_nvidia.json \
  VK_DRIVER_FILES=/usr/share/vulkan/icd.d/nvidia_icd.json \
  python3 testsuite/qt-quarter/run_isolated.py \
  --server xwayland --weston-prefix /tmp/coin-p16-weston \
  --artifacts /tmp/coin-p20-nvidia-session --exec -- \
  python3 scripts/coinrender/run_p20_physical_matrix.py \
  --device nvidia --output-dir /tmp/coin-p20-nvidia \
  --bgfx-build "$PWD/build-bgfx-recovery/coin-build" \
  --wgpu-build /tmp/coin-p17-wgpu-release \
  --scenes-dir /tmp/coin-p17-scenes
```

Para fechar P20 ainda faltam uma GPU Intel física; resolver ou delimitar a
falha NVIDIA BGFX/OpenGL; um oráculo de pixels confiável para Coin/GL; e
comparação por pixel da transparência com tolerâncias explícitas. A matriz
Windows, macOS/Wayland nativo e Android permanece em P21–P23.
