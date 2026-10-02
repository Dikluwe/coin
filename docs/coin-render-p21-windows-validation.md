# CoinRender — campanha Windows x64 (2026-10-02)

Base publicada: `origin/codex/coin-render`, commit `19225706d52d22324413dec47652216004adb04e`.
A execução exigiu as correções de portabilidade desta entrega. Os resultados
não descrevem a árvore original sem essas correções.
O commit de código validado é `dec024f8dd224287e8b1ee5a35309a452c2fd06f`.

## Máquina e toolchain

- Windows 10 Pro x64, `10.0.19045`; Intel Core i5-4670K, quatro cores; 16 GiB RAM.
- NVIDIA GeForce GTX 1060 6GB, `10de:1c03`, driver `581.08`, 6144 MiB VRAM.
- Visual Studio 2022 Build Tools; MSVC `19.44.35229.0`; SDK Windows `10.0.26100.0`.
- CMake `3.31.6-msvc6`; Rust/Cargo `1.99.0`, `stable-x86_64-pc-windows-msvc`.
- Coin/GL: WGL, `NVIDIA GeForce GTX 1060 6GB/PCIe/SSE2`, OpenGL
  `4.6.0 NVIDIA 581.08`. Quatro unidades fixas de textura, oito conjuntos
  de coordenadas e 32 samplers de fragmento. O probe de sombras informa
  `max_texture_units=8`, `native_eight_maps=0`.

Os probes wgpu identificaram a mesma GPU física em Vulkan (`renderer=1`) e
D3D12 (`renderer=4`). Nenhum desses probes usou o Microsoft Basic Render Driver.

## Resultados

| Verificação | Resultado |
|---|---|
| Build x64 Release de Coin, CoinRender, exemplos e testes | Passou |
| Rust: Core, shaders WGSL, composição, transparência e transporte de pixels | 23/23, sem skips |
| CoinTests, suíte básica C++ | 368 testes, 85.367 verificações |
| Win32/D3D12: duas janelas, captura, ausência de readback normal, resize e minimizar/restaurar | Passou |
| Win32/Vulkan: mesma fixture e lifecycle | Passou |
| Janela versus offscreen, opaco e transparente, em ambas as APIs | Delta máximo de canal 0; tolerância 3 |
| Instalação e consumidor CMake externo usando as DLLs instaladas | Passou em D3D12 |
| Suíte CTest D3D12 com GPU e oráculo Coin/GL obrigatórios | 96/99 consolidados; três divergências GL, sem skips |
| Suíte CTest Vulkan com GPU e oráculo Coin/GL obrigatórios | 96/99; mesmas três divergências GL, sem skips |
| Fixtures de sombras, ALPHA_TEST, qualidade ampliada, peeling/OIT/layers | 39/39 por API, GPU e Coin/GL obrigatórios |
| Multi-device, readback assíncrono e RTT direto com estresse | Passaram em ambas as APIs |
| DrawStyle, Multitexture e Transparency sem comparação GL obrigatória | Verificações numéricas CPU/GPU passaram nas duas APIs |

A rodada D3D12 completa teve inicialmente 95/99 em 991,71 s, incluindo timeout
de RTT direto aos 30 s. O teste passou isoladamente em 66,2 s e, após ajustar
seu limite Windows, passou pelo CTest em 74,75 s. O reteste também repetiu os
dois smokes finais (3/3). A consolidação de 96/99 não apaga o timeout original.
A rodada Vulkan completa levou 241,63 s. As três falhas GL permanecem falhas:
o runner encerra com erro e a matriz completa não é declarada qualificada.
O reteste numérico Vulkan foi 3/3. No D3D12, os mesmos três testes passaram na
rodada diagnóstica anterior; esse log preserva também duas expectativas antigas
de outros testes, posteriormente corrigidas e aprovadas na rodada completa.

Os [logs](validation/p21-windows/) preservam os resultados. O smoke registra
96 DPI; o mesmo checksum aparece nas duas APIs e nas duas janelas para o mesmo
tamanho. A captura publicada tem bytes RGBA8; a superfície negocia o formato
compatível por meio do conector. O teste não expõe o formato DXGI/Vulkan concreto.

## Correções necessárias

- Isolar os handles Xlib da compilação Windows e converter o XID para o tamanho
  nativo com verificação. A ponte respeita `WGPU_BACKEND` pela configuração
  padrão de ambiente do wgpu, preservando a seleção automática sem a variável.
- Vincular as dependências Windows do staticlib Rust, exportar suas entradas FFI
  privadas da DLL CoinRender e corrigir a declaração `class/struct` do plano RTT.
