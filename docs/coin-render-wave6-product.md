# Onda 6 — camada experimental de produto WebGPU

Esta onda transforma o perfil de renderização das ondas 0–5 em algo que um
consumidor pode experimentar sem incorporar headers privados da árvore de build.
Ela **não** promove WebGPU à API/ABI pública de `libCoin` no Coin 4.

## Contrato e matriz de capacidades

`coin_render_query_capabilities()` retorna um struct C versionado
(`CoinRenderCapabilities`) para `OFFSCREEN` ou `XLIB_WINDOW`.
`features` descreve o perfil **compilado**; `gpu_available` informa, de forma
separada, se o backend encontrou um adaptador nesta máquina. A versão 2 também
expõe `probe_status`, renderer, IDs de vendor/device, limite de attachments,
flags normalizadas para RGBA8, D24S8, D32F, RGBA16F e R16F, além de MRT,
independent blend, compute e timestamps. Retorno 1 indica alvo não suportado, 2
indica buffer inválido ou curto; nesses erros o buffer permanece intacto. O
prefixo exato da struct v1 continua aceito e é devolvido com `version=1`; novos
consumidores fornecem `sizeof` da v2.

| Backend | Offscreen | Janela Xlib/Linux | GPU | Readback assíncrono | RTT direto |
| --- | --- | --- | --- | --- | --- |
| Rust bridge | Sim | Sim | Se houver adaptador | Offscreen | Offscreen, opt-in |
| BGFX | Sim | Sim | Probe temporário Vulkan/OpenGL | Não | Não |
| Recording/CPU | Sim | Não | Não | Não | Não |
| Dawn/wgpu-native spike | Sem perfil anunciado | Não | Não | Não | Não |

O perfil Rust offscreen anuncia triângulos, geometria indexada, linhas/pontos,
textura 2D da unidade 0, até oito luzes por draw, fog, alpha ordenado,
cor/profundidade, até oito níveis de `SoSceneTexture2` e orçamento de 64 MiB
por apply. A rota RTT direta exige adicionalmente
`COIN_RENDER_RTT_GPU_DIRECT=1`; o bit significa que a implementação oferece
essa rota, não que esteja ligada naquele processo. Consulte
`wgpu-experimental-profile.md` para as restrições precisas de cada recurso.

## Manager e viewer

`CoinRenderSceneManager` é um dono pequeno de `CoinRenderAction` (ou
`SoBGFXRenderAction` em builds BGFX) e
`CoinRenderTarget`, não um substituto de `SoRenderManager`/`SoSceneManager`.
Inicialize `SoDB` e registre a action antes de construí-lo. O manager retém
uma referência da raiz passada a `setSceneGraph()`, aceita resize inclusive
suspensão 0×0, relata status/diagnóstico do último render e expõe o target
para readback. Em `renderAsync()`, o ticket continua válido após destruir o
manager, conforme o contrato de `CoinRenderTarget::pollReadback()`.

Para janela, o chamador cria e administra a janela nativa e o loop de eventos.
O manager deve ser destruído **antes** da janela/display. O exemplo
`coin_render_viewer` usa Xlib sem contexto OpenGL; setas giram o cone, `+`/`-` ou
roda mudam o zoom, espaço alterna animação, `R` restaura e `Esc`/`Q` sai.
`--frames N` permite smoke test não interativo. Esta implementação de viewer
é somente Linux/X11; não há abstração de janela multiplataforma nesta onda.

## Build, instalação e consumidor

```sh
cmake -S . -B build-wgpu -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_STANDARD=11 \
  -DCOIN_BUILD_WGPU=ON -DCOIN_WGPU_BACKEND=RUST_BRIDGE \
  -DCOIN_BUILD_TESTS=ON -DCOIN_BUILD_WGPU_WINDOW_EXAMPLE=ON \
  -DCOIN_INSTALL_WGPU_EXPERIMENTAL=ON
cmake --build build-wgpu -j
ctest --test-dir build-wgpu --output-on-failure
cmake --install build-wgpu --prefix /caminho/de/teste
```

O pacote experimental é opt-in e separado do pacote Coin. Ele instala
`libCoinRender`, os headers de `experimental/include`, o target
`CoinRender::CoinRender` e a configuração CMake própria.
Com a opção de exemplos ativa, também instala `coin_render_window_cone`,
`coin_render_viewer`, `coin_render_gl_benchmark` (se GL legado estiver ligado) e suas fontes
em `share/doc/Coin/examples/coinrender`. Um consumidor C++11 externo pode usar:

```cmake
find_package(Coin CONFIG REQUIRED)
find_package(CoinRender CONFIG REQUIRED)
target_link_libraries(meu_app PRIVATE
  CoinRender::CoinRender Coin::Coin)
```

O projeto `testsuite/installed-coin-render-package-smoke` valida essa integração
somente através dos pacotes instalados.

## Validação local (24/09/2026)

