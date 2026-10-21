# R29 — textura RGBA 2×2 e orientação do oráculo

O gate de BLEND imediato com textura executa **1×1 e 2×2** em toda
rodada de `CoinRenderStereoEyeTest`. A textura 2×2 usa RGB branco, alpha
128/64/192/96 e `SoTexture2::MODULATE`. Cada olho direto é comparado com
CoinGL; a composição ANAGLYPH deve usar exatamente R do olho esquerdo e
G/B do direito. Uma terceira rodada combina a textura 2×2 com
[transparência do material](coin-render-r29-material-texture-alpha-20261017.md).

A primeira sonda indicou falsamente erro RGB máximo 63 em três células
Vulkan. `SoOffscreenRenderer::getBuffer()` entrega linhas no sentido do
readback GL, enquanto `CoinRenderTarget::readbackRGBA()` as entrega no
sentido oposto. A cena anterior, simétrica em Y, escondia essa diferença.
Um contracontrole de uma face deslocada em Y produziu erro máximo 190 antes
da normalização e **zero** ao espelhar as linhas do buffer CoinGL. Desligar
o fast path do cubo não alterou a divergência aparente. O teste agora
normaliza a orientação antes de comparar os pixels da textura.

| Célula GPU Linux | Textura 1×1 | Textura 2×2 × CoinGL | Composição de canais |
| --- | --- | --- | --- |
| BGFX/Vulkan AMD | PASS, máximo 0 | PASS, máximo 1 | máximo 0 |
| BGFX/OpenGL AMD | PASS, máximo 0 | PASS, máximo 0 | máximo 0 |
| BGFX/Vulkan NVIDIA | PASS, máximo 0 | PASS, máximo 0 | máximo 0 |
| BGFX/OpenGL NVIDIA | PASS, máximo 0 | PASS, máximo 0 | máximo 0 |
| wgpu/Vulkan AMD | PASS, máximo 0 | PASS, máximo 1 | máximo 0 |
| wgpu/OpenGL AMD | PASS, máximo 0 | PASS, máximo 0 | máximo 0 |
| wgpu/Vulkan NVIDIA | PASS, máximo 0 | PASS, máximo 0 | máximo 0 |

Sete processos GPU completos retornaram zero. A face deslocada teve
CoinGL máximo 0 contra BGFX/Vulkan AMD depois da normalização. Texturas
maiores, filtros/mips, RTT e janela com textura 2×2 não foram qualificados
por esta sonda. `COIN_RENDER_STEREO_TEXTURE_PROBE_PLANE=1` seleciona a
face deslocada no terceiro caso; `COIN_RENDER_STEREO_PROBE_DISABLE_FAST_PATH=1`
desliga o fast path da action direta para diagnóstico.

[Resumo auditável e logs individuais](validation/coin-render-r29-texture-2x2-20261017/verification.json).
