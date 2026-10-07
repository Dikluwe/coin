# P21 — rota Win32 e validação parcial

A [campanha Windows x64 de 2026-10-02](coin-render-p21-windows-validation.md)
compilou e executou os smokes e os 99 testes em D3D12 e Vulkan numa GTX 1060
física, sem falhas ou skips após corrigir as referências GL. Duas
janelas, captura RGBA, ausência de readback normal, resize e minimizar/restaurar
passaram. As fixtures opaca e transparente tiveram delta zero entre janela e
offscreen em ambas as APIs. DPI entre monitores e perda/recriação em janela
continuam pendentes; a campanha documenta as correções das referências Coin/WGL
e seus limites de equivalência.

P21 começa pela apresentação wgpu/D3D12 em uma janela Win32. O descriptor
público já continha `HINSTANCE` e `HWND`, mas o alvo comum rejeitava o tag
Win32 e a ponte só construía handles Xlib. Agora, em builds Windows com a
ponte Rust, `CoinRenderTarget::createWindow()` aceita um `HWND` não nulo,
transporta os handles pela FFI e cria um `RawWindowHandle::Win32` para a
superfície wgpu. `HINSTANCE` é opcional no handle bruto. O alvo não assume a
posse da janela: criação, resize, render e destruição seguem a thread dona
do `HWND`. O tamanho passado à API continua em **pixels de framebuffer**;
o host deve converter medidas lógicas de DPI antes de chamar criação/resize.

`COIN_RENDER_RENDERER_D3D12` identifica o renderer sem confundi-lo com
`OTHER`. O probe wgpu informa D3D12 quando esse é o backend do adaptador.
Na primeira superfície, uma opção explícita seleciona apenas adaptadores da API
pedida compatíveis com o `HWND`; depois que o device compartilhado existe, pedidos
para outra API retornam `UNSUPPORTED`, sem trocar de device nem aplicar fallback.
O [conector BGFX atual](coin-render-bgfx-windows.md) também aceita Win32 e
D3D12, com shaders DXBC e device compartilhado; Vulkan e OpenGL são seleções
explícitas. A campanha BGFX Windows documenta 163 testes distintos aprovados,
incluindo 39 de sombras com GPU obrigatória. D3D11 não foi implementado.
Esses resultados anteriores não qualificam automaticamente as mudanças Linux
de 2026-10-07. Nenhum perfil Windows é marcado
como qualificado pela API de capacidades.

O CMake da ponte Rust agora usa a extensão `.lib` do `staticlib` com MSVC,
remove a biblioteca Unix `m` no Windows, acrescenta as bibliotecas de sistema
e anuncia superfície nativa no build Win32/wgpu. O alvo
`coin_render_win32_smoke` cria duas janelas reais, solicita D3D12, apresenta
e captura RGBA, compara a cor central, confirma ausência de readback normal,
redimensiona e minimiza/restaura. Registra DPI e identidade do adaptador e
passou no Windows físico descrito na campanha. Os exemplos Xlib continuam
dependentes de X11.

| Combinação | Estado nesta entrega | Evidência ainda necessária |
|---|---|---|
| wgpu/D3D12 + `HWND` | Build e smoke passaram em Windows/NVIDIA 581.08 | Perda/recriação e demais pendências da campanha |
| wgpu/Vulkan + `HWND` | Build e smoke passaram em Windows/NVIDIA 581.08 | Perda/recriação e demais pendências da campanha |
| BGFX/D3D11 | Pendente | Shaders, build, surface e matriz física |
| BGFX/D3D12 | Win32/shaders implementados; campanha física documentada em BGFX Windows | Requalificar a revisão atual, DPI entre monitores e perda real de surface/device |
| Win32 resize/DPI/multiwindow | Duas janelas, resize e minimizar/restaurar passaram a 96 DPI | Mudança entre monitores DPI distintos |

## Critérios para fechar

Em um host Windows x64 com MSVC, Rust MSVC e GPU, configurar e executar:

```powershell
cmake -S . -B build-win -DCOIN_BUILD_RENDER=ON -DCOIN_RENDER_BACKEND=RUST_BRIDGE -DCOIN_BUILD_RENDER_WINDOW_EXAMPLE=ON -DCOIN_BUILD_LEGACY_GL_RENDERER=OFF
cmake --build build-win --config Release --target coin_render_win32_smoke
.\build-win\bin\Release\coin_render_win32_smoke.exe
```

O caminho do executável depende do gerador CMake. O smoke cria duas janelas
independentes e registra renderer, vendor/device, DPI e checksum da captura.
Para fechar P21, registrar também formato e driver. Comparar a mesma fixture
opaca/transparente de P20 em janela e offscreen, com tolerância por pixel;
confirmar captura RGBA solicitada e ausência de readback no render normal.
Aplicar `WM_SIZE`, minimização/restauração e mudança de DPI usando o tamanho
físico do client area. Destruir cada alvo antes de seu `HWND`; injetar perda
de superfície/device e confirmar recuperação ou diagnóstico sem publicar um
quadro incompleto. Repetir por API e driver, com skips explícitos. D3D11 e
BGFX/D3D12 exigem implementação antes de entrar na matriz funcional.

Neste host Linux não existem toolchain/target Rust Windows nem Wine. Por isso
os resultados desta entrega são de **compilação e regressão Linux**, não de
apresentação Win32: CoinRender wgpu compilou; `CoinRenderSelectionTest`,
`CoinRenderLightingTest` e `CoinRenderSurfaceTest` passaram. O último também
passou com `--require-vulkan` em Xwayland/RADV, verificando seleção explícita
do adaptador Vulkan em superfície real. O teste anterior verifica que pedir
D3D12 no Linux retorna `UNSUPPORTED`, sem selecionar Vulkan/OpenGL.
A execução Windows posterior está registrada na campanha vinculada acima.
