# RTT direto: mipmaps GPU e dimensão 4096 — 2026-10-11

Branch `codex/coin-render`. O RTT direto já gerava mipmaps na GPU para
RGBA8 linear e RGBA16F, incluindo dimensões NPOT. Esta rodada removeu o
teto artificial de 2048 por eixo **somente da rota direta**: até 4096 por
eixo, sujeito ao orçamento conservador de 64 MiB por grafo e ao limite real
do adaptador. A rota staged conserva 2048 por eixo. SRGB e BC3 continuam
fora do formato de saída RTT; não são convertidos silenciosamente.

O novo controle renderiza um produtor RGBA8 de **4096×1024**, gera os mips
na GPU e consome a média do checker no alvo 32×32. O quad do produtor
preenche o aspecto 4:1, para que a expectativa seja a média de toda a
textura. A seguir, 4096×4096 é recusado por exceder 64 MiB sem publicar
novo quadro, e o produtor 64×64 original recupera os pixels anteriores.
No Core, 4097×1 é recusado antes de alocação, staged 4096×1024 continua
recusado e o plano direto 4096×1024 com mips cabe no orçamento.

| Perfil Linux | Mips diretos | Mips staged | Texturas avançadas |
| --- | ---: | ---: | ---: |
| wgpu/Vulkan AMD RADV | PASS | PASS | PASS |
| wgpu/Vulkan NVIDIA | PASS | PASS | PASS |
| wgpu/OpenGL AMD radeonsi | PASS | PASS | PASS |
| BGFX/Vulkan AMD | PASS | PASS | PASS |
| BGFX/Vulkan NVIDIA | PASS | PASS | PASS |
| BGFX/OpenGL (vendor não identificado no relatório) | PASS | PASS | PASS |

Cada PASS de mips diretos incluiu o caso 4096×1024 e a rejeição/recuperação.
O gate Core passou nos dois builds. Quatro dos seis gates gerais de
`CoinRenderTextureSamplingTest` falharam no caso *projective mip footprint*
com AMD/OpenGL ou AMD/Vulkan; o mesmo caso já falhava no
[ledger anterior](../advanced-textures-20261007/summary.json), antes da
mudança de dimensão. Esses FAILs continuam no relatório integral e não
são contados como aprovação da suíte completa.

O runner foi `testsuite/coinrender/run-advanced-textures.py` com
`--wgpu-build /tmp/coin-render-closure-wgpu`,
`--bgfx-build /tmp/coin-render-closure-bgfx` e os seis perfis acima.
Quatro execuções adicionais de `CoinRenderRttProfileTest --mips-direct
--portable-sampling` passaram em wgpu/BGFX × AMD/NVIDIA Vulkan. Após
preservar o teto staged, repetimos Core, mips diretos e staged nos dois
builds com ICD NVIDIA; os seis gates passaram. Logs integrais, ambientes,
saídas e SHA-256 estão em
`/mnt/Laranja/Git/externos/coin-render-artifacts/mips-wide-20261011/`
(`summary.json`, `portable-summary.json`, `final-gates.json`).

O resultado qualifica a implementação e os limites nas rotas Linux medidas.
Metal, D3D12, Android físico e outros formatos RTT não foram testados nesta
rodada.
