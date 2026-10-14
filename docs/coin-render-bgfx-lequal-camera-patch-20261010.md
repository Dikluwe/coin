# Câmera móvel BGFX/OpenGL com depth `LEQUAL`

Em 2026-10-10, na branch `codex/coin-render`, a cidade de 40 mil instâncias
voltou a usar o patch de câmera na rota BGFX/OpenGL. O plano da ação já era
reutilizado, mas o lowering BGFX recusava o patch porque `LEQUAL` usa
instâncias em coordenadas de câmera. Isso reconstruía malhas e instâncias a
cada frame, mesmo com geometria e materiais constantes.

O patch conserva a representação em coordenadas de câmera para preservar a
seleção de material nas superfícies coplanares. A cada câmera, recalcula
`model * view`, normal e posição das instâncias, e atualiza apenas o buffer
dinâmico de instâncias. Malhas, índices e batches permanecem em cache. Quando
o payload de um milhão de instâncias excede o orçamento de metadados, o cache
guarda apenas as escalas de posição e os dados de material são lidos do plano
validado para montar o próximo upload. A validação do overlay de câmera e as recusas para modelos singulares
continuam ativas. Um teste compara os bytes das instâncias e dos draws do
patch com um lowering completo após rotação da câmera em 300 ocorrências
coplanares `LEQUAL`, inclusive depois que o payload CPU é liberado.

## Medida local

Campanha X11 de 1280×960 na AMD Renoir física, BGFX/OpenGL, cidade 40 mil,
dois frames medidos após um warmup. Métrica: retorno CPU de
render/present; não é duração da GPU nem latência de exibição.

| Rota | Frame 0 | Frame 1 | RGB janela/offscreen inicial/final |
| --- | ---: | ---: | --- |
| Native, patch ativo | 16,96 ms | 17,57 ms | 0 / 0 |
| Native, reconstrução forçada | 89,21 ms | 90,57 ms | 0 / 0 |
| Portable, patch ativo | 16,66 ms | 17,04 ms | 0 / 0 |
| Um milhão, native, patch ativo | 662,43 ms | 661,43 ms | 0 / 0 |
| Um milhão, native, reconstrução | 1497,88 ms | 1508,57 ms | 0 / 0 |
| Um milhão, portable, patch ativo | 674,72 ms | 677,28 ms | 0 / 0 |

O PPM final da janela foi byte a byte igual entre patch native,
reconstrução native e patch portable: SHA-256
`7b5c511a18f2819056a66ac4a5bc90a05136ca66ab1f94d0dd5a97f0e02ec965`.
Na cidade de um milhão, a mesma comparação byte a byte passou com SHA-256
`606607a75805f314bf4446416775088533c26b9465e84ca6d5fdbc28e996ee1c`.
Os [CSV e logs](/mnt/Laranja/Git/externos/coin-render/docs/validation/bgfx-lequal-camera-20261010) guardam os recibos;
os PPMs grandes permaneceram locais em `/tmp/bgfx-lequal-*.ppm`.

`CoinBgfxCoreTest`, `CoinBgfxInstancingTest` e `CoinRenderDepthContractTest`
passaram. A seleção ampliada de 40 testes BGFX/câmera/depth terminou com
39 aprovados e um ignorado (`CoinRenderCameraReuseReferenceTest`). Os dois
controles `CoinRenderDepthDisabled_*` usam `SoOffscreenRenderer` da rota
CoinGL original como referência independente de pixels. Sem configuração
GLX, o oracle não conseguiu criar contexto e os testes pararam antes da
comparação. Neste X11, ambos passaram com:

```sh
DISPLAY=:0 XAUTHORITY=/home/dikluwe/.Xauthority \
COIN_GLX_PIXMAP_DIRECT_RENDERING=1 COIN_GLXGLUE_NO_PBUFFERS=1 \
ctest --test-dir build-coinrender-camera-bgfx -j 8 \
  -R 'CoinBgfx|CoinRender(Camera|FrameReuse|Depth)' --output-on-failure
```

O primeiro ajuste permite contexto GLX direto em pixmap; o segundo evita a
troca para pbuffer. A configuração já era usada em outras campanhas Linux
do projeto. Ela é necessária para executar o oracle neste servidor X11;
não altera o critério de comparação nem o backend testado.

O frame de um milhão ainda gasta cerca de 0,30 s para remontar o payload
de instâncias no lowering, além do custo do plano de cena; esta campanha
curta não estabelece orçamento de produção. A primeira captura intermitente
wgpu/OpenGL e a revalidação Windows continuam pendentes.
