# Fechamento Linux de apoio e consumidores associados

Auditoria em 6 de outubro de 2026. Destino `master` `674e74267df863dbaf50416c477bc7f918a826d8`; base integrada fixa `lab/open-prs-integration` `b27e37a6dd3ae941bcffcf480a377adc81e3fc27` (master e 17 PRs abertos registrados em [CONTROLE_BRANCHES.md](CONTROLE_BRANCHES.md)). Nenhuma das bases foi alterada. Nenhuma candidata foi publicada ou enviada ao remoto. Cada branch pré-PR deriva diretamente do master, e sua cópia `lab/teste` deriva diretamente do lab fixo e contém somente os commits daquela candidata.

Os patch-ids estáveis dos deltas pré-PR e lab coincidem em sete pares. Em `cc_list`, o contexto de `cc_list_insert()` difere porque o lab já contém a correção de OOM (`coin_oom_abort`); a comparação das 74 linhas adicionadas/removidas confirma que a contribuição de limites é idêntica nas duas bases.

## Destino das fontes

| Fonte auditada | Conclusão |
| --- | --- |
| `codex/cc-list-hardening` `42815f7385` | Crescimento transacional, falhas de construção/inserção e API `try_*` já aparecem no lab pelos PRs de OOM. O último commit troca o retorno nulo do construtor padrão por `abort()`, diferente do contrato C integrado, e não foi transportado. O delta ainda necessário no master era validar índices negativos/fora de faixa sem corromper a lista; isolado em `codex/pre-pr/cc-list-bounds`. |
| `codex/improve-sblist` `e506a1c4fe` | A fonte combina estudo e regressões de `SbPList`, estudo de callback e mudança de `SbList`. O crescimento seguro de ambas as listas e remoção de item ausente já estão no master. Os contratos de índice e falhas de valor restantes foram separados em `codex/pre-pr/sblist-value-bounds` e `codex/pre-pr/sbplist-bounds`, preservando os testes já integrados. A regressão de cópia/destruição de `SoCallbackList` foi isolada em `codex/pre-pr/callback-list-copy-stress`, sem mudança de produção. |
| `codex/cc-test-audit` `95458242d7` | Não é candidato de dict/hash: o último commit remove testes antigos de dict/hash, heap, list e worker e ajusta o scheduler. Os testes atuais de dict/hash nos fechamentos do grupo cobrem os contratos sem essa poda. As mudanças de concorrência de worker/scheduler pertencem ao estudo de threads, não a uma PR de mapas. Fonte preservada. |
| `codex/work/bump-cache-ready-path` `bb6158a01e` | Delta de um arquivo isolado em `codex/pre-pr/bump-cache-ready-path`: retorna diretamente quando o cache está `READY`, evitando construir/zerar o buffer de erro em cada consulta bem-sucedida. |
| `codex/work/bump-cache-diagnostics-memory` `c098efdfb6` | Delta isolado em `codex/pre-pr/bump-cache-diagnostics-memory`: diagnóstico curto inline de 64 bytes e texto completo, limitado a 512 bytes, alocado só após erro; conserva diagnóstico abreviado se a alocação falhar, sem interromper rollback GL. |
| `codex/work/bump-shared-program-pool` `191a685710` | O pré-requisito `c6f6b317aa` de `make_shared` já está no master/lab e foi descartado no cherry-pick. Somente o pool de programas validados foi isolado em `codex/pre-pr/bump-shared-programs`. O cache por renderizador mantém token, callback, sensor e diagnóstico próprios; entradas do pool são separadas por id de contexto e família diffuse/specular. |
| `codex/work/glglue-lifetime-audit` `5d28cf8e95` | Já fechado em `codex/pre-pr/glglue-lifetime-complete`; sem novo delta nesta auditoria. |
| `codex/work/sbheap-cancel-documentation` `83925a4743` | Contrato confirmado no código: callback amostrado a cada 32 nós pais; cancelamento interrompe o laço antes de processar os demais, deixando possível ordem parcial. Documentação isolada em `codex/pre-pr/sbheap-cancel-contract`. |

