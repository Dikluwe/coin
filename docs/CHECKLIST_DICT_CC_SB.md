# Checklist das branches de dicionários e mapas

Estado em 6 de outubro de 2026. Abrange `cc_dict`, `cc_hash`, `SbDict`, `SbHash`, `SbSmallMap`, os mapas experimentais, seus consumidores discutidos e o apoio de alocadores, testes e fixtures.

**Uma marca `[x]` significa que o escopo indicado está concluído no Linux.** Um teste aprovado isoladamente não encerra uma revisão pendente. Nos labs, a marca significa validação daquele snapshot; nos arquivos históricos, significa apenas preservação, quando explicitado. Windows e publicação têm etapas próprias.

## Fechamento dos componentes

- [x] `cc_dict` — escopo conhecido fechado no Linux; branch pré-PR pronta para Windows.
- [x] Fixture bump estático — revisão encerrada e branch independente pronta para Windows.
- [x] `cc_hash` — escopo conhecido fechado no Linux; cliente C, primos/capacidade e compatibilidade conferidos; pronto para Windows.
- [x] `SbDict` — migração e contratos fechados no Linux, compatibilidade conferida; pronto para Windows.
- [x] `SbHash` — escopo conhecido fechado no Linux; cópia, falhas, primos e sequência revisados; pronto para Windows.
- [x] `SbSmallMap` — tipo fechado no Linux: igualdade, falhas, cópia e ponteiros não proprietários; pronto para Windows. Consumidores #758 concluíram o ciclo Linux próprio abaixo; Windows pendente.

## Branches prontas no Linux

- [x] `codex/pre-pr/glglue-lifetime-complete` · `0d31242881` — lifetime e IDs de contexto fechados: 80/80 sobre master, 107/107 no lab, threads e ASan/UBSan/LSan sem exclusões de glue. [Registro](FECHAMENTO_GLGLUE_LIFETIME.md).

- [x] `codex/pre-pr/gl-contexts-small-complete` · `87cc9003e9` — #758 reconciliado e complementado: 107/107 no lab, 80/80 sobre master, cinco contextos GLX e sanitizadores. [Registro e limites](FECHAMENTO_GL_CONTEXT_MAPS.md).

- [x] `codex/pre-pr/sbsmallmap-complete` · `ff1d09ddf3` — tipo fechado; lab 106/106, master/fixture estático 79/79 e ASan/UBSan aprovados. [Registro](FECHAMENTO_SBSMALLMAP.md).

- [x] `codex/pre-pr/sbhash-complete` · `5112bbcaf0` — fechamento SbHash; lab 106/106, estática 91/91, ASan/UBSan e regressão anterior conferidos. [Registro](FECHAMENTO_SBHASH.md).
- [x] `codex/pre-pr/sbhash-prerequisites` · `25829e5e6b` — snapshot local de lazy storage reconciliado sobre #772/fixture; dependência para transportar o fechamento, sem PR próprio planejado.

- [x] `codex/pre-pr/cc-dict-complete` · `9b9237f13d` — complemento final de cc_dict. Lab Debug: 108 testes; pré-PR Release estática: 93; oito cenários ASan/UBSan. [Registro](VALIDACAO_CC_DICT.md).
- [x] `codex/pre-pr/cc-hash-complete` · `c5dc4c643a` — complemento final de cc_hash. Lab Debug: 106 testes; pré-PR Release estática: 91; cliente C/header antigo e hash/pool com ASan/UBSan. [Registro](FECHAMENTO_CC_HASH.md).
- [x] `codex/pre-pr/sbdict-complete` · `b5ae0d4ff3` — migração e contratos de SbDict sobre cc_dict fechado. Lab com pré-requisito: 112 testes; pré-PR estática: 97; cliente antigo e sanitizadores aprovados. [Registro](FECHAMENTO_SBDICT.md).
- [x] `codex/pre-pr/bump-fixture-static` · `55dbefa033` — patch do fixture sobre master, um arquivo e três linhas. Master estático: 78 testes; lab compartilhado: 105. [Registro](VALIDACAO_FIXTURE_BUMP.md).
- [x] `codex/pre-pr/bump-static-testfix` · `c9c9234768` — mesmo patch do fixture sobre #772, validado como pré-requisito local de cc_dict. Preservar esse SHA; a branch independente acima é o destino para publicar o fixture.

