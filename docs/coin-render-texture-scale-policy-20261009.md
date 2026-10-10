# SoTextureScalePolicy: perfil NPOT pequeno e FRACTURE

Continuação local de 2026-10-09 na branch `codex/coin-portable-sampling-study`.
O código CoinGL em `SoGLImage::createGLDisplayList` só chama `resizeImage` para
2D NPOT quando o contexto não anuncia suporte NPOT, ou quando pede mips sem
geração compatível. `SCALE_DOWN` e `SCALE_UP` não forçam um resize nesse caminho
quando o hardware aceita o tamanho original. `FRACTURE` escolhe `SoGLBigImage`
e recorta a geometria em subtexturas; ignorá-la não é equivalente.

## Contrato implementado nesta rodada

- `SoTextureScalePolicy` agora atualiza os elementos de política e qualidade
  durante `SoCallbackAction`, com o mesmo escopo de `Separator` usado por
  CoinGL. Antes a captura portátil não via esse estado.
- `USE_TEXTURE_QUALITY`, `SCALE_DOWN` e `SCALE_UP` conservam os texels e o
  tamanho 3×5 no perfil NPOT nativo comprovado neste PC. Isso não implementa
  rescaling quando a API/driver não oferece NPOT.
- `FRACTURE` com textura ativa retorna `UNSUPPORTED` na captura, antes de
  submeter qualquer frame. Pixels e serial previamente publicados permanecem;
  voltar à política padrão recupera a cena. Com textura inativa, a política
  não causa recusa. Um `Separator` contendo `FRACTURE` não afeta formas após
  o fim do seu escopo.

## Evidências

A sonda `CoinRenderTextureSamplingTest --scale-policy-probe` passou em quatro
processos: AMD/NVIDIA × Vulkan/OpenGL. Em cada processo, as três políticas
usuais foram comparadas em `quality=0,3` e `0,8`, com imagem RGBA 3×5 de
contraste, contra CPU e CoinGL. Todas as 6 células por processo tiveram
erro máximo CPU/GPU ≤2 e GPU/CoinGL ≤1; o snapshot manteve 3×5. Os recibos
Vulkan identificam AMD `1002:1638` e NVIDIA `10de:2560`; callbacks BGFX
identificam Radeon Renoir/Mesa 25.2.8 e RTX 3060/driver 615.71.09 em OpenGL.
NVIDIA/OpenGL usou `EGL_PLATFORM=surfaceless`; CoinGL usou GLX/Mesa como
oráculo separado.

A sonda `--texture-quality-cache-probe` ampliou o perfil NPOT nativo para a
qualidade armazenada de `SoTexture2`: primeiro upload em 0,3, mudança para
0,1 sem notificar a imagem e reupload após mudança de wrap. Antes da
correção, AMD/Vulkan tinha erro GPU/CoinGL máximo 7 no segundo contexto;
depois, passou em AMD/NVIDIA Vulkan e NVIDIA/Mesa OpenGL, com máximo 1.
Um controle sem CoinGL confirma que duas capturas portáteis do mesmo nó
conservam o filtro do primeiro upload. O nó associa essa qualidade à revisão
da imagem, sem cache global de ponteiros. Uma qualidade inválida no primeiro
uso é recusada antes de entrar nesse estado; corrigir o campo recupera a cena.

Antes da recusa, a sonda bruta de `FRACTURE` no mesmo 3×5 mediu diferença
máxima RGB 181 entre CPU/CoinGL e 182 entre BGFX/CoinGL. Esse processo de
diagnóstico retornou 1, como esperado. Os quatro processos finais passaram
também rejeição, preservação, recuperação, textura inativa e escopo.

O gate CPU completo `CoinRenderTextureSamplingTest` passou (`234` cenas,
quatro rejeições). Os gates GPU completos `--gpu` native/portable em
AMD/Vulkan e NVIDIA/OpenGL ainda retornam 1 no controle conhecido de
footprint projetivo em `quality=0,5`, sem `SoTextureScalePolicy`: a diferença
CPU/CoinGL é MAE 8,08/máximo 78 no mesmo reproducer registrado no
[estudo anterior](coin-render-native-sampling-path-study-20261008.md). Eles
não são contados como PASS desta rodada; a sonda isolada teve PASS 4/4.

Os [logs da sonda, controle CPU, FAIL projetivo e contraste FRACTURE](validation/texture-scale-policy-20261009/)
preservam métricas e recibos. A matriz foi executada no build BGFX separado
`/mnt/Laranja/Git/externos/coin-portable-sampling-artifacts/20261009-npot-bgfx/build`.

## Ainda aberto

O [resize POT sob override NPOT](coin-render-texture-pot-resize-20261009.md)
cobre os casos Linux com simage. O fallback GLU, `FRACTURE` com recorte real,
imagens acima do limite e outros dispositivos continuam fora do perfil.
O gate de sampling projetivo anterior segue aberto
no Mesa instalado, sem mudança de tolerância. Com o Mesa privado corrigido,
o [gate GPU completo](coin-render-full-gpu-sampling-gate-20261009.md) passou
8/8 processos; os FAIL deste ledger permanecem como controle do driver instalado.
