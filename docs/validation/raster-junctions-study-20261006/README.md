# Evidências do estudo de raster nas junções

Observações diagnósticas de 2026-10-06 na worktree `codex/coin-render`,
base `50ac49e02a`, com alterações de desenvolvimento ainda não qualificadas
integralmente. Estes logs documentam o estudo; não constituem fechamento
P02/P04/P05/P06 nem resultados da suíte completa.

- `sphere-depth-diagnostic.log`: reproducer `CoinRenderProceduralTextureTest
  --probe-sphere`; CPU/wgpu máximo RGB 1, CPU/CoinGL máximo 9 e retorno de
  falha esperado do oráculo nativo. Inclui testemunhos de profundidade D24.
- `sphere-filled-control.log`: controle com `COIN_PROBE_FILLED=1` e o mesmo
  reproducer; máximo RGB CPU/wgpu 1, CPU/CoinGL 1 e wgpu/CoinGL 0.

- `line-junction-controls.log`: controles `--probe-style-line`,
  `--probe-style-line-depthless` e `--probe-style-line-width1`; CPU/wgpu
  máximo RGB 1, referências CoinGL 18/24/18. Cada divergência nativa retorna
  falha explícita no reproducer.

- `bgfx-opengl-curved-alpha.log`: diagnóstico anterior à correção comum;
  84 células, 42 LINES falham e 42 POINTS passam, com os limites RGB originais.
- `bgfx-opengl-d32-diagnostic.log`: trocar o attachment para D32F não elimina
  o máximo 110 na esfera. Mudança experimental descartada, não publicada.

- `bgfx-opengl-common-ownership-qualified.log`: as mesmas 84 células passam
  depois da política comum de ownership top/left. A campanha integrada de
  fechamento conserva o gate OpenGL completo, sem exclusões provisórias.

Ambiente: Linux, build Release RUST_BRIDGE, NVIDIA RTX 3060 Laptop,
CoinGL via GLX NVIDIA e wgpu Vulkan. Variáveis de seleção:
`__NV_PRIME_RENDER_OFFLOAD=1`, `__GLX_VENDOR_LIBRARY_NAME=nvidia`,
`VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/nvidia_icd.json`,
`COIN_GLX_PIXMAP_DIRECT_RENDERING=1`, `COIN_RENDER_REQUIRE_GL_REFERENCE=1`,
`DISPLAY=:0`.

Sem hashes das revisões intermediárias, esses diagnósticos têm alcance
observacional. A campanha futura deverá congelar fontes/binários e registrar
comandos completos, API/driver e imagens por execução antes de comparar
abordagens ou medir desempenho.

Decisão e roteiro: [estudo de raster](../../coin-render-raster-junctions-study.md).

## Sondas de alpha em endpoints — 2026-10-10

`witness-50-alpha-20261010.cpp` e `witness-36-alpha-20261010.cpp` isolam
os segmentos autorais das bordas 50/1 e 36/1 em EGL/OpenGL.
`alpha-endpoints-manifest-20261010.json` registra SHA-256, seleção EGL,
renderizador e os seis logs Radeon/NVIDIA/llvmpipe. Compilar com
`g++ -std=c++17 -O2 <fonte> -o <binário> -lEGL -lGL`; usar
`EGL_PLATFORM=surfaceless` e o arquivo EGL vendor indicado no manifesto.
São controles de cobertura e blend, não aprovação do contrato portátil.

## Par CoinRender transparente — 2026-10-10

`portable-alpha-20261010/CoinRenderJunctionAlphaStudy.cpp` é uma variante
manual da sonda `CoinRenderRasterStudy`, compilada contra os builds wgpu/BGFX
congelados de 2026-10-07 e contra o build BGFX deste checkout de 2026-10-09.
`summary.json` lista 32 execuções nas duas GPUs; `summary-current-bgfx.json`
lista mais 16. Cada subdiretório guarda o log e `cpu/gpu/coin-gl.rgba8.gz`,
com 80×80 RGBA8 top-down e SHA-256 no manifesto. Os modos 0–3 representam
autoral, invertido, par no mesmo sentido e par oposto. O código foi ligado
aos `libCoinRender.so`/`libCoin.so` dos builds indicados, com os includes do
checkout `coin-portable-sampling-restored`; as bibliotecas são identificadas
por SHA-256 nos manifestos. As execuções exigem display GLX para CoinGL,
Vulkan físico selecionado por `VK_ICD_FILENAMES` e backend Vulkan.

Leitura: RGB/cobertura portátil e CoinGL coincidem até 1; alpha CPU/GPU
segue o contrato portátil e difere do CoinGL legado. Este é um diagnóstico
manual, sem alteração de gates ou do renderizador.
