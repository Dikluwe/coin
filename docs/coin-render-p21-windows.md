# P21 — preparação da rota Win32 (ainda não qualificada)

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
Uma opção explícita deve coincidir com o adaptador ativo; a ponte não troca
de device nem aplica fallback. No BGFX atual, a opção D3D12 responde
`UNSUPPORTED`: seu conector e o build de shaders ainda são Linux/Xlib com
Vulkan/OpenGL. D3D11 não foi implementado. Nenhum perfil Windows é marcado
como qualificado pela API de capacidades.

O CMake da ponte Rust agora usa a extensão `.lib` do `staticlib` com MSVC,
remove a biblioteca Unix `m` no Windows, acrescenta as bibliotecas de sistema
e anuncia superfície nativa no build Win32/wgpu. Exemplos Xlib existentes
continuam dependentes de X11; não constituem aplicativo de validação Win32.

| Combinação | Estado nesta entrega | Evidência ainda necessária |
|---|---|---|
| wgpu/D3D12 + `HWND` | Rota de handles e build preparados | Build Windows real, apresentação, captura RGBA, perda/recriação |
| wgpu/Vulkan + `HWND` | Mesma rota de superfície, seleção tipada existente | Build e driver Windows reais |
| BGFX/D3D11 | Pendente | Shaders, build, surface e matriz física |
| BGFX/D3D12 | Rejeição explícita, sem fallback | Infra, shaders, build e matriz física |
| Win32 resize/DPI/multiwindow | Contrato de pixel e lifecycle comum existente | Eventos reais `WM_SIZE`/DPI, minimização, duas janelas, destruição ordenada |

## Critérios para fechar

Em um host Windows com toolchain e GPU, compilar a ponte e um exemplo Win32
com `COIN_BUILD_RENDER=ON`, `COIN_RENDER_BACKEND=RUST_BRIDGE` e
`COIN_BUILD_RENDER_WINDOW_EXAMPLE=OFF`. Criar duas janelas independentes;
registrar renderer, vendor/device, formato e driver. Comparar a mesma fixture
opaca/transparente de P20 em janela e offscreen, com tolerância por pixel;
confirmar captura RGBA solicitada e ausência de readback no render normal.
Aplicar `WM_SIZE`, minimização/restauração e mudança de DPI usando o tamanho
físico do client area. Destruir cada alvo antes de seu `HWND`; injetar perda
de superfície/device e confirmar recuperação ou diagnóstico sem publicar um
quadro incompleto. Repetir por API e driver, com skips explícitos. D3D11 e
BGFX/D3D12 exigem implementação antes de entrar na matriz funcional.

Neste host Linux não existem toolchain/target Rust Windows nem Wine. Por isso
os resultados desta entrega são de **compilação e regressão Linux**, não de
apresentação Win32: CoinRender wgpu e BGFX compilaram; `CoinRenderSelectionTest`
passou nos dois; `CoinRenderSurfaceTest` passou no wgpu. O teste novo verifica
que pedir D3D12 no Linux retorna `UNSUPPORTED`, sem selecionar Vulkan/OpenGL.
A árvore Windows permanece por validar em um host Windows real.
