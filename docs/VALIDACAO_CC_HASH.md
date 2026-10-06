# Validação do complemento da API cc_hash

Este registro conserva o candidato preliminar. O [fechamento de cc_hash](FECHAMENTO_CC_HASH.md), em `codex/pre-pr/cc-hash-complete` (`c5dc4c643a`), conclui primos/capacidade e cliente C, separa as mudanças próprias de SbDict e é a referência atual para Windows.

O complemento de `cc_hash` passou no conjunto de PRs do lab em 6 de outubro de 2026. A contribuição preserva as entradas ao trocar o hash, protege as cadeias contra exceções de callbacks, permite remover a entrada atual durante a travessia e acrescenta regressões públicas de `SbDict` e `cc_hash`.

## Referências usadas

| Referência | SHA |
| --- | --- |
| Master upstream conferido | `674e74267df863dbaf50416c477bc7f918a826d8` |
| Base fixa `lab/open-prs-integration` | `b27e37a6dd3ae941bcffcf480a377adc81e3fc27` |
| Fonte anterior `codex/cc-hash-hardening-followup` | `a60f2bea89` |
| Base do delta extraído da fonte anterior | `fb4af4497c` |
| Branch de trabalho `codex/work/cc-hash-api` | `ebae1bd6530e7c0d33fb47c60a94df667754c242` |
| Cópia temporária `lab/teste/cc-hash-api` testada | `2dcf47b70398e7f4b4493237678c37261cb8bf51` |

A composição da base fixa é a dos 17 heads registrados em [Controle atual de branches](CONTROLE_BRANCHES.md#composição-atual-do-lab). A cópia temporária começou exatamente nessa base e recebeu os dois commits da contribuição, por cherry-pick. As árvores finais das branches de trabalho e de teste são iguais. A referência da base fixa permaneceu inalterada.

Worktree de trabalho: `/home/dikluwe/.codex/worktrees/cc-hash-api-work/coin`.

Worktree da cópia temporária: `/home/dikluwe/.codex/worktrees/cc-hash-api-test/coin`.

## Contribuição incorporada

O diff sobre o lab contém somente:

- `include/Inventor/C/base/hash.h`;
- `src/base/hash.cpp`;
- `src/base/SbDict.cpp`;
- `testsuite/CcHashApiTest.cpp`;
- `testsuite/CcHashOomTest.cpp`;
- `testsuite/regressions/CcHashApi.cmake`.

O delta foi extraído depois dos commits de OOM dos consumidores na branch anterior. Fontes, GL, scheduler, worker pool, alocador, `cc_dict` e `SbHash` dos outros PRs não entram na contribuição nova.

O registro antigo do teste em `testsuite/CMakeLists.txt` não se aplicava ao layout do lab. A correção foi feita na branch de trabalho: o alvo agora está em um módulo descoberto por `testsuite/RegressionTests.cmake`. O commit `0801ab7455` contém o complemento funcional e `ebae1bd653` contém a adaptação do registro. A cópia temporária recebeu os equivalentes `725e87caa2` e `2dcf47b703`.

## Compilação em RAM

O build e os temporários do compilador ficam no filesystem `tmpfs` de `/dev/shm`, conforme solicitado. Os logs de configuração, compilação e testes ficam em `/tmp`, para conservar os resultados independentemente do build em RAM.

Configuração: Debug, biblioteca compartilhada, renderer OpenGL legado habilitado, `COIN_THREADSAFE=OFF`, testes habilitados e documentação desabilitada. É a configuração do lab fixo registrada no controle.

```bash
mkdir -p /dev/shm/coin-cc-hash-20261006/compiler-tmp

TMPDIR=/dev/shm/coin-cc-hash-20261006/compiler-tmp \
cmake -S /home/dikluwe/.codex/worktrees/cc-hash-api-test/coin \
  -B /dev/shm/coin-cc-hash-20261006/lab-debug -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DCOIN_BUILD_TESTS=ON \
  -DCOIN_BUILD_DOCUMENTATION=OFF -DCOIN_THREADSAFE=OFF

TMPDIR=/dev/shm/coin-cc-hash-20261006/compiler-tmp \
cmake --build /dev/shm/coin-cc-hash-20261006/lab-debug --parallel 8
```

Configuração e compilação concluídas, incluindo todos os alvos da suíte.

## Testes direcionados e do conjunto

```bash
TMPDIR=/dev/shm/coin-cc-hash-20261006/compiler-tmp \
ctest --test-dir /dev/shm/coin-cc-hash-20261006/lab-debug \
  -R '(CcHash|CcDict|CcMemalloc|CcSbHash)' --output-on-failure

TMPDIR=/dev/shm/coin-cc-hash-20261006/compiler-tmp \
xvfb-run -a -s '-screen 0 1280x1024x24 +extension GLX' \
  env LIBGL_ALWAYS_SOFTWARE=1 COIN_GLX_PIXMAP_DIRECT_RENDERING=1 \
  ctest --test-dir /dev/shm/coin-cc-hash-20261006/lab-debug \
    --output-on-failure --parallel 4
```

| Execução | Resultado |
| --- | --- |
| Regressões direcionadas | 9 aprovadas, zero falhas |
| Conjunto completo | 106 aprovadas, zero falhas, nenhum teste ignorado |
| Whitespace do diff | `git diff --check` aprovado |
| Identidade entre trabalho e cópia | `git diff --exit-code codex/work/cc-hash-api lab/teste/cc-hash-api` aprovado |

Logs: `/tmp/cc-hash-lab-configure-20261006.log`, `/tmp/cc-hash-lab-build-20261006.log`, `/tmp/cc-hash-lab-focused-20261006.log` e `/tmp/cc-hash-lab-full-20261006.log`.

## Gate de publicação

A branch de trabalho nasceu do lab e carrega os PRs da base em seu histórico. Ela não deve ser enviada como PR diretamente contra master. Preparar a branch de PR com apenas a contribuição acima, sobre o master de destino que ofereça os pré-requisitos necessários, e repetir a compilação e os testes nessa base.

O master conferido ainda não oferece `src/base/oomp.h`, `cc_memalloc_construct_aligned` nem o dispatcher modular de regressões. Esses elementos estão nos PRs #770, #772 e #771, respectivamente. O complemento pressupõe o comportamento de OOM já presente no conjunto. A validação em master para publicação fica pendente da integração desses pré-requisitos ou da definição de um PR dependente com base e diff explícitos.

A cópia temporária permanece identificada para este ciclo. Após concluir a validação da branch de PR e a publicação, arquivar seu worktree e eliminar `lab/teste/cc-hash-api`, conservando o resultado na branch de trabalho ou de PR e neste registro. Uma mudança no lab ou na contribuição exige nova validação dos SHAs resultantes.
