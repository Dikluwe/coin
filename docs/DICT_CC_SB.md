# Dicionários CC e Sb

Este chat concentra o estudo e a organização dos dicionários e mapas do Coin. O trabalho imediato é reconciliar as correções existentes, completar os contratos públicos e preparar contribuições com dependências claras. A base upstream verificada em 6 de outubro de 2026 é `origin/master` em `674e74267df863dbaf50416c477bc7f918a826d8`.

O [controle de branches](CONTROLE_BRANCHES.md) mantém a composição do lab e o estado geral dos trabalhos. Aplicar o [método de branches](METODO_BRANCHES.md): comparar com master, testar no lab futuro e transportar somente a contribuição proposta para a branch de PR. Correções incorporadas no lab ainda dependem de aceitação upstream.

A [checklist do grupo](CHECKLIST_DICT_CC_SB.md) reúne as branches existentes, as contribuições planejadas e o que já está concluído no Linux.

## Famílias e responsabilidades

| Componente | Papel | Representação e dependências | Direção do trabalho |
| --- | --- | --- | --- |
| `cc_dict` | Dicionário C privado usado por heap, fontes, GL, storage, sync e scheduler | Buckets encadeados, chaves `uintptr_t`, valores `void *`, pool `cc_memalloc` | Robustez e comunicação de falha aos consumidores |
| `cc_hash` | API C pública obsoleta com clientes externos | Implementação paralela a `cc_dict`; mantém símbolos exportados | Preservar API e ABI, corrigir rehash, OOM e travessia |
| `SbDict` | API C++ pública compatível com Open Inventor | Usa `cc_hash` em master; a migração candidata usa `cc_dict` atrás do ponteiro privado | Manter comportamento externo e testar a mudança de backend |
| `SbHash` | Template C++ privado usado por registros e caches internos | Buckets encadeados e pool de entradas; contratos de cópia, exceções e iteradores | Concluir a sequência publicada e reconciliar OOM com armazenamento lazy |
| `SbSmallMap` | Mapa privado para poucos elementos | Busca linear, armazenamento contíguo em `SbList`, quatro entradas inline | Aplicar aos consumidores cujo perfil favorece o mapa pequeno |
| `SbAdaptiveMap` | Experimento de mapa com promoção de armazenamento | Inline, vetor e hash conforme quantidade de entradas | Auditar contratos e benefício antes de novas migrações |
| `SbSequentialMap` | Experimento para chaves inteiras | Vetor ordenado ou indexação direta com ocupação | Medir chaves densas e esparsas, memória e ordem observável |
| `cc_namemap` e `SbName` | Internamento de nomes | Identidade e vida útil dos nomes têm contrato próprio | Manter como assunto associado, com auditoria separada |

`SbDict` e `SbHash` não são fachadas equivalentes. O primeiro é uma API pública de valores não proprietários; o segundo é um template interno com nós que contêm objetos C++. `cc_hash` e `cc_dict` também são implementações distintas em master.

## O que já está em master

