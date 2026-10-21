# B10 — timestamps GPU da cena city dinâmica

A cena city histórica de 40 mil objetos, 512×512, com geometria 100%
dinâmica e transparência por objeto foi executada em BGFX e wgpu, nas GPUs
AMD e NVIDIA por Vulkan. Cada processo teve três quadros de aquecimento e
cinco medidos, com `COIN_RENDER_TRACE_PHASES=1` e
`COIN_WGPU_GPU_TIMESTAMPS=1`. Os números abaixo são medianas dos oito
registros de trace de cada processo, incluindo o aquecimento.

| GPU/backend | GPU render/frame | CPU lowering/encode | Espera por readback GPU |
| --- | ---: | ---: | ---: |
| AMD BGFX | 0,854 ms | 39,780 ms lowering | 3,197 ms |
| NVIDIA BGFX | 0,516 ms | 42,285 ms lowering | 2,258 ms |
| AMD wgpu | 0,819 ms render + 0,054 ms copy | 2,534 ms draw encode Rust | 1,323 ms |
| NVIDIA wgpu | 0,339 ms render + 0,166 ms copy | 2,280 ms draw encode Rust | 1,248 ms |

O intervalo GPU é muito menor que a preparação CPU observada nesse perfil.
No BGFX, o lowering sozinho excede o frame GPU em dezenas de vezes; o
próximo A/B deve mirar a montagem/validação/lowering do plano, preservando
o gate visual e o benchmark intercalado. Os tempos CPU wgpu acima medem
somente o trecho Rust; a action C++ e o preparo do overlay estão fora dessa
coluna e já foram localizados na [linha de base](coin-render-b10-current-baseline-20261017.md).

Esta é uma amostra diagnóstica de um processo por célula, com trace ativo e
apenas cinco quadros medidos. Não estabelece p95, ganho de otimização,
latência de janela/scanout nem extrapolação para outras cenas/APIs. O
`gpu_frame_ms` BGFX e os timestamps Rust wgpu têm escopos diferentes; não
foram subtraídos para atribuir custo ao driver.

[Traces, métricas e SHA-256](validation/coin-render-b10-gpu-timestamps-20261017/results.json).
