# CoinRender BGFX no Windows

O conector BGFX usa Direct3D12 por padrão no Windows. A seleção explícita
usa `COIN_BGFX_RENDERER=d3d12`, `vulkan` ou `opengl`. No Linux, o padrão
continua sendo Vulkan. Uma solicitação explícita não pode ser atendida por
outro renderizador.

Os shaders Direct3D12 são compilados por shaderc para DXBC (`s_5_0`). A
seleção inclui os programas de geometria, leitura de profundidade,
composição transparente e sombras. Os caminhos de superfície recebem um
HWND Win32 e compartilham o dispositivo BGFX entre alvos.

## Dependências usadas nesta máquina

- bgfx.cmake: `eb88f6be55ae223966698c802542c24b0d93fd25`.
- bgfx: `7346c3e731bd65f35c7e6a99819840e554f5b748`.
- bimg: `6b08e87de28e7aa54782d5ce1b279dca373a10c6`.
- bx: `1c986bd1e9a176a08ae885a6cdcefe76c3f700fc`.
- Visual Studio 2022 Build Tools, MSVC 19.44, Windows SDK 10.0.26100.0.
- Windows 10, NVIDIA GTX 1060 6 GB, driver 581.08.

Os diretórios de dependências e build ficam em `build/`, separados da
compilação Rust/wgpu.

```powershell
git clone --recursive https://github.com/bkaradzic/bgfx.cmake.git build/bgfx-windows-source
git -C build/bgfx-windows-source checkout eb88f6be55ae223966698c802542c24b0d93fd25
git -C build/bgfx-windows-source submodule update --init --recursive
cmake -S build/bgfx-windows-source -B build/bgfx-windows-msvc -G "Visual Studio 17 2022" -A x64 `
  -DBGFX_BUILD_EXAMPLES=OFF -DBGFX_BUILD_TESTS=OFF -DBGFX_BUILD_TOOLS=ON `
  -DBGFX_INSTALL=ON -DBGFX_CONFIG_MAX_DRAW_CALLS=131072 `
  -DCMAKE_INSTALL_PREFIX=H:/Git/coin/build/bgfx-windows-install
cmake --build build/bgfx-windows-msvc --config Release --target install --parallel 2

cmake -S build/coin-render-source -B build/coin-render-bgfx-msvc -G "Visual Studio 17 2022" -A x64 `
  -DCOIN_BUILD_RENDER=ON -DCOIN_RENDER_BACKEND=BGFX -DCOIN_BUILD_TESTS=ON `
  -DCOIN_BUILD_RENDER_WINDOW_EXAMPLE=ON -DCOIN_BUILD_LEGACY_GL_RENDERER=ON `
  -DCOIN_INSTALL_RENDER_EXPERIMENTAL=ON -DCMAKE_INSTALL_PREFIX=H:/Git/coin/build/coin-render-bgfx-install `
  -DCMAKE_PREFIX_PATH=H:/Git/coin/build/bgfx-windows-install `
  -DCOIN_BGFX_SHADERC_EXECUTABLE=H:/Git/coin/build/bgfx-windows-install/bin/shaderc.exe `
  -DCOIN_BGFX_SHADER_INCLUDE_DIR=H:/Git/coin/build/bgfx-windows-install/include/bgfx