## PRs existentes do grupo

O fechamento Linux abaixo vale para a contribuição de cada linha. Os PRs #769–772 integram o escopo de cc_dict já revisado, com testes de falha e integração; os complementos finais ficam na branch pré-PR.

- [x] `codex/cc-dict-resize-followup` · `dacbbd984d` — #769: relink e falha opcional de crescimento; cobertura mantida no lab.
- [x] `codex/cc-memalloc-hardening` · `729a5b9cd5` — #770: alinhamento, limites e falhas do pool; validado no conjunto de cc_dict.
- [x] `codex/cc-dict-hardening` · `5a311648e3` — #771: números, construção, resultado de inserção e apply; complemento de hash/primos separado na pré-PR.
- [x] `codex/cc-oom-consumers` · `6a169aaac4` — #772: política de OOM dos consumidores e primitivas; validado no conjunto. A política de inserção SbHash foi reconciliada e validada no fechamento abaixo.
- [x] `fix/sbhash-04-noexcept-contract` · `4810469369` — revisão Linux encerrada no fechamento SbHash; permanece como PR existente e pré-requisito.
- [x] `fix/sbhash-05-relink-resize` · `25a0c03d62` — revisão Linux encerrada no fechamento SbHash; permanece como PR existente e pré-requisito.
- [x] `fix/sbhash-06-lazy-storage` · `7fa9bae533` — revisão Linux encerrada no fechamento SbHash; permanece como PR existente e pré-requisito.
- [x] `codex/maps/use/gl-contexts-small` · `900f11a157` — revisão Linux #758 concluída, complemento em `codex/pre-pr/gl-contexts-small-complete`. Head publicado preservado; não contém ainda as correções adicionais. Windows/transporte pendentes.

## Candidatos centrais auditados

- [x] `codex/work/cc-hash-api` · `ebae1bd653` — candidato preliminar substituído pelos fechamentos cc_hash e SbDict; conservar a referência até publicação. [Auditoria](FECHAMENTO_CANDIDATOS_CENTRAIS.md).
- [x] `codex/cc-hash-hardening-followup` · `a60f2bea89` — fonte anterior reconciliada com os PRs existentes e os fechamentos; conservar referência até publicação. [Auditoria](FECHAMENTO_CANDIDATOS_CENTRAIS.md).
- [x] `codex/sbdict-ccdict-migration` · `77ca5b22eb` — migração substituída pelo fechamento SbDict; o fixture próprio coincide com a branch independente. [Auditoria](FECHAMENTO_CANDIDATOS_CENTRAIS.md).
- [x] `codex/sbdict-hash-hardening` · `2ecc31d7cf` — versão inicial e testes reconciliados com cc_hash/SbDict finais; nenhuma contribuição restante para PR. [Auditoria](FECHAMENTO_CANDIDATOS_CENTRAIS.md).
- [x] `codex/work/sbhash-insert-allocation-failure` · `5c9bc244ea` — auditoria do delta SbHash encerrada: premissa de exceção substituída pela política opcional de #772; testes úteis reconciliados na pré-PR. Conservar referência até publicação.
- [x] `codex/coin-prime-boundary` · `1d087a752d` — primos e limites incorporados nos três fechamentos; helper legado preservado com semântica histórica. Arquivo comum entra uma vez na publicação. [Auditoria](FECHAMENTO_CANDIDATOS_CENTRAIS.md).

## Mapas e consumidores em estudo

