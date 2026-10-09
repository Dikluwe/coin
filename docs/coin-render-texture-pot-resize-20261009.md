# Resize POT legado sob override NPOT

Continuação local de 2026-10-09 na branch `codex/coin-portable-sampling-study`.

Com `COIN_GLGLUE_DISABLE_NON_POWER_OF_TWO_TEXTURES=1`, o CoinGL redimensiona
imagens 2D NPOT para potência de dois. A captura portátil agora acompanha esse
override para texturas armazenadas RGBA8, usando a escolha de dimensões de
`SoGLImageP::resizeImage`: o mapeamento por vizinho de `fast_image_resize`
quando `SoTextureScalePolicy.quality < 0,5`, e a mesma `simage_resize` usada
pelo CoinGL quando a qualidade é ≥0,5 e simage ≥1.1.1 está disponível. O
resize acontece antes do digest, geração de mips e compressão. Sem o override,
o perfil NPOT nativo anterior mantém os texels originais.

`USE_TEXTURE_QUALITY` respeita o limiar legado 0,7 para dimensões a partir de
256; `SCALE_DOWN` reduz para a potência inferior quando a superior excede 16;
`SCALE_UP` usa a potência superior. Se simage não estiver disponível para um
resize de alta qualidade, a captura retorna `UNSUPPORTED` antes da publicação;
o fallback GLU do CoinGL ainda não tem equivalência portátil. Texturas RTT,
limites físicos de tamanho e `FRACTURE` permanecem fora deste recorte.

## Validação local

`CoinRenderTextureSamplingTest --scale-policy-pot-probe`, com o override NPOT,
comparou imagens 17×19 de 1, 2, 3 e 4 componentes em `textureQuality=0,3`,
com qualidades de escala 0,3 e 0,8: `USE_TEXTURE_QUALITY` e `SCALE_UP`
produziram 32×32; `SCALE_DOWN`, 16×16. Foram 24 comparações por renderizador
(3 políticas × 2 qualidades × 4 formatos). Em todas, a comparação GPU/CoinGL
teve erro máximo de 1 canal. Cada combinação foi renderizada com uma textura
nova. Ao alternar apenas `SoTextureScalePolicy.quality` sobre a mesma textura,
o CoinGL preserva o upload anterior. A captura portátil agora guarda o
primeiro resize por revisão do nó de textura e o reutiliza entre frames.
Uma notificação da imagem muda essa revisão e produz novo upload. O cache
retém até 128 MiB por ação; ao atingir o limite, a captura recusa outra
entrada antes de publicar o frame.

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

A sonda `--scale-policy-pot-cache-probe` passou nos quatro executores acima:
primeiro upload 32×32 com `SCALE_UP`/qualidade 0,3; mudança para
`SCALE_DOWN`/qualidade 0,8 preservando os texels 32×32; notificação da imagem
gerando novo upload 16×16. O erro GPU/CoinGL foi ≤1 canal em cada etapa.
Mudanças em outros campos do nó também alteram seu ID e podem invalidar a
captura mais cedo que o cache do CoinGL; revisões antigas ficam no orçamento
de 128 MiB até a ação ser destruída. Esses casos, e a troca de contexto,
continuam sem contrato completo.

Ainda falta reproduzir em dispositivo sem NPOT nativo, tratar o fallback GLU,
limitar pelo máximo físico do adaptador, cobrir 3D e qualificar a mudança de
qualidade em outros contextos e após alterações de campos não relacionados à
imagem.
