# Onda 6 — camada experimental de produto WebGPU

Esta onda transforma o perfil de renderização das ondas 0–5 em algo que um
consumidor pode experimentar sem incorporar headers privados da árvore de build.
Ela **não** promove WebGPU à API/ABI pública de `libCoin` no Coin 4.

## Contrato e matriz de capacidades

`coin_wgpu_experimental_query_capabilities()` retorna um struct C versionado
(`CoinWgpuExperimentalCapabilities`) para `OFFSCREEN` ou `XLIB_WINDOW`.
`features` descreve o perfil **compilado**; `gpu_available` informa, de forma
separada, se o backend encontrou um adaptador nesta máquina. Retorno 1 indica
alvo não suportado, 2 indica buffer inválido ou curto; nesses erros o buffer
do chamador permanece intacto. O chamador deve fornecer `sizeof` do struct
da versão 1. Não há negociação de versões futuras nesta onda.

| Backend | Offscreen | Janela Xlib/Linux | GPU | Readback assíncrono | RTT direto |
| --- | --- | --- | --- | --- | --- |
| Rust bridge | Sim | Sim | Se houver adaptador | Offscreen | Offscreen, opt-in |
| Recording/CPU | Sim | Não | Não | Não | Não |
| Dawn/wgpu-native spike | Sem perfil anunciado | Não | Não | Não | Não |

O perfil Rust offscreen anuncia triângulos, geometria indexada, linhas/pontos,
textura 2D da unidade 0, até oito luzes por draw, fog, alpha ordenado,
cor/profundidade, até oito níveis de `SoSceneTexture2` e orçamento de 64 MiB
por apply. A rota RTT direta exige adicionalmente
`COIN_WGPU_RTT_GPU_DIRECT=1`; o bit significa que a implementação oferece
essa rota, não que esteja ligada naquele processo. Consulte
`wgpu-experimental-profile.md` para as restrições precisas de cada recurso.

## Manager e viewer

`SoWgpuSceneManager` é um dono pequeno de `SoWgpuRenderAction` e
`SoWgpuRenderTarget`, não um substituto de `SoRenderManager`/`SoSceneManager`.
Inicialize `SoDB` e registre a action antes de construí-lo. O manager retém
uma referência da raiz passada a `setSceneGraph()`, aceita resize inclusive
suspensão 0×0, relata status/diagnóstico do último render e expõe o target
para readback. Em `renderAsync()`, o ticket continua válido após destruir o
manager, conforme o contrato de `SoWgpuRenderTarget::pollReadback()`.

Para janela, o chamador cria e administra a janela nativa e o loop de eventos.
O manager deve ser destruído **antes** da janela/display. O exemplo
`wgpu_viewer` usa Xlib sem contexto OpenGL; setas giram o cone, `+`/`-` ou
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
`libCoinWgpuExperimental`, os headers de `experimental/include`, o target
`CoinWgpuExperimental::CoinWgpuExperimental` e a configuração CMake própria.
Com a opção de exemplos ativa, também instala `wgpu_window_cone`,
`wgpu_viewer`, `wgpu_gl_benchmark` (se GL legado estiver ligado) e suas fontes
em `share/doc/Coin/examples/wgpu`. Um consumidor C++11 externo pode usar:

```cmake
find_package(Coin CONFIG REQUIRED)
find_package(CoinWgpuExperimental CONFIG REQUIRED)
target_link_libraries(meu_app PRIVATE
  CoinWgpuExperimental::CoinWgpuExperimental Coin::Coin)
```

O projeto `testsuite/installed-wgpu-package-smoke` valida essa integração
somente através dos pacotes instalados.

## Validação local (24/09/2026)

Em Debug/C++11, passaram 24/24 testes do backend Recording e 35/35 do
backend Rust em execução serial sob Xvfb. O teste de produto passou nos dois
backends; o viewer executou três frames e o viewer **instalado** executou mais
dois frames sem `LD_LIBRARY_PATH`. O consumidor C++11 externo compilou e
executou contra o prefixo instalado, e o header de capacidades compilou em
C11. A lista de símbolos exportados de `libCoin.so` foi idêntica em builds
WebGPU ON/OFF equivalentes.

Na primeira execução Rust com quatro testes em paralelo, o gate antigo
`WgpuSceneTextureDirectTest` falhou na injeção de OOM após o pass filho;
passou três vezes isolado e na suíte serial. Isto sugere interferência por
carga GPU entre testes, mas a causa não foi provada. Para um gate local
confiável da Onda 6, executar a suíte Rust em série; a estabilidade do teste
sob paralelismo continua uma investigação separada.


## Medição comparativa

`wgpu_gl_benchmark` executa a mesma cena estática de 36 cubos em offscreen
nos dois renderizadores, com frames de aquecimento e leitura de RGBA incluída
nos tempos. Reporta adaptador, tamanho, mediana, p95, mínimo e máximo.

```sh
xvfb-run -a env COIN_GLX_PIXMAP_DIRECT_RENDERING=1 \
  build-wgpu/bin/wgpu_gl_benchmark \
  --frames 30 --warmup 8 --size 256
```

Os dois caminhos têm implementações e custos de readback diferentes; isto é
uma comparação end-to-end, **não** um microbenchmark equivalente de GPU nem
um SLA de desempenho. Repetir em Release, registrar GPU/driver e testar cena
dinâmica antes de uma conclusão de produto. Um contexto GLX funcional é
necessário mesmo sob Xvfb. Neste ambiente, o Coin precisa de
`COIN_GLX_PIXMAP_DIRECT_RENDERING=1`: sem isso, tenta inicialmente um
contexto indireto e falha. Com a opção, o benchmark executou. Ainda é
necessário alinhar o adaptador GL e WebGPU antes de interpretar desempenho.
O benchmark também aceita `--scene cena.iv`. No ensaio com sete exemplos
exportados do FreeCAD, os dois caminhos usaram a mesma GPU AMD: houve
paridade geométrica em `BASE_COLOR`, mas WebGPU Debug foi mais lento que GL
em todos os modelos. A divergência inicial de iluminação `PHONG` vinha da
geração de normais no caminho rápido WebGPU e foi corrigida no Coin, sem
alterar as cenas exportadas nem a ABI pública do Coin 4. Depois da correção,
o erro médio RGB de `PHONG` contra GL nos sete exemplos originais ficou
entre 0,019 e 0,207, com IoU de silhueta entre 0,999860 e 1,000000. Isso
demonstra paridade no perfil testado, não superioridade visual. Ainda não
há ganho de desempenho demonstrado; os testes usam malhas exportadas
offscreen, não a viewport real do FreeCAD. Resultados, condições de medição
e imagens em
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