## Branches pré-PR e cópias de validação

| Contribuição | Head sobre master | Head sobre lab fixo | Diff exclusivo |
| --- | --- | --- | --- |
| Limites `cc_list` | `codex/pre-pr/cc-list-bounds` `ea8e90543611087e1c85fa468c671175343c323d` | `lab/teste/cc-list-bounds` `820e8bb99f5dfc827026e1eb6c6272f51b8095d0` | `src/base/list.cpp` |
| Valor/limites `SbList` | `codex/pre-pr/sblist-value-bounds` `af3ae606439805f1cc34fdebea3a40f6c26cf2e9` | `lab/teste/sblist-value-bounds` `d3f033ed24bcd0d2833707a59452a598ca6b7657` | `include/Inventor/lists/SbList.h`, `src/lists/SbList.cpp`, expectativa do filtro de testes |
| Limites `SbPList` | `codex/pre-pr/sbplist-bounds` `969a37276e931f20cd56c7d2f8c9f97f9113d8c0` | `lab/teste/sbplist-bounds` `a63d490810adddfd2d32da2e5785862ac0dac410` | `include/Inventor/lists/SbPList.h`, `src/lists/SbPList.cpp` |
| Cache bump pronto | `codex/pre-pr/bump-cache-ready-path` `9d9c16f53af802d5af165a810f69144b38cc814c` | `lab/teste/bump-cache-ready-path` `7e6271af6857a954e46eef064bd31ca0ad51d16f` | `src/shapenodes/soshape_bumprender.cpp` |
| Diagnóstico bump | `codex/pre-pr/bump-cache-diagnostics-memory` `1a373951dd4140ce7e42907a721b82154b0b0447` | `lab/teste/bump-cache-diagnostics-memory` `c6fc3011167709b6778ee842173cb648d61e50c9` | implementação bump e `testsuite/bumprender/FailureTest.cpp` |
| Pool bump | `codex/pre-pr/bump-shared-programs` `9b772453d41cf7a38595db480c91e6251becb148` | `lab/teste/bump-shared-programs` `623d9daf2f2b3aa7a8a7e8cbe54a266e0bfcf256` | implementação bump, teste de falhas/lifetime e README do teste |
| Cancelamento `SbHeap` | `codex/pre-pr/sbheap-cancel-contract` `6436c4d1936b17fbd81978e10048b6443fb1b100` | `lab/teste/sbheap-cancel-contract` `0a599812df21517612c997f78d4f6459478b0040` | comentário em `src/base/SbHeap.cpp` |
| Cópia de `SoCallbackList` | `codex/pre-pr/callback-list-copy-stress` `23ccce4b84b1adea848518f033ca35319b7e1234` | `lab/teste/callback-list-copy-stress` `8fff68f2211df08f6daccfe6c94e0e85e6e02b1d` | teste em `src/lists/SoCallbackList.cpp` |

## Comandos e resultados Linux

Para cada par, configuração em `/dev/shm/coin-<assunto>-{master,lab}` com `cmake -S <worktree> -B <build> -DCMAKE_BUILD_TYPE=Debug -DCOIN_BUILD_SHARED_LIBS=ON -DCOIN_BUILD_TESTS=ON -DCOIN_BUILD_DOCUMENTATION=OFF -DCOIN_THREADSAFE=ON`, seguida de `cmake --build <build> --parallel 6` (ou 8). Suíte completa: `xvfb-run -a -s '-screen 0 1280x1024x24 +extension GLX' env LIBGL_ALWAYS_SOFTWARE=1 COIN_GLX_PIXMAP_DIRECT_RENDERING=1 ctest --test-dir <build> --output-on-failure --parallel 4`.

