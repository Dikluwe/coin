# Fechamento pré-PR de SbSmallMap

Em 6 de outubro de 2026, o escopo conhecido do tipo SbSmallMap foi fechado no Linux. A contribuição corrige o contrato completo de igualdade, explicita a propriedade de ponteiros e acrescenta cobertura de falhas, cópia e reutilização. Windows permanece pendente. A revisão dos quatro consumidores GL do PR #758 foi concluída no ciclo seguinte, registrado em [FECHAMENTO_GL_CONTEXT_MAPS.md](FECHAMENTO_GL_CONTEXT_MAPS.md).

## Referências e contribuição

| Referência | SHA |
| --- | --- |
| Master de destino usado no teste | `674e74267df863dbaf50416c477bc7f918a826d8` |
| Fixture estático, único pré-requisito local | `55dbefa0330ca59a68ff2d47591f1256e7cba7b2` |
| `codex/pre-pr/sbsmallmap-complete` | `ff1d09ddf3d458bb73023aa3fc001afce1e54fd6` |
| Lab fixo, preservado | `b27e37a6dd3ae941bcffcf480a377adc81e3fc27` |
| `lab/teste/sbsmallmap-complete` | `52ed877e66304d22e204a143782eaa26774e89ef` |

Worktrees: `/home/dikluwe/.codex/worktrees/sbsmallmap-complete-work/coin` e `/home/dikluwe/.codex/worktrees/sbsmallmap-complete-test/coin`.

