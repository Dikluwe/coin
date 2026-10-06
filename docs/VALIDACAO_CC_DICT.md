# Fechamento pré-PR de cc_dict

Em 6 de outubro de 2026, o escopo conhecido de `cc_dict` foi concluído e revisado no Linux. As correções e melhorias registradas nos estudos de `cc_dict`, na revisão do #726, na rodada de OOM e nos candidatos de primos e migração de `SbDict` têm destino explícito abaixo. A próxima etapa é a validação nativa no Windows antes da publicação.

## Branches e referências

| Referência | SHA |
| --- | --- |
| Master de referência | `674e74267df863dbaf50416c477bc7f918a826d8` |
| Lab fixo, preservado | `b27e37a6dd3ae941bcffcf480a377adc81e3fc27` |
| Pré-requisitos publicados, `codex/cc-oom-consumers` | `6a169aaac4725014cddd0bd7665224ccd8950a67` |
| Fixture estático separado, `codex/pre-pr/bump-static-testfix` | `c9c9234768b47e5889bbc657c707c9da5a7f6bb2` |
| Branch pronta para Windows, `codex/pre-pr/cc-dict-complete` | `9b9237f13da3ab73492579e0ac7f06e2e8490a32` |
| Cópia de validação, `lab/teste/cc-dict-complete` | `44eedede9cd8e4b1d9d143a98378665373ab0d03` |

Worktree pré-PR: `/home/dikluwe/.codex/worktrees/cc-dict-complete-work/coin`.

Worktree de teste: `/home/dikluwe/.codex/worktrees/cc-dict-complete-test/coin`.

