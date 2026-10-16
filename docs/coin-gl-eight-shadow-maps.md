# CoinGL — oitavo shadow map com oito unidades de coordenadas

Implementação na branch `codex/coingl-eight-shadow-maps`, baseada em `b86404cafc9ec13e91d0bac67f2e875f2b206a07`. O limite anterior vinha de `SoShadowGroup`: uma unidade fixa para a cena e sete mapas nas outras sete unidades de coordenadas. Neste host, AMD e NVIDIA anunciam `GL_MAX_TEXTURE_COORDS=8`, `GL_MAX_TEXTURE_IMAGE_UNITS=32` e `GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS=192`. Não foi alterado o limite informado pelo driver.

## Mecanismo

A escolha agora é feita pelos limites do contexto: conjuntos de coordenadas (`GL_MAX_TEXTURE_COORDS`), samplers de fragmento (`GL_MAX_TEXTURE_IMAGE_UNITS`), unidades combinadas (`GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS`) e luzes (`GL_MAX_LIGHTS`). O número de mapas é limitado a oito e ao orçamento real de samplers e luzes. Primeiro são usadas as unidades fixas livres, depois unidades somente de fragmento quando a cena usa uma unidade de textura e o contexto comporta cena + oito mapas. Cenas com mais unidades de textura continuam no caminho fixo, com o número de mapas reduzido à capacidade disponível.

Para cada mapa além das unidades de coordenadas, `SoSceneTexture2` o produz temporariamente na primeira unidade fixa livre. O CoinGL liga a imagem numa unidade de fragmento adicional, fornece sua matriz ao shader como uniforme e restaura a imagem/matriz fixa emprestada. O binding anterior de cada unidade adicional é restaurado ao sair do grupo. Peeling usa a primeira unidade livre após as sombras apenas quando ela cabe nos limites de fragmento e combinado. Neste host, isso significa sete mapas nas unidades fixas 7–1, o oitavo na unidade de fragmento 8 e peeling na unidade 9; em outro contexto, esses índices são calculados a partir dos limites consultados.

O teste estrito consulta coordenadas, samplers de fragmento e samplers combinados separadamente. Além da comparação GL/GPU, ele mede `(imagem sombreada − imagem sem sombras)` com a oitava luz ativa e com intensidade zero. Uma oitava luz apenas comum cancela nessa diferença; o teste exige contribuição de sombra superior a três níveis. O atalho anterior de sete mapas CoinGL só opera sem `COIN_RENDER_REQUIRE_GL_EIGHT_MAP_REFERENCE=1`.

## Qualificação Linux, 2026-10-09

Build Release em worktree isolado. AMD Renoir/Mesa 25.2.8 e NVIDIA RTX 3060 Laptop/615.71.09 físicos, GLX direto, BGFX/Vulkan com ICD fixado; BGFX/OpenGL também foi testado nas duas GPUs. `COIN_RENDER_REQUIRE_GL_REFERENCE=1`, `COIN_RENDER_REQUIRE_GL_EIGHT_MAP_REFERENCE=1` e `COIN_RENDER_REQUIRE_BGFX_SHADOW_GPU=1` nos gates estritos. [Resumo com hashes, saídas e logs](</home/dikluwe/Área de trabalho/Estudo coin/consolidacao-frentes-20261016/renderer-history/docs/validation/coingl-eight-shadow-maps-20261009/summary.json>).

| Perfil por GPU | AMD | NVIDIA |
| --- | --- | --- |
| `--gl-capacity` | 8 coords, 32 fragment, rota fragment-only | 8 coords, 32 fragment, rota fragment-only |
| `--transparent-maps 8` | PASS | PASS |
| `--shadow-oit 8 1` e `8 2` | PASS / PASS | PASS / PASS |
| `--quality 8` | PASS | PASS |
| `--alpha-test 8 0`, `8 1`, `8 2` | PASS / PASS / PASS | PASS / PASS / PASS |
| `--transparent-maps 8` em BGFX/OpenGL | PASS | PASS |
| Controle de sete mapas em BGFX/OpenGL | PASS | PASS |

Todos os 14 gates estritos Vulkan saíram com código 0 e sem `qualified=0`. A prova incremental da oitava sombra apareceu em transparência, peeling, OIT e ALPHA_TEST, com máximos de 4–20 níveis na AMD e 5–20 na NVIDIA. Os dois gates estritos BGFX/OpenGL também passaram com prova incremental. O controle de sete mapas preservou o comportamento anterior. O CMake build compilou Coin, CoinRender e o executável de teste sem erro.

Esta qualificação fecha o limite nativo de oito mapas **neste Linux e nesta branch**. O código seleciona dinamicamente as rotas para outros limites, mas esta máquina só forneceu contextos físicos com oito conjuntos de coordenadas. A rota com menos conjuntos e a rota fixa com pelo menos nove ainda requerem execução em hardware/driver correspondente; a elegibilidade por capacidade não substitui essa prova. Cenas com duas ou mais unidades de textura continuam com admissão conservadora. A integração na branch de estudo ainda requer portar ou mesclar esta mudança e repetir seus gates naquela revisão.


Historical qualification predates this consolidated tree. No GPU qualification was repeated during consolidation. Original receipts remain available through tag archive/2026-10-16/codex/coingl-eight-shadow-maps.
