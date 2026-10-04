# Protocolo de benchmark de animação

Este protocolo compara as variantes com o **OpenGL clássico do Coin3D (CoinGL)**,
seguindo o [padrão de comparação](coin-render-benchmark-standard.md). Esta página
define reprodução e interpretação; resultados medidos devem registrar sua campanha.

## Cena, atualização e seleção

Os executáveis [offscreen](../examples/coinrender/coin_render_gl_benchmark.cpp) e
[janela](../examples/coinrender/coin_render_window_benchmark.cpp) compartilham o
[helper de animação](../examples/coinrender/CoinRenderBenchmarkAnimation.h).
O fluxo novo é opt-in por `--animation`: na janela, cenas importadas recebem a
mesma câmera perspectiva do offscreen, orientação `(-.5,-.35,-1)` e `viewAll(1.15)`.
Sem essa opção, a janela conserva a câmera ortográfica e as fórmulas legadas de
`--dynamic`/`--material-dynamic`; essas flags não podem acompanhar `--animation`.

| Modo | Alteração determinística por quadro |
|---|---|
| `static` | Nenhuma mutação; permite medir reuso de planos e recursos. |
| `camera` | Pan/dolly da posição da câmera relativo ao enquadramento inicial. |
| `transforms` | Posição dos prédios selecionados. |
| `materials` | Cor difusa dos materiais selecionados. |
| `geometry` | Dimensões dos cubos selecionados. |

`--animated-percent 1..100` seleciona ocorrências nos três modos de objetos.
O perfil reconhecido exige Separators de prédios com Material, Translation ou
Transform e Cube locais; solo e materiais globais ficam fora da seleção.
São escolhidos `ceil(elegíveis × percentual / 100)` prédios em intervalos
distribuídos pela ordem da cena, com a mesma seleção em todos os backends.
Materiais e cubos compartilhados recebem clones por ocorrência selecionada na
preparação, fora dos timers de quadro. Esse custo consta da preparação desde
`main`; `eligible`, `selected`, `prepared_clones` e o digest de seleção o documentam.
Static e camera não selecionam objetos; os modos de objetos rejeitam cenas sem
esse perfil, incluindo os quads transparentes padrão da janela.

`--animation-step N` exige inteiro positivo, padrão 1. Cada execução aplica
`update(i × N)` e usa `t = i × N / 60`, sem depender da velocidade da máquina.
Warmup usa índices `i=-warmup..-1`; os medidos são `0..frames-1`.
O CSV preserva `frame_index`, `logical_frame=i×N` e `warmup`; a janela também
registra `t_seconds`. Use **step 1** na campanha de desempenho.

## Escopos e estatísticas

`--samples-output arquivo.csv` conserva todos os quadros, inclusive warmup.
A escrita CSV ocorre após a medição. As estatísticas novas excluem warmup:
mediana é a média dos dois valores centrais quando a contagem é par; p95/p99
usam nearest rank, índice `ceil(p × n)-1` na lista ordenada. Registre também
máximo e contagens estritamente acima de `1000/60` e `1000/30` ms, sem arredondar
os limites para 16,67 ou 33,33 antes de comparar.

- **Janela:** `update_ms` inclui setters; `render_present_ms` inclui a chamada
  render/present; `total_ms` começa antes da primeira mutação. `event_ms` mede
  `consumeEvents` separadamente e fica fora desse total por quadro. O throughput
  usa o loop completo desde a primeira mutação medida, incluindo eventos entre
  quadros. A campanha não usa `--capture-window` e não lê pixels da janela.
- **Offscreen:** `update_ms`, `render_ms` e `publication_ms` separam atualização,
  render com readback de cor e cópia RGBA para o consumidor. `total_ms` inclui
  as três etapas. CoinGL inclui seu `getBuffer()` com readback tardio na etapa
  render; experimental usa `readbackRGBA()` para publicação. Mantenha o padrão
  síncrono `color` + `copy`, sem borrowed output, async ou profundidade.
- **Inicialização:** preserve logs de startup/prepare, primeiro quadro e tempo
  até seu resultado desde `main`. GPU prepare lazy continua dentro do primeiro
  render; `COIN_RENDER_TRACE_PHASES=1` detalha suas fases em execução diagnóstica
  separada. O primeiro quadro é o primeiro do warmup, ou o primeiro medido quando
  warmup é zero. Processo novo não
  implica caches de driver ou sistema vazios.

