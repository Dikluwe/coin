# Fechamento Linux da vida útil do GL glue

Em 6 de outubro de 2026, o escopo conhecido de lifetime do glue foi fechado no Linux. Registros retirados do cache agora são liberados depois dos callbacks dos proprietários; strings, dicionário de extensões e handle dinâmico são liberados pelo mesmo helper, inclusive no shutdown. O header documenta a duração do empréstimo. Windows nativo permanece pendente.

## Referências

| Referência | SHA |
| --- | --- |
| Master de destino usado no teste | `674e74267df863dbaf50416c477bc7f918a826d8` |
| Fixture estático, único pré-requisito local | `55dbefa0330ca59a68ff2d47591f1256e7cba7b2` |
| `codex/pre-pr/glglue-lifetime-complete` | `0d31242881b0a2bc996ae348c3768fa8730c7a98` |
| Lab fixo, preservado | `b27e37a6dd3ae941bcffcf480a377adc81e3fc27` |
| `lab/teste/glglue-lifetime-complete` | `c71f71965c7bc1f00b0b33ff5e3edfa78dbc0083` |
| Fonte experimental preservada | `5d28cf8e95657eae70c60980383602b3013a01e2` |

Worktrees: `/home/dikluwe/.codex/worktrees/glglue-lifetime-work/coin` e `/home/dikluwe/.codex/worktrees/glglue-lifetime-test/coin`.

Contribuição sobre o fixture: seis arquivos, 142 inserções e 13 remoções. Não incorpora os PRs do lab. A reconciliação preserva os checks de OOM de #772: master insere com cc_dict_put; lab insere com cc_dict_try_put e verifica o resultado. Ambos normalizam o ID para uint32_t antes de convertê-lo em chave uintptr_t. As linhas contribuídas dos patches foram comparadas por arquivo, normalizando apenas essa diferença na operação de inserção. O lab fixo não mudou.

## Correções e contrato

| Assunto | Fechamento |
| --- | --- |
| Registro removido do cache | Libera o cc_glglue e suas estruturas; não deixa o registro inacessível à limpeza de saída |
| Recursos de CPU | Mesmo helper libera version/vendor/renderer/extensions strings, glextdict e o registro |
| Handle dinâmico | cc_dl_close passa ao helper; fecha também registros ativos na saída, sem fechar duas vezes na destruição de contexto |
| Recurso GL próprio | normalizationcubemap continua sendo excluído com o contexto corrente antes da liberação do registro |
| Ordem | SoContextHandler mantém os callbacks antes de coin_glglue_destruct; os proprietários podem usar o glue até o final dessa janela |
| Ausência de callbacks | Mesmo sem registry de callbacks, destructingContext notifica o glue e libera o registro existente |
| IDs extremos | Lookup, inserção e destruição usam os mesmos 32 bits do ID; corrige a divergência entre int assinado e uint32_t em LP64, testada com 0x80000001 e UINT32_MAX |
| Reutilização de ID | Depois da destruição, adquirir novamente cria metadata nova; não se usa o ponteiro da geração anterior |
| Empréstimo público | Ponteiro válido durante a vida do contexto e seus callbacks; inválido quando destructingContext retorna ou no shutdown. Quem usa o contexto deve serializar seus empréstimos com a destruição |
| Compatibilidade | Header público muda somente comentários; 219 símbolos C cc_glglue preservados; nenhum membro acrescentado à representação opaca |
| Experimento antigo | O switch COIN_GLGLUE_LIFETIME_STUDY_FREE não foi transportado. Seu acesso intencional após destruição não é um uso válido do contrato documentado |

A normalização é necessária nas duas rotas de inserção. O primeiro teste de limite revelou que normalizar apenas o lookup e o try_put do lab deixava o cc_dict_put antigo de master com extensão de sinal, provocando criação recursiva durante o callback de contexto criado. A inserção legada foi corrigida e a validação final repetida nas duas bases.

## Borrowers internos

A busca por campos cc_glglue persistentes encontrou dois:

1. **SoGLShaderObject::glctx**: objetos são excluídos nos callbacks dos proprietários antes do glue. O teste integrado dos quatro caches GL verifica criação, falha/retry e as duas ordens de teardown, com gl.cpp e handler instrumentados.
2. **SoGLMultiTextureCoordinateElementP::glue**: initRender adquire o ponteiro do contexto para enviar coordenadas; trocar o contexto de SoGLRenderAction invalida o estado. A integração adicional usa textura e uma ação reaproveitada em cinco contextos, destruindo o contexto anterior antes de voltar a renderizar em outro.

Os campos de glue citados no estudo antigo do bump não existem mais no ProgramCache atual; a leitura da versão atual substitui essa descrição histórica. A biblioteca não passa a suportar dereferência depois da destruição nem destruição concorrente com uso do mesmo contexto. O cache global continua protegendo lookup/removal, mas não pode estender um empréstimo público sem retenção explícita.

## Validação final

