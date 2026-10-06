# Fechamento Linux dos mapas e consumidores em estudo

Auditoria em 6 de outubro de 2026. Base de destino `master` `674e74267df863dbaf50416c477bc7f918a826d8`; base integrada fixa `lab/open-prs-integration` `b27e37a6dd3ae941bcffcf480a377adc81e3fc27`, composta por master e 17 PRs abertos conforme [CONTROLE_BRANCHES.md](CONTROLE_BRANCHES.md). Nenhum head dessas bases foi movido. As branches de origem continuam preservadas. Ainda não houve push nem publicação.

## Resultado por fonte

| Fonte | Resultado da auditoria |
| --- | --- |
| `codex/maps/use/coinresources-adaptive` `241764d557` | Troca o `SbHash<SbName, ResourceHandle *>` existente por `SbAdaptiveMap`. Não traz correção funcional exclusiva; depende do tipo adaptativo reprovado por perda de entradas sob exceção. Sem PR dessa troca. |
| `codex/maps/use/scxml-attributes-adaptive` `8b77560293` | Troca `std::map` por armazenamento adaptativo inseguro. A correção funcional de alias já foi extraída e validada em `codex/pre-pr/scxml-attribute-alias` `6ab2915cb7`; sem PR da troca de mapa. |
| `codex/maps/use/scxml-document-ids-adaptive` `aab125aa41` | O `std::map::insert` original já preserva o primeiro ID duplicado. A promoção da tabela pequena instala `hashmap` antes de concluir a migração; falha intermediária deixa IDs antigos invisíveis. Sem PR dessa otimização. |
| `codex/maps/use/scxml-evaluator-temporaries-adaptive` `82b9f29c78` | A troca por tabela adaptativa tem promoção/ownership frágeis. Havia um defeito independente: substituir um temporário pelo próprio objeto obtido via `locate()` liberava a origem antes da cópia. Extraído em `codex/pre-pr/scxml-temporary-alias` `b4204f98594c5fb4eafdeea685ee96fde6cf3c67`; mantém `std::map`. |
| `codex/maps/core/sequential-map` `3ba09c803a` | Não aprovado: em Release aceita uma chave negativa, aumenta `size()`, mas `getPtr()` retorna nulo para a mesma chave. Também há operações de intervalo suscetíveis a overflow e o ganho medido não é consistente. Sem PR do tipo neste ciclo. |
| `codex/maps/integration/profiler-containers` `ba0ed99669` | Agrega AdaptiveMap, Robin Hood hash, SequentialMap e migrações do profiler em 2.261 linhas adicionadas. Depende de tipos ainda não aprovados. O único conserto funcional isolado é o lifetime de dados por ação descrito abaixo. Sem PR do agregado. |
| `codex/maps/use/profiler-action-timings-sequential` `edaaec9b5c` | A troca de `SbHash` por SequentialMap depende do tipo reprovado. Extraída apenas a liberação dos `SbProfilingData *` pertencentes ao PImpl em `codex/pre-pr/profiler-stats-lifetime` `bb32da5fc2041e56bf75e158c8df63d6bdb66243`. Sem PR da troca de mapa. |
| `codex/maps/use/fieldcontainer-mfield-sizes-hash` `339a1d6b49` | Substitui comparações de nomes internados por uma tabela global alocada na inicialização para uma rotina de estimativa de memória. O tratamento de MFields desconhecidos (`elementsize = 0`) já existe no master. Sem correção exclusiva ou medida de ganho para justificar a nova tabela/lifetime; sem PR desta migração. |
| `codex/maps/use/scxml-type-registry-hash-name` `eb1d50e0d2` | Migra registries `std::map` para `SbHash` e cria helper `SbHashName.h`. Não há defeito funcional identificado nem medida de ganho; o helper não é requisito das correções isoladas. Sem PR desta migração. |
| `codex/sbname/experiment/compact-entry-pool` `7b439fb7c2` | Reorganiza a tabela de internamento em blocos de 256 entradas; as strings continuam em arenas, o que preserva identidade durante crescimento normal. `realloc` é atribuído diretamente ao único ponteiro, e alocações de bloco/buckets são dereferenciadas sem verificação; o limite `UINT32_MAX` depende apenas de `assert`. Precisa de política para falha de alocação, limites e comparação de desempenho/memória antes de PR. Preservar como experimento. |

O problema da promoção de `SbAdaptiveMap` e a regressão correspondente estão em [FECHAMENTO_ADAPTIVE_MAP.md](FECHAMENTO_ADAPTIVE_MAP.md). Nenhuma das dez fontes acima deve ser publicada diretamente: seus diffs incluem mudanças experimentais e pré-requisitos além das duas correções funcionais isoladas.

## Candidatos isolados e bases

