# Fechamento pré-PR de SbHash

Em 6 de outubro de 2026, o escopo conhecido de SbHash foi fechado no Linux. A branch está pronta para validação nativa Windows. O fechamento combina noexcept, relink, lazy storage e OOM dos PRs existentes, corrige o vazamento da cópia interrompida e conclui limites/primos e testes de falhas.

## Referências

| Referência | SHA |
| --- | --- |
| Master de referência | `674e74267df863dbaf50416c477bc7f918a826d8` |
| Fixture sobre #772, base local | `c9c9234768b47e5889bbc657c707c9da5a7f6bb2` |
| `codex/pre-pr/sbhash-prerequisites` | `25829e5e6bb0189c5a3a6af4fd7b6aafed6fe4ec` |
| `codex/pre-pr/sbhash-complete` | `5112bbcaf06755fb505772f3a77a737f33e0d3a1` |
| Lab fixo, preservado | `b27e37a6dd3ae941bcffcf480a377adc81e3fc27` |
| `lab/teste/sbhash-complete` | `10cfd24634fcbc6220432b40bc9e82657e01682c` |

Worktrees: `/home/dikluwe/.codex/worktrees/sbhash-complete-work/coin` e `/home/dikluwe/.codex/worktrees/sbhash-complete-test/coin`.

O pré-requisito local aplica sobre #772/fixture somente a resolução de lazy storage do lab: SbHash.h e seu teste em InternalMacroTest.cpp. Isso evita transportar os outros PRs do lab para a branch de contribuição. Não é um novo PR planejado: é um snapshot de pré-requisitos para transporte e teste. A contribuição própria é o diff `25829e5e6b..5112bbcaf0`, seis arquivos, 224 inserções e sete remoções. Esse diff é idêntico, conferido por `cmp`, ao delta `b27e37a6dd..10cfd24634` do lab temporário.

## Correções e decisões

| Assunto | Fechamento |
| --- | --- |
| Exceção no construtor de cópia | Libera buckets, entradas já copiadas e pool antes de propagar a exceção; reproduzido vazamento de 1680 bytes/quatro alocações no header anterior |
| Atribuição | Mantém a garantia básica histórica: se a cópia lança, destino válido pode conter parte da origem; origem preservada, autoatribuição sem efeito e reutilização testadas |
| Construção de entrada | Exceção de chave ou valor não publica a entrada e devolve a unidade ao pool; contagem e reutilização verificadas |
| Crescimento opcional | Falha dos novos buckets mantém a inserção bem-sucedida e adia o crescimento; `put` retorna TRUE para a nova chave e operator[] continua utilizável |
| Candidato antigo de inserção | Sua premissa era crescimento com exceção. #772 usa new(nothrow), portanto o movimento pré-inserção não foi transportado; o caso de falha/retry foi reconciliado com a política atual |
| OOM obrigatório | Buckets iniciais, construtor do pool, entrada e capacidade impossível abortam com diagnóstico; quatro subprocessos confirmam SIGABRT |
| Primos | Usa o mesmo primep.h de cc_dict/cc_hash: primo exato inicial, tabela geométrica no crescimento, primo máximo 4294967291 e zero quando não há capacidade representável |
| Fator de carga | Não finito/não positivo normalizado para 0,75; limite saturado em UINT_MAX; testes sem alocação gigante |
| Noexcept | Mantém o requisito incluindo conversões implícitas de chave; compilação positiva/negativa confirma o contrato e o build compila os consumidores reais |
| Relink | Preserva endereços de entradas/valores, sem cópia dos antigos durante crescimento; obtenha novos iteradores após inserção, pois o estado de travessia pode ser invalidado |
| Lazy storage | Construção vazia sem buckets/pool; clear retém armazenamento; releaseStorage libera e permite novo uso; iteração vazia permanece coberta pela sequência |
| Chaves C-string | const char* calcula hash dos bytes, mas igualdade continua identidade do ponteiro, para nomes internados; char* continua hash/igualdade por identidade |
| API e representação | Classe privada; nenhum header instalado modificado, nenhum membro acrescentado e nenhuma troca de backend ou função pública de hash |
| Listas | makeKeyList acrescenta após o prefixo existente, sem promessa de ordem; nenhuma nova garantia de rollback de listas introduzida |
| Desempenho | Mantém relink, lazy storage, hash sem cache por entrada e módulo por primos; não foram repetidos benchmarks nem reivindicado novo ganho medido |

