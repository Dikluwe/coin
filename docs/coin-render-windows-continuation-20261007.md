# Continuação Windows — checklist e código Linux de 2026-10-07

A auditoria encontrou o checkout Linux limpo e confirmou no fork
`https://github.com/Dikluwe/coin.git`, branch `codex/coin-render`, o código
`e9ff5397a5f7613e5f810cfa4334e457245a1ebe`. Todas as mudanças de código,
checklist, testes e evidências das rodadas Linux estão nesse commit ou em seus
ancestrais. Este roteiro é uma atualização de documentação; não acrescenta
qualificação Windows ao código. A [checklist principal](coin-render-next-fronts-checklist.md)
e o [relatório Linux](coin-render-hardware-surfaces-linux.md) continuam sendo
os registros de fechamento.

## Checkout e pré-requisitos

Preservar o checkout principal e seus arquivos locais. Uma opção é buscar a
branch e criar um worktree separado, sem alterar a branch do checkout principal:

```powershell
$TaskCheckout = Join-Path (Split-Path $PWD -Parent) 'coin-render-windows-20261007'
git fetch https://github.com/Dikluwe/coin.git codex/coin-render
if ($LASTEXITCODE -ne 0) { throw 'Fetch falhou' }
git worktree add --detach $TaskCheckout FETCH_HEAD
if ($LASTEXITCODE -ne 0) { throw 'Criação do checkout separado falhou' }
Set-Location $TaskCheckout
git rev-parse HEAD
```

Executar os builds em Developer PowerShell x64 do Visual Studio 2022, com
MSVC/Windows SDK, CMake, Git, Python e Rust MSVC
`x86_64-pc-windows-msvc`. Usar GPU física e driver com suporte às APIs escolhidas.
BGFX precisa do pacote CMake instalado, `shaderc.exe`, `bgfx_shader.sh` e
shaders DXBC/GLSL/SPIR-V recompilados. As revisões de bgfx.cmake/bgfx/bx/bimg,
os comandos de instalação e o orçamento de 131.072 draws estão em
[coin-render-bgfx-windows.md](coin-render-bgfx-windows.md).

Os comandos abaixo assumem que o diretório corrente é o checkout separado.
Ajustar somente os caminhos de build/dependências/evidência à máquina:

```powershell
$CoinSrc = (Resolve-Path .).Path
$BgfxPrefix = (Resolve-Path '../bgfx-windows-install').Path
$BgfxBuild = Join-Path $CoinSrc 'build/windows-bgfx'
$WgpuBuild = Join-Path $CoinSrc 'build/windows-wgpu'
$Evidence = Join-Path $CoinSrc 'build/windows-validation-20261007'
New-Item -ItemType Directory -Force $Evidence | Out-Null

cmake -S $CoinSrc -B $BgfxBuild -G 'Visual Studio 17 2022' -A x64 `
  -DCOIN_BUILD_RENDER=ON -DCOIN_RENDER_BACKEND=BGFX -DCOIN_BUILD_TESTS=ON `
  -DCOIN_BUILD_RENDER_WINDOW_EXAMPLE=ON -DCOIN_BUILD_LEGACY_GL_RENDERER=ON `
  "-DCMAKE_PREFIX_PATH=$BgfxPrefix" `
  "-DCOIN_BGFX_SHADERC_EXECUTABLE=$BgfxPrefix/bin/shaderc.exe" `
  "-DCOIN_BGFX_SHADER_INCLUDE_DIR=$BgfxPrefix/include/bgfx"
if ($LASTEXITCODE -ne 0) { throw 'Configuração BGFX falhou' }
cmake --build $BgfxBuild --config Release --parallel 2
if ($LASTEXITCODE -ne 0) { throw 'Build BGFX falhou' }

cmake -S $CoinSrc -B $WgpuBuild -G 'Visual Studio 17 2022' -A x64 `
  -DCOIN_BUILD_RENDER=ON -DCOIN_RENDER_BACKEND=RUST_BRIDGE -DCOIN_BUILD_TESTS=ON `
  -DCOIN_BUILD_RENDER_WINDOW_EXAMPLE=ON -DCOIN_BUILD_LEGACY_GL_RENDERER=ON
