# P19 — reuso e readback

P19 fecha a **campanha** de reuso/readback do perfil experimental F15. A
[matriz de 54 execuções](inventories/coin-render-p19-runs.csv) contém 18
casos, cada um com um trace e duas repetições sem trace. Foram usados a Radeon
Renoir `1002:1638` (Mesa radeonsi/RADV), builds Release, 512², 10 quadros de
aquecimento e 40 medidos. O runner guarda stdout, stderr, impressão digital
dos executáveis, SHA-256 das cenas e `glxinfo -B`. O trace é intrusivo; tempos
e throughput abaixo vêm somente das repetições sem trace. São observações
nesta máquina, não promessa de desempenho.

Na referência Coin/GL, `SoOffscreenRenderer::getBuffer()` materializa os
pixels com `glReadPixels()` após renderizar, enquanto o caminho de janela
usa `SoSceneManager::render()` seguido de `glXSwapBuffers()` sem solicitar
pixels. Os executores BGFX/wgpu preservam essa distinção do alvo, mas usam
staging e tickets próprios. A captura de câmera/material e o planejamento
continuam compartilhados em CoinRender; patches, bindings, agrupamento e
rings são mecanismos da Infra.

A cena base é a `opaque-interleaved.iv` do P17. Para testar o agrupamento de
fato, o runner insere `SoDepthBuffer { test TRUE write TRUE function LESS
range 0 1 }` no escopo externo. Esse estado explícito satisfaz a condição
de reordenação segura do BGFX. A cena P17 original não a satisfaz; nela o
interruptor de agrupamento não altera a ordem. A variante produz os mesmos
pixels nas duas configurações. No benchmark, `--material-dynamic` altera o
primeiro `SoMaterial` em cada quadro; `--dynamic` altera a câmera. Nenhuma
dessas escolhas interpreta Coin dentro da Infra.

| Comparação offscreen | Evidência de mecanismo no trace | FPS observado, sem trace |
|---|---|---:|
| BGFX câmera: patch / reconstrução | `camera_patch=1 / 0`; RGBA igual | 1156 / 800 |
| wgpu câmera: bindings persistentes / base | 36 bindings reutilizados por quadro / 0; RGBA igual | 1223 / 964 |
| BGFX material alterado | `material_patch=1`, uma faixa de 36 vértices; RGBA igual ao wgpu | 489 |
| wgpu material alterado | captura comum e submissão normal; RGBA igual ao BGFX | 551 |
| BGFX agrupado / sem agrupamento, cena segura | 4 / 36 transições lógicas de material; RGBA igual | 459–1219 / 461–466 por repetição |
| wgpu attachments persistentes / base | `attachments_reused=1 / 0`; RGBA igual | 1036 / 1071 |

Nos frames estáticos e de câmera, o BGFX registrou `resource_cache_hit=1` e
`geometry_buffer_reused=1`, com capacidade de 2048 vértices/índices; a
alteração de material manteve o buffer mas exigiu novo conteúdo
(`resource_cache_hit=0`). Os contadores `geometry_active_bytes` e de buffers
do cache wgpu ficaram zero nesta cena: eles não medem toda a geometria
transitória nem permitem concluir memória GPU nula. A persistência observada
no wgpu foi a dos bindings de câmera e attachments quando ativados.

A faixa larga do agrupamento resulta de uma repetição atípica; estes 40 quadros
não justificam atribuir ganho de throughput ao agrupamento, embora a redução
de transições seja real. O cache de attachments do wgpu também não mostrou
ganho estável nesta amostra. Os caminhos de câmera produziram ganho nesta
cena, mas continuam sujeitos à matriz de hardware P20. `COIN_WGPU_CAMERA_BINDINGS=1`
e `COIN_WGPU_ATTACHMENT_CACHE=1` seguem opt-in; BGFX mantém seus patches e
agrupamento com os controles A/B existentes. A mudança de material continua
mais cara que a de câmera: o patch BGFX evita upload de toda a geometria, mas
não elimina a captura/plano comum nem a espera de readback.

## Profundidade, latência e memória

| BGFX offscreen, profundidade | FPS observado | Idade do frame publicado no trace | Espera de leitura mediana no trace | Rings GPU + CPU |
|---:|---:|---:|---:|---:|
| 1 | 1264 | 0 quadros | 0,573 ms | 2 MiB |
| 2 | 1527 | 1–2 quadros | 0,00011 ms | 4 MiB |
| 3 | 1572 | 2 quadros | 0,00013 ms | 6 MiB |