- [x] `codex/maps/core/adaptive-map` · `3957f93c76` — auditoria encerrada: experimento não aprovado para PR por perda reproduzida de entradas sob exceção, outros caminhos de promoção sem rollback e benefício insuficientemente demonstrado. Preservar a fonte; [registro](FECHAMENTO_ADAPTIVE_MAP.md).
- [x] `codex/maps/use/coinresources-adaptive` · `241764d557` — troca de mapa sem correção exclusiva; depende de AdaptiveMap reprovado. [Auditoria](FECHAMENTO_MAPAS_CONSUMIDORES.md).
- [x] `codex/maps/use/scxml-attributes-adaptive` · `8b77560293` — migração descartada; correção de alias já isolada e validada. [Auditoria](FECHAMENTO_MAPAS_CONSUMIDORES.md).
- [x] `codex/pre-pr/scxml-attribute-alias` · `6ab2915cb7` — correção funcional de alias extraída sem migração de mapa; master 78/78, lab 105/105 e regressão ASan/UBSan/LSan. [Registro](FECHAMENTO_ADAPTIVE_MAP.md).
- [x] `codex/maps/use/scxml-document-ids-adaptive` · `aab125aa41` — promoção pode ocultar IDs anteriores se falhar; migração descartada. [Auditoria](FECHAMENTO_MAPAS_CONSUMIDORES.md).
- [x] `codex/maps/use/scxml-evaluator-temporaries-adaptive` · `82b9f29c78` — migração descartada; alias/ownership extraído abaixo. [Auditoria](FECHAMENTO_MAPAS_CONSUMIDORES.md).
- [x] `codex/pre-pr/scxml-temporary-alias` · `b4204f9859` — correção isolada sobre master; lab 105/105, master 78/78. Windows/publicação pendentes. [Registro](FECHAMENTO_MAPAS_CONSUMIDORES.md).
- [x] `codex/maps/core/sequential-map` · `3ba09c803a` — chave negativa invisível em Release e benchmark sem ganho consistente; não aprovado. [Auditoria](FECHAMENTO_MAPAS_CONSUMIDORES.md).
- [x] `codex/maps/integration/profiler-containers` · `ba0ed99669` — agregado experimental substituído por correção isolada de lifetime. [Auditoria](FECHAMENTO_MAPAS_CONSUMIDORES.md).
- [x] `codex/maps/use/profiler-action-timings-sequential` · `edaaec9b5c` — migração descartada; liberação de dados extraída abaixo. [Auditoria](FECHAMENTO_MAPAS_CONSUMIDORES.md).
- [x] `codex/pre-pr/profiler-stats-lifetime` · `bb32da5fc2` — correção isolada sobre master; lab 105/105, master 78/78. Windows/publicação pendentes. [Registro](FECHAMENTO_MAPAS_CONSUMIDORES.md).
- [x] `codex/maps/use/fieldcontainer-mfield-sizes-hash` · `339a1d6b49` — sem correção exclusiva; tabela global nova não justificada por medição. [Auditoria](FECHAMENTO_MAPAS_CONSUMIDORES.md).
- [x] `codex/maps/use/scxml-type-registry-hash-name` · `eb1d50e0d2` — migração sem correção exclusiva ou ganho demonstrado; preservada como fonte. [Auditoria](FECHAMENTO_MAPAS_CONSUMIDORES.md).
- [x] `codex/sbname/experiment/compact-entry-pool` · `7b439fb7c2` — identidade normal validada; falhas/limites de alocação e ganho pendentes antes de PR. [Auditoria](FECHAMENTO_MAPAS_CONSUMIDORES.md).

## Apoio e consumidores associados

O `[x]` encerra a auditoria da fonte; apenas as contribuições pré-PR indicadas no registro foram validadas para transporte ao Windows. As fontes antigas ficam preservadas até publicação.