if ($LASTEXITCODE -ne 0) { throw 'Configuração wgpu falhou' }
cmake --build $WgpuBuild --config Release --parallel 2
if ($LASTEXITCODE -ne 0) { throw 'Build wgpu falhou' }
```

Guardar os binários anteriores antes de atualizar builds ou instalações.
Recompilar Coin e CoinRender juntos: não misturar DLLs, headers, shaders ou
ponte Rust de revisões distintas. Para a referência CoinGL, manter o renderer
legado ligado e o contexto WGL disponível. A lista e os caminhos reais dos
executáveis são os produzidos pelo gerador, não os de outro host.

## Prioridades Windows

1. **Regressões da revisão nova.** Build/link MSVC dos dois backends, CoinTests
   e suíte comum, geometria/viewport, filtros/mipmaps/multitextura, RTT
   staged/direct e matriz/viewport herdados, publication/ownership, async
   readback, cache e recuperação. Incluir nós tardios, recursos portáteis e os
   contratos rejeitados sem publicação. FreeCAD requer build próprio compatível,
   Qt/Quarter e fontes/hooks da aplicação; a prova FreeCAD Linux não cobre Windows.
2. **APIs físicas.** BGFX: D3D12, Vulkan e OpenGL. wgpu: D3D12 (`dx12`),
   Vulkan e OpenGL (`gl`) no offscreen conforme suporte. Identificar
   backend/API/GPU/driver/alvo em cada resultado. D3D11 continua uma pendência
   de implementação, não uma API que se deva aprovar por fallback.
3. **Sombras e câmera.** Toda a família `CoinRenderShadow*`, incluindo
   `CoinRenderShadowEightMapTest`, qualidade/transparência/peeling/OIT/alfa RTT,
   viewport, Wiring e falhas; câmera com reúso versus captura completa/CoinGL.
   A nova expectativa de oito mapas conserva intensidade e verifica contribuição
   da oitava luz, rejeição da nona sem publicação e recuperação exata.
4. **Win32/DPI/multiwindow.** Duas ou mais janelas com dimensões/seriais
   independentes, janela e offscreen simultâneos, primeiro target de cada tipo,
   destruição de uma preservando a outra, captura apenas sob pedido. Passar
   por `WM_DPICHANGED` entre monitores físicos com DPI distintos, resize e
   minimizar/restaurar. Tamanhos do target são pixels de framebuffer.
5. **Perda/recriação real.** Destruir/recriar HWND com liberação prévia de
   action/target; verificar handles novos, pixels, tickets, serial e janela
   sobrevivente. Planejar device removal/loss em sessão de teste apropriada,
   registrar o diagnóstico e reconstruir recursos. Falhas injetadas e resize
   zero não substituem perda real de device; não resetar a GPU que atende
   trabalho ativo sem uma campanha controlada.
6. **Desempenho depois dos gates.** Revalidar reserva de updates de objetos,
   câmera/material/geometria e readback em A/B Windows com saída equivalente.
   `COIN_RENDER_DISABLE_OBJECT_UPDATE_RESERVE=1` é o controle literal; `0`
   mantém reserva. Tempos Linux e campanhas antigas Windows não qualificam
   ganhos nesta revisão/máquina. Serializar medições, registrar energia/display,
   mediana/p95, hashes e imagens; tracing deve ficar fora dos timers principais.

## Gates e seleção de API

Flags para os testes que exigem opt-in:

```powershell
$env:COIN_RENDER_REQUIRE_GL_REFERENCE = '1'
$env:COIN_WGPU_REQUIRE_GL_REFERENCE = '1'
$env:COIN_RENDER_REQUIRE_CAMERA_REFERENCE = '1'
$env:COIN_RENDER_REQUIRE_BGFX_SHADOW_GPU = '1'
$env:COIN_RENDER_REQUIRE_WGPU_SHADOW_GPU = '1'

$env:COIN_BGFX_RENDERER = 'd3d12' # repetir: vulkan, opengl
$env:WGPU_BACKEND = 'dx12'       # repetir: vulkan, gl