| Contribuição | Branch sobre master | Cópia sobre lab fixo | Diff contra master |
| --- | --- | --- | --- |
| Substituição segura de temporário SCXML | `codex/pre-pr/scxml-temporary-alias` `b4204f98594c5fb4eafdeea685ee96fde6cf3c67` | `lab/teste/scxml-temporary-alias` `97a1a57654f7e21a58cdd8567b3a5bd0fff9dba8` | Só `src/soscxml/ScXMLCoinEvaluator.cpp` (38 adições, 12 remoções). |
| Liberação de dados por ação no profiler | `codex/pre-pr/profiler-stats-lifetime` `bb32da5fc2041e56bf75e158c8df63d6bdb66243` | `lab/teste/profiler-stats-lifetime` `7821599d5ad66b2a6c4919fb830bb0ccf814c8d5` | Só `src/profiler/SoProfilerStats.cpp` (34 adições, 1 remoção). |

O temporário SCXML agora é validado/avaliado e clonado antes de trocar o valor antigo; a cópia permanece sob `unique_ptr` se a inserção lançar exceção. O PImpl do profiler agora chama `clearProfilingData()` no próprio destrutor, liberando seus ponteiros de ação mesmo quando a rotina de limpeza não foi chamada explicitamente. As duas branches pré-PR foram criadas a partir do master de destino, e cada cópia `lab/teste` contém somente o respectivo cherry-pick sobre o SHA fixo do lab. `git diff --check` passou nos quatro heads. Os demais 17 PRs do lab não entram nos diffs contra master dessas branches.

## Validação em RAM

Configuração comum em `/dev/shm/coin-<assunto>-{master,lab}`: `cmake -S <worktree> -B <diretório> -DCMAKE_BUILD_TYPE=Debug -DBUILD_SHARED_LIBS=ON -DCOIN_BUILD_TESTS=ON -DCOIN_BUILD_DOCUMENTATION=OFF -DCOIN_THREADSAFE=ON`; compilação `cmake --build <diretório> --parallel 8`. Testes completos com `xvfb-run -a -s '-screen 0 1280x1024x24 +extension GLX' env LIBGL_ALWAYS_SOFTWARE=1 COIN_GLX_PIXMAP_DIRECT_RENDERING=1 ctest --test-dir <diretório> --output-on-failure --parallel 4`.

| Contribuição | Regressão dirigida | Suíte master | Suíte lab |
| --- | --- | --- | --- |
| Temporário SCXML | `ScXMLCoinEvaluator_TestSuite/TemporaryReplacementFromStoredObject`: 1 teste, 7 verificações em master e lab | 78/78 | 105/105 |
| Lifetime do profiler | `SoProfilerStats_TestSuite/SoProfilerStats_releases_collected_action_data`: 1 teste, 1 verificação em master e lab | 78/78 | 105/105 |

Em ambas as branches pré-PR, o target `CoinTests` também foi compilado em `/dev/shm/coin-<assunto>-san` com `-fsanitize=address,undefined -fno-omit-frame-pointer` em C/C++ e `-fsanitize=address,undefined` no linker. Cada regressão dirigida passou com `ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1`: SCXML 1/7 e profiler 1/1, sem diagnóstico de ASan, UBSan ou LeakSanitizer. O teste do profiler cobre uma coleta real antes da destruição; o sanitizador confirma que os ponteiros coletados foram liberados nesse caminho.

Para `SbSequentialMap`, compilei um cliente mínimo com `g++ -std=c++11 -O2 -DNDEBUG` e inseri a chave `-1`: `inserted=1 size=1 visible=0`. Um microbenchmark de 100 mil inserções/consultas por tamanho, com 1, 4, 8 e 16 chaves e passos 1/100, alternou vitórias entre SequentialMap, `std::map` e `std::unordered_map`; por exemplo, com quatro chaves densas: 6,35 ms, 4,30 ms e 3,46 ms. Este ensaio pequeno não substitui perfil do aplicativo, mas não sustenta uma migração. Os IDs observados de ações comuns foram 284, 286, 292 e 294, sem sequência densa única.

A fonte de `SbName` foi compilada separadamente em `/dev/shm/coin-sbname-compact-audit` com Debug, testes e threads. `CoinTests` executou 335 casos/103.693 verificações sem falha, inclusive os testes adicionados para nomes de 70 mil bytes, 20 mil nomes e concorrência; `ctest --test-dir /dev/shm/coin-sbname-compact-audit --output-on-failure` passou 1/1. Esses testes verificam funcionamento normal, mas não exercitam a falha de `realloc`/`malloc` nem o limite dos índices; por isso o experimento permanece fora da fila de PR.

As cópias `lab/teste` ficam disponíveis para conferência e teste no Windows até o ciclo de publicação; depois devem ser eliminadas. Ainda faltam Windows e publicação das duas branches pré-PR.