- [x] `codex/cc-list-hardening` · `42815f7385` — OOM já integrado no lab; limites isolados em `codex/pre-pr/cc-list-bounds` `ea8e905436`, 78/78 master e 105/105 lab. [Registro](FECHAMENTO_APOIO_CONSUMIDORES.md).
- [x] `codex/improve-sblist` · `e506a1c4fe` — crescimento prévio já no master; `SbList`, `SbPList` e regressão de cópia de callback isolados em contribuições independentes. [Registro](FECHAMENTO_APOIO_CONSUMIDORES.md).
- [x] `codex/pre-pr/sblist-value-bounds` · `af3ae60643` — contrato de valor/índice e testes revisados; 78/78 master, 105/105 lab. [Registro](FECHAMENTO_APOIO_CONSUMIDORES.md).
- [x] `codex/pre-pr/sbplist-bounds` · `969a37276e` — contrato de índice e regressões; 78/78 master, 105/105 lab. [Registro](FECHAMENTO_APOIO_CONSUMIDORES.md).
- [x] `codex/pre-pr/callback-list-copy-stress` · `23ccce4b84` — regressão de propriedade após cópia/destruição de `SoCallbackList`; 78/78 master, 105/105 lab. [Registro](FECHAMENTO_APOIO_CONSUMIDORES.md).
- [x] `codex/cc-test-audit` · `95458242d7` — poda antiga sem delta útil para dict/hash fechado; mudanças de worker/scheduler ficam no estudo de threads. [Registro](FECHAMENTO_APOIO_CONSUMIDORES.md).
- [x] `codex/work/bump-cache-ready-path` · `bb6158a01e` — delta isolado em `codex/pre-pr/bump-cache-ready-path` `9d9c16f53a`; medição e 78/78 master, 105/105 lab. [Registro](FECHAMENTO_APOIO_CONSUMIDORES.md).
- [x] `codex/work/bump-cache-diagnostics-memory` · `c098efdfb6` — delta isolado em `codex/pre-pr/bump-cache-diagnostics-memory` `1a373951dd`; 78/78 master, 105/105 lab e falhas testadas. [Registro](FECHAMENTO_APOIO_CONSUMIDORES.md).
- [x] `codex/work/bump-shared-program-pool` · `191a685710` — pré-requisito já integrado; pool isolado em `codex/pre-pr/bump-shared-programs` `9b772453d4`; 78/78 master, 105/105 lab e sanitizadores. [Registro](FECHAMENTO_APOIO_CONSUMIDORES.md).
- [x] `codex/work/glglue-lifetime-audit` · `5d28cf8e95` — estudo encerrado no Linux: política de empréstimo explicitada, borrowers validados e correção em `codex/pre-pr/glglue-lifetime-complete`. Conservar a fonte experimental até publicação.
- [x] `codex/work/sbheap-cancel-documentation` · `83925a4743` — contrato confirmado e documentado em `codex/pre-pr/sbheap-cancel-contract` `6436c4d193`; 78/78 master, 105/105 lab. [Registro](FECHAMENTO_APOIO_CONSUMIDORES.md).

## Labs e benchmarks

- [x] `lab/teste/cc-list-bounds` · `820e8bb99f` — somente limites cc_list sobre lab fixo; 105/105. [Registro](FECHAMENTO_APOIO_CONSUMIDORES.md).
- [x] `lab/teste/sblist-value-bounds` · `d3f033ed24` — somente contrato SbList; 105/105. [Registro](FECHAMENTO_APOIO_CONSUMIDORES.md).
- [x] `lab/teste/sbplist-bounds` · `a63d490810` — somente contrato SbPList; 105/105. [Registro](FECHAMENTO_APOIO_CONSUMIDORES.md).
- [x] `lab/teste/callback-list-copy-stress` · `8fff68f221` — somente regressão de cópia de callback sobre lab fixo; 105/105. [Registro](FECHAMENTO_APOIO_CONSUMIDORES.md).
- [x] `lab/teste/bump-cache-ready-path` · `7e6271af68` — somente caminho pronto; 105/105. [Registro](FECHAMENTO_APOIO_CONSUMIDORES.md).
- [x] `lab/teste/bump-cache-diagnostics-memory` · `c6fc301116` — somente diagnóstico bump; 105/105. [Registro](FECHAMENTO_APOIO_CONSUMIDORES.md).
- [x] `lab/teste/bump-shared-programs` · `623d9daf2f` — somente pool bump, pré-requisito já integrado; 105/105. [Registro](FECHAMENTO_APOIO_CONSUMIDORES.md).
- [x] `lab/teste/sbheap-cancel-contract` · `0a599812df` — somente documentação de cancelamento; 105/105. [Registro](FECHAMENTO_APOIO_CONSUMIDORES.md).

