# Fechamento pré-PR de cc_hash

Em 6 de outubro de 2026, o escopo conhecido de `cc_hash` foi concluído e revisado no Linux. A branch pré-PR está pronta para validação Windows. Este registro substitui o candidato preliminar de [validação da API](VALIDACAO_CC_HASH.md) como referência do fechamento.

## Branches e dependências

| Referência | SHA |
| --- | --- |
| Master de referência | `674e74267df863dbaf50416c477bc7f918a826d8` |
| Base pré-PR `codex/pre-pr/bump-static-testfix` | `c9c9234768b47e5889bbc657c707c9da5a7f6bb2` |
| Branch `codex/pre-pr/cc-hash-complete` | `c5dc4c643a48a7f1bf38fe32ac9da7cb24be62a8` |
| Lab fixo, preservado | `b27e37a6dd3ae941bcffcf480a377adc81e3fc27` |
| Cópia `lab/teste/cc-hash-complete` | `0505c1caed9e378195badf3f3ff3d26ef1fbca09` |

Worktrees: `/home/dikluwe/.codex/worktrees/cc-hash-complete-work/coin` e `/home/dikluwe/.codex/worktrees/cc-hash-complete-test/coin`.

A base pré-PR contém os pré-requisitos publicados de OOM/alocador e o fixture estático separado. A contribuição nova não carrega os demais PRs do lab por esse caminho. O diff aplicado na cópia do lab é idêntico ao diff sobre a base pré-PR, conferido com `cmp`; `git diff --check` passou.

O helper `src/primep.h` é byte a byte igual ao já validado em `codex/pre-pr/cc-dict-complete`. É um único helper compartilhado pelos dois componentes. Na publicação, integrar esse arquivo uma vez; ao transportar cc_hash para o master que já contenha o complemento de cc_dict, sua adição deixa de fazer parte do diff. Preservar os heads validados para a passagem Windows.

## Escopo fechado

| Correção ou melhoria conhecida | Resultado |
| --- | --- |
| Troca de hash com entradas existentes | Reindexa preservando nós, valores e contagem; impede perda de acesso e duplicatas |
| Hash nulo | Restaura o hash padrão em tabela vazia ou preenchida |
| Exceção durante rehash/resize | Calcula destinos antes de alterar links; libera temporários e conserva a tabela anterior |
| Falha de buckets ou plano de destinos | Conserva hash e links; falha de crescimento opcional permite nova tentativa |
| Relink e pool | Preserva identidade dos nós; não recria entradas durante crescimento |
| Números e primos | Normaliza fatores não finitos/não positivos, satura limiar, escolhe primo inicial exato e cresce geometricamente |
| Capacidade esgotada | Pedido inicial acima do maior primo e contador de elementos esgotado seguem a política de diagnóstico/abort; crescimento sem primo maior mantém a tabela |
| Resultado público de put | TRUE para inserir, FALSE para substituir; falha obrigatória não é confundida com substituição |
| apply | Suporta leitura e remoção da entrada atual; demais mutações e destruição durante a travessia ficam fora do contrato |
| Estatísticas vazias | Média zero, sem divisão por zero |
| Matriz funcional | Zero, valor nulo, chaves largas, colisões, remoção em posições variadas, clear/reúso e 20 mil operações contra modelo |
| Cliente C e compatibilidade | Teste compilado como C; cliente com header antigo passa na nova biblioteca; mesmas declarações, estrutura privada e dez símbolos públicos |

O hash padrão não exige plano auxiliar. Hash personalizado pode usar um índice por entrada temporariamente; sua função deve ser estável e não modificar a tabela. Uma exceção durante a consulta inicial não insere a chave; durante crescimento opcional, a chave já inserida permanece na tabela válida. Essas garantias estão documentadas no header e no fonte.

As ideias de substituir a API obsoleta por open addressing ou uma fachada sobre cc_dict não foram adotadas neste fechamento: preservamos a implementação e os contratos externos, com o relink já corrigido. A migração do consumidor SbDict tem contribuição própria; não é necessário mudar também o backend da API C para corrigir seus defeitos conhecidos.

## Contribuição isolada

O diff contém seis arquivos:

- `include/Inventor/C/base/hash.h` — comentários de contrato e compatibilidade;
- `src/base/hash.cpp` — correções e estatísticas;
- `src/primep.h` — helper compartilhado já validado;
- `testsuite/CcHashApiTest.c` — cliente C externo e modelo;
- `testsuite/CcHashOomTest.cpp` — implementação real com falhas determinísticas e exceções;
- `testsuite/regressions/CcHashApi.cmake` — registro do cliente C.

`src/base/SbDict.cpp` e `src/base/hashp.h` não mudaram sobre a base pré-PR. Autoatribuição, testes específicos e migração de SbDict permanecem nas branches anteriores para o próximo fechamento. O header público mudou somente comentários, confirmado removendo comentários e espaços e comparando com o header de master.

## Compilação e testes em RAM

