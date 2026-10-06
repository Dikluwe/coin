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

## Candidatos centrais que ainda precisam de fechamento

- [ ] `codex/work/cc-hash-api` · `ebae1bd653` — candidato preliminar substituído pelo fechamento. Preservar e transportar as mudanças próprias de SbDict antes de encerrar. [Validação anterior](VALIDACAO_CC_HASH.md).
- [ ] `codex/cc-hash-hardening-followup` · `a60f2bea89` — fonte anterior; 12 CTests na conferência isolada. Conferir conteúdo remanescente e encerrar depois da consolidação.
- [ ] `codex/sbdict-ccdict-migration` · `77ca5b22eb` — fonte anterior, consolidada na pré-PR final; conferir conteúdo residual antes de encerrar a branch substituída.
- [ ] `codex/sbdict-hash-hardening` · `2ecc31d7cf` — versão inicial; conferir e transportar testes úteis antes de encerrar.
- [x] `codex/work/sbhash-insert-allocation-failure` · `5c9bc244ea` — auditoria do delta SbHash encerrada: premissa de exceção substituída pela política opcional de #772; testes úteis reconciliados na pré-PR. Conservar referência até publicação.
- [ ] `codex/coin-prime-boundary` · `1d087a752d` — política de primos e limite de capacidade. Deltas de cc_dict, cc_hash e SbHash já aproveitados; concluir o destino do helper antigo antes de encerrar.

## Mapas e consumidores em estudo

- [ ] `codex/maps/core/adaptive-map` · `3957f93c76` — revisar promoções, exceções, propriedade e benefício medido.
- [ ] `codex/maps/use/coinresources-adaptive` · `241764d557` — medir e validar o consumidor sobre o tipo revisado.
- [ ] `codex/maps/use/scxml-attributes-adaptive` · `8b77560293` — conferir contrato, lifetime e perfil de atributos.
- [ ] `codex/maps/use/scxml-document-ids-adaptive` · `aab125aa41` — conferir substituição, lifetime de IDs e promoção.
- [ ] `codex/maps/use/scxml-evaluator-temporaries-adaptive` · `82b9f29c78` — conferir limpeza, propriedade dos temporários e falhas.
- [ ] `codex/maps/core/sequential-map` · `3ba09c803a` — revisar chaves densas/esparsas, limites, ordem observável e memória.
- [ ] `codex/maps/integration/profiler-containers` · `ba0ed99669` — separar o experimento agregado em contribuições justificadas.
- [ ] `codex/maps/use/profiler-action-timings-sequential` · `edaaec9b5c` — medir o perfil real e conferir a ordem de travessia.
- [ ] `codex/maps/use/fieldcontainer-mfield-sizes-hash` · `339a1d6b49` — conferir diff exclusivo, resultados e lifetime das chaves.
- [ ] `codex/maps/use/scxml-type-registry-hash-name` · `eb1d50e0d2` — conferir hash, identidade e lifetime dos nomes.
- [ ] `codex/sbname/experiment/compact-entry-pool` · `7b439fb7c2` — auditar internamento, identidade e lifetime antes de decidir a contribuição.

## Apoio e consumidores associados

Estas branches têm ações registradas na organização do grupo; não são declaradas prontas por terem servido de base para um experimento.

- [ ] `codex/cc-list-hardening` · `42815f7385` — conferir o conteúdo já absorvido pela sequência de OOM e o delta restante.
- [ ] `codex/improve-sblist` · `e506a1c4fe` — conferir bounds e falhas de valores contra #756 integrado; aproveitar somente o que ainda faltar aos mapas.
- [ ] `codex/cc-test-audit` · `95458242d7` — conferir poda de regressões e cobertura de dict/hash antes de consolidar.
- [ ] `codex/work/bump-cache-ready-path` · `bb6158a01e` — medir o caminho pronto e validar o retorno sem inicializar diagnóstico.
- [ ] `codex/work/bump-cache-diagnostics-memory` · `c098efdfb6` — validar redução de memória, mensagens longas e OOM.
- [ ] `codex/work/bump-shared-program-pool` · `191a685710` — isolar e validar compartilhamento, contexto, propriedade e concorrência.
- [x] `codex/work/glglue-lifetime-audit` · `5d28cf8e95` — estudo encerrado no Linux: política de empréstimo explicitada, borrowers validados e correção em `codex/pre-pr/glglue-lifetime-complete`. Conservar a fonte experimental até publicação.
- [ ] `codex/work/sbheap-cancel-documentation` · `83925a4743` — conferir contrato do cancelamento e preparar contribuição de documentação.

## Labs e benchmarks

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
- [x] `lab/teste/cc-hash-api` · `2dcf47b703` — 106 testes aprovados; fechamento do candidato cc_hash ainda pendente.
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
- [ ] `codex/pre-pr/action-apply-exceptions` — planejada, ainda não criada: corrigir referência da raiz, desbloqueio/restauração sob exceção em SoAction::apply; reprodutor mínimo confirmou refs 1→2. [Evidência](FECHAMENTO_GL_CONTEXT_MAPS.md).
- [ ] Consolidar o helper de primos depois de distribuir os deltas dos três hashes; evitar duplicação entre branches.
- [ ] Conferir testes úteis das branches substituídas e eliminar redundâncias somente depois da transferência.
- [ ] Levar os heads pré-PR concluídos ao Windows, registrar resultados e corrigir o que falhar.
- [ ] Antes de publicar, validar cada branch sobre seu master de destino com diff exclusivo da contribuição.
- [ ] Encerrar as cópias temporárias ao fim de cada ciclo de validação/publicação.

## Sequência de fechamento

1. `cc_dict`, `cc_hash`, `SbDict` e fixture: concluídos no Linux; próximos passos Windows e publicação.
2. `cc_hash` anterior: transportar o material próprio de SbDict e encerrar as branches substituídas quando seguro.
3. `SbDict` anterior: conferir conteúdo residual e encerrar as branches substituídas quando seguro.
4. `SbHash`: concluído no Linux; Windows/publicação pendentes.
5. `SbSmallMap` e quatro consumidores GL do #758: concluídos no Linux; Windows/transporte pendentes.
6. Lifetime do glue: concluído no Linux; Windows pendente. Próximo: epílogo de SoAction::apply sob exceção.
7. Experimentos, benchmarks e branches históricas: decidir e consolidar.

O [inventário de contratos](DICT_CC_SB.md) e o [controle de branches](CONTROLE_BRANCHES.md) complementam esta checklist com a composição da base e os registros de validação.
