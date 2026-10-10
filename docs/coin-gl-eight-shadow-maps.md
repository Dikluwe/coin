# CoinGL — oitavo shadow map com oito unidades de coordenadas

Implementação na branch `codex/coingl-eight-shadow-maps`, baseada em `b86404cafc9ec13e91d0bac67f2e875f2b206a07`. O limite anterior vinha de `SoShadowGroup`: uma unidade fixa para a cena e sete mapas nas outras sete unidades de coordenadas. Neste host, AMD e NVIDIA anunciam `GL_MAX_TEXTURE_COORDS=8`, `GL_MAX_TEXTURE_IMAGE_UNITS=32` e `GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS=192`. Não foi alterado o limite informado pelo driver.

## Mecanismo

No perfil de uma unidade de cena, oito unidades fixas e ao menos nove samplers de fragmento/combined, o grupo admite a oitava luz. `SoSceneTexture2` produz seu mapa usando temporariamente a unidade fixa 7. O CoinGL captura a imagem produzida, liga seu objeto GL na unidade de fragmento 8 e restaura imagem/matriz da primeira sombra na unidade 7. O shader recebe a matriz do oitavo mapa em `shadowMatrix7`, sem consultar `gl_TextureMatrix[8]`. A unidade de peeling passa para 9 quando necessário; o binding anterior da unidade 8 é restaurado após o grupo. Contextos com pelo menos nove unidades fixas conservam o caminho anterior; outros perfis conservam sua admissão anterior.

O teste estrito consulta coordenadas, samplers de fragmento e samplers combinados separadamente. Além da comparação GL/GPU, ele mede `(imagem sombreada − imagem sem sombras)` com a oitava luz ativa e com intensidade zero. Uma oitava luz apenas comum cancela nessa diferença; o teste exige contribuição de sombra superior a três níveis. O atalho anterior de sete mapas CoinGL só opera sem `COIN_RENDER_REQUIRE_GL_EIGHT_MAP_REFERENCE=1`.

## Qualificação Linux, 2026-10-09

Build Release em worktree isolado. AMD Renoir/Mesa 25.2.8 e NVIDIA RTX 3060 Laptop/615.71.09 físicos, GLX direto, BGFX/Vulkan com ICD fixado; BGFX/OpenGL também foi testado nas duas GPUs. `COIN_RENDER_REQUIRE_GL_REFERENCE=1`, `COIN_RENDER_REQUIRE_GL_EIGHT_MAP_REFERENCE=1` e `COIN_RENDER_REQUIRE_BGFX_SHADOW_GPU=1` nos gates estritos. [Resumo com hashes, saídas e logs](validation/coingl-eight-shadow-maps-20261009/summary.json).

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

Esta qualificação fecha o limite nativo de oito mapas **neste Linux e nesta branch**. Não generaliza para cenas que já usam duas unidades de textura, GPUs/OS não testados ou outros limites de samplers; esses perfis mantêm a admissão anterior. A integração na branch de estudo ainda requer portar ou mesclar esta mudança e repetir seus gates naquela revisão.