O helper privado antigo coin_geq_prime_number ainda tem consumidores nas branches de pré-requisito cc_dict/cc_hash. A [auditoria conjunta](FECHAMENTO_CANDIDATOS_CENTRAIS.md) decidiu preservar esse símbolo com a semântica histórica; os três fechamentos passam a usar primep.h diretamente. O arquivo tem conteúdo idêntico nas três contribuições: integrar uma vez e eliminar a adição duplicada ao preparar o diff final de publicação.

## Validação

| Configuração | Resultado |
| --- | --- |
| Lab Debug compartilhado | 106/106 CTests, incluindo gráficos, sem testes ignorados |
| Pré-PR Release estática sobre os pré-requisitos identificados | 91/91 CTests, incluindo gráficos, sem testes ignorados |
| ASan/UBSan e float-cast-overflow | SbHash e memalloc.cpp reais: modelo, exceções de cópia/chave/entrada, crescimento opcional e quatro aborts obrigatórios aprovados |
| Reprodutor da cópia anterior | LeakSanitizer acusa 1680 bytes; o mesmo reprodutor com o novo header passa |
| Conversão de chave | Variante noexcept compila; variante potencialmente lançadora é rejeitada pela static_assert esperada |

O novo SbHashFailureTest compara 20 mil operações com std::map e verifica cópia independente, falhas em cada prefixo da construção de cópia, atribuição parcial e recuperação, identidade após relink, crescimento após falha, operator[], listas preenchidas, chaves largas e igualdade histórica de ponteiros. Os testes anteriores de iteradores, colisões, estatísticas, real threshold e lazy storage permanecem no CoinTests.

A injeção de new[] do fixture possui também overload nothrow: o primeiro passe ASan mostrou que seu interceptor contornava a injeção anterior. O fixture foi corrigido e todos os resultados finais foram repetidos sem desativar verificações dos sanitizadores.

## Comandos e logs

Compilação e temporários do compilador em tmpfs `/dev/shm/coin-sbhash-20261006`, sem build em disco. Para cada worktree, com `BUILD` igual a `lab-debug-shared` ou `pre-pr-release-static`:

```sh
export TMPDIR=/dev/shm/coin-sbhash-20261006/compiler-tmp
cmake -S "$WORKTREE" -B "/dev/shm/coin-sbhash-20261006/$BUILD" -G Ninja \
  -DCMAKE_BUILD_TYPE="$TYPE" -DCOIN_BUILD_SHARED_LIBS="$SHARED" \
  -DCOIN_BUILD_TESTS=ON -DCOIN_BUILD_DOCUMENTATION=OFF -DCOIN_THREADSAFE=OFF
cmake --build "/dev/shm/coin-sbhash-20261006/$BUILD" --parallel 8
xvfb-run -a -s '-screen 0 1280x1024x24 +extension GLX' \
  env LIBGL_ALWAYS_SOFTWARE=1 COIN_GLX_PIXMAP_DIRECT_RENDERING=1 \
  ctest --test-dir "/dev/shm/coin-sbhash-20261006/$BUILD" --output-on-failure --parallel 4
```

TYPE/SHARED: Debug/ON no lab; Release/OFF na pré-PR. Rebuilds finais usaram --parallel 6. Logs `/tmp/sbhash-{lab,pre-pr}-{configure,build,rebuild,full}-20261006.log`. Instrumentação reproduzível em `/tmp/run-sbhash-sanitizers.py`, comandos/resultados em `/tmp/sbhash-sanitizers-final-20261006.log`; compila com `-fsanitize=address,undefined,float-cast-overflow -O1 -g -fno-omit-frame-pointer`, detect_leaks=1 e halt_on_error=1. O restante da biblioteca usada apenas pelo reprodutor histórico não foi inteiramente instrumentado.

## Próximas etapas

Windows nativo DLL/estático e LLP64 permanecem pendentes. Antes de publicar, reaplicar somente a contribuição sobre o master de destino com os pré-requisitos já integrados, repetir a validação e conferir o diff exclusivo. Nenhum push ou PR novo foi feito. Conservar as referências e o worktree temporário até concluir Windows/publicação; então arquivar o worktree gerenciado e eliminar lab/teste/sbhash-complete.
