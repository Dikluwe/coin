# Resize POT legado sob override NPOT

Continuação local de 2026-10-09 na branch `codex/coin-portable-sampling-study`.

Com `COIN_GLGLUE_DISABLE_NON_POWER_OF_TWO_TEXTURES=1`, o CoinGL redimensiona
imagens 2D NPOT para potência de dois. A captura portátil agora acompanha esse
override para texturas armazenadas RGBA8, usando a escolha de dimensões de
`SoGLImageP::resizeImage` e o mesmo mapeamento por vizinho de
`fast_image_resize` quando `SoTextureScalePolicy.quality < 0,5`. O resize
acontece antes do digest, geração de mips e compressão. Sem o override, o
perfil NPOT nativo anterior mantém os texels originais.

`USE_TEXTURE_QUALITY` respeita o limiar legado 0,7 para dimensões a partir de
256; `SCALE_DOWN` reduz para a potência inferior quando a superior excede 16;
`SCALE_UP` usa a potência superior. Um resize solicitado com qualidade de
escala ≥0,5 retorna `UNSUPPORTED` antes da publicação: o CoinGL usa simage ou
GLU nesse caminho, ainda sem equivalência portátil comprovada. Texturas RTT,
limites físicos de tamanho e `FRACTURE` permanecem fora deste recorte.

## Validação local

`CoinRenderTextureSamplingTest --scale-policy-pot-probe`, com o override NPOT,
comparou uma imagem RGBA 17×19 em `quality=0,3`: `USE_TEXTURE_QUALITY` e
`SCALE_UP` produziram 32×32; `SCALE_DOWN`, 16×16. Em todos os modos, a
comparação GPU/CoinGL teve erro máximo de 1 canal, e a recusa de qualidade de
escala 0,8 preservou o frame e recuperou ao voltar a 0,3.

| Executor | Recibo | GPU/CoinGL |
| --- | --- | --- |
| BGFX Vulkan AMD | `1002:1638` | máximo 1 |
| BGFX Vulkan NVIDIA | `10de:2560` | máximo 1 |
| BGFX OpenGL NVIDIA | vendor `10de` | máximo 1 |
| BGFX OpenGL Mesa | BGFX reportou vendor `0000`; GLX do display identificou AMD Radeon Graphics `1638` | máximo 0 |

O gate CPU completo permaneceu em 234 cenas e quatro recusas esperadas. O
teste de núcleo também cobre `17 → 16/32`, `300 → 256/512` conforme política
e qualidade, além dos texels do resize por vizinho. O build usado foi
`/mnt/Laranja/Git/externos/coin-portable-sampling-artifacts/20261009-npot-bgfx/build`.

Ainda falta reproduzir em dispositivo sem NPOT nativo, implementar o resize
de alta qualidade, limitar pelo máximo físico do adaptador e cobrir 3D.
