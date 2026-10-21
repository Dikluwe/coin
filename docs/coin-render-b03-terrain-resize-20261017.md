# B03 — terreno histórico e correção de resize BGFX/OpenGL

**B03.3 está corrigido e qualificado no Linux disponível.** O novo cruzamento
terreno iluminado × câmera × janela/offscreen × resize revelou uma cena perdida
em BGFX/OpenGL AMD. A reconfiguração do drawable agora tem uma etapa explícita
antes da cena. A ocorrência histórica wgpu/OpenGL aos 50 ms permanece aberta
em B03.2: ela não foi reproduzida nem atribuída a esta causa BGFX.

## Antes e depois

| Verificação | Antes | Depois |
| --- | --- | --- |
| BGFX/OpenGL AMD nativo, resize 1280×960→640×480 | Novo carimbo não chegou em cinco segundos, serial preservado; FAIL | Uma aplicação da cena; RGB máximo 0 |
| Mesmo cruzamento canônico | Mesmo FAIL | RGB máximo 0 |
| Confirmação prévia do tamanho X11 | Não eliminou o FAIL nativo | Mantida como requisito correto do consumidor |
| Duas aplicações da cena antes da captura | Recuperavam a imagem | Controle mantido; a correção não depende dele |
| Terreno, sete células, nativo/canônico/dupla aplicação | 23 processos positivos aprovados, dois reprovados | 25/25 positivos, 75/75 imagens RGB exatas contra offscreen |
| Submissão omitida | — | Rejeitada, retorno 1, carimbo novo ausente |
| wgpu/OpenGL NVIDIA | Indisponível | Retorno 77, fora dos passes |

A fixture original de `linux-visual-20261010/main.cpp` foi recompilada sem
alterações contra o SDK atual. Os PPMs inicial/final de janela e offscreen
são iguais byte a byte; a recomputação encontrou 401.466 e 400.710 pixels
visíveis. O novo helper preserva as 501.501 coordenadas, 500.000 quads,
20 bandas, paleta, duas luzes direcionais e câmera ortográfica do terreno.
CoinRenderAction captura três milhões de vértices e um milhão de triângulos.

O observador usa um clear distinto por revisão, guarda as leituras imediata
e aos 50 ms e observa novamente sem render adicional por até cinco segundos.
Somente depois da chegada do carimbo e da captura X11 ele renderiza a
referência offscreen. A comparação exige RGB máximo 0 e cobertura não vazia.
Alpha não é observado pelo X11. Os modos de uma/duas aplicações são explícitos
nos recibos; os três quadros cobrem câmera e resize.

## Correção e evidência causal

`XResizeWindow` numa janela gerenciada é um pedido ao WM. O teste agora espera
a dimensão nativa confirmada; XSync isolado ainda observava 1280×960 após o
pedido de 640×480. Essa correção da fixture foi testada separadamente e o
quadro continuou perdido em BGFX/OpenGL AMD.

A instrumentação EGL, anexada à resolução dinâmica usada pelo SDK, observou
o clear anterior antes do swap do quadro que falhou. O SDK preservava a
swapchain/contexto e atualizava os anexos sem uma etapa separada de apresentação
da reconfiguração. A [especificação EGL 1.5](https://registry.khronos.org/EGL/specs/eglspec.1.5.pdf)
permite detectar e ajustar a dimensão nativa na troca; isso é contexto para o
diagnóstico, sem provar por si só um defeito interno do Mesa.

No CoinBgfxBackend, ao reconfigurar uma swapchain OpenGL já existente,
a preparação usa uma view nativa sem geometria (`touch` + `frame`) e reseta
essa view antes de codificar a cena. Não recria a superfície EGL, não duplica
captura/lowering/desenho da cena e não publica um serial de cena na preparação.
Os recibos comprovam `bgfx_window_resize_prepare ... scene_draws=0`.
A mesma preparação cobre a reconstrução de anexos por mudança de composição.
Vulkan e offscreen não passam por essa etapa. Dimensão inalterada com anexos
válidos retorna antes da preparação. O custo dessa etapa fica em B10.

A evidência sustenta a correção desse ponto de integração: o caso falhava
mesmo após confirmar o tamanho nativo; a etapa de preparação elimina a perda
com a mesma cena/oráculo e uma única aplicação. A causa interna exata do
comportamento do driver não foi inferida apenas dos pixels ou da especificação.
O SDK compartilhado e os shaders não foram modificados nesta correção.

## Regressões e limite da qualificação

Além dos 25 processos do terreno nas sete células físicas, **20/20 processos
BGFX** passaram em AMD/NVIDIA × Vulkan/OpenGL:

- Quatro execuções B01 de janela, 192 casos por célula, com cor/RTT/resize.
- Dezesseis execuções de múltiplas janelas: composição comum, weighted OIT,
  peeling e criação offscreen antes das janelas; incluem câmera, alpha,
  resize, suspensão, recriação e perda injetada.

O teste antigo dispensava os pixels X11 no caminho OpenGL. A nova seleção
`COIN_BGFX_TEST_REQUIRE_X11_PIXELS=1` exige seus predicados originais de cor
central das janelas, além da cor offscreen e lifecycle. Isso não é comparação
integral das imagens de múltiplas janelas nem teste de perda física real.
Foram acrescentados os respectivos CTests com lock GPU. Os testes do terreno
continuam exigindo a imagem inteira, nos dois modos de rasterização.

B03.1 (observador anterior) mantém seus 51 processos/153 quadros arquivados;
B03.3 fecha este cruzamento novo no perfil Linux executado. B03.2 mantém aberta
a ocorrência histórica de wgpu/OpenGL aos 50 ms. Intel, Windows, Android e
Metal não recebem qualificação por estes resultados. As etapas 2–4 completas
permanecem em andamento.

[Fontes e manifesto](validation/coin-render-b03-terrain-20261017/matrix/manifest.json),
[verificação](validation/coin-render-b03-terrain-20261017/matrix/verification.json),
[resultados finais](validation/coin-render-b03-terrain-20261017/matrix/results.json),
[falhas anteriores](validation/coin-render-b03-terrain-20261017/matrix/before/results.json),
[regressões](validation/coin-render-b03-terrain-20261017/matrix/window-regressions/results.json),
[fixture](../testsuite/coinrender/CoinRenderTerrainFixture.h),
[teste](../testsuite/coinrender/CoinRenderFirstPresentTest.cpp) e
[matriz](coin-render-feature-matrix.md).

```sh
python3 scripts/coinrender/run_first_present_matrix.py --terrain-matrix \
  --gl-repetitions 4 --wgpu-build /dev/shm/coinrender-b01-wgpu \
  --bgfx-build /dev/shm/coinrender-stage2-bgfx --output-dir /tmp/b03-terrain-repeat
python3 scripts/coinrender/audit_first_present_matrix.py \
  docs/validation/coin-render-b03-terrain-20261017/matrix
```

O primeiro comando produz uma nova campanha. O segundo audita o pacote
arquivado, os fontes congelados, os pixels originais, os FAIL anteriores e
as regressões. Os hashes dos binários ficam associados a cada execução.