A composição dos 17 PRs do lab está no [controle de branches](CONTROLE_BRANCHES.md#composição-atual-do-lab). A cópia recebeu somente o complemento de `cc_dict`. O patch dos cinco arquivos é idêntico ao da branch pré-PR sobre seu pré-requisito separado; a comparação dos diffs com `cmp` passou.

## Encerramento do escopo conhecido

| Assunto imaginado e registrado | Decisão e implementação |
| --- | --- |
| Reindexar ao trocar hash e estatísticas vazias | Já integrado pelo #726; cobertura mantida |
| Resize por relink, identidade dos nós e ausência de crescimento recursivo | #769 no lab; cobertura de hash distribuído e colisão total |
| Falha opcional de buckets no resize | Conserva a tabela e a chave recém-inserida; próxima inserção pode tentar crescer |
| Fator de carga enorme, não finito e não positivo | #771: normalização e saturação do limiar; testes numéricos mantidos e caso subnormal acrescentado |
| Falha de construção e alocação de entrada | #770/#771: limpeza parcial e resultado explícito; testes de estrutura, buckets, pool e entrada |
| Inserção, substituição e OOM | `cc_dict_try_put` distingue três resultados; retorno histórico de `cc_dict_put` preservado |
| Consumidores sob OOM | #771/#772 tratam os caminhos ligados ao dicionário, propagando erro ou encerrando com diagnóstico segundo a política definida |
| Remover durante `apply` | Remoção da entrada atual suportada; inserir, limpar, rehash e destruir durante o callback ficam fora do contrato, conforme uso existente |
| Exceção de hash durante rehash/resize | Plano de destinos calculado antes de alterar links; exceção libera temporários, propaga e conserva a tabela original |
| OOM no plano de destinos do hash personalizado | Conserva a tabela e o hash anterior; regressão determinística de falha e nova tentativa |
| Hash nulo para a futura migração de `SbDict` | Restaura o hash padrão em tabela preenchida e vazia; testes de identidade e consulta |
| Primos iniciais e crescimento | Menor primo adequado na construção; tabela de primos para crescimento geométrico, conforme candidato anterior |
| Limite de capacidade e retorno zero | Construção rejeita pedido acima do maior primo de 32 bits antes de alocar; crescimento sem primo maior mantém a capacidade |
| Limite do contador e de bytes | Guarda de `UINT_MAX` preservada e testada com contador sintético; multiplicações verificadas antes de alocar buckets ou plano |
| Chave zero, valor nulo, sobrescrita, colisões, clear e chaves largas | Cobertura existente do runner mantida; operações aleatórias comparadas ao modelo |

Não há pendência funcional conhecida de `cc_dict` deixada para depois desta etapa Linux. A comparação completa dos cinco componentes e os experimentos de mapas continuam no [inventário](DICT_CC_SB.md).

O hash padrão não precisa do plano auxiliar. Um hash personalizado usa temporariamente um índice por entrada, além dos novos buckets, para assegurar rollback sem copiar ou realocar os nós. A função deve produzir resultados estáveis e não modificar o dicionário durante seu cálculo. Uma exceção na consulta inicial da inserção deixa a tabela inalterada; uma exceção no crescimento opcional ocorre depois da inserção e conserva também a nova chave. Esse comportamento está documentado, não promete ausência de efeito para toda exceção.

## Diff e pré-requisitos

O complemento contém somente:

- `src/base/dict.cpp`;
- `src/base/dict.h`;
- `src/primep.h`;
- `testsuite/CcDictResizeTest.cpp`;
- `testsuite/regressions/CcDict.cmake`.

O helper privado de primos foi aproveitado de `codex/coin-prime-boundary`. Nesta contribuição ele é usado apenas por `cc_dict`; as mudanças de `cc_hash`, `SbHash` e do helper antigo seguem para seus próprios ciclos. A proteção de exceções e o reset de hash foram extraídos do candidato de migração de `SbDict`, sem migrar o backend de `SbDict` nesta branch.

O primeiro build estático encontrou uma colisão preexistente de `bumphack` entre a biblioteca e o renderer incluído no fixture. O patch já existente em `77ca5b22eb` foi isolado em `codex/pre-pr/bump-static-testfix`, alterando apenas `testsuite/bumprender/TestAdapter.h`. A branch de `cc_dict` foi rebaseada sobre esse pré-requisito e o build repetido com sucesso. O fixture tem seu próprio commit e deve ter destino separado na publicação.

A revisão posterior preparou o mesmo patch diretamente sobre master em `codex/pre-pr/bump-fixture-static` (`55dbefa033`), sem dependência dos PRs de cc_dict. Passaram 78 testes no master estático e 105 no lab compartilhado; ver [registro da revisão do fixture](VALIDACAO_FIXTURE_BUMP.md). O pré-requisito local `c9c9234768` foi preservado para manter os SHAs validados de cc_dict.

O diff próprio deve ser conferido com:

```bash
git diff codex/pre-pr/bump-static-testfix codex/pre-pr/cc-dict-complete
git diff --check codex/pre-pr/bump-static-testfix codex/pre-pr/cc-dict-complete
```

Os pré-requisitos incluem a sequência #769–772 e suas dependências de conteúdo. A branch pré-PR não nasce do lab e não carrega os demais PRs do lab por esse caminho. Ainda assim, o diff diretamente contra o master atual inclui pré-requisitos abertos: antes de publicar contra master, transportar apenas o complemento quando a base de destino oferecer esses pré-requisitos e repetir a validação. Não publicar o histórico da pilha inteira como uma contribuição nova.

## Builds e testes em RAM

Builds e temporários do compilador ficaram em `/dev/shm/coin-cc-dict-20261006`, um filesystem `tmpfs`. Os logs foram conservados em `/tmp`.

```bash
mkdir -p /dev/shm/coin-cc-dict-20261006/compiler-tmp
export TMPDIR=/dev/shm/coin-cc-dict-20261006/compiler-tmp

cmake -S /home/dikluwe/.codex/worktrees/cc-dict-complete-test/coin \
  -B /dev/shm/coin-cc-dict-20261006/lab-debug -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DCOIN_BUILD_TESTS=ON \
  -DCOIN_BUILD_DOCUMENTATION=OFF -DCOIN_THREADSAFE=OFF
cmake --build /dev/shm/coin-cc-dict-20261006/lab-debug --parallel 8

cmake -S /home/dikluwe/.codex/worktrees/cc-dict-complete-work/coin \
  -B /dev/shm/coin-cc-dict-20261006/pre-pr-release-static -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCOIN_BUILD_SHARED_LIBS=OFF \
  -DCOIN_BUILD_TESTS=ON -DCOIN_BUILD_DOCUMENTATION=OFF -DCOIN_THREADSAFE=OFF
cmake --build /dev/shm/coin-cc-dict-20261006/pre-pr-release-static --parallel 8
```

Para cada um dos dois diretórios de build:

```bash
ctest --test-dir "$dict_build" \
  -R '(CcDict|CcMemalloc|CcListOom|CcSyncOom)' --output-on-failure
xvfb-run -a -s '-screen 0 1280x1024x24 +extension GLX' \
  env LIBGL_ALWAYS_SOFTWARE=1 COIN_GLX_PIXMAP_DIRECT_RENDERING=1 \
  ctest --test-dir "$dict_build" --output-on-failure --parallel 4
```

| Configuração | Direcionados | Conjunto completo |
| --- | --- | --- |
| Lab fixo + complemento, Debug compartilhado | 11/11 | 108/108 |
| Pré-PR + fixture corrigido, Release estático | 11/11 | 93/93 |

Nenhuma falha e nenhum teste ignorado nas execuções finais; os testes gráficos foram executados. As quantidades diferem porque a base pré-PR contém os pré-requisitos, enquanto o lab contém todos os 17 PRs.

A implementação real incluída por `CcDictResizeTest.cpp` também foi compilada com `-fsanitize=address,undefined,float-cast-overflow -fno-omit-frame-pointer -g -O1`, usando os includes do build Debug e ligando com sua `libCoin`. Os oito modos `relink`, `failure`, `numeric`, `oom`, `apply`, `exception`, `capacity` e `rebuild-oom` passaram com detecção de vazamentos habilitada e sanitizadores configurados para interromper no primeiro erro. Essa instrumentação cobre o dicionário e o fixture; a biblioteca compartilhada ligada não foi inteiramente instrumentada.

```bash
dict_source=/home/dikluwe/.codex/worktrees/cc-dict-complete-test/coin
dict_build=/dev/shm/coin-cc-dict-20261006/lab-debug
c++ -std=c++11 -g -O1 -fno-omit-frame-pointer \
  -fsanitize=address,undefined,float-cast-overflow \
  -DCOIN_INTERNAL -DHAVE_CONFIG_H \
  -I"$dict_source/src" -I"$dict_source/include" \
  -I"$dict_build/src" -I"$dict_build/include" \
  "$dict_source/testsuite/CcDictResizeTest.cpp" \
  -L"$dict_build/lib" -Wl,-rpath,"$dict_build/lib" -lCoin \
  -o /dev/shm/coin-cc-dict-20261006/cc-dict-sanitized
for mode in relink failure numeric oom apply exception capacity rebuild-oom; do
  ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
  UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  /dev/shm/coin-cc-dict-20261006/cc-dict-sanitized "$mode" || exit 1
done
```

Logs: `/tmp/cc-dict-lab-{configure,build,focused,full}-20261006.log`, `/tmp/cc-dict-pre-pr-{configure,build,build-retry,focused,full}-20261006.log` e `/tmp/cc-dict-sanitizer-{build,tests}-20261006.log`.

## Passagem para Windows e encerramento da cópia

Validar o head `9b9237f13d` em builds nativos x64 compartilhado e estático, executando as regressões `CcDict`, o runner e o conjunto disponível. Isso verifica também os tipos LLP64, as chaves da largura de ponteiro e o fixture estático. Não houve execução Windows ou 32-bit nesta etapa Linux.

A cópia temporária continua identificada enquanto o ciclo Windows/publicação está pendente. Ao encerrar esse ciclo, arquivar seu worktree e eliminar `lab/teste/cc-dict-complete`, mantendo os commits na branch pré-PR e este registro. A referência do lab fixo permaneceu em `b27e37a6dd`.