Os tempos de janela são CPU/parede, sem timestamps GPU ou latência até a tela.
Não há fence final público equivalente nos backends nativos: sua fila pode
continuar pendente ao fim do throughput. CoinGL conserva um `glFinish()` final
que inclui a cauda GL, com escopo explicitamente diferente e custo registrado.
VSync é solicitado desligado; driver, fila e compositor podem impor espera.
Resultados limitados à frequência do monitor exigem investigação separada.
Os dois criadores de janela solicitam tamanho fixo ao WM e validam o drawable
após MapNotify; `window_geometry_detail` registra pedido, tamanho real, origem,
tela e workarea. Um tamanho diferente ou resize durante o loop aborta a medição,
em vez de comparar recortes silenciosamente. Um drawable que ultrapasse a
workarea é explicitamente registrado; sua área continua sendo a solicitada e
nenhum desses tempos mede a apresentação completa na tela.

## Reprodução local

Use builds Release da mesma revisão, com ambos os exemplos e CoinGL habilitados,
um processo por variante e nenhuma build ou carga GPU concorrente. Os caminhos
da campanha local são temporários; substitua-os ao reproduzir em outra máquina:

```sh
cd /tmp/coin-render-first-frame
BGFX_BUILD=/tmp/coin-render-first-frame-bgfx
WGPU_BUILD=/tmp/coin-render-first-frame-wgpu
CITY=/tmp/coin-render-city-40000.iv
python3 scripts/coinrender/run_animation_benchmark.py \
  --bgfx-build "$BGFX_BUILD" --wgpu-build "$WGPU_BUILD" --scene "$CITY" \
  --scope offscreen --mode measure --gpu nvidia --cases static,camera,transforms-10 \
  --output /tmp/coin-render-animation-offscreen
python3 scripts/coinrender/run_animation_benchmark.py \
  --bgfx-build "$BGFX_BUILD" --wgpu-build "$WGPU_BUILD" --scene "$CITY" \
  --scope window --mode measure --gpu nvidia --cases static,camera,transforms-10 \
  --output /tmp/coin-render-animation-window
```

O [runner](../scripts/coinrender/run_animation_benchmark.py) usa por padrão
3 rodadas intercaladas, 60 warmup, 600 quadros, 1024² e quatro variantes:
`coingl,bgfx-vulkan,bgfx-opengl,wgpu-vulkan`. Os sete casos são `static,camera,
transforms-1,transforms-10,transforms-100,materials-10,geometry-10`.
Ele grava comandos, ambiente, revisão, hashes de cena/binários, RSS, logs e CSV;
`medians.json` agrega a mediana das estatísticas de cada processo por caso.
`--rounds`, `--warmup`, `--frames`, `--cases` e `--variants` permitem pilotos.
Para retomar, use `--resume` com os mesmos parâmetros, cena e binários.

Confirme **CoinGL na mesma GPU física** em diagnóstico separado com
`COIN_DEBUG_GLGLUE=1`; preserve identificação e contexto nos registros.
O runner configura offload/ICD NVIDIA e verifica o adaptador experimental.
Uma campanha AMD opcional usa `--gpu amd` e outro diretório de saída, com nova
referência CoinGL AMD. wgpu/OpenGL pode ser investigado como `wgpu-opengl` no
offscreen AMD, separado da matriz NVIDIA; nunca agregue GPUs distintas.

## Verificação RGB separada

```sh
python3 scripts/coinrender/run_animation_benchmark.py \
  --bgfx-build "$BGFX_BUILD" --wgpu-build "$WGPU_BUILD" --scene "$CITY" \
  --scope offscreen --mode verify --gpu nvidia \
  --output /tmp/coin-render-animation-verify
python3 scripts/coinrender/analyze_animation_images.py /tmp/coin-render-animation-verify
```

Verify usa uma rodada, warmup 0, sete quadros e step 100: estados lógicos
`0,100,200,300,400,500,600`. Cada captura registra digest do estado da cena e
hash RGB, além do RGBA. O [analisador](../scripts/coinrender/analyze_animation_images.py)
exige estados iguais por quadro entre variantes, mudança nos estados/imagens
dinâmicos e estabilidade estática; o hash final sozinho não valida a animação.
Os PPM já têm orientação superior normalizada; compare RGB com CoinGL, sem
corrigir imagens para eliminar diferenças. Registre MAE, erro máximo e pixels
diferentes/acima de 3 por canal. Alpha bruto não é critério entre APIs.
Capturas, hashes e I/O ficam fora dos timers de quadro, mas afetam o tempo de
parede da execução verify; suas métricas não pertencem à campanha de desempenho.