| Configuração | Resultado |
| --- | --- |
| Master + fixture, Release estática, COIN_THREADSAFE=ON | 80/80 CTests, sem falhas ou testes ignorados |
| Lab temporário, Debug compartilhado, COIN_THREADSAFE=ON | 107/107 CTests, sem falhas ou testes ignorados |
| Fontes reais gl.cpp, SoContextHandler.cpp e driver, ASan/UBSan | Casos de callback, liberação, IDs extremos, duas threads e shutdown aprovados com LeakSanitizer ativo |
| Implementação anterior | Falha na verificação de liberação do registro; comparação antes/depois reproduz a retenção |
| Quatro caches GL + novo glue, instrumentados | Cinco contextos sem compartilhamento, falhas/retry, pixels e teardown aprovados; nenhuma exclusão de glue do LeakSanitizer |
| Estado reaproveitado com textura | Mesma ação passa por cinco contextos e volta a renderizar após destruir o anterior, sem finding |
| API | Header idêntico depois de remover comentários; mesmo conjunto de 219 símbolos cc_glglue C |

GLGlueLifetimeTest exercita 72 gerações lógicas do cache sobre dois contextos GLX nativos: 32 ciclos sequenciais, 16 em cada uma de duas threads, quatro com bit alto e quatro com UINT32_MAX. Cada thread serializa uso e destruição de seu próprio contexto. Os callbacks verificam que o mesmo registro ainda está acessível; sob ASan, o teste consulta o shadow para confirmar a liberação do registro e das seis estruturas possuídas, sem dereferenciá-los depois da destruição. Um registro ativo adicional é liberado por SoDB::finish. Outro processo cobre a ausência de callbacks.

O restante da biblioteca ligada não foi inteiramente instrumentado. Não foi executado TSan; o teste concorrente valida contextos distintos com os locks da configuração thread safe. WGL/EGL/CGL nativos não foram executados neste ciclo Linux/GLX.

## Comandos e logs

Builds, executáveis instrumentados e temporários do compilador em RAM tmpfs `/dev/shm/coin-glglue-lifetime-20261006`:

```sh
export TMPDIR=/dev/shm/coin-glglue-lifetime-20261006/compiler-tmp
cmake -S "$WORKTREE" -B "/dev/shm/coin-glglue-lifetime-20261006/$BUILD" -G Ninja \
  -DCMAKE_BUILD_TYPE="$TYPE" -DCOIN_BUILD_SHARED_LIBS="$SHARED" \
  -DCOIN_BUILD_TESTS=ON -DCOIN_BUILD_DOCUMENTATION=OFF -DCOIN_THREADSAFE=ON
cmake --build "/dev/shm/coin-glglue-lifetime-20261006/$BUILD" --parallel 8
xvfb-run -a -s '-screen 0 1280x1024x24 +extension GLX' \
  env LIBGL_ALWAYS_SOFTWARE=1 COIN_GLX_PIXMAP_DIRECT_RENDERING=1 \
  ctest --test-dir "/dev/shm/coin-glglue-lifetime-20261006/$BUILD" --output-on-failure --parallel 4
```

BUILD/TYPE/SHARED: master-release-static/Release/OFF e lab-debug-shared/Debug/ON. Rebuilds finais usaram --parallel 6. Logs `/tmp/glglue-lifetime-{master,lab}-{configure,build,rebuild,focused,full}-20261006.log`.

Instrumentação reproduzível em `/tmp/run-glglue-sanitizers.py`; comandos/resultados finais em `/tmp/glglue-lifetime-sanitizers-final-20261006.log`. Usa `-O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined`, detect_leaks=1 e halt_on_error=1. Só o reprodutor anterior, que termina na asserção esperada sem completar a limpeza do fixture, usa detect_leaks=0.

Integração reproduzível em `/tmp/run-glglue-map-integration.py`; resultados `/tmp/glglue-lifetime-map-integration-reuse-20261006.log`. Instrumenta o driver e quatro consumidores do fechamento `87cc9003e9`, mais os gl.cpp/SoContextHandler.cpp atuais, ligados à biblioteca daquela frente já validada. Essa composição adicional usa a configuração thread safe OFF da biblioteca de consumidores; os testes dedicados desta contribuição usam ON. O driver adicional é salvo em RAM como map-integration/GLContextMapsReuse.cpp. Não define COIN_GL_MAP_LSAN_IGNORE_GLUE: os vazamentos antes excluídos precisam estar efetivamente resolvidos. API/símbolos registrados em `/tmp/glglue-lifetime-api-20261006.log`.

## Windows e publicação

Levar o head ao Windows para DLL/estático, WGL e LLP64 antes de publicar. A biblioteca e o glue são testados sobre master com somente o fixture como pré-requisito local; antes da publicação, repetir no master de destino atualizado, preservando separadamente a política de OOM de #772 quando integrada. Nenhum push ou PR novo feito.

Conservar as referências e lab/teste/glglue-lifetime-complete até concluir Windows/publicação; depois arquivar seu worktree gerenciado e eliminar a cópia temporária. A pendência de SoAction::apply sob exceção continua registrada como próximo ciclo, sem entrar neste diff.