- Remover `dllimport/dllexport` dos templates de pimpl, impedir cópia de alvos que
  possuem recursos exclusivos e exportar o construtor público de `UnitData`.
- Evitar macros Win32 `min/max/near`, usar `_putenv_s` nos pontos POSIX e compilar
  as funções privadas de dict/dynarray diretamente na suíte básica Windows DLL.
- Comparar unidades de textura pelos campos, sem ler padding. O MSVC expôs
  estados iguais tratados como diferentes, fragmentação de draws e logs
  não determinísticos. Uma regressão injeta padding distinto em valores iguais.
- Atualizar expectativas antigas de annotations, revisão da FFI e orçamento de
  cache calculado pelo layout atual. Annotations desativam profundidade e não
  limpam a profundidade base; o vértice da ponte inclui os UVs adicionais.
- Registrar os smokes no CTest e comparar janela/offscreen também com alpha.
  As fixtures grandes de sombras têm limite de 600 s no Windows: o processo
  D3D12/FXC compila muitos pipelines e excedeu os limites Linux de 60/120 s.
  O RTT direto passou isoladamente em 66,2 s; seu limite Windows passa de 30
  para 180 s. A rodada completa original e a repetição são preservadas.

## Divergências do oráculo Coin/GL

A referência GL é obrigatória na campanha completa; divergências não são
convertidas em aprovação nem escondidas por aumento de tolerância.

- `CoinRenderTransparencyTest`: a fixture usa a unidade sete, além das quatro
  unidades fixas disponíveis no WGL. No primeiro caso aditivo, Core=128 e
  GL=191. A variante diagnóstica usa a unidade três e três conjuntos inferiores
  de coordenadas; passou na matriz de onze modos, CPU e GPU, com Coin/GL
  obrigatório. Ela não
  substitui o teste original da unidade sete.
- `CoinRenderMultitextureTest`: DOT3_RGBA (`0x86af`), RGB scale=2 e alpha scale=1
  produziu GL=26 versus Core=13. O contrato numérico mantém escalas independentes,
  como a [especificação ARB](https://registry.khronos.org/OpenGL/extensions/ARB/ARB_texture_env_dot3.txt).
  A equivalência desse caso com o Coin/WGL deste driver permanece aberta.
- `CoinRenderDrawStyleTest`: a referência de stipple nativo passou, mas a
  comparação de contorno com clipping encontrou pixels distintos. O log
  registra as coordenadas e os valores; a variante sem oráculo GL obrigatório
  passou nas verificações numéricas CPU/GPU.

## Limites desta evidência

P21 avançou de preparação para execução nativa real. Continuam pendentes mudança
entre monitores com DPI distintos, registro do formato concreto da superfície,
perda/recriação de superfície/device em janela e as comparações GL acima.
Os testes de falha offscreen da suíte não substituem a perda de uma superfície
Win32 real. Não se altera a máscara de perfis qualificados da API só por estes
smokes. BGFX/D3D11 e BGFX/D3D12 continuam sem conector Windows; o CMake BGFX
permanece Linux-only. A oitava sombra Coin/GL nativa também continua pendente:
este WGL não fornece o contexto necessário de nove unidades utilizáveis.

## Reproduzir

```powershell
cmake -S . -B build-win -G "Visual Studio 17 2022" -A x64 `
  -DCOIN_BUILD_RENDER=ON -DCOIN_RENDER_BACKEND=RUST_BRIDGE `
  -DCOIN_BUILD_RENDER_WINDOW_EXAMPLE=ON -DCOIN_BUILD_TESTS=ON `
  -DCOIN_BUILD_LEGACY_GL_RENDERER=ON -DCOIN_INSTALL_RENDER_EXPERIMENTAL=ON
cmake --build build-win --config Release --parallel 2
& .github/scripts/qualify-coin-render-windows.ps1 `
  -BuildDirectory build-win -Backend dx12 -EvidenceDirectory evidence-dx12 `
  -ExpectedAdapterPattern 'NVIDIA GeForce GTX 1060'
& .github/scripts/qualify-coin-render-windows.ps1 `
  -BuildDirectory build-win -Backend vulkan -EvidenceDirectory evidence-vulkan `
  -ExpectedAdapterPattern 'NVIDIA GeForce GTX 1060'
```

O runner verifica a API/adaptador, exige GPU e Coin/GL e falha se houver falha ou
skip. A configuração de ambiente de cada smoke escolhe sua API explicitamente;
por isso uma rodada completa inclui ambos os smokes, independentemente do
backend offscreen escolhido para os demais testes.
