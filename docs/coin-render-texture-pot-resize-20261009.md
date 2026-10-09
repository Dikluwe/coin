# Resize POT legado sob override NPOT

Continuação local de 2026-10-09 na branch `codex/coin-portable-sampling-study`.

Com `COIN_GLGLUE_DISABLE_NON_POWER_OF_TWO_TEXTURES=1`, o CoinGL redimensiona
imagens 2D NPOT para potência de dois. A captura portátil agora acompanha esse
override para texturas armazenadas RGBA8, usando a escolha de dimensões de
`SoGLImageP::resizeImage`: o mapeamento por vizinho de `fast_image_resize`
quando `SoTextureScalePolicy.quality < 0,5`, e a mesma `simage_resize` usada
pelo CoinGL quando a qualidade é ≥0,5 e simage ≥1.1.1 está disponível. Sem
simage, a captura reproduz em CPU a interpolação linear com convolução por
caixa de um pixel do GLU quando ele está disponível; sem GLU, usa o mesmo
fallback por vizinho do CoinGL. O
resize acontece antes do digest, geração de mips e compressão. Sem o override,
o perfil NPOT nativo anterior mantém os texels originais.

`USE_TEXTURE_QUALITY` respeita o limiar legado 0,7 para dimensões a partir de
256; `SCALE_DOWN` reduz para a potência inferior quando a superior excede 16;
`SCALE_UP` usa a potência superior. O caminho GLU da captura não chama
`gluScaleImage`: reamostra bytes RGBA8 em CPU, com a mesma área e extensão
periódica nas bordas observadas no GLU 1.3 deste Linux. A seleção segue a
disponibilidade da biblioteca dinâmica, como em `SoGLImageP::resizeImage`.
Texturas RTT,
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
primeiro resize por revisão de upload de `SoTexture2` e o reutiliza entre frames.
Notificações da imagem ou do wrap mudam essa revisão e produzem novo upload;
uma mudança de `model` preserva os texels. O cache
retém até 128 MiB por ação; ao atingir o limite, a captura recusa outra
entrada antes de publicar o frame. A versão anterior do mesmo nó é removida
quando a revisão muda.

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
primeiro upload 32×32 com `SCALE_UP`/qualidade 0,3; habilitar
`enableCompressedTexture` sem notificação manteve RGBA8, como no CoinGL;
mudança de `model`
preservando os texels; mudança para `SCALE_DOWN`/qualidade 0,8 ainda usando
32×32; notificação de wrap gerando 16×16; alteração dos pixels seguida de
notificação da imagem gerando novo upload. O erro GPU/CoinGL
foi ≤1 canal em cada etapa. A sonda
`--scale-policy-pot-compression-probe` começou com BC3, desligou a dica de
compressão e notificou o wrap. O formato permaneceu BC3 nas três etapas,
com erro GPU/CoinGL zero nos quatro executores. A escolha de compressão do
`SoGLImage` persiste até mesmo após um novo upload da imagem no mesmo nó.

A sonda `--scale-policy-pot-context-probe` antes reproduzia erro máximo 11
em AMD/Vulkan: um renderizador novo usava a mesma `SoTexture2` após o
primeiro upload 32×32 em `SCALE_UP`, com política mudada para `SCALE_DOWN`.
O CoinGL preserva a validade e as flags do `SoGLImage` no nó, enquanto cria
uma textura GL por contexto. A captura agora lê esses hints no nó: enquanto
o upload é válido, a política nova não muda o resize; após notificação de
wrap, `SCALE_DOWN` entra em vigor; em outros contextos, essa flag e a escolha
BC3 continuam persistentes. A captura também usa a `textureQuality` guardada
no `SoGLImage` para filtro e extensão POT até que a imagem seja notificada.
A sonda cobre mudanças 0,3→0,1→0,8 sem notificação, reupload em 0,8 e o
limiar 300×300→256×256/512×512. Passou em AMD/NVIDIA Vulkan e NVIDIA/Mesa
OpenGL, com erro GPU/CoinGL máximo 2 para escala/qualidade e 0 para a
imagem uniforme comprimida.
O gate CPU completo continuou em 234 cenas e quatro recusas esperadas. O
gate GPU AMD geral ainda retorna a divergência projetiva conhecida em
`textureQuality=0,5`; as sondas POT desta mudança passaram.