| Contribuição | Master | Lab | Regressão dirigida |
| --- | --- | --- | --- |
| `cc_list` | 78/78 | 105/105 | `list_TestSuite`: 2 testes, 7 verificações |
| `SbList` | 78/78 | 105/105 | `SbList_TestSuite`: 19 testes, 17.172 verificações em master e lab |
| `SbPList` | 78/78 | 105/105 | `SbPList_TestSuite`: 19 testes, 54.872 verificações |
| Bump pronto | 78/78 | 105/105 | `BumpProgramFailuresAndLifetime` e `BumpProgramGLX` incluídos |
| Diagnóstico bump | 78/78 | 105/105 | Falha de alocação, mensagem curta/longa e rollback incluídos no teste bump |
| Pool bump | 78/78 | 105/105 | Compartilhamento, contextos, último owner, deferred e concorrência incluídos no teste bump |
| Contrato `SbHeap` | 78/78 | 105/105 | Código `buildHeap()` conferido contra o texto; alteração só documental |
| Cópia `SoCallbackList` | 78/78 | 105/105 | `SoCallbackList_TestSuite/owned_callback_state_survives_copy_destruction_stress`: 1 teste, 257 verificações nas duas bases |

A regressão de callback repete 128 vezes a cópia de uma lista com estado próprio, destrói a seleção original, despacha o callback tipado e depois cria uma lista comum para exercitar o reaproveitamento de endereço no registro lateral. Em ambos os heads, `git diff --name-only <base> <head>` listou somente `src/lists/SoCallbackList.cpp`; os patch-ids estáveis coincidem (`947873edcc6bc0ab5b9aa8959fbde75a7e7e9d5e`). Os merge-bases foram exatamente `674e74267df863dbaf50416c477bc7f918a826d8` e `b27e37a6dd3ae941bcffcf480a377adc81e3fc27`. Os comandos de build acima foram aplicados em `/dev/shm/coin-callback-copy-stress-{master,lab}`; a regressão dirigida foi executada com `bin/CoinTests --run_test=SoCallbackList_TestSuite/owned_callback_state_survives_copy_destruction_stress --report_level=short`, seguida da suíte completa com `ctest` sob Xvfb. Logs: `/dev/shm/coin-callback-copy-stress-{master,lab}-ctest.log`.

Na integração de `SbList`, um merge seletivo manteve o `delete[]` manual no `catch` junto ao novo `unique_ptr`; os testes dirigidos de falha em `fit()` expuseram a dupla liberação. Removi o trecho legado, recompilei e repeti a suíte. A adição de 14 testes mudou a contagem do filtro `CoinTestsFilterSuite` de 5 para 19; atualizei sua expectativa e repeti o conjunto master até 78/78. Os resultados finais acima são posteriores às correções.

`SbList` também foi compilado em `/dev/shm/coin-sblist-value-bounds-san` com `-fsanitize=address,undefined -fno-omit-frame-pointer` e linker correspondente. `ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 /dev/shm/coin-sblist-value-bounds-san/bin/CoinTests --run_test=SbList_TestSuite --report_level=short` passou 19 testes/17.172 verificações sem diagnóstico. O harness de diagnóstico bump, instrumentado da mesma forma e ligado ao libCoin normal, também passou todos os casos, incluindo mensagens longas, falha de alocação e rollback GL.

`SoCallbackList` foi compilado em `/dev/shm/coin-callback-copy-stress-san` com `-fsanitize=address,undefined -fno-omit-frame-pointer` para C/C++ e o linker, usando `cmake --build <build> --target CoinTests --parallel 8`. `env ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 /dev/shm/coin-callback-copy-stress-san/bin/CoinTests --run_test=SoCallbackList_TestSuite/owned_callback_state_survives_copy_destruction_stress --report_level=short` passou 1 teste/257 verificações sem diagnóstico de ASan, UBSan ou LeakSanitizer. Configuração e build constam de `/dev/shm/coin-callback-copy-stress-san-{configure,build}.log`.

