# Replay BGFX de textura POT/NPOT em AMD e NVIDIA — 2026-10-11

Runner congelado `coin_render_window_benchmark`, Xlib 1280×720,
`COIN_RENDER_TRACE_PHASES=1`, BGFX/Vulkan, sem readback. As cenas congeladas
foram `texture-8.iv` e `npot-8.iv`; políticas native e portable; dois warmups
e oito frames medidos por processo.

| GPU física | Processos | Plano | Recursos, textura e geometria |
| --- | ---: | --- | --- |
| NVIDIA `10de:2560` | 4/4 PASS | `full_rebuild` 1º, `reuse` 2º–10º | miss 1º, hit 2º–10º |
| AMD `1002:1638` | 4/4 PASS | `full_rebuild` 1º, `reuse` 2º–10º | miss 1º, hit 2º–10º |

Cada processo relatou dez linhas de action e dez de BGFX, saída zero e o
adaptador físico esperado. Os resumos JSON com hashes SHA-256 dos oito logs
e CSVs estão em
`/mnt/Laranja/Git/externos/coin-portable-sampling-artifacts/20261008-sampling-api-linux/replay-textures-dual-gpu-20261011/`.
SHA-256 do `summary.json`: NVIDIA
`b05bc25498891ed23e77c17b4fe264679c1aae7153691f36f2d7e1330837194c`,
AMD `3739cb18f31fc81edb287458597edd8e4cfb56714de5501b42c18d0f7c7e7437`.

Esta sonda confirma reutilização do backend em cargas texturizadas estáticas;
não mede duração GPU nem explica a variação de processo do wgpu.