### Fallback sem simage

O helper versionado
`testsuite/coinrender/CoinRenderHideSimageForPotProbe.c` intercepta o
carregamento dinâmico de simage apenas no processo de teste. Compilação:

```sh
cc -shared -fPIC -o /tmp/coin-hide-simage.so \
  testsuite/coinrender/CoinRenderHideSimageForPotProbe.c -ldl
```

Com `LD_PRELOAD=/tmp/coin-hide-simage.so`,
`COIN_GLGLUE_DISABLE_NON_POWER_OF_TWO_TEXTURES=1` e a sonda
`--scale-policy-pot-probe`, os quatro perfis Linux locais passaram, com
24 combinações cada (17×19, 1–4 componentes, três políticas, qualidade de
escala 0,3/0,8). O log `COIN_DEBUG_SIMAGE=1 COIN_DEBUG_GLU_INFO=1` confirmou
falha no carregamento de simage e `libGLU.so` 1.3 carregada. Máximo
GPU/CoinGL: 2 canais em AMD/NVIDIA Vulkan, NVIDIA OpenGL e Mesa OpenGL
privado. O teste CPU completo passou com 234 cenas e quatro recusas
esperadas; o núcleo verifica interpolação na borda periódica e falha
atômica por dimensão inválida. Com `COIN_TEST_HIDE_GLU=1` adicional, o
fallback por vizinho passou em AMD/Vulkan nas mesmas 24 combinações,
GPU/CoinGL máximo 1. A validação cobre GLU 1.3 neste Linux; outras
implementações de GLU e plataformas ainda requerem execução própria.

O gate `CoinRenderTextureSamplingGluFallbackTest` força ausência de simage
na configuração (`SIMAGE_RUNTIME_LINKING=OFF` e
`CMAKE_DISABLE_FIND_PACKAGE_simage=TRUE`) e recusa a execução se GLU não
estiver disponível. Com backend `RECORDING`, as comparações CPU/CoinGL
não exigem BGFX. Neste Linux, `xvfb-run` passou os testes de captura e
fallback GLU (2/2). A matriz em
`.github/workflows/coin-glu-pot-fallback.yml` executa esse gate em
Ubuntu, Windows e macOS quando a branch de estudo recebe um push.
Na primeira execução [#37962743507](https://github.com/Dikluwe/coin/actions/runs/37962743507),
Ubuntu passou. O runner macOS 14 Apple Silicon compilou e passou a captura
CPU, mas não criou o contexto CoinGL: CGL recusou os formatos de pixel
offscreen antes de qualquer resize. A matriz passou a manter nesse runner
o teste CPU e tentar o oráculo GLU em macOS 15 Intel. Windows compilou Coin,
mas a DLL CoinRender encontrou quatro símbolos internos de Coin não
exportados no link; a matriz passou a usar bibliotecas estáticas nesse
perfil para testar o algoritmo sem alterar a ABI compartilhada.

Na segunda execução [#37963579955](https://github.com/Dikluwe/coin/actions/runs/37963579955),
Ubuntu e a captura CPU macOS 14 passaram. O Mac Intel também compilou e
passou a captura CPU, mas seu CoinGL encontrou a mesma recusa CGL de formato
offscreen. O Windows compilou com bibliotecas estáticas, porém a captura
CPU excedeu 120 segundos; o teste seguinte permaneceu em execução e a
campanha foi cancelada. Para distinguir GLU de CoinGL e do runtime Coin,
`CoinRenderGluNativeOracle` cria um contexto OpenGL mínimo do sistema e
compara uma borda reamostrada pelo GLU nativo com o resultado CPU esperado.
O workflow executa esse oráculo em Windows e nos dois macOS, conservando a
sonda CoinGL macOS como diagnóstico não bloqueante.

Ainda falta reproduzir em dispositivo sem NPOT nativo,
limitar pelo máximo físico do adaptador e cobrir 3D. A sonda de qualidade
entre contextos cobre o caminho POT do override legado; outros caminhos de
textura permanecem nos seus contratos próprios.