### Release e biblioteca estática das listas

As três branches de listas passaram **78/78** em Release compartilhada. A primeira configuração usou `-DBUILD_SHARED_LIBS=OFF`, que o projeto ignora; `CMakeCache.txt` mostrou `COIN_BUILD_SHARED_LIBS=ON`. Reconfigurei os mesmos builds em RAM com `-DCMAKE_BUILD_TYPE=Release -DCOIN_BUILD_SHARED_LIBS=OFF -DCOIN_BUILD_TESTS=ON -DCOIN_BUILD_DOCUMENTATION=OFF -DCOIN_THREADSAFE=ON` e confirmei `libCoin.a` no comando de link de `CoinTests`.

O link estático dos dois harnesses bump encontrou o `bumphack` global duplicado, problema do fixture já isolado em `codex/pre-pr/bump-fixture-static` `55dbefa033`. Os demais 76 testes passaram em cada branch. Apliquei **temporariamente** apenas o patch desse fixture a `testsuite/bumprender/TestAdapter.h` de cada worktree, compilei `CoinBumpProgramTest` e `CoinBumpGLXTest` com `cmake --build <build> --target CoinBumpProgramTest CoinBumpGLXTest --parallel 8` e restaurei o arquivo por `git restore`. Os três worktrees ficaram limpos e os diffs pré-PR continuam exclusivos. Com os executáveis corrigidos, `xvfb-run ... ctest --test-dir <build> --output-on-failure --parallel 4` passou **78/78** em cada branch estática.

Para o pool bump, o harness de `FailureTest.cpp` foi compilado também com `g++ -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer` e ligado à `libCoin` normal do build master. `ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 /dev/shm/coin-bump-shared-programs-harness-san` passou todos os casos de falha, contexto, último owner, concorrência e reentrada, sem diagnóstico. Um build em que tanto o harness quanto `libCoin` foram instrumentados sinalizou a duplicação ODR do global `bumphack`, porque o harness inclui a implementação em outra classe privada; com essa verificação ODR desativada, o mesmo conjunto também passou. A execução do harness instrumentado isoladamente mantém a verificação ODR padrão ativa e é a evidência principal dos sanitizadores.

No mock, dois renderizadores no mesmo contexto produziram **três uploads** para a família especular compartilhada, contra seis sem o pool; a família difusa adicionou dois uploads, usados pelos dois renderizadores. O teste confirmou liberação somente após o último owner e novo upload após destruição do contexto. Isto mede nomes GL no cenário coberto, não tempo de renderização real.

Uma versão de medição do mesmo harness, compilada com `g++ -O2 -DNDEBUG`, avaliou `sizeof` na plataforma Linux x86-64: `soshape_bump_program_error` caiu de **536 para 104 bytes** e `ProgramCache::Context`, que contém dois diagnósticos, de **1.104 para 240 bytes**. O texto longo é alocado somente após erro; o teste de falha de alocação confirma a mensagem curta e o rollback.

### Medição do caminho pronto

Um benchmark local usou o mock de `testsuite/bumprender/FailureTest.cpp`, com um renderizador aquecido, 500 mil consultas prontas por repetição, cinco repetições alternando base e candidata. Compilação C++11 `-O2 -DNDEBUG` contra os mesmos headers/libs dos builds RAM. Medianas das duas rodadas alternadas: aproximadamente **42 ms na base** e **31 ms na candidata** por 500 mil consultas, com o mesmo checksum. Esta medição cobre o caminho do cache sem custo de GPU; não prevê ganho de frame inteiro.

Os três candidatos bump alteram o mesmo arquivo e podem exigir resolução de conflito ao serem transportados juntos; cada diff e teste acima é independente sobre o master e o lab fixos. Antes de publicação, transportar cada PR isoladamente ao master de destino e repetir o conjunto após qualquer rebase. Windows e publicação seguem pendentes; as cópias `lab/teste` serão eliminadas depois desse ciclo.
