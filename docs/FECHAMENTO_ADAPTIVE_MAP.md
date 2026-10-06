# Revisão de `SbAdaptiveMap` e correção isolada de atributos SCXML

Estado em 6 de outubro de 2026. A branch experimental `codex/maps/core/adaptive-map` (`3957f93c76d527908d520b15f5e6d33041e8fdfd`) foi examinada em um worktree separado. Ela parte de um master antigo, altera `SbHash` e acrescenta `SbAdaptiveMap`, `SbRobinHoodHash` e testes. **A implementação experimental não está pronta para PR**. Nenhum desses arquivos foi transportado para a contribuição descrita abaixo.

## Evidência da decisão

`SbAdaptiveMap::spillAndPut()` destrói as entradas inline antes de construir a entrada nova no vetor. Se a cópia do valor novo lança, `getNumElements()` passa a zero e a entrada preexistente some. O reprodutor `/tmp/adaptive-map-fault-repro.cpp`, compilado em `/dev/shm/coin-adaptive-audit-20261006/fault-repro-asan` com `g++ -std=c++11 -O0 -g -fsanitize=address,undefined -fno-omit-frame-pointer`, terminou com `spill failure 2: lost existing entry, visible count 0`. O código fonte ainda deixa o hash auxiliar parcialmente preenchido quando a promoção vetor→hash falha; a tentativa seguinte exige um hash vazio. O backend `SbRobinHoodHash` modifica a tabela durante inserção, remoção e rehash com cópias que podem lançar; esses caminhos precisam de uma política transacional antes de integrar o tipo.

Microbenchmark local em RAM, `g++ -std=c++11 -O2 -DNDEBUG`, inteiros, construção de um mapa e cinco leituras por chave em cada repetição; tempos em microssegundos para aproximadamente 100 mil inserções por cardinalidade:

| Entradas | Adaptive | `std::map` | `std::unordered_map` |
| ---: | ---: | ---: | ---: |
| 1 | 893 | 2828 | 5279 |
| 5 | 1062 | 3023 | 2688 |
| 6 | 2041 | 2911 | 2636 |
| 8 | 2368 | 3096 | 2651 |
| 24 | 4128 | 3588 | 2777 |
| 25 | 3006 | 3803 | 2815 |
| 64 | 2264 | 5178 | 3176 |

Este microbenchmark mostra um possível ganho para conjuntos muito pequenos, mas não representa o perfil dos consumidores nem mede alocações. Em 24 entradas o vetor já perde para as duas alternativas. A vantagem observada não justifica publicar agora dois tipos novos e cerca de 900 linhas de infraestrutura com os defeitos de integridade acima. A branch experimental permanece preservada como fonte; sua migração de armazenamento fica encerrada como candidata à publicação neste ciclo. Os consumidores devem ser avaliados por seus próprios contratos e medições, sem assumir `SbAdaptiveMap` como pré-requisito aprovado.

## Mudança aproveitada dos atributos SCXML

A branch antiga `codex/maps/use/scxml-attributes-adaptive` (`8b77560293`) continha uma correção funcional independente: `ScXMLElt::setXMLAttribute()` liberava o valor antigo antes de copiar o argumento. Quando o argumento era o próprio valor armazenado ou um subtrecho, `strlen()`/`strcpy()` liam memória liberada. A branch `codex/pre-pr/scxml-attribute-alias` (`6ab2915cb7007683b4548c2a6e79cb3b0803001c`) parte diretamente de master `674e74267df863dbaf50416c477bc7f918a826d8`. Ela duplica antes da troca, mantém o valor anterior se a alocação falhar, e conserva a cópia em `unique_ptr` até a inserção no `std::map` terminar. O diff do PR contém somente `src/scxml/ScXMLElt.cpp` (44 inserções, 14 remoções), com regressão para alias integral, subtrecho e remoção.

A cópia `lab/teste/scxml-attribute-alias` (`5b1b44b1a878c7064314ad2226273521cf259fd8`) parte do lab fixo `b27e37a6dd3ae941bcffcf480a377adc81e3fc27`, composto do master e 17 PRs, e recebeu somente o commit da correção. O lab fixo não foi alterado.

Comandos de validação, usando compilação em RAM:

```bash
cmake -S <worktree> -B /dev/shm/coin-scxml-attribute-alias-master -DCMAKE_BUILD_TYPE=Debug -DCOIN_BUILD_TESTS=ON -DCOIN_BUILD_DOCUMENTATION=OFF -DCOIN_THREADSAFE=ON
cmake --build /dev/shm/coin-scxml-attribute-alias-master --parallel 8
/dev/shm/coin-scxml-attribute-alias-master/bin/CoinTests --run_test=ScXMLElt_TestSuite/scxml_attribute_alias_replacement
xvfb-run -a -s '-screen 0 1280x1024x24 +extension GLX' env LIBGL_ALWAYS_SOFTWARE=1 COIN_GLX_PIXMAP_DIRECT_RENDERING=1 ctest --test-dir /dev/shm/coin-scxml-attribute-alias-master --output-on-failure --parallel 4
```

Resultado sobre master: compilação aprovada, regressão **1 teste/7 verificações** e conjunto **78/78**. A mesma configuração no build `/dev/shm/coin-scxml-attribute-alias-lab` passou a regressão **1/7** e o conjunto **105/105**. A variante Clang em `/dev/shm/coin-scxml-attribute-alias-asan`, configurada com `-fsanitize=address,undefined -fno-omit-frame-pointer` em C/C++ e linkagem, compilou `CoinTests`; a regressão **1/7** passou com `ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1`. Windows permanece pendente. As branches e o worktree do lab ficam preservados até o ciclo de Windows/publicação; nenhum PR foi publicado.