ctest --test-dir $BgfxBuild -C Release -N
ctest --test-dir $WgpuBuild -C Release -N
```

Um primeiro gate por API pode selecionar os testes de renderer herdado do
ambiente, sem misturar variantes que fixam outra API:

```powershell
$Gate = '^(CoinTests|CoinRenderProductTest|CoinRenderCameraReuseReferenceTest|CoinRenderShadowEightMapTest|CoinRenderShadowReferenceTest|CoinRenderShadowWiringTest|CoinRenderPublicationTest|CoinRenderRttOwnershipTest|CoinRenderMultiTargetTest)$'
ctest --test-dir $BgfxBuild -C Release --parallel 1 --output-on-failure `
  -R $Gate --output-junit "$Evidence/bgfx-${env:COIN_BGFX_RENDERER}.xml"
if ($LASTEXITCODE -ne 0) { throw 'Gate BGFX falhou; preservar logs' }
ctest --test-dir $WgpuBuild -C Release --parallel 1 --output-on-failure `
  -R $Gate --output-junit "$Evidence/wgpu-${env:WGPU_BACKEND}.xml"
if ($LASTEXITCODE -ne 0) { throw 'Gate wgpu falhou; preservar logs' }
```

Depois ampliar à família completa de sombras e às regressões da prioridade 1.
Consultar `CTestTestfile.cmake`/`ctest --show-only=json-v1`: vários testes têm
propriedade `ENVIRONMENT` que força OpenGL/Vulkan/D3D12. Por exemplo,
`CoinBgfxInstancingTest` fixa Vulkan, mesmo se o processo pai definir D3D12;
para D3D12 executar o binário diretamente com a API selecionada. Uma rodada
integral CTest é mista e deve registrar a API efetiva de cada caso. O gate
acima não é a suíte completa nem fecha os critérios de hardware.

Os smokes Win32 registrados pedem **explicitamente D3D12 ou Vulkan**.
`coin_render_win32_smoke --vulkan` escolhe Vulkan; sem argumento pede D3D12.
Apenas mudar `WGPU_BACKEND=gl`/`COIN_BGFX_RENDERER=opengl` não transforma esse
smoke em qualificação Win32/OpenGL. É necessário um host/teste que solicite
OpenGL explicitamente para essa superfície. Os testes `CoinBgfxMultiWindow*`
baseados em Xlib são condicionados a X11 e não substituem multiwindow Win32.

A suíte histórica `validation/linux-updates-windows-20261005/qualify.ps1`
contém caminhos `H:/...` e listas em `build/` daquela máquina. Não executar
sem adaptação; o CTest da revisão atual é a fonte da lista disponível.
Scripts P20/P27 Linux usam X11/GLX/EGL/ICDs Linux e não são runners Windows.

## Limites e registro de evidência

- Não impor `COIN_RENDER_REQUIRE_GL_EIGHT_MAP_REFERENCE=1` sem confirmar nove
  unidades de coordenadas utilizáveis no WGL. O controle negativo em contexto
  de oito unidades é uma rejeição esperada; o teste portátil de oito mapas
  não qualifica oito mapas nativos CoinGL, nem oito câmeras distintas por si só.
- Preservar diferenças de raster/GL e seus gates. Os quatro pixels P20 AMD
  e a diferença de câmera AMD/Linux estão como estudos; não se transporta
  tolerância nova para aprovar Windows. `BumpProgramGLX` é exclusivo de GLX.
- DPI entre monitores requer monitores físicos; Intel requer GPU Intel física.
  Software adapters, APIs indisponíveis e testes inexistentes são skips/limites,
  separados de passes. Não fechar a checklist só porque CTest retornou zero.
- Registrar SHA de fonte/binários/DLLs/shaders, SO/toolchain/GPU/driver,
  API efetiva, formato/escala de superfície, comandos/ambiente, XML/logs,
  métricas/pixels, rejeição sem publicação e recuperação. Guardar falhas
  iniciais e ligá-las a re-tests aprovados, sem sobrescrever sua evidência.
- AppKit/Metal, Wayland e Android exigem suas plataformas próprias. O link
  Android permanece aberto no Coin base por GL desktop; não é pendência
  que Windows ou uma execução apenas do smoke resolva.