Em cada slot BGFX, 1 MiB é staging GPU e 1 MiB é staging CPU, para 512²
RGBA8. `readback_pipeline_bytes` soma só esses rings; não representa toda a
memória GPU nem inclui o buffer publicado. A maior profundidade desloca a
espera e publica um quadro mais antigo; não se deve usar o resultado como se
fosse o quadro recém-submetido. O benchmark usou cena estática para que o
checksum RGBA fosse comparável: `0xc83ab6d423157d11` nas três
profundidades e no wgpu síncrono/assíncrono. O wgpu assíncrono com dois tickets
mediu 1343 FPS e latência de ticket mediana de 0,694 ms; seu modelo é por
ticket/serial, distinto da idade implícita do ring BGFX. Não há profundidade
3 nesse benchmark wgpu.

O teste `CoinBgfxReadbackModesTest` confirma admissão de 16 tickets e rejeição
do 17º com `NOT_READY` e token zero. O novo trecho de
`CoinRenderAsyncActionTest` confirma o mesmo limite no wgpu, verifica que os
16 tickets aceitos ainda publicam cor e depth corretos e que a recusa não
publica ticket. A política de 128 MiB e o contrato transacional permanecem os
do [P14](coin-render-multi-target-contract.md) e
[P13](coin-render-rtt-publication-contract.md).

## Janela sem readback

Os quatro casos de janela, BGFX/wgpu estático e com material alterado,
reportaram `readback=none`. O trace BGFX registrou zero em espera, pipeline e
staging GPU/CPU de readback; o wgpu registrou zero em staging de cor/depth e
pool livre. Houve attachments de apresentação, que não são readback. Assim,
renderizar para janela não cria staging nem solicita cópia CPU de pixels,
mesmo quando a cena muda. A [captura explícita de janela RGBA8](coin-render-window-readback.md) foi
fechada depois desta campanha; os números P19 acima continuam descrevendo
o caminho normal sem pedido de pixels. A
memória GPU total do driver e timestamps GPU de janela wgpu continuam
indisponíveis, como em [P18](coin-render-p18-profiling.md).

## Reprodução e limites

A coleta partiu do commit P18 `1893c00bf0`, com as alterações P19 aplicadas.
SHA-256 dos binários de benchmark offscreen: BGFX
`9d534d927f5d05f5dc1ad66cf716373aca1ba9f071afbd517525d5640d1ed3b6`,
wgpu `d1e556f45382a8b7a6c36639e508a852948d4cc3f74ff6ff7eda6cb7eedf09d1`.
SHA-256 da cena base:
`32854a58541b03ad07bbf03f8cace6e8b394b456d83d6e9be0e20e6d5473c072`;
variante agrupável:
`d3e9902b7c78bd6a03fd829b6f4f222c62c2ecfb98d57a1e53d67e6568085ec1`.
O CSV guarda cada repetição e o trace agregado por caso. Dados brutos locais
ficaram em `/tmp/coin-p19-final`.

```sh
python3 scripts/coinrender/generate_p17_scenes.py --output-dir /tmp/coin-p17-scenes
env __GLX_VENDOR_LIBRARY_NAME=mesa \
  __EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/50_mesa.json \
  VK_DRIVER_FILES=/usr/share/vulkan/icd.d/radeon_icd.json \
  python3 testsuite/qt-quarter/run_isolated.py \
  --server xwayland --weston-prefix /tmp/coin-p16-weston \
  --artifacts /tmp/coin-p19-session --exec -- \
  python3 scripts/coinrender/run_p19_reuse.py \
  --output-dir /tmp/coin-p19-output \
  --bgfx-build "$PWD/build-bgfx-recovery/coin-build" \
  --wgpu-build /tmp/coin-p17-wgpu-release \
  --scene /tmp/coin-p17-scenes/opaque-interleaved.iv \
  --warmup 10 --frames 40 --repetitions 2
```

Os testes focados `CoinBgfxCoreTest`, `CoinBgfxReadbackModesTest` e
`CoinRenderAsyncActionTest` passaram na Radeon. O fingerprint FNV-1a verifica
identidade exata dentro dos A/B e entre BGFX/wgpu para esta cena; não substitui
oráculos visuais gerais nem a matriz física P20. A variabilidade do tempo de
janela sem vsync e do agrupamento exige campanhas mais longas antes de decisões
globais de default.
