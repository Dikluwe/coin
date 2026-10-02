# CoinRender — campanha Windows x64 (2026-10-02)

Base publicada: `origin/codex/coin-render`, commit `19225706d52d22324413dec47652216004adb04e`.
A execução exigiu as correções de portabilidade desta entrega. Os resultados
não descrevem a árvore original sem essas correções.
A portabilidade inicial foi validada em `dec024f8dd224287e8b1ee5a35309a452c2fd06f`.
As fixtures GL corrigidas estão em `f345a08ea1a9ba22e7150d91990eaa69cefa0bb0`.

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
| Suíte CTest D3D12 com GPU e oráculo Coin/GL obrigatórios | 99/99 após corrigir as fixtures GL, sem skips |
| Suíte CTest Vulkan com GPU e oráculo Coin/GL obrigatórios | 99/99 após corrigir as fixtures GL, sem skips |
| Fixtures de sombras, ALPHA_TEST, qualidade ampliada, peeling/OIT/layers | 39/39 por API, GPU e Coin/GL obrigatórios |
| Multi-device, readback assíncrono e RTT direto com estresse | Passaram em ambas as APIs |
| DrawStyle, Multitexture e Transparency com GL obrigatório | CPU/GPU e referências equivalentes passaram nas duas APIs |

As rodadas corrigidas passaram pelo runner estrito, com GPU física e Coin/GL
obrigatórios: D3D12 99/99 em 1.021,28 s; Vulkan 99/99 em 261,75 s, sem skips.
Os XMLs e probes estão em [D3D12](validation/p21-windows/corrected-d3d12/) e
[Vulkan](validation/p21-windows/corrected-vulkan/). Na campanha final D3D12,
DrawStyle levou 41,70 s, Multitexture 11,63 s e Transparency 58,89 s.

A rodada D3D12 original teve 95/99 em 991,71 s, incluindo timeout de RTT direto
aos 30 s. Após o ajuste desse limite Windows e o reteste 3/3, consolidou 96/99;
Vulkan originalmente teve 96/99 em 241,63 s. As três falhas de referência GL e
os logs originais continuam preservados. O reteste numérico Vulkan foi 3/3;
o diagnóstico D3D12 também aprovou esses três contratos e registra expectativas
antigas de outros testes, corrigidas antes da rodada completa original.

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

## Correção das referências Coin/GL

Os logs originais preservam as três falhas. A correção mantém os valores
numéricos, a unidade sete e os oito slots nas verificações CPU/GPU, assim como
as tolerâncias de canais e a exigência de renderização Coin/GL. A referência
GL usa fixtures equivalentes nos casos descritos abaixo; isso não certifica
recursos fixos que este contexto WGL não oferece.

- `CoinRenderTransparencyTest`: a cena CPU/GPU continua na unidade sete.
  Uma segunda cena GL, com o mesmo material, textura, combine e geometria,
  usa a unidade zero. A comparação continua nos onze modos, com a exceção
  preexistente de combine no peeling GL documentada no próprio teste.
- `CoinRenderMultitextureTest`: DOT3_RGBA com escalas distintas é comparado
  contra DOT3_RGB com alpha constante igual ao produto escalar independente
  (`.16` nesta fixture), escalado por ALPHA_SCALE. A referência continua
  executando textura, combine e blend em GL; escalas iguais usam DOT3_RGBA
  nativo. Isso preserva as escalas independentes exigidas pelo
  [OpenGL 1.3, seção 3.8.12](https://registry.khronos.org/OpenGL/specs/gl/glspec13.pdf),
  apesar do resultado observado no caminho fixo NVIDIA. Nos casos acima da
  unidade três, a última etapa REPLACE é projetada para a unidade zero GL,
  preservando imagem, UV, matriz e sampler; o teste rejeita essa projeção se a
  etapa final não for REPLACE. O estado original é restaurado após o GL.
- `CoinRenderDrawStyleTest`: o contador Core continua sendo verificado
  exatamente, incluindo continuidade, reset, cantos e alpha sem dupla cobertura.
  O Coin/GL é comparado pela geometria sólida/vazia e por uma fase consistente
  por aresta, comum a todas as máscaras, repetições, espessuras e caminhos.
  A referência nativa de line strips continua com comparação direta de pixels.
  O GL pode iniciar a borda poligonal em outra aresta, alterando a fase; a
  [especificação de rasterização poligonal](https://registry.khronos.org/OpenGL/specs/gl/glspec11.pdf)
  define o reset na primeira aresta rasterizada. As exceções preexistentes de
  cortes no frustum permanecem explicitamente registradas.

Na primeira repetição D3D12, a matriz completa de transparência alcançou o
limite de 90 s durante execução concorrente. A execução direta terminou com
sucesso em 79,57 s. O limite Windows passa para 180 s para acomodar compilação
FXC e carga concorrente, sem alterar pixels ou tolerâncias. O timeout inicial
e a medição bem-sucedida estão preservados nos logs.

## Limites desta evidência

P21 avançou de preparação para execução nativa real. Continuam pendentes mudança
entre monitores com DPI distintos, registro do formato concreto da superfície,
perda/recriação de superfície/device em janela. As comparações GL corrigidas
usam as equivalências descritas acima, sem qualificar oito unidades fixas nativas.
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

## Integração na branch comum

Os quatro commits Windows, de `dec024f8dd` até `4e72bfa2ba`, foram integrados
por fast-forward em `codex/coin-render`, sem conflitos. O plano comum registra
Windows/NVIDIA qualificado e conserva GPU Intel, macOS, DPI entre monitores,
perda real de superfície e os conectores BGFX Windows como pendências próprias.

Após a integração, Coin e os alvos afetados foram recompilados no Linux.
As regressões passaram na RTX 3060 Laptop, driver 610.57.04, com referência
Coin/GL na própria NVIDIA: BGFX/OpenGL 9/9 e wgpu/Vulkan 9/9, sem skips.
As fixtures incluem FrameCore (padding), Selection, Lighting, Stabilization,
DrawStyle, Multitexture, Transparency, ShadowReference e ShadowWiring.
Rust passou 23 testes; Coin passou 370 testes e 85.374 verificações.
Os [logs da integração](validation/p21-windows/linux-integration/) preservam
os resultados. A execução Linux verifica regressões das mudanças comuns;
a evidência Windows continua sendo a campanha nativa descrita acima.
