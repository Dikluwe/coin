# R36/C26 — componentes offscreen

Continuação: [TIFF L/LA/RGB/RGBA qualificado nas sete células Linux](coin-render-r36-tiff-20261017.md), com LA armazenado como RGBA para preservar luminância e alpha. Os limites abaixo descrevem esta campanha anterior.

O `CoinRenderTarget` expõe `readbackLegacyComponents()` para L, LA, RGB e
RGBA após uma publicação síncrona RGBA8 offscreen. A saída segue a ordem de
linhas bottom-up de `SoOffscreenRenderer::getBuffer()`. L usa a mesma expressão
de CoinGL (`0.30 R + 0.59 G + 0.11 B`, truncada para byte); LA conserva o alpha.
Formato inválido ou publicação indisponível retorna falha e limpa o vetor.
`encodeLegacyRGB()` produz o cabeçalho SGI RGB de 512 bytes e os planos de
canal não comprimidos do `writeToRGB()` legado. Dimensões acima de 65535 são
recusadas para evitar truncamento dos campos de 16 bits.
`encodeLegacyPostScript()` produz o EPS/ASCII85 do legado com página e DPI
explícitos. Como no CoinGL, LA e RGBA omitem alpha no PostScript.
`writeLegacyImageFile()` encaminha o buffer de componentes ao mesmo wrapper
opcional `simage` usado por `SoOffscreenRenderer::writeToFile()`, selecionando
o codec pela extensão explícita e recusando formatos indisponíveis.

Um teste com pixels assimétricos verifica todos os bytes, a orientação, o
cabeçalho e os planos da exportação. O teste de produto exercita readback e
exportação nos quatro formatos nos sete pares GPU/API Linux
disponíveis: BGFX AMD/NVIDIA × Vulkan/OpenGL; wgpu AMD/NVIDIA Vulkan e AMD
OpenGL. Uma segunda fixture renderiza pelo `SoOffscreenRenderer`, codifica o
buffer com as rotinas novas e compara os arquivos inteiros com `writeToRGB()`
e `writeToPostScript()` do CoinGL. Passou nas GPUs AMD e NVIDIA para
L/LA/RGB/RGBA. Nesses drivers,
o limite consultado de pixels de pbuffer veio como zero; a fixture fixa o
tile em 13×9 e solicita GLX pixmap direto para criar o contexto. Os onze logs estão em
[evidências](validation/coin-render-r36-components-20261017/). A compilação,
os testes unitários e `git diff --check` passaram.

O teste de produto também salvou e decodificou PNG para L, LA, RGB e RGBA
em cada uma das sete células Linux. Os 28 roundtrips preservaram dimensões,
número de canais e bytes da leitura de compatibilidade. O suporte a outros
codecs depende dos savers instalados no `simage` e ainda não foi qualificado.

Esta é uma parcela de R36/C26. Demais codecs, opções de
contexto do renderer legado, integração dos consumidores, viewport e outros
formatos/targets permanecem abertos na matriz.