A branch pré-PR nasce do fixture independente sobre master; o tipo, o profiler e a segurança de crescimento de SbList já estão integrados nesse master (#756/#757). Não incorpora os PRs abertos do lab. A contribuição é o diff `55dbefa033..ff1d09ddf3`: quatro arquivos, 134 inserções e quatro remoções. O lab temporário recebe somente esse delta. Os patches do master e do lab são idênticos após remover apenas as linhas `index` dos hashes dos blobs: CMakeLists.txt tem conteúdo de base diferente nas duas referências. Não houve alteração no lab fixo.

## Correções e contratos

| Assunto conhecido | Fechamento |
| --- | --- |
| Igualdade sem exceções | static_assert verifica também a conversão do resultado de operator== para bool; fecha a brecha de um proxy com conversão lançadora |
| Domínio de tipos | Mantém chaves/valores triviais e igualdade não lançadora; nenhum novo backend ou domínio de valores complexos |
| Inserção/subscript sob falha | Preserva tamanho, entradas e endereços anteriores quando o novo bloco não pode ser alocado; tentativa seguinte funciona |
| Crescimento | Falhas nas transições 4→8, 8→16, 16→32 e 32→64 exercitadas; substituição de chave existente não aloca |
| Referência usada como argumento | put recebe valor de uma entrada do próprio mapa durante spill; cópia local anterior ao crescimento preserva o valor |
| Cópia/atribuição | Estruturas independentes, autoatribuição válida; falha de alocação mantém origem e destino anterior; construtor de cópia interrompido não vaza |
| Ponteiros | Valores não proprietários: substituição, erase, clear, destruição e cópia não liberam o recurso apontado; consumidor controla a vida útil |
| Limpeza | clear retém armazenamento spilled; cópia vazia não aloca; atribuição de vazio permite reutilizar a capacidade existente |
| Iteração/remoção | Remoção rápida pode trocar ordem e invalidar referências/iteradores; nenhuma ordem prometida; modelo verifica todas as entradas visitadas |
| makeKeyList | Acrescenta após o prefixo; se uma alocação posterior falha, pode deixar um sufixo parcial válido; origem permanece intacta, retry testado |
| Limites | SbList já verifica INT_MAX/2 antes da duplicação e publica o novo bloco somente após sucesso; não duplica essa política no mapa |
| Chaves largas | Teste de duas chaves distintas incluindo bit 63, portátil entre LP64 e LLP64 |
| API/ABI | Header privado, sem novo membro ou mudança em header instalado; armazenamento inline continua quatro entradas |

Não foi introduzido rollback adicional em makeKeyList nem propriedade automática de recursos. Esses comportamentos foram explicitados e testados. O teste de ponteiros usa um proprietário independente e confirma que as operações do mapa não destroem o recurso.

## Resultados

| Configuração | Resultado final |
| --- | --- |
| Master + fixture, Release estática | 79/79 CTests, sem falhas ou testes ignorados |
| Lab temporário, Debug compartilhado | 106/106 CTests, sem falhas ou testes ignorados, incluindo gráficos |
| ASan/UBSan, detecção de vazamentos | Teste completo do template real e SbList real aprovado |
| Contrato de igualdade, positivo | Proxy com conversão bool noexcept compila e funciona |
| Contrato de igualdade, negativo | Proxy com conversão bool potencialmente lançadora compila no header antigo e é rejeitado no novo pela static_assert esperada |

O novo SbSmallMapFailureTest força falhas em new[] real, verifica ausência de alocação nas primeiras quatro inserções, cópia/atribuição, alias de valor durante spill, listas com prefixo, ponteiros não proprietários e chaves largas. Compara 20 mil operações mistas e toda a iteração com std::map. Os seis testes anteriores de InternalSmallMapTest.cpp continuam no CoinTests, incluindo a falha da quinta inserção pelo hook específico do tipo.

Nenhum benchmark foi repetido neste ciclo; não há novo ganho de desempenho reivindicado. As medições históricas orientam os consumidores, cuja validação atual fica no ciclo de #758.

## Comandos reproduzíveis

Todos os builds, executáveis instrumentados e temporários do compilador ficaram na RAM tmpfs `/dev/shm/coin-sbsmallmap-20261006`:

```sh
export TMPDIR=/dev/shm/coin-sbsmallmap-20261006/compiler-tmp
cmake -S "$WORKTREE" -B "/dev/shm/coin-sbsmallmap-20261006/$BUILD" -G Ninja \
  -DCMAKE_BUILD_TYPE="$TYPE" -DCOIN_BUILD_SHARED_LIBS="$SHARED" \
  -DCOIN_BUILD_TESTS=ON -DCOIN_BUILD_DOCUMENTATION=OFF -DCOIN_THREADSAFE=OFF
cmake --build "/dev/shm/coin-sbsmallmap-20261006/$BUILD" --parallel 8
xvfb-run -a -s '-screen 0 1280x1024x24 +extension GLX' \
  env LIBGL_ALWAYS_SOFTWARE=1 COIN_GLX_PIXMAP_DIRECT_RENDERING=1 \
  ctest --test-dir "/dev/shm/coin-sbsmallmap-20261006/$BUILD" --output-on-failure --parallel 4
```

BUILD/TYPE/SHARED: master-release-static/Release/OFF e lab-debug-shared/Debug/ON. Rebuilds finais usaram --parallel 6. Logs em `/tmp/sbsmallmap-{master,lab}-{configure,build,rebuild,full}-20261006.log`.

Instrumentação e comparação do header anterior reproduzíveis em `/tmp/run-sbsmallmap-sanitizers.py`; comandos completos e resultados em `/tmp/sbsmallmap-sanitizers-20261006.log`. Usa `-std=c++11 -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined`, ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 e UBSAN_OPTIONS=halt_on_error=1. O teste standalone não liga a biblioteca inteira: instrumenta o template e SbList diretamente, sem testes substitutos do armazenamento.

## Consumidores e próximos gates

A leitura dos consumidores atuais confirma guardas já existentes: SoVBO e SoGLSLShaderProgram apagam o objeto GL novo se put falha; SoShaderObject usa unique_ptr até publicar no mapa; SoUniformShaderParameter usa unique_ptr na criação/substituição e libera parâmetros na destruição; o TextureDict do profiler usa SoRefPtr e adquire a referência do cache somente depois da inserção. Essa leitura não encerra o gate de recursos/contextos do #758. Falhas reais nos consumidores e perfis com vários contextos serão validados separadamente.

Levar o head ao Windows para DLL/estático e LLP64. Antes da publicação, transportar somente o delta sobre o master de destino atual, com o fixture resolvido separadamente, repetir a validação e conferir o diff. Nenhum push ou PR novo feito. Conservar lab/teste/sbsmallmap-complete até encerrar Windows/publicação; depois arquivar seu worktree gerenciado e eliminar a branch temporária.
