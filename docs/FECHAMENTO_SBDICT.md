# Fechamento pré-PR de SbDict

Em 6 de outubro de 2026, o escopo conhecido de `SbDict` foi concluído e revisado no Linux. A contribuição migra o backend para `cc_dict`, preserva as declarações e a representação histórica da classe, corrige autoatribuição e a ponte de callbacks, e fecha os contratos de cópia e listas. A branch está pronta para validação nativa Windows antes da publicação.

## Referências e dependência

| Referência | SHA |
| --- | --- |
| Master de referência | `674e74267df863dbaf50416c477bc7f918a826d8` |
| Pré-requisito fechado, `codex/pre-pr/cc-dict-complete` | `9b9237f13da3ab73492579e0ac7f06e2e8490a32` |
| Pré-PR `codex/pre-pr/sbdict-complete` | `b5ae0d4ff39f8783648b69b0c2503b1bb5516a69` |
| Lab fixo, preservado | `b27e37a6dd3ae941bcffcf480a377adc81e3fc27` |
| Snapshot temporário do lab com o pré-requisito cc_dict | `92e648c4daa975ab404989cfd2c0b9dfb432aba6` |
| Cópia testada, `lab/teste/sbdict-complete` | `e464ab0cb4fcdcb5d10d372b3aa6c981abfdf737` |

Worktrees: `/home/dikluwe/.codex/worktrees/sbdict-complete-work/coin` e `/home/dikluwe/.codex/worktrees/sbdict-complete-test/coin`.

A migração precisa do contrato de hash nulo e da proteção de exceções do cc_dict fechado, ausentes no lab fixo. Por isso, a cópia temporária recebeu primeiro esse pré-requisito identificado e depois apenas a contribuição SbDict. O lab fixo não foi alterado. O diff de SbDict sobre `92e648c4da` é idêntico ao diff da pré-PR sobre `9b9237f13d`, conferido com `cmp`; os dois deltas permanecem separados em commits.

## Correções e decisões

| Assunto conhecido | Fechamento |
| --- | --- |
| Backend obsoleto | SbDict usa cc_dict; os símbolos cc_hash permanecem na biblioteca para clientes externos |
| Autoatribuição | Guarda antes de liberar a tabela; preserva entradas e hash atual sem reconstrução |
| Cópia comum | Cópia rasa dos valores não proprietários; estruturas independentes; mantém a política histórica de usar o hash padrão |
| Cópia do hash personalizado | Não adotada como mudança de comportamento; contrato histórico explicitado e testado por contagem das chamadas ao hash |
| Hash trocado/nulo | Rehash preserva acesso e identidade; NULL restaura o padrão em tabela vazia ou preenchida |
| Exceção do hash | Propaga preservando função e cadeias anteriores durante rehash; a função deve ser estável e não modificar a tabela |
| Duas variantes de applyToAll | Leitura e remoção da entrada atual suportadas; demais mutações e destruição ficam fora do contrato; nenhuma ordem prometida |
| Ponte sem closure | Usa uma estrutura local contendo o ponteiro de função; remove a conversão não portátil de função para void* e suporta travessias aninhadas |
| makePList preenchido | Acrescenta após os prefixos existentes; mantém correspondência entre os pares acrescentados, sem prometer ordem |
| makePList sob falha | Exceção de append restaura comprimentos e conteúdo das duas listas; capacidades e buffers podem ter mudado |
| OOM obrigatório | Construção, construção na atribuição e inserção abortam com diagnóstico; não confundem falha com substituição |
| Chaves e exemplos LLP64 | Usa uintptr_t/SbDict::Key; corrige casts para unsigned long nos exemplos de SoTexture2 e ImageTexture |
| Documentação do tamanho | Corrige a referência a potência de dois para quantidade prima adequada |
| Compatibilidade | Header muda apenas comentários; mesmo ponteiro privado histórico, tamanho, alinhamento e símbolos públicos |

