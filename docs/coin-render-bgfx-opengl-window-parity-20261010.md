# Paridade da janela BGFX/OpenGL

As divergências BGFX/OpenGL persistiam com câmera estática: a cidade 40 mil
mostrava um pixel diferente e o terreno, 14 pixels, um deles com diferença
RGB 123. O visual X11 usado pela janela tem cor RGBA8, profundidade 24 e
stencil 8, como o framebuffer offscreen configurado pelo Coin. A diferença
vinha do caminho de renderização diretamente na swapchain OpenGL.

A janela BGFX/OpenGL agora renderiza a cena em um framebuffer RGBA8/D24S8
equivalente ao alvo offscreen e apresenta a textura por uma passagem de tela
inteira. Vulkan e as outras APIs conservam o caminho anterior. O consumidor
Linux ganhou `COIN_VISUAL_FORCE_XGETIMAGE=1` para verificar os pixels
efetivamente apresentados, além da captura interna BGFX.

## Recibos físicos

Campanha X11 em AMD Renoir/Mesa 25.2.8, 1280×960, dois frames medidos e um
warmup. Cada célula mostra o RGB máximo inicial/final entre janela e
offscreen; zero significa igualdade exata.

| Cena | OpenGL native | OpenGL portable |
| --- | --- | --- |
| Cidade 40 mil | 0/0 | 0/0 |
| Cidade um milhão | 0/0 | 0/0 |
| Terreno um milhão | 0/0 | 0/0 |
| Sólidos | 0/0 | não repetido |

XGetImage confirmou 0/0 para cidade 40 mil e terreno em OpenGL/native.
Vulkan/native na cidade 40 mil permaneceu 0/0. A bateria `ctest -R '^CoinBgfx'`
passou 32/32, cobrindo janela, múltiplas janelas, instâncias e transparência
nas duas APIs. Os CSVs, PPMs e logs locais estão em
`/mnt/Laranja/Git/externos/coin/build-coinrender-terrain-results/` com
prefixos `probe-bgfx-gl-windowfbo-`, `x11-bgfx-gl-windowfbo-` e `final-bgfx-`.

Na repetição curta da cidade 40 mil, as rotas antiga e nova mediram ambas
84–88 ms por frame. O registro anterior da rota antiga medira cerca de 7 ms;
a variação do ambiente impede concluir o custo da passagem adicional.
Falta uma campanha de desempenho controlada, além da repetição em Windows.
