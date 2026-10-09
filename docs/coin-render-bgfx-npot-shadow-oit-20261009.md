# BGFX NPOT com oito sombras e transparência

Campanha local de 2026-10-09 na branch `codex/coin-portable-sampling-study`.
O novo fixture `CoinRenderShadowReferenceTest --npot-shadow-oit 1|2` cria um
produtor RTT direto 63×47 com oito mapas de sombra, transparência por peeling
(1) ou weighted OIT (2), e consumidor que pede mips. A cadeia 63×47 gera cinco
níveis adicionais por área em frames BGFX próprios. O controle exige que
sombras e transparência alterem pixels, que o nono mapa seja recusado sem
alterar serial/pixels publicados e que a cena recuperada reproduza os pixels.

## Resultado funcional

Seis dos oito perfis planejados passaram. Os recibos Vulkan identificam
NVIDIA `10de:2560` e AMD `1002:1638`; o callback do BGFX identifica a Radeon
Renoir/Mesa 25.2.8 nos perfis OpenGL AMD.

| GPU/API | Peeling | Weighted OIT |
| --- | --- | --- |
| NVIDIA/Vulkan | PASS | PASS |
| AMD/Vulkan | PASS | PASS |
| AMD/OpenGL | PASS | PASS |
| NVIDIA/OpenGL | bloqueado na admissão BGFX | bloqueado na admissão BGFX |

Os dois perfis NVIDIA/OpenGL param antes do primeiro frame com `BGFX renderer
lacks the required window/offscreen capabilities`. O controle independente
`CoinRenderAdvancedTextureTest --gpu`, que havia passado na campanha NPOT
anterior, apresentou a mesma falha neste momento. `glxinfo -B` com o fornecedor
NVIDIA falhou com `NV-GLX BadValue`. Portanto os dois resultados não demonstram
falha da redução NPOT, mas também não a qualificam neste cenário combinado.

## Custo exploratório

`--npot-shadow-oit-bench` mede `action.apply` seguido de readback, com dez
iterações de aquecimento e mediana das 30 seguintes. Compara 64×64 POT nativo
e 63×63 NPOT por área, sempre com oito sombras e a mesma estratégia de
transparência. É tempo de captura sincronizada completa, não tempo isolado do
shader ou timestamp GPU. A ordem POT→NPOT e a única rodada por perfil limitam
a precisão da razão.

| GPU/API | Estratégia | POT ms | NPOT ms | NPOT/POT |
| --- | --- | ---: | ---: | ---: |
| NVIDIA/Vulkan | peeling | 21,68 | 29,11 | 1,34× |
| NVIDIA/Vulkan | weighted | 19,11 | 28,23 | 1,48× |
| AMD/Vulkan | peeling | 16,16 | 17,25 | 1,07× |
| AMD/Vulkan | weighted | 16,66 | 17,75 | 1,07× |
| AMD/OpenGL | peeling | 13,82 | 22,08 | 1,60× |
| AMD/OpenGL | weighted | 11,46 | 16,10 | 1,41× |

Os [logs individuais](validation/bgfx-npot-shadow-oit-20261009/) preservam
recibos, medianas e os dois erros de admissão. O build usado fica em
`/mnt/Laranja/Git/externos/coin-portable-sampling-artifacts/20261009-npot-bgfx/build`.

## Pendências

Repetir NVIDIA/OpenGL quando o contexto do driver voltar a admitir RTT BGFX;
medir tempo GPU de mips isoladamente se for preciso decidir sobre desempenho;
qualificar DX11 no Windows. A implementação permanece na branch de estudo.
