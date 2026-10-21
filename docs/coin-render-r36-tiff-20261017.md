# R36/C26 — TIFF e política de componentes

O caminho offscreen de `writeLegacyImageFile()` foi exercitado com TIFF nas
sete células GPU Linux disponíveis: BGFX AMD/NVIDIA × Vulkan/OpenGL e wgpu
AMD/NVIDIA Vulkan mais AMD OpenGL. Os layouts L, LA, RGB e RGBA preservaram
dimensões e todos os valores de cor/alpha após salvar e decodificar com
`simage`: **28/28 roundtrips TIFF**. O controle PNG L/LA/RGB/RGBA passou
**28/28** na mesma campanha. As aliases TIFF LA `tif` e `TIFF` também passaram
14/14. Cada processo executou os demais gates de `CoinRenderProductTest`,
inclusive os controles CoinGL habilitados.

O codec `simage` instalado anuncia TIFF e aceita LA (luminância + alpha),
mas gravar diretamente os dois canais corrompe a interpretação: num frame
uniforme de 8×8, o segundo pixel decodificado foi 255 em vez de 92.
`writeLegacyImageFile()` converte LA para RGBA (`L,L,L,A`) antes de chamar o
writer TIFF. O arquivo tem quatro canais; luminância e alpha são preservados.
O teste usa alpha de fundo 0,35, de modo que uma implementação que apenas
substituísse alpha por 1 não passaria. PNG LA continua com dois canais.

[Logs, identidade de adaptadores e hashes](validation/coin-render-r36-tiff-20261017/verification.json).
O recibo cobre RGBA8 offscreen síncrono nos codecs e drivers instalados;
integração de consumidores, outros codecs, formatos, janelas e plataformas
seguem na matriz.
