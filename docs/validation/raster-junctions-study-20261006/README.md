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
