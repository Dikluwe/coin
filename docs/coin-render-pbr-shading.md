# Shading PBR opaco no CoinRender

O perfil M2 executa GGX por fragmento em BGFX/Vulkan, acumula radiância linear
em RGBA16F e produz uma imagem SDR com exposição, Khronos PBR Neutral e uma
codificação sRGB. `SoPBRMaterial` continua dedicado à superfície. O exemplo
`coin_render_viewer` usa os mesmos fixtures, câmeras e bibliotecas Coin de M0.

## BRDF e coordenadas

O modelo usa GGX isotrópico, Smith correlacionado e Fresnel de Schlick, com
IOR dielétrico 1,5 (`F0=0.04`). Para rugosidade perceptual `r`, a avaliação usa
`alpha=max(r,0.045)^2`; o fator autorado permanece intacto no nó e no FramePlan.
O especular é `D*V*F`, onde `V=G/(4 NoV NoL)`. O difuso é
`(1-metallic)*(1-F)*baseColor/pi`. A soma é multiplicada por `NoL` e pela
radiância incidente. [Referência das funções GGX e Smith](https://google.github.io/filament/main/filament.html).

Os atributos existentes transportam posição e normal em coordenadas de vista.
O vertex shader PBR não aplica iluminação. O fragment shader normaliza a
normal interpolada e avalia a BRDF; a direção até o observador depende da
posição em perspectiva e é `(0,0,1)` em projeção ortográfica. O lowering usa
`SbMatrix` e a transformação de normais existente por inversa transposta.
Escalas não uniformes e reflexões foram comparadas com uma referência
independente. Matrizes singulares ou não afins e normais nulas são recusadas.

A iluminação direta aceita as luzes direcionais, pontuais e spot capturadas
pelo Coin. Intensidade e RGB conservam a convenção normalizada existente; a
atenuação usa os coeficientes quadrático, linear e constante da captura.
Esse perfil não define lumens, lux ou uma escala fotométrica do mundo. O termo
ambiente legado não é convertido implicitamente em IBL.

## HDR e transformação de saída

A geometria escreve cor linear RGBA16F e profundidade em um framebuffer privado
do target. A emissão e a iluminação permanecem acima de 1. A saturação numérica
ocorre somente em 65504, limite finito de binary16. O passe de saída recebe
`2^pbrExposureStops * radiance`, aplica Neutral e codifica sRGB. O framebuffer
SDR não usa uma segunda conversão sRGB. [Especificação Khronos PBR Neutral](https://github.com/KhronosGroup/ToneMapping/tree/main/PBR_Neutral).

`CoinRenderOptions::pbrExposureStops` é imutável por target, tem default zero e
aceita valores finitos de -16 a +16. Cenas inteiramente legadas preservam seu
pipeline anterior. A cor de fundo do target é interpretada como radiância
linear no caminho PBR e participa da mesma transformação de saída.

O readback público continua RGBA8 após a transformação. Um hook interno de
validação lê radiância diretamente do alvo float, usando `SbList<SbVec3f>` e a
conversão binary16 existente em CoinRender. A profundidade pública é extraída
do framebuffer da geometria, preservando o contrato de clipping e picking.
Janela e offscreen usam o mesmo passe de saída e têm hashes de pixels iguais
nos três fixtures CAD verificados.

Os recursos HDR são alocados quando o primeiro frame PBR é admitido, pertencem
ao target e são destruídos/recriados com seus attachments em resize. Shaders
e uniforms são preparados uma vez. View IDs permanecem dentro do bloco já
reservado ao target. Instancing, overlays de câmera e agrupamento de geometria
PBR permanecem desativados neste perfil; o reaproveitamento de um frame estático
continua ativo.

## Perfil admitido

M2 aceita faces PBR opacas, fatores OVERALL, luzes diretas, emissão, clipping e
saída síncrona em janela/offscreen. A capacidade PBR é anunciada somente pelo
executor Vulkan com suporte de framebuffer RGBA16F. A campanha usa Linux,
AMD RADV RENOIR e a configuração de M0; isso não qualifica outras APIs/drivers.

Texturas, fog, grupos de sombra, strokes PBR, mistura de superfícies legadas,
overlays, readback assíncrono e produtores de scene texture PBR recebem
`UNSUPPORTED`. Recording e Rust reconhecem os dados e recusam shading PBR.
Uma rejeição conserva a publicação anterior. A conversão de materiais de
arquivos CAD importados não faz parte do viewer M2; arquivos importados ainda
servem como controle legado.

O modelo M2 representa uma interação especular. A integração de white furnace
da referência numérica conserva a perda esperada de metais rugosos; ela não é
uma imagem GPU sob IBL. A compensação coerente de múltiplo espalhamento e a
filtragem especular pertencem aos próximos marcos. As views M2 usam somente
luz direta, por isso metais fora do highlight aparecem escuros.

## Validação

`CoinRenderPbrGpuTest` compara radiância GPU com um oracle em dupla precisão
que usa Smith Lambda e o denominador explícito de Cook–Torrance. Cobre 20
combinações de câmera/metallic/roughness, três posições interiores por draw,
highlight no piso de rugosidade, escala/reflexão, luzes pontuais/spot, emissão
HDR, profundidade, exposição e transformação de saída. A tolerância principal
é `0.003*max(1,expected)`, compatível com o armazenamento binary16. Os testes de
energia e reciprocidade qualificam a referência; a comparação GPU cobre a
avaliação direta do shader.

A campanha do viewer tem 24 casos positivos e três rejeições: três fixtures
por seis câmeras, três controles de janela, grade de materiais, corte e
exposição +2 stops. Cada caso positivo compara primeiro frame e replay.
Não usa cor de fundo ou limiar de brilho como máscara de geometria PBR.
Os controles de cobertura/clipping usam profundidade no teste GPU.

Comandos, logs, imagens e resultados estão em `docs/validation/pbr/m2/`.
A matriz de 72 views com dois ambientes continua reservada para M4.

```sh
coin_render_viewer --fixture assembly --view iso-perspective --pbr \
  --exposure 0 --offscreen --frames 2 --capture-prefix /tmp/assembly-pbr
```