- [x] `lab/teste/scxml-attribute-alias` · `5b1b44b1a8` — somente a correção de alias de atributos sobre o lab fixo; 105 testes aprovados em Linux com threads. Conservar até Windows/publicação.

- [x] `lab/teste/gl-render-exceptions` · `06bcd67d50` — pré-requisito SoAction seguido somente do delta GL; 107 testes aprovados em GLX com threads, base fixa preservada.

- [x] `lab/teste/action-apply-exceptions` · `c1aae576bf` — somente a correção de `SoAction::apply` sobre o lab fixo; 106 testes com threads aprovados no Linux. Conservar até Windows/publicação.

- [x] `lab/teste/glglue-lifetime-complete` · `c71f71965c` — 107 testes com threads; somente delta do glue, base fixa preservada.

- [x] `lab/teste/gl-contexts-small-complete` · `547caebd30` — 107 testes com SbSmallMap como dependência separada; base fixa preservada.

- [x] `lab/teste/sbsmallmap-complete` · `52ed877e66` — 106 testes aprovados; contribuição equivalente à pré-PR, conservar até Windows/publicação.

- [x] `lab/teste/sbhash-complete` · `10cfd24634` — 106 testes aprovados; delta idêntico ao da pré-PR, conservar até Windows/publicação.

As marcas nesta seção se referem à validação do snapshot. Esses labs não são branches de PR.

- [x] `lab/open-prs-integration` · `b27e37a6dd` — base fixa com os 17 PRs; conjunto de 105 testes aprovado. Preservar durante cada ciclo.
- [x] `lab/teste/cc-dict-complete` · `44eedede9c` — 108 testes aprovados; conservar até terminar Windows/publicação.
- [x] `lab/teste/cc-hash-complete` · `0505c1caed` — 106 testes aprovados; conservar até terminar Windows/publicação.
- [x] `lab/teste/sbdict-complete` · `e464ab0cb4` — 112 testes com o pré-requisito cc_dict identificado; conservar até terminar Windows/publicação.
- [x] `lab/teste/bump-fixture-static` · `f780ab9a44` — 105 testes aprovados; conservar até terminar Windows/publicação.
- [x] `lab/teste/cc-hash-api` · `2dcf47b703` — snapshot preliminar com 106 testes aprovados; contribuição substituída pelo fechamento cc_hash. Preservar até Windows/publicação.
- [ ] `bench/sbhash-relink-local` · `26a41d7e26` — reusar as ferramentas e conferir medições contra os heads finais.
- [ ] `lab/map-instrumentation` · `63ab43b305` — consolidar instrumentação aplicável e encerrar o lab auxiliar.
- [ ] `lab/gl-bump-smallmap-evaluation` · `9cc9dea628` — conferir medições de mapas e consumidores bump e encerrar o lab auxiliar.
- [ ] `lab/sb-lists-evaluation` · `0d25cf7969` — conferir resultados aplicáveis ao armazenamento dos mapas e encerrar o lab auxiliar.
- [ ] `lab/cc_round2_tsan` · `030ecbe4c9` — conferir o que ainda é evidência útil dos consumidores concorrentes; não pressupõe que cc_dict seja internamente thread-safe.

## Branches históricas preservadas

Encerrar a auditoria de conteúdo antes de eliminar qualquer uma destas referências. A presença de `archive/` no nome não significa que essa conferência terminou.

- [ ] `archive/lab-gl-bump-smallmap-evaluation-legacy-20261002` · `d7d43b44c8` — conferir diferenças úteis frente ao lab atual.
- [ ] `archive/lab-map-instrumentation-legacy-20261002` · `f372e0b7cf` — conferir instrumentação aproveitável.
- [ ] `archive/lab-sb-lists-evaluation-legacy-20261002` · `fcb6a26ad0` — conferir evidências do armazenamento.
- [ ] `archive/local-pre-fork-sync-20261003/codex/cc-oom-consumers` · `a0b1142312` — conferir conteúdo contra o head publicado de #772.
- [ ] `archive/superseded/gl-bump-contexts-small-20260919` · `5d72d968a3` — conferir conteúdo substituído pelas migrações atuais.

