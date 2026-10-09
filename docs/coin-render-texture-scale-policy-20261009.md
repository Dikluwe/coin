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

Rescaling de imagens quando NPOT não é nativo, política `quality` no resize,
`FRACTURE` com recorte real, imagens acima do limite e outros dispositivos
continuam fora do perfil. O gate de sampling projetivo anterior segue aberto
em sua própria frente, sem mudança de tolerância.
