# BGFX NPOT com oito sombras e transparência

Campanha local de 2026-10-09 na branch `codex/coin-portable-sampling-study`.
O novo fixture `CoinRenderShadowReferenceTest --npot-shadow-oit 1|2` cria um
produtor RTT direto 63×47 com oito mapas de sombra, transparência por peeling
(1) ou weighted OIT (2), e consumidor que pede mips. A cadeia 63×47 gera cinco
níveis adicionais por área em frames BGFX próprios. O controle exige que
sombras e transparência alterem pixels, que o nono mapa seja recusado sem
alterar serial/pixels publicados e que a cena recuperada reproduza os pixels.

## Resultado funcional

Os oito perfis planejados passaram. Os recibos Vulkan identificam NVIDIA
`10de:2560` e AMD `1002:1638`; o callback do BGFX identifica a Radeon
Renoir/Mesa 25.2.8 e a NVIDIA RTX 3060/driver 615.71.09 em OpenGL.

| GPU/API | Peeling | Weighted OIT |
| --- | --- | --- |
| NVIDIA/Vulkan | PASS | PASS |
| AMD/Vulkan | PASS | PASS |
| AMD/OpenGL | PASS | PASS |
| NVIDIA/OpenGL | PASS com EGL surfaceless | PASS com EGL surfaceless |

No contexto GL padrão desta sessão, NVIDIA/OpenGL parou antes do primeiro
frame com `BGFX renderer lacks the required window/offscreen capabilities`;
o controle independente `CoinRenderAdvancedTextureTest --gpu` teve a mesma
falha e `glxinfo -B` informou `NV-GLX BadValue`. Com
`EGL_PLATFORM=surfaceless`, o controle passou (426 controles) e ambos os
cenários combinados passaram com recibo físico NVIDIA no callback BGFX.
Esse parâmetro é necessário para reproduzir o gate NVIDIA/OpenGL neste PC.

## Custo exploratório

`--npot-shadow-oit-bench` mede `action.apply` seguido de readback, com dez
iterações de aquecimento e mediana das 30 seguintes por condição. Compara
64×64 POT com mips nativos, 63×63 NPOT sem mips e 63×63 NPOT com mips por
área, sempre com oito sombras e a mesma estratégia de transparência. É tempo
de captura sincronizada completa, não tempo isolado do shader ou timestamp
GPU. A ordem POT→NPOT sem mips→NPOT com mips e a única rodada por perfil
limitam a precisão. A razão incremental é NPOT com mips / NPOT sem mips.

| GPU/API | Estratégia | POT mip ms | NPOT sem mip ms | NPOT mip ms | Incremental |
| --- | --- | ---: | ---: | ---: | ---: |
| NVIDIA/Vulkan | peeling | 18,91 | 19,69 | 35,04 | 1,78× |
| NVIDIA/Vulkan | weighted | 18,99 | 18,25 | 27,75 | 1,52× |
| NVIDIA/OpenGL | peeling | 20,88 | 24,42 | 31,68 | 1,30× |
| NVIDIA/OpenGL | weighted | 9,82 | 11,44 | 24,84 | 2,17× |
| AMD/Vulkan | peeling | 14,75 | 14,70 | 18,25 | 1,24× |
| AMD/Vulkan | weighted | 15,01 | 20,35 | 16,27 | 0,80× |
| AMD/OpenGL | peeling | 13,42 | 13,06 | 18,58 | 1,42× |
| AMD/OpenGL | weighted | 12,33 | 11,73 | 19,97 | 1,70× |

A razão abaixo de 1 em AMD/Vulkan weighted mostra que a captura completa
tem ruído relevante. Uma sonda dos timestamps BGFX por view não retornou
tempos válidos para os frames de redução neste build; ela foi retirada do
código. Esses números servem para dimensionar o custo observável, não para
atribuir tempo GPU exclusivamente aos mips.

Os [logs individuais](validation/bgfx-npot-shadow-oit-20261009/) preservam
recibos e medianas. O build usado fica em
`/mnt/Laranja/Git/externos/coin-portable-sampling-artifacts/20261009-npot-bgfx/build`.

## Pendências

Na campanha inicial, faltavam tempo GPU isolado dos mips e qualificação DX11
no Windows. A implementação permanece na branch de estudo.

Continuação: os [timestamps isolados dos cinco frames de mips](coin-render-bgfx-npot-mip-gpu-20261009.md)
foram medidos em oito perfis; a qualificação DX11 permanece aberta.