## Contribuições planejadas

As linhas abertas abaixo são propostas para as branches finais, ainda não criadas, ou ações pendentes. As linhas marcadas registram as contribuições já concluídas. Aproveitar os candidatos existentes e abrir uma contribuição somente quando houver delta próprio.

- [x] `codex/pre-pr/cc-hash-complete` — criada e concluída no Linux, com contribuição isolada sobre os pré-requisitos.
- [x] `codex/pre-pr/sbdict-complete` — criada e fechada no Linux: migração, cópia, callbacks, listas, exemplos LLP64 e compatibilidade.
- [x] `codex/pre-pr/sbhash-complete` — criada e fechada no Linux; requisitos de inserção, cópia, primos e noexcept/relink/lazy/OOM registrados.
- [x] `codex/pre-pr/sbsmallmap-complete` — criada e fechada no Linux; contrato completo de igualdade corrigido, falhas e propriedade de ponteiros testadas.
- [x] Revisão Linux de #758 — nova branch pré-PR reconciliada; caches, falhas, cinco contextos e limpeza validados. Head publicado aguarda transporte após Windows.
- [ ] Decidir os experimentos adaptive/sequential — aprovar com medições e revisão, corrigir ou encerrar com justificativa.
- [ ] Definir contribuição de cc_namemap/SbName depois da auditoria do internamento e do pool candidato.
- [x] `codex/pre-pr/action-apply-exceptions` · `b6afd3f1d7` — criada diretamente sobre master; referências, lock, estado, lista compacta e profiler restaurados sob exceção. [Fechamento Linux](FECHAMENTO_SOACTION_APPLY.md). Windows pendente.
- [x] `codex/pre-pr/gl-render-exceptions` · `2f47f7fcf3` — branch empilhada em SoAction; restaura estado de renderização, caminhos e vínculo WBOIT sob exceção. GLX e conjuntos Linux aprovados; [registro](FECHAMENTO_GL_RENDER_EXCEPTIONS.md). Windows pendente.
- [x] Decisão do helper de primos: `primep.h` idêntico nas três branches será integrado uma vez; manter `coin_geq_prime_number()` legado sem mudar semântica. Conferir o transporte na publicação. [Auditoria](FECHAMENTO_CANDIDATOS_CENTRAIS.md).
- [ ] Conferir testes úteis das branches substituídas e eliminar redundâncias somente depois da transferência.
- [ ] Levar os heads pré-PR concluídos ao Windows, registrar resultados e corrigir o que falhar.
- [ ] Antes de publicar, validar cada branch sobre seu master de destino com diff exclusivo da contribuição.
- [ ] Encerrar as cópias temporárias ao fim de cada ciclo de validação/publicação.

## Sequência de fechamento

1. `cc_dict`, `cc_hash`, `SbDict` e fixture: concluídos no Linux; próximos passos Windows e publicação.
2. `cc_hash` anterior: conteúdo reconciliado com os fechamentos; preservar referências antigas até publicação.
3. `SbDict` anterior: conteúdo residual e fixture conferidos; preservar referências antigas até publicação.
4. `SbHash`: concluído no Linux; Windows/publicação pendentes.
5. `SbSmallMap` e quatro consumidores GL do #758: concluídos no Linux; Windows/transporte pendentes.
6. Lifetime do glue, epílogo de `SoAction::apply` e estado de `SoGLRenderActionP::render`: concluídos nos conjuntos Linux; Windows pendente.
7. Experimentos, benchmarks e branches históricas: decidir e consolidar.

O [inventário de contratos](DICT_CC_SB.md) e o [controle de branches](CONTROLE_BRANCHES.md) complementam esta checklist com a composição da base e os registros de validação.
