# Matriz física BGFX

A matriz de produto do conector BGFX roda em GPUs físicas; um resultado em
Xvfb, llvmpipe, lavapipe, softpipe ou SwiftShader não conta como cobertura de
hardware. O workflow manual `.github/workflows/bgfx-gpu-matrix.yml` usa runners
Linux autogerenciados e executa cada API em um processo separado.

## Células obrigatórias

| Runner | API | Driver esperado | Identidade exigida |
| --- | --- | --- | --- |
| `amd-mesa` | Vulkan | RADV | PCI vendor `0x1002` |
| `amd-mesa` | OpenGL | radeonsi | `glxinfo -B` contém AMD e radeonsi |
| `intel` | Vulkan | ANV/Mesa Intel | PCI vendor `0x8086` |
| `intel` | OpenGL | iris, crocus ou Intel | `glxinfo -B` contém Intel |
| `nvidia` | Vulkan | NVIDIA | PCI vendor `0x10de` |
| `nvidia` | OpenGL | NVIDIA | `glxinfo -B` contém NVIDIA |

Os dois jobs que compartilham um rótulo de runner são serializados. A matriz
não é disparada automaticamente em todo pull request: sem os três runners ela
ficaria permanentemente pendente. Execute **BGFX physical GPU matrix** por
`workflow_dispatch` para qualificar uma revisão ou uma versão.

## Contrato dos runners

Cada máquina precisa dos rótulos `self-hosted`, `linux`, `x64`, `gpu` e do
rótulo da tabela. Também precisa de:

- um servidor X11 real em `DISPLAY`, com acesso concedido ao serviço do runner;
- CMake, Ninja, compilador C++11, `vulkaninfo`, `glxinfo`, X11/GL/Vulkan e as
  dependências normais do Coin;
- uma instalação BGFX compatível, incluindo `bgfxConfig.cmake`, headers e
  `shaderc`; a variável de repositório `COIN_BGFX_PREFIX` aponta para ela;
- para Vulkan, `VK_DRIVER_FILES` ou o nome legado `VK_ICD_FILENAMES` apontando
  para **um único ICD**. Isso impede que o inventário encontre RADV enquanto o
  BGFX seleciona outra GPU;
- para OpenGL híbrido, a seleção do servidor X/DRI deve fazer `glxinfo -B` e o
  processo BGFX enxergarem a mesma GPU. Configure `DRI_PRIME` ou as variáveis
  PRIME/NVIDIA no serviço do runner, não dentro do teste.

O executor recusa software rasterizers e falha se vendor ou driver não
corresponderem à célula. Para Vulkan ele também compara o PCI vendor retornado
pela sonda BGFX com o valor esperado.

## O que cada célula executa

`.github/scripts/build-and-test-bgfx-gpu.sh` configura um build Release com
BGFX, testes, janela Xlib e warnings estritos. Em seguida executa, somente para
a API da célula:

- contrato de produto e compatibilidade entre backends;
- offscreen, resize, perda/recriação e handoff do singleton;
- transparência object, sorted layers e weighted OIT;
- apresentação Xlib e adaptador de `SoRenderManager`.

A execução é serial. Cada job arquiva três evidências:

- `<api>-inventory.txt`: `vulkaninfo --summary` ou `glxinfo -B`;
- `<api>-capabilities.txt`: renderer, PCI IDs, formatos, MRT, independent blend,
  compute e timestamps reportados por `coin_render_query_capabilities`;
- `<api>-tests.txt`: resultado completo do CTest daquela célula.

Execução local equivalente:

```sh
export DISPLAY=:0
export COIN_BGFX_PREFIX=/opt/bgfx
export COIN_CI_GPU_VENDOR_PATTERN="AMD|Advanced Micro Devices"
export COIN_CI_GPU_DRIVER_PATTERN="radv|MESA_RADV"
export COIN_CI_GPU_VENDOR_ID=0x1002
export VK_DRIVER_FILES=/usr/share/vulkan/icd.d/radeon_icd.json
.github/scripts/build-and-test-bgfx-gpu.sh vulkan
```

Para radeonsi, remova `COIN_CI_GPU_VENDOR_ID`, ajuste os padrões para
`AMD|ATI` e `radeonsi`, e execute com `opengl`.

## Validação local — 26/09/2026

O executor foi validado nesta árvore em AMD Renoir com display X11 físico:

- RADV/Vulkan, ICD único `radeon_icd.json`: 9/9 testes passaram; a sonda
  retornou vendor `0x1002`, device `0x1638`, oito attachments e os bits MRT,
  independent blend, compute e timestamps;
- radeonsi/OpenGL: 9/9 testes passaram; `glxinfo -B` confirmou render direto em
  `AMD Radeon Graphics (radeonsi, renoir)`. Como o BGFX devolveu PCI IDs zero
  nesse renderer, a evidência GLX é preservada junto à sonda Coin.

Esses resultados validam o executor e duas células, não substituem as quatro
células Intel/NVIDIA que dependem dos respectivos runners físicos.

## Próxima expansão

Direct3D e Metal não fazem parte desta primeira matriz porque o conector BGFX
ainda só seleciona Vulkan ou OpenGL e a superfície implementada é Xlib. A
expansão será feita quando existirem, respectivamente, targets Win32 e Cocoa
com seleção explícita de Direct3D 11/12 e Metal. Esses jobs deverão reutilizar
o mesmo contrato de produto e produzir inventário/capacidades equivalentes,
sem marcar como coberta uma API que apenas compilou.