O membro privado continua declarado como `struct cc_hash *`, como no header antigo. Ele é um handle opaco: o fonte armazena o ponteiro do novo backend e só o dereferencia após convertê-lo de volta para seu tipo real `cc_dict *`. Assim, a migração não altera sequer a declaração histórica desse membro no header instalado.

A fachada de cc_hash sobre cc_dict e a troca por flat hash não foram incluídas. O fechamento independente de cc_hash já preserva sua API; a migração do consumidor SbDict é suficiente para este escopo, sem substituir também a representação da API C obsoleta.

## Diff próprio

Nove arquivos, apenas a contribuição de SbDict sobre seu pré-requisito:

- `include/Inventor/SbDict.h` — comentários de contratos; declarações preservadas;
- `src/base/SbDict.cpp` — backend, autoatribuição, callbacks e rollback das listas;
- `src/nodes/SoTexture2.cpp` e `src/vrml97/ImageTexture.cpp` — exemplos de chaves da largura de ponteiro;
- `testsuite/SbDictApiTest.cpp` — cliente público, cópia, callbacks, listas, hashes e modelo;
- `testsuite/SbDictDifferentialTest.cpp` — 20 mil operações comparadas nos três níveis e no modelo;
- `testsuite/SbDictListFailureTest.cpp` — oito pontos de falha em crescimento das listas e nova tentativa;
- `testsuite/SbDictOomTest.cpp` — implementação real com falhas obrigatórias injetadas;
- `testsuite/regressions/SbDict.cmake` — registro dos testes.

O teste diferencial compara operações comuns de inserção, substituição, consulta, remoção e clear com o hash padrão; não pressupõe ordem de iteração nem testa a troca de hash no cc_hash anterior da base. O teste público de SbDict cobre os hashes personalizados separadamente.

## Builds e testes em RAM

```bash
mkdir -p /dev/shm/coin-sbdict-20261006/compiler-tmp
export TMPDIR=/dev/shm/coin-sbdict-20261006/compiler-tmp

cmake -S /home/dikluwe/.codex/worktrees/sbdict-complete-work/coin \
  -B /dev/shm/coin-sbdict-20261006/pre-pr-release-static -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCOIN_BUILD_SHARED_LIBS=OFF \
  -DCOIN_BUILD_TESTS=ON -DCOIN_BUILD_DOCUMENTATION=OFF -DCOIN_THREADSAFE=OFF
cmake --build /dev/shm/coin-sbdict-20261006/pre-pr-release-static --parallel 8

cmake -S /home/dikluwe/.codex/worktrees/sbdict-complete-test/coin \
  -B /dev/shm/coin-sbdict-20261006/lab-debug-shared -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DCOIN_BUILD_SHARED_LIBS=ON \
  -DCOIN_BUILD_TESTS=ON -DCOIN_BUILD_DOCUMENTATION=OFF -DCOIN_THREADSAFE=OFF
cmake --build /dev/shm/coin-sbdict-20261006/lab-debug-shared --parallel 6

for sbdict_build in \
  /dev/shm/coin-sbdict-20261006/pre-pr-release-static \
  /dev/shm/coin-sbdict-20261006/lab-debug-shared; do
  ctest --test-dir "$sbdict_build" -R '(SbDict|CcDict|CcMemalloc)' --output-on-failure
  xvfb-run -a -s '-screen 0 1280x1024x24 +extension GLX' \
    env LIBGL_ALWAYS_SOFTWARE=1 COIN_GLX_PIXMAP_DIRECT_RENDERING=1 \
    ctest --test-dir "$sbdict_build" --output-on-failure --parallel 4
done
```

| Configuração | Direcionados | Conjunto |
| --- | --- | --- |
| Lab + pré-requisito cc_dict + SbDict, Debug compartilhado | 13/13 | **112/112** |
| Pré-PR sobre cc_dict, Release estático | 13/13 | **97/97** |

Sem falhas ou testes ignorados, incluindo gráficos. Os builds e temporários do compilador ficaram no tmpfs de `/dev/shm`.

## Sanitizadores e regressões anteriores

