# Terreno de um milhão de triângulos: limite do buffer wgpu

Continuação de 2026-10-10 na branch `codex/coin-render`. A campanha visual
[Linux](validation/linux-visual-20261010/README.md) e a
[Windows](coin-render-windows-visual-scenes-20261010.md) terminavam antes da
captura do terreno com `Coin Uncached Vertex Buffer` inválido (status 7). O
trace físico wgpu/Vulkan neste Linux mediu 492.000.000 bytes de vértices e
12.000.000 bytes de índices no frame, contra `max_buffer_size=268435456` bytes
do dispositivo. O frame contém 20 draws. O buffer único de vértices excedia o
limite negociado com wgpu.

A ponte Rust agora mantém o upload compartilhado dos frames menores e divide
somente o frame uncached que ultrapassa o limite. Cada draw recebe vértices e
índices locais; os índices são rebased antes do upload. Se um draw isolado
exceder `max_buffer_size` ou referenciar vértices fora de seu intervalo local,
a ponte retorna `Unsupported` com diagnóstico, antes de criar um buffer inválido.
O trace `COIN_RENDER_TRACE_PHASES=1` registra o tamanho e a quantidade de draws
quando essa rota é selecionada.

## Verificação física

SDK wgpu/CoinRender e consumidor instalado foram reconstruídos em prefixos
separados a partir desta branch. X11 `:0`; wgpu/Vulkan usou RTX 3060 Laptop,
wgpu/OpenGL usou AMD Renoir. Cada processo executou a mesma cena 1280×960,
duas políticas, dois frames e um warmup da campanha original.

| API/política | Erro de buffer | RGB inicial janela/offscreen | RGB final | Pixels finais diferentes |
| --- | --- | ---: | ---: | ---: |
| Vulkan/native | não | 0 | 1 | 8 |
| Vulkan/portable | não | 0 | 1 | 8 |
| OpenGL/native | não | 0 | 1 | 6 |
| OpenGL/portable | não | 0 | 1 | 6 |

O controle `solids` wgpu/Vulkan/native continuou com RGB 0 inicial/final. A
ponte passou `cargo check --locked --offline` e 42/42 testes Rust com
`--test-threads=1`. A primeira execução paralela dos testes terminou em
`SIGSEGV` após 36 passes; essa falha não foi atribuída à alteração.

Os PPMs e logs desta continuação estão no diretório local ignorado
`/mnt/Laranja/Git/externos/coin/build-coinrender-terrain-results/`; o SDK e o
consumidor ficam em `build-coinrender-terrain-sdk/` e
`build-coinrender-terrain-consumer/`. O trace
`terrain-trace2.log` contém os bytes e o limite acima. A igualdade RGB estrita
do terreno **continua aberta**: os pixels finais diferentes não foram
normalizados nem classificados como passe. A correção também precisa de
revalidação Windows antes de atualizar a célula física daquela plataforma.

## Próximo diagnóstico visual

Na campanha anterior, 35 dos 38 pixels finais divergentes da cidade de 40 mil
em BGFX/Vulkan coincidem em posição com os 42 de wgpu/Vulkan. Em OpenGL,
7 dos 8 pixels BGFX coincidem com os 24 de wgpu. No milhão, a interseção foi
55 pixels em Vulkan e 55 em OpenGL. Essa sobreposição entre backends sugere
investigar primeiro o plano de cena, a geometria e as regras de cobertura
compartilhadas; não demonstra ainda uma causa comum. O SDK atual repetiu a
cidade 40 mil wgpu/Vulkan/native com 42 pixels diferentes e RGB máximo 136.

O controle estático wgpu/OpenGL/native de height 140 passou 3/3 repetições
nesta rodada (RGB 0 inicial e final). A primeira captura preta da campanha
anterior permanece intermitente e não foi declarada corrigida.