| Mudança | PR integrado | Resultado |
| --- | --- | --- |
| `cc_dict` | [#726](https://github.com/coin3d/coin/pull/726) | Reindexação ao trocar o hash, reciclagem de entradas após resize e estatísticas vazias |
| Autoatribuição de `SbHash` | [#750](https://github.com/coin3d/coin/pull/750) | Preserva os elementos na autoatribuição |
| Iteradores de `SbHash` | [#751](https://github.com/coin3d/coin/pull/751) | Travessia mutável e const com estados completos |
| Estatísticas de `SbHash` | [#752](https://github.com/coin3d/coin/pull/752) | Cálculo corrigido |
| Crescimento de `SbList` | [#756](https://github.com/coin3d/coin/pull/756) | Segurança de exceções para o armazenamento usado por mapas pequenos |
| `SbSmallMap` e cache do profiler | [#757](https://github.com/coin3d/coin/pull/757) | Tipo e primeiro consumidor integrados |
| Cache bump com mapa pequeno | [#760](https://github.com/coin3d/coin/pull/760) | Redução de alocações no consumidor bump |

Os estudos antigos ainda descrevem várias dessas mudanças como propostas. Usar o estado dos PRs e o código da referência indicada acima para decidir o trabalho atual.

## PRs abertos

| PR | Branch e head verificado | Contribuição | Próxima ação |
| --- | --- | --- | --- |
| [#769](https://github.com/coin3d/coin/pull/769) | `codex/cc-dict-resize-followup` · `dacbbd984d` | Relink e preservação da tabela se a alocação opcional do resize falhar | Acompanhar revisão; conservar esta branch enquanto o PR estiver aberto |
| [#770](https://github.com/coin3d/coin/pull/770) | `codex/cc-memalloc-hardening` · `729a5b9cd5` | Alinhamento, limites e falhas do pool | Validar o conjunto com os consumidores |
| [#771](https://github.com/coin3d/coin/pull/771) | `codex/cc-dict-hardening` · `5a311648e3` | Limites numéricos, construção, inserção com resultado explícito e remoção da entrada atual em `apply` | Conferir contrato e integração após os pré-requisitos |
| [#772](https://github.com/coin3d/coin/pull/772) | `codex/cc-oom-consumers` · `6a169aaac4` | Propagação de OOM e política dos consumidores, incluindo primitivas e `SbHash` | Reconciliar com os heads atuais de `SbHash` e acompanhar revisão |
| [#753](https://github.com/coin3d/coin/pull/753) | `fix/sbhash-04-noexcept-contract` · `4810469369` | Contrato de hash sem exceções | Validar tipos e conversões usados pelos consumidores |
| [#754](https://github.com/coin3d/coin/pull/754) | `fix/sbhash-05-relink-resize` · `25a0c03d62` | Resize sem copiar chaves e valores | Manter em sequência com o contrato de hash |
| [#755](https://github.com/coin3d/coin/pull/755) | `fix/sbhash-06-lazy-storage` · `7fa9bae533` | Adiamento da alocação dos buckets e pool | Comparar com o candidato de falha na inserção e com #772 |
| [#758](https://github.com/coin3d/coin/pull/758) | `codex/maps/use/gl-contexts-small` · `900f11a157` | Mapas pequenos para caches de contexto GL | Revisão Linux concluída; validar Windows e transportar o complemento da pré-PR |

Esses oito heads estão incorporados no lab fixo `lab/open-prs-integration` em `b27e37a6dd`, mas não são ancestrais de master. As dependências de conteúdo não devem ser inferidas apenas de uma cadeia de commits: os PRs receberam merges e resoluções independentes.

## Trabalhos sem PR

| Branch | Conteúdo atual | Destino |
| --- | --- | --- |
| `codex/pre-pr/sbsmallmap-complete` · `ff1d09ddf3` | Tipo, contratos e regressões de falha | Fechado no Linux sobre master/fixture; Windows pendente |
| `codex/pre-pr/sbhash-complete` · `5112bbcaf0` | Cópia interrompida, primos/capacidade e falhas | Fechado no Linux; Windows pendente |
| `codex/pre-pr/cc-dict-complete` · `9b9237f13d` | Complemento final de hash/primos/capacidade de cc_dict | Fechado no Linux; validar Windows antes de publicar |
| `codex/pre-pr/cc-hash-complete` · `c5dc4c643a` | Complemento final da API C, cliente C e primos/capacidade; sem mudanças próprias de SbDict | Fechado no Linux; validar Windows e transportar para o master com pré-requisitos antes de publicar |
| `codex/cc-hash-hardening-followup` · `a60f2bea89` | Fonte anterior das correções de hash e das mudanças próprias de SbDict | Conteúdo reconciliado nos fechamentos; preservar referência até publicação |
| `codex/pre-pr/sbdict-complete` · `b5ae0d4ff3` | Migração, contratos e cliente público com compatibilidade preservada | Fechado no Linux sobre cc_dict; validar Windows antes de publicar |
| `codex/sbdict-ccdict-migration` · `77ca5b22eb` | Fonte anterior da migração, proteção do hash e fixture | Conteúdo conferido em SbDict final e fixture separado; preservar referência até publicação |
| `codex/sbdict-hash-hardening` · `2ecc31d7cf` | Correção inicial deste chat, anterior às revisões de API, OOM e callbacks | Testes e contrato reconciliados com cc_hash/SbDict finais; preservar referência até publicação |
| `codex/work/sbhash-insert-allocation-failure` · `5c9bc244ea` | Candidato antigo com crescimento lançador; premissa reconciliada com crescimento opcional de #772 | Delta SbHash revisado no fechamento; preservar até publicação |
| `codex/coin-prime-boundary` · `1d087a752d` | Escolha de primo inicial, política geométrica de crescimento e limite de buckets em 32 bits | Conteúdo absorvido pelos três fechamentos; helper legado mantido sem mudança semântica |
| `codex/maps/core/adaptive-map` e seus consumidores | Tipo adaptativo e experimentos em CoinResources e SCXML | Auditoria concluída: promoção insegura; correções funcionais de alias SCXML isoladas, sem migração de mapa. [Fechamento](FECHAMENTO_MAPAS_CONSUMIDORES.md) |
| `codex/maps/core/sequential-map`, `codex/maps/integration/profiler-containers` e `codex/maps/use/profiler-action-timings-sequential` | Tipo para chaves inteiras e experiências no profiler | Tipo reprovado por chave negativa invisível em Release; lifetime do profiler corrigido em branch independente. [Fechamento](FECHAMENTO_MAPAS_CONSUMIDORES.md) |
| `codex/maps/use/fieldcontainer-mfield-sizes-hash` e `codex/maps/use/scxml-type-registry-hash-name` | Migrações pontuais para hash | Sem defeito exclusivo nem medição que justifique as tabelas; preservar fontes experimentais. [Fechamento](FECHAMENTO_MAPAS_CONSUMIDORES.md) |
| `bench/sbhash-relink-local`, `lab/map-instrumentation` e `lab/gl-bump-smallmap-evaluation` | Benchmarks e instrumentação anteriores | Aproveitar somente as medições e ferramentas aplicáveis aos heads atuais; consolidar e encerrar versões substituídas |
| `codex/sbname/experiment/compact-entry-pool` | Pool experimental de internamento | Identidade normal validada; falhas e limites de alocação ainda impedem PR. [Fechamento](FECHAMENTO_MAPAS_CONSUMIDORES.md) |

As fontes de apoio `cc-list-hardening`, `improve-sblist`, `cc-test-audit` e os três estudos bump foram auditadas no [fechamento de apoio](FECHAMENTO_APOIO_CONSUMIDORES.md). Os contratos ainda úteis de `cc_list`, `SbList`, `SbPList`, cache bump e `SbHeap`, além da regressão de cópia de `SoCallbackList`, estão em pré-PRs isoladas sobre master, cada uma testada também sobre o lab fixo. A fonte `cc-test-audit` não altera o fechamento de dict/hash; worker/scheduler seguem como assunto de threads.

Os heads de `cc_hash`, migração de `SbDict`, correção de inserção e primos não estão incorporados no lab fixo. As branches de `cc_hash` e da migração carregam versões anteriores de pré-requisitos; a migração ser descendente de `cc_hash` não demonstra compatibilidade com todos os PRs publicados atuais.

## Contratos que orientam as correções

| Operação | Contrato a preservar ou explicitar |
| --- | --- |
| Inserção C | Diferenciar chave nova, sobrescrita e falha de recursos no caminho privado; evitar reinterpretar silenciosamente um retorno público existente |
| Resize C | Falha dos buckets opcionais conserva a tabela e a chave já inserida; uma inserção futura pode tentar crescer |
| `SbHash::put` | Exceção ao copiar entrada não publica chave; crescimento opcional com falha conserva inserção bem-sucedida e permite retry |
| Troca de hash | Entradas continuam acessíveis; falha de alocação ou exceção do hash conserva as cadeias anteriores |
| Hash nulo | O candidato externo restaura a função padrão; os pré-requisitos de `cc_dict` publicados ainda precisam ser conciliados com essa semântica da migração |
| Travessia C | Ler e remover apenas a entrada atual é o contrato candidato; inserir, limpar, rehash ou destruir a mesma tabela durante o callback não faz parte dele |
| Cópia de `SbDict` | Autoatribuição já está protegida nas branches posteriores; definir e testar cópia comum, política de hash e independência dos dicionários |
| `makePList` | Conferir o acréscimo às listas recebidas, o pareamento entre chaves e valores e a ausência de promessa de ordem |
| Mapas pequenos e adaptativos | Preservar propriedade de recursos se a inserção ou promoção falhar; medir memória incluindo armazenamento mantido após `clear` |
| ABI pública | Conservar símbolos e layout; conferir clientes compilados com headers antigos e builds DLL e estáticos |

## Prioridades

1. **Validar os fechamentos no Windows.** cc_dict, cc_hash, SbDict, SbHash, SbSmallMap e fixture estão prontos na etapa Linux. Conferir as dependências e os diffs próprios antes de publicar.
2. **Corrigir o epílogo de SoAction::apply sob exceção.** O lifetime do glue concluiu o ciclo Linux, incluindo liberação, borrowers e IDs extremos. Falta fechar a retenção da raiz e a restauração/desbloqueio na rota lançadora da ação.
3. **Conferir as branches fontes substituídas.** Os testes públicos de SbDict cobrem cópia comum, autoatribuição, hashes, ambas as variantes de callback, listas preenchidas e modelo. A cópia normal mantém o hash padrão histórico; Windows x64 confirma a etapa LLP64. Transportar apenas algum caso útil que ainda não tenha equivalente antes de encerrar referências antigas.
4. **Conferir capacidade e falha em cada camada.** Avaliar fatores não finitos, limites de `unsigned int`, estouro de bytes, primo máximo, retorno zero do helper e falha em construção, buckets e entrada. Usar os PRs existentes para evitar patches duplicados.
5. **Reavaliar otimizações com a base final.** Medir `SbSmallMap`, `SbAdaptiveMap` e `SbSequentialMap` contra `SbHash` com relink e lazy storage. Preservar a separação entre defeito funcional, contrato e experimento de desempenho.

## Validação atual

O lifetime do glue está fechado no Linux em `codex/pre-pr/glglue-lifetime-complete` (`0d31242881`): **80 testes sobre master e 107 no lab, threads ativadas, 72 gerações, IDs extremos e ASan/UBSan/LSan sem exclusões do glue**. Borrowers e integração com os quatro caches GL validados; Windows pendente. [Registro](FECHAMENTO_GLGLUE_LIFETIME.md).

Os quatro consumidores GL #758 estão revisados no Linux em `codex/pre-pr/gl-contexts-small-complete` (`87cc9003e9`): **107 CTests no lab com dependência SbSmallMap, 80 na base de master, cinco contextos GLX, falhas dos quatro mapas, limpeza e ASan/UBSan**. LeakSanitizer usa exclusão restrita dos registros do glue; esse lifetime e SoAction::apply sob exceção são achados separados. [Registro](FECHAMENTO_GL_CONTEXT_MAPS.md).

O tipo `SbSmallMap` está fechado no Linux em `codex/pre-pr/sbsmallmap-complete` (`ff1d09ddf3`): **106 testes no lab, 79 sobre master/fixture estático, ASan/UBSan e contrato de igualdade positivo/negativo**. Consumidores #758 concluíram o gate Linux próprio; Windows/publicação continuam pendentes; [registro](FECHAMENTO_SBSMALLMAP.md).

O `SbHash` está fechado no Linux em `codex/pre-pr/sbhash-complete` (`5112bbcaf0`): **106 testes no lab, 91 na pré-PR estática, ASan/UBSan, reprodução do vazamento anterior e contrato noexcept positivo/negativo**. Windows/publicação pendentes; [registro e dependências](FECHAMENTO_SBHASH.md).

O escopo conhecido de `SbDict` foi fechado no Linux em `codex/pre-pr/sbdict-complete` (`b5ae0d4ff3`): **112 testes no lab com cc_dict como pré-requisito explícito e 97 na pré-PR estática**, cliente com header antigo e SbDict/backend/pool com ASan/UBSan. Foram preservados o header histórico, tamanho/alinhamento, 15 símbolos de SbDict e dez de cc_hash. O [fechamento de SbDict](FECHAMENTO_SBDICT.md) registra decisões, regressões anteriores e passagem Windows.

O escopo conhecido de `cc_hash` foi fechado no Linux em `codex/pre-pr/cc-hash-complete` (`c5dc4c643a`), com **106 testes no lab Debug, 91 na pré-PR Release estática, cliente C compilado com header antigo e hash/pool instrumentados com ASan/UBSan**. Os dez símbolos públicos foram preservados; as mudanças próprias de SbDict ficaram fora desse delta. A branch está pronta para Windows; ver [fechamento de cc_hash](FECHAMENTO_CC_HASH.md).

O escopo conhecido de `cc_dict` foi fechado para a etapa Linux em `codex/pre-pr/cc-dict-complete` (`9b9237f13d`). Inclui proteção de exceções durante rehash, reset do hash padrão e política de primos/capacidade. Passaram **108 testes no lab Debug compartilhado, 93 na base pré-PR Release estática e os oito cenários instrumentados com ASan/UBSan**. O fixture estático tem pré-requisito separado. A branch está pronta para a validação Windows antes da publicação; decisões, dependências e comandos estão no [fechamento de cc_dict](VALIDACAO_CC_DICT.md).

O candidato preliminar de `cc_hash`, `codex/work/cc-hash-api` (`ebae1bd653`), passou na época em **9 regressões direcionadas e 106 testes do conjunto** no lab. Ele foi substituído pelo fechamento `codex/pre-pr/cc-hash-complete`, validado também sobre sua base de pré-requisitos para publicação. [Registro anterior](VALIDACAO_CC_HASH.md) e [auditoria das fontes](FECHAMENTO_CANDIDATOS_CENTRAIS.md).

Em 6 de outubro, os builds Release existentes estavam atualizados para os fontes dos respectivos worktrees. A seleção `^(CoinTests|CcHash)` passou em `codex/cc-hash-hardening-followup`: **12 CTests**, incluindo testes do runner. A seleção `^(CoinTests|CcDict|CcHash)` passou em `codex/sbdict-ccdict-migration`: **18 CTests**, incluindo runner, OOM, remoção da entrada atual e exceção no hash de `cc_dict`.

Logs dessa conferência anterior: `/tmp/dict-inventory-hash-tests.log` e `/tmp/dict-inventory-sbdict-tests.log`. A reconciliação posterior está em [Fechamento dos candidatos centrais](FECHAMENTO_CANDIDATOS_CENTRAIS.md); Windows DLL, estático e ABI dos heads finais continuam pendentes.

## Referências de estudo

Os estudos detalhados existentes ficam em `/home/dikluwe/Área de trabalho/Estudo coin/estudos/`: `cc/cc_dict.md`, `cc/cc_hash.md`, `Sb/SbDict.md`, `Sb/SbHash.md` e `Sb/SbSmallMap.md`. Eles incluem história e experimentos, mas seus rótulos de status não substituem o estado atual de master e dos PRs.

O [comentário do #726](https://github.com/coin3d/coin/pull/726#issuecomment-5886621445) continua sendo a origem do follow-up de resize. O pico de memória depende da estratégia do pool; a eliminação de realocações e de crescimento recursivo não implica que toda execução anterior duplicasse o número de entradas ativas.