SbDict.cpp, dict.cpp e memalloc.cpp reais foram compilados com `-fsanitize=address,undefined,float-cast-overflow -fno-omit-frame-pointer -g -O1`. Passaram o cliente público/modelo, o teste de falha das listas e os quatro subprocessos de OOM obrigatório, com detecção de vazamentos e interrupção no primeiro erro. O restante da biblioteca ligada não foi inteiramente instrumentado. Os comandos completos estão no log de sanitizadores.

Os fontes anteriores de SbDict e cc_hash, também instrumentados, reproduziram **heap-use-after-free na autoatribuição**, em cc_hash_get_num_elements chamado por operator=. Essa falha esperada pertence à reprodução da versão anterior, não à suíte nova.

O teste das listas ligado à biblioteca anterior falhou porque uma lista já havia recebido um elemento quando a outra lançou. O mesmo teste na biblioteca nova passou, incluindo nova tentativa após o rollback e preservação das entradas do dicionário.

## Compatibilidade com o cliente anterior

O cliente público foi compilado em C++11, com `-Wall -Wextra -Werror`, usando o header SbDict.h de master, e executado contra a biblioteca nova. Passou em cópia, autoatribuição, chaves largas, callbacks, listas, hash e modelo. `static_assert` confirma tamanho e alinhamento de um ponteiro nas duas declarações. Removendo comentários e espaços, o header novo é igual ao anterior.

```bash
sbdict_source=/home/dikluwe/.codex/worktrees/sbdict-complete-test/coin
sbdict_build=/dev/shm/coin-sbdict-20261006/lab-debug-shared
sbdict_old_include=/dev/shm/coin-sbdict-20261006/old-include
mkdir -p "$sbdict_old_include/Inventor"
git show origin/master:include/Inventor/SbDict.h > "$sbdict_old_include/Inventor/SbDict.h"
c++ -std=c++11 -Wall -Wextra -Werror \
  -I"$sbdict_old_include" -I"$sbdict_source/include" -I"$sbdict_build/include" \
  "$sbdict_source/testsuite/SbDictApiTest.cpp" \
  -L"$sbdict_build/lib" -Wl,-rpath,"$sbdict_build/lib" -lCoin \
  -o /dev/shm/coin-sbdict-20261006/old-header-client
/dev/shm/coin-sbdict-20261006/old-header-client
```

`nm -D --defined-only` confirmou os mesmos **15 símbolos de SbDict e dez de cc_hash**. Essas verificações são do recorte de compatibilidade no Linux; a validação DLL/LLP64 ainda cabe à etapa Windows.

Logs: `/tmp/sbdict-pre-pr-{configure,build,focused,full}-20261006.log`, `/tmp/sbdict-lab-{configure,build,focused,full}-20261006.log`, `/tmp/sbdict-old-client-{build,run}-20261006.log`, `/tmp/sbdict-symbols-20261006.log`, `/tmp/sbdict-sanitizers-20261006.log` e `/tmp/sbdict-list-before-after-20261006.log`.

## Windows e publicação

Validar o head `b5ae0d4ff3` em Windows x64 compartilhado e estático, incluindo SbDictApi, SbDictDifferential, o runner e o conjunto disponível. O teste diferencial compila cc_dict no executável para dispensar exports privados da DLL. A injeção por interposição de new[] e fork/SIGABRT é POSIX e não foi executada no Windows.

Antes de publicar, o master de destino precisa oferecer o cc_dict fechado e seus pré-requisitos. Transportar somente o diff de SbDict, conferir dependências e repetir a validação nessa base. A branch pré-PR atual carrega a pilha de pré-requisitos; seu diff direto contra o master atual não é o diff próprio de SbDict. Nenhum push ou PR foi realizado.

A cópia temporária fica identificada até concluir Windows/publicação; depois, arquivar o worktree e eliminar `lab/teste/sbdict-complete`. As branches fontes da migração e da correção inicial permanecem disponíveis até concluir a conferência e o encerramento das versões substituídas.