Em Debug/C++11, passaram 24/24 testes do backend Recording e 35/35 do
backend Rust em execução serial sob Xvfb. O teste de produto passou nos dois
backends; o viewer executou três frames e o viewer **instalado** executou mais
dois frames sem `LD_LIBRARY_PATH`. O consumidor C++11 externo compilou e
executou contra o prefixo instalado, e o header de capacidades compilou em
C11. A lista de símbolos exportados de `libCoin.so` foi idêntica em builds
WebGPU ON/OFF equivalentes.

Após a otimização de CoinRenderFramePlan, os mesmos gates foram reexecutados: 35/35 em
Debug/Rust e 24/24 em Debug/Recording. Um build Release/Rust com as duas
referências GL adicionais passou 37/37. O gate RTT aceita que um recurso já
removido da tabela ativa permaneça, de forma segura, na fila de aposentadoria
após um poll não bloqueante; exige drenagem zero depois do frame de
recuperação.

O gate antigo `CoinRenderSceneTextureDirectTest` exigia que um único poll
não bloqueante drenasse imediatamente a fila RTT após OOM. O perfil mais
rápido expôs a corrida: o token já estava fora da tabela ativa, mas o trabalho
GPU podia continuar pendente. O teste agora aceita no máximo uma entrada
aposentada nesse ponto e exige zero depois do frame de recuperação. Isso
preserva a verificação de vazamento sem transformar `poll` em espera por idle.


## Medição comparativa

`coin_render_gl_benchmark` executa a mesma cena em offscreen nos dois renderizadores,
com frames de aquecimento e leitura de RGBA incluída nos tempos. Desde o
Prompt 008B, o padrão `--readback color` não solicita depth ao WebGPU,
igualando os outputs observados no GL. `--backend wgpu --readback color-depth`
mede separadamente o contrato legado de cor+profundidade; essa modalidade
não é apresentada como comparação equivalente com GL. Campanhas anteriores
rotuladas como RGBA no WebGPU ainda copiavam depth internamente e não são um
A/B pareado com o novo padrão. O benchmark reporta
adaptador, tamanho, mediana, p95, mínimo e máximo. `--scene` carrega uma cena
Inventor, `--dynamic` altera a câmera em cada frame e `--backend wgpu|gl`
permite medir memória em processos separados.

```sh
xvfb-run -a env COIN_GLX_PIXMAP_DIRECT_RENDERING=1 \
  build-wgpu/bin/coin_render_gl_benchmark \
  --frames 30 --warmup 8 --size 256
```

Os dois caminhos têm implementações e custos de readback diferentes; isto é
uma comparação end-to-end, **não** um microbenchmark equivalente de GPU nem
um SLA de desempenho. Uma comparação interpretável deve usar Release,
registrar GPU/driver e testar cenas estáticas e dinâmicas. Um contexto GLX funcional é
necessário mesmo sob Xvfb. Neste ambiente, o Coin precisa de
`COIN_GLX_PIXMAP_DIRECT_RENDERING=1`: sem isso, tenta inicialmente um
contexto indireto e falha. Com a opção, o benchmark executou. Ainda é
necessário alinhar o adaptador GL e WebGPU antes de interpretar desempenho.
No ensaio Release/C++11, AMD RADV, 512×512, 8 frames de aquecimento e 30
medidos, o WebGPU estático superou GL no Assembly: mediana 1,977 contra
4,221 ms e p95 2,283 contra 4,826 ms, com readback incluído. Não houve ganho
nos outros três modelos reavaliados. Ao alterar a câmera a cada frame, o
Assembly ficou em 20,387/21,096 ms (mediana/p95) contra 4,006/4,727 ms no GL;
traversal e CoinRenderFramePlan ainda dominam. O RSS isolado do Assembly estático foi
148.424 KiB no WebGPU e 119.604 KiB no GL.

A divergência inicial de iluminação `PHONG` vinha da geração de normais no
caminho rápido WebGPU e foi corrigida no Coin, sem alterar as cenas exportadas
nem a ABI pública do Coin 4. A nova campanha Release manteve MAE abaixo de 1
e IoU 1,0 nas quatro cenas avaliadas. Isso demonstra paridade no perfil
testado e um ganho estático específico, não superioridade geral nem ganho na
viewport real do FreeCAD. RTT direto e readback assíncrono não foram usados
para justificar esses números. Resultados, condições de medição e imagens em
[`wgpu-freecad-examples-validation.md`](wgpu-freecad-examples-validation.md).

## Política de estabilidade e Coin 5

- Coin 4: `libCoin` e seus headers públicos preservam a ABI; todo o novo
  contrato fica em biblioteca e headers experimentais opt-in. A ABI desse
  módulo ainda é provisória e não deve ser tratada como estável.
- Coin 5: decidir explicitamente se e como promover manager, capacidades,
  surface e action à API pública; nessa etapa podem ser consideradas mudanças
  de ABI que não cabem no Coin 4. Não existe uma versão 6 pressuposta.
- Antes de promoção: validar backend nativo, outras plataformas de janela,
  matriz por adaptador, estabilidade de API, testes de device loss no viewer e
  benchmarks de cenas representativas. Nenhum desses itens é prometido pela
  Onda 6 atual.