```bash
mkdir -p /dev/shm/coin-cc-hash-complete-20261006/compiler-tmp
export TMPDIR=/dev/shm/coin-cc-hash-complete-20261006/compiler-tmp

cmake -S /home/dikluwe/.codex/worktrees/cc-hash-complete-test/coin \
  -B /dev/shm/coin-cc-hash-complete-20261006/lab-debug-shared -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DCOIN_BUILD_SHARED_LIBS=ON \
  -DCOIN_BUILD_TESTS=ON -DCOIN_BUILD_DOCUMENTATION=OFF -DCOIN_THREADSAFE=OFF
cmake --build /dev/shm/coin-cc-hash-complete-20261006/lab-debug-shared --parallel 8

cmake -S /home/dikluwe/.codex/worktrees/cc-hash-complete-work/coin \
  -B /dev/shm/coin-cc-hash-complete-20261006/pre-pr-release-static -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCOIN_BUILD_SHARED_LIBS=OFF \
  -DCOIN_BUILD_TESTS=ON -DCOIN_BUILD_DOCUMENTATION=OFF -DCOIN_THREADSAFE=OFF
cmake --build /dev/shm/coin-cc-hash-complete-20261006/pre-pr-release-static --parallel 6

for hash_build in \
  /dev/shm/coin-cc-hash-complete-20261006/lab-debug-shared \
  /dev/shm/coin-cc-hash-complete-20261006/pre-pr-release-static; do
  ctest --test-dir "$hash_build" \
    -R '(CcHash|CcDict|CcMemalloc|CcSbHash)' --output-on-failure
  xvfb-run -a -s '-screen 0 1280x1024x24 +extension GLX' \
    env LIBGL_ALWAYS_SOFTWARE=1 COIN_GLX_PIXMAP_DIRECT_RENDERING=1 \
    ctest --test-dir "$hash_build" --output-on-failure --parallel 4
done
```

| Configuração | Direcionados | Conjunto |
| --- | --- | --- |
| Lab Debug compartilhado | 9/9 | **106/106** |
| Pré-PR Release estático | 9/9 | **91/91** |

Sem falhas ou testes ignorados, incluindo os gráficos. Builds e temporários ficaram no `tmpfs` de `/dev/shm`.

Também foram instrumentados `hash.cpp` e `memalloc.cpp` reais com `-fsanitize=address,undefined,float-cast-overflow -fno-omit-frame-pointer -g -O1`. O cliente C/modelo e o teste de falhas/exceções passaram com detecção de vazamentos e interrupção no primeiro erro. O restante da biblioteca ligada não foi inteiramente instrumentado. Os comandos completos dessa instrumentação estão no log de sanitizadores.

Os seis subprocessos de falha obrigatória encerraram por SIGABRT conforme o contrato: estrutura, buckets, alocador, entrada, capacidade inicial e contador de elementos. As falhas opcionais e exceções foram verificadas no processo principal, incluindo rollback dos links e preservação dos nós.

## Cliente anterior e símbolos

O mesmo fonte C foi compilado com o header de `origin/master`, em C99 com `-Wall -Wextra -Werror`, e ligado à biblioteca nova. Passou em todos os casos. Ao carregar a biblioteca anterior do lab via `LD_LIBRARY_PATH`, o mesmo executável falhou ao consultar as entradas após trocar o hash; ao carregar a nova, passou. Isso demonstra a regressão e a compatibilidade do cliente com as declarações antigas.

```bash
hash_source=/home/dikluwe/.codex/worktrees/cc-hash-complete-test/coin
hash_build=/dev/shm/coin-cc-hash-complete-20261006/lab-debug-shared
hash_old_include=/dev/shm/coin-cc-hash-complete-20261006/old-include
mkdir -p "$hash_old_include/Inventor/C/base"
git show origin/master:include/Inventor/C/base/hash.h \
  > "$hash_old_include/Inventor/C/base/hash.h"
cc -std=c99 -Wall -Wextra -Werror \
  -I"$hash_old_include" -I"$hash_source/include" -I"$hash_build/include" \
  "$hash_source/testsuite/CcHashApiTest.c" \
  -L"$hash_build/lib" -Wl,-rpath,"$hash_build/lib" -lCoin \
  -o /dev/shm/coin-cc-hash-complete-20261006/old-header-client
/dev/shm/coin-cc-hash-complete-20261006/old-header-client
LD_LIBRARY_PATH=/dev/shm/coin-cc-dict-20261006/lab-debug/lib \
  /dev/shm/coin-cc-hash-complete-20261006/old-header-client
# A última execução deve falhar na consulta após a troca do hash.
```

`nm -D --defined-only` confirmou os mesmos dez símbolos `cc_hash_*`, sem adição ou remoção. A estrutura privada `hashp.h` também foi preservada. Essas conferências são do recorte cc_hash no Linux; validação DLL/LLP64 permanece para Windows.

Logs: `/tmp/cc-hash-complete-lab-{configure,build,focused,full}-20261006.log`, `/tmp/cc-hash-complete-pre-pr-{configure,build,focused,full}-20261006.log`, `/tmp/cc-hash-complete-old-client-{build,run}-20261006.log`, `/tmp/cc-hash-complete-before-after-20261006.log`, `/tmp/cc-hash-complete-symbols-20261006.log` e `/tmp/cc-hash-complete-sanitizers-20261006.log`.

## Passagem Windows e publicação

Validar o head `c5dc4c643a` em Windows x64 compartilhado e estático, incluindo `CcHashApi`, o runner e o conjunto disponível. A injeção com fork/SIGABRT é POSIX; não foi executada no Windows nesta etapa.

Antes de publicar, transportar somente a contribuição para o master de destino com os pré-requisitos disponíveis, conferir o helper compartilhado e repetir a validação nessa base. O master atual ainda não contém todos os pré-requisitos; o diff direto contra ele inclui a pilha anterior. Nenhum push ou PR foi feito.

A cópia temporária continua identificada até concluir Windows/publicação. Depois, arquivar o worktree e eliminar `lab/teste/cc-hash-complete`, conservando a contribuição e o registro. O lab fixo foi preservado. As branches antigas de cc_hash ainda contêm material de SbDict a transportar antes de encerrá-las.