cmake --build build/coin-render-bgfx-msvc --config Release --parallel 2
cmake --install build/coin-render-bgfx-msvc --config Release
```

O orçamento de 131.072 chamadas evita truncar a cidade de 40.000 edifícios
com o limite padrão do BGFX. Ele deve ser definido ao compilar a biblioteca
BGFX, e não apenas o Coin.

## Validação local

Os resultados finais cobrem 163 testes distintos aprovados, sem skips
pendentes. A execução completa inicial teve 112 aprovações, 19 falhas e
32 skips. Após corrigir as premissas dos testes para Windows e BGFX, a
reexecução dirigida passou em 58/58, incluindo todas as falhas e skips
iniciais e os 39 testes de sombras com GPU obrigatória. Esse total combina
as duas execuções; não representa uma única execução integral sem falhas.
Os logs, XMLs e resultados por teste estão em
[validation/bgfx-windows](validation/bgfx-windows/), com o consolidado em
[validation-summary.json](validation/bgfx-windows/validation-summary.json).

```powershell
$env:COIN_BGFX_RENDERER = 'd3d12'
$env:COIN_RENDER_REQUIRE_GL_REFERENCE = '1'
$env:COIN_WGPU_REQUIRE_GL_REFERENCE = '1'
$env:COIN_RENDER_REQUIRE_BGFX_SHADOW_GPU = '1'
ctest --test-dir build/coin-render-bgfx-msvc -C Release --output-on-failure
```

O dispositivo ativo foi confirmado como `BGFX Direct3D 12 NVIDIA`, vendor
`0x10de`, device `0x1c03`. A cena de 100 edifícios renderizou corretamente
em 256×256. O teste Win32 passou com dois HWNDs, resize e minimizar/restaurar;
as capturas opaca e transparente tiveram diferença máxima zero em relação
ao offscreen. Vulkan também passou no smoke Win32.

As três cidades grandes passaram em 1024×1024, com quatro frames de
aquecimento e oito medidos. Os tempos incluem readback RGBA síncrono.

| Edifícios | Triângulos | Primeiro frame | Mediana | P95 |
|---|---:|---:|---:|---:|
| 2.500 | 30.012 | 546,452 ms | 20,7516 ms | 22,5917 ms |
| 10.000 | 120.012 | 1.230,63 ms | 423,806 ms | 451,764 ms |
| 40.000 | 480.012 | 4.023,4 ms | 1.630,15 ms | 1.672,61 ms |

```powershell
python build/coin-render-source/examples/coinrender/generate_large_scene.py build/large-scenes/city-40000.iv --grid 200
& ./build/coin-render-bgfx-msvc/bin/coin_render_gl_benchmark.exe `
  --backend bgfx --scene build/large-scenes/city-40000.iv `
  --size 1024 --warmup 4 --frames 8 `
  --image-output build/large-scenes/city-40000-bgfx-d3d12.ppm
```

O log de 40.000 edifícios ainda usa a chave histórica
`WebGPU_first_frame_ms`; o renderizador confirmado nessa execução é
BGFX/D3D12. O benchmark atualizado identifica o backend nessa marca.

A cidade de 40.000 edifícios (480.012 triângulos) renderizou em 1024×1024.
Com quatro frames de aquecimento e oito medidos, a mediana foi 1.630,15 ms
por frame, incluindo renderização e readback RGBA. O primeiro frame levou
4.023,4 ms; o carregamento da cena, 506,575 ms; a destruição dos recursos,
132,708 ms. Esse cenário usa muitas chamadas de desenho e continua lento.
O OpenGL legado havia medido 35,55 ms na mesma cidade.

A captura BGFX/D3D12 tem delta médio RGB de `[0,01450; 0,03040; 0,02292]`
em relação à captura OpenGL, em uma escala de 0 a 255. Essa medida descreve
as imagens desta cena; não substitui os oráculos e tolerâncias dos testes.
As capturas de 10.000 e 40.000 edifícios são pixel a pixel idênticas às
capturas wgpu/Vulkan correspondentes. A imagem maior está preservada em
[city-40000-bgfx-d3d12.png](validation/bgfx-windows/city-40000-bgfx-d3d12.png).

A preparação inicial fazia uma busca linear em todos os estados anteriores
para cada matriz de modelo nova. O índice por matriz remove essa busca
quadrática e mantém a comparação completa dos candidatos, a ordem e os
slots de estado. Zeros com sinais diferentes são normalizados na chave.
A regressão captura 128 transformações, uma repetição e um material
diferente na mesma transformação, reutilizando o builder em duas capturas.

A leitura de screenshot aceita a última linha sem padding final, como
permitido pelos footprints D3D12. As janelas de 344×241 e 504×351 exercitam
larguras que não coincidem com o alinhamento da linha de readback.

Os perfis públicos qualificados não incluem D3D12 automaticamente. A
disponibilidade do dispositivo e os formatos concretos são consultados no
runtime; qualificação exige evidência dos testes do perfil correspondente.
Essa validação local não encerra a issue 136 nem todos os requisitos P21,
como DPI entre monitores físicos e recuperação completa do dispositivo.
