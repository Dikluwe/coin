# Checklist de branches XML e SCXML

Estado local em 2026-10-06. Esta lista cobre as refs locais cujo nome contém `xml`/`scxml` (não há refs remotas com esses nomes neste clone) **e uma branch legada com conteúdo XML/SCXML exclusivo**, além das contribuições ainda planejadas. O [controle de branches](CONTROLE_BRANCHES.md#frente-xml-e-scxml) guarda os SHAs completos, comandos e resultados dos ciclos já executados.

**Critério de “pronto no Linux”:** contribuição isolada em `codex/pr/*`, compilada e testada tanto numa cópia temporária do lab fixo `b27e37a6dd3ae941bcffcf480a377adc81e3fc27` quanto sobre o destino de PR, com diff conferido. O destino normal é `master` (`674e74267df863dbaf50416c477bc7f918a826d8`); uma contribuição dependente declara e testa sua branch de pré-requisito, que por sua vez parte desse master. Não significa aprovada no Windows, publicada ou testada em combinação com todas as demais candidatas desta lista.

## Prontas no Linux; Windows e publicação pendentes

- [x] **Expat embarcado 2.9.0:** pré-PR local `codex/pre-pr/cc-xml-expat-2.9.0`; mínimo C99 declarado só para o vendorizado. Master 78/78, lab individual 105/105, biblioteca única compilada; matriz cumulativa abaixo: 108/108 com Expat embarcado e 108/108 com Expat externo 2.6.1. A política de `DOCTYPE` não mudou.
- [x] **API/ABI de entidades:** trabalho `codex/fix/cc-xml-entity-abi` → PR local `codex/pr/cc-xml-entity-abi`. Lab 107/107; master 80/80; testes específicos 2/2 em ambos. Os headers C isoláveis já estão no master (`c973e1dd3d`); esta contribuição preserva os símbolos C++ legados como bridges ABI.
- [x] **`cc_xml_path` — laços, cópia e truncamento:** trabalho `codex/fix/cc-xml-path-loops-truncate` → PR local `codex/pr/cc-xml-path-loops-truncate`. Lab 105/105; master 78/78; 2 testes específicos, 30 checks, em ambos.
- [x] **Escaping e inteiros `PRI*`/`SCN*`:** trabalho `codex/fix/cc-xml-escaping-inttypes` → PR local `codex/pr/cc-xml-escaping-inttypes`. Lab 105/105; master 78/78; 2 testes específicos, 33 checks, em ambos.
- [x] **Parser transacional, erros, `ferror()` e reuso:** trabalho `codex/work/cc-xml-parser-transactional` → PR local `codex/pr/cc-xml-parser-transactional`. Lab 106/106; master 79/79; teste específico 1/1 em ambos. Não inclui a política de `DOCTYPE`.
- [x] **Coalescência de character data:** origem `codex/fix/cc-xml-coalesce-cdata` → trabalho `codex/work/cc-xml-coalesce-cdata` → PR local `codex/pr/cc-xml-coalesce-cdata`. Lab 105/105; master 78/78; 5 testes específicos com 82 checks em ambos. A origem antiga permanece até a auditoria de conteúdo exclusivo.
- [x] **Ownership de raiz, atributos e filhos:** origem `codex/fix/cc-xml-dom-ownership-hardening` → trabalho `codex/work/cc-xml-dom-ownership` → PR local `codex/pr/cc-xml-dom-ownership`. Lab 105/105; master 78/78; 3 testes específicos/29 checks e API C em ambos. ASan/UBSan com detecção de vazamentos passou nos testes específicos e no cliente C. A raiz anterior agora é apagada na substituição; `release_root_x()` permite preservá-la. O restante da origem antiga foi extraído na contribuição dependente abaixo.
- [x] **Regressões DOM e fuzzing:** origem `codex/fix/cc-xml-dom-ownership-hardening` → trabalho `codex/work/cc-xml-dom-regressions-fuzz` → PR local `codex/pr/cc-xml-dom-regressions-fuzz`, **dependente de `codex/pr/cc-xml-dom-ownership`**. Lab + ownership: 105/105; destino ownership/master: 78/78; 5 testes DOM/43 checks em ambos. Clang + ASan/UBSan/LeakSanitizer: regressões, cliente C e smoke libFuzzer (100 execuções) aprovados. O diff próprio só contém comparação DOM, proteção de CDATA nula, sanitizers e fuzzer.
- [x] **SCXML `ScXMLSendElt` — tokenização com `SbString`/`SbList`:** trabalho `codex/scxml-sendelt-tokenize-sbstring` → PR local `codex/pr/scxml-sendelt-tokenize`. Lab 105/105; master 78/78; 1 teste específico, 9 checks, em ambos.
- [x] **SCXML `ScXMLEvent` — ownership dos valores de associações:** trabalho `codex/work/scxml-event-association-ownership` → PR local `codex/pr/scxml-event-association-ownership`. Lab 105/105; master 78/78; 2 testes específicos/8 checks em ambos. Clang + ASan/UBSan/LeakSanitizer: 2 testes/8 checks; combinação temporária com `ScXMLSendElt` passou 3 testes/17 checks, incluindo o caso que revelava o vazamento. Não inclui a migração legada de armazenamento.
- [ ] Repetir o gate de cada PR no Windows antes de qualquer publicação. Preservar as refs locais e levar também esta documentação (ainda não rastreada no checkout atual); os worktrees em `/tmp` não são o meio de transporte.

## Branches existentes, ainda não prontas no Linux

- [ ] `codex/fix/cc-xml-reject-doctype` — contém **dois commits de assuntos distintos**. O parser transacional (`0de5e2339d`) já foi extraído e validado; a rejeição de `DOCTYPE` (`bd8287e327`) foi extraída para a branch de trabalho abaixo, mas depende de confirmação da política e do gate lab/PR antes de encerrar a origem.
- [ ] `codex/work/cc-xml-doctype-policy` — candidata local **dependente de `codex/pr/cc-xml-parser-transactional`** em `8100371983c0fd19de9950373050fef188cdaf32`, com rejeição explícita de qualquer `DOCTYPE`. Build em RAM, regressão transacional 1/1 e conjunto 79/79 sobre o destino dependente passaram. A política ainda aguarda confirmação; sem branch de PR e sem gate no lab fixo. Se a decisão for suportar DTD, reavaliar esta candidata em vez de publicá-la.
- [ ] `codex/maps/use/scxml-attributes-adaptive` — extrair o delta de `ScXMLElt` da infraestrutura antiga `SbAdaptiveMap`/`SbHash`; declarar pré-requisito de mapas e validar isoladamente.
- [ ] `codex/maps/use/scxml-document-ids-adaptive` — extrair `ScXMLDocument` e preservar a semântica do primeiro ID duplicado; reconciliar `SbHashName` e validar.
- [ ] `codex/maps/use/scxml-evaluator-temporaries-adaptive` — extrair mudanças de `ScXMLEventTarget`, navegação e avaliador Coin; reconciliar `SbHashName` e validar.
- [ ] `codex/maps/use/scxml-type-registry-hash-name` — extrair registry `ScXMLP`; reconciliar `SbHash`/`SbHashName` e validar.
- [ ] `archive/lab-sb-lists-evaluation-legacy-20261002` — **legado, não base de PR**. Apesar do nome, contém commits exclusivos de SCXML (`ScXMLEvent`/associações compactas, preservação de valores com alias, remoção de mapa ocioso em `ScXMLObject`) e de `cc_xml_load_file()` (tratamento de `ftell()` e export para teste no Windows). Auditar cada delta contra as branches atuais, extrair o que ainda for útil para trabalhos separados e só então encerrar o arquivo legado. Não transportar o merge inteiro.

As quatro branches SCXML de mapas têm pré-requisitos cruzados com `codex/maps/core/adaptive-map` e com os PRs de hash presentes no lab; não transportar seu histórico antigo inteiro para uma branch de PR.

## Contribuições planejadas; branches ainda não criadas

Os nomes abaixo são **propostas**, não refs existentes. Criar somente quando o escopo e a dependência estiverem definidos; cada candidata segue o ciclo `codex/work` → `lab/teste` temporário → `codex/pr` sobre master.

- [ ] `codex/work/cc-xml-limits` — **estudar antes de fixar valores**: bytes de entrada/expandidos, nós, atributos, profundidade e custo de serialização; cobrir exatamente limite e limite+1. Tratar a conversão pública `size_t` → `int` nos caminhos `XML_Parse()`.
- [ ] `codex/work/cc-xml-io-errors` — verificar `fopen`, escrita curta, `ferror()`/`fclose()` em `cc_xml_doc_write_to_file()`; revisar erro/truncamento de `cc_xml_load_file()` sem loop sem progresso.
- [ ] `codex/work/scxml-event-associations` — decidir se as otimizações exclusivas de `ScXMLEvent`/`ScXMLElt` no lab legado merecem contribuição própria, com regressões de alias, ordem e ownership. O vazamento dos valores já foi isolado na candidata pronta acima; qualquer migração de armazenamento deve preservar essa correção.
- [ ] `codex/work/cc-xml-expat-matrix` — atualização do vendorizado para 2.9.0 e testes embarcado/externo concluídos; falta estudar a fronteira header/biblioteca e ampliar a matriz para plataformas/versões de sistema relevantes.
- [ ] `codex/work/scxml-document-defensive-read` — verificar se `ScXMLDocument` deve rejeitar raiz nula/inesperada mesmo após a correção do parser, com teste regressivo do consumidor.

## Gate para encerrar a frente XML no Linux

- [ ] Validar cada contribuição pendente sobre uma cópia temporária do lab fixo e sobre o master de destino, registrando SHAs, comandos, testes e diff isolado.
- [x] Montar uma cópia temporária cumulativa **parser transacional + coalescência**: build em RAM, teste transacional 1/1, coalescência 5 testes/82 checks e conjunto 106/106 aprovados.
- [x] Ampliar a cópia cumulativa com **ownership**: build em RAM, parser 1/1, API C 1/1, 8 testes específicos/111 checks e conjunto 106/106 aprovados.
- [x] Ampliar a cópia cumulativa com **regressões DOM e fuzzer**: build normal em RAM, parser 1/1, 10 testes DOM/coalescência/125 checks e conjunto 106/106 aprovados; o smoke instrumentado do fuzzer foi validado na mesma candidata, isoladamente.
- [x] Montar **uma cópia temporária cumulativa de todas as nove candidatas XML/SCXML prontas escolhidas**, incluindo Expat 2.9.0: builds em RAM, conjunto 108/108 com Expat embarcado e 108/108 com Expat externo 2.6.1; 15 regressões XML/SCXML e 185 checks. Exclui a política de `DOCTYPE`, limites e SCXML de mapas, ainda não prontas. ASan/UBSan: XML 14 testes/176 checks e fuzzer smoke 1/1; o teste SCXML passa sem erro de memória imediato, mas LeakSanitizer acusa o vazamento legado de associações.
- [ ] Repetir o lab cumulativo completo incluindo a nova candidata de ownership de `ScXMLEvent`; o teste combinado instrumentado com `ScXMLSendElt` já passou, mas a matriz completa Expat + XML + SCXML ainda não foi repetida com esta décima candidata.
- [ ] Executar sanitizers/fuzzer e a matriz de Expat nos casos previstos, inclusive regressões de limites/DTD e parsing parcial.
- [ ] Depois do gate Linux, repetir a matriz acordada no Windows antes de publicar; só então eliminar branches de trabalho substituídas, após conferir commits exclusivos e worktrees.

Nenhum `lab/teste` desta frente permanece: as cópias individuais, cumulativas e a combinação instrumentada com `ScXMLSendElt` já utilizadas foram removidas. O lab fixo e as branches de PR locais permanecem intactos.

As branches `bench/honeycomb-scale-benchmark` e `bench/sbhash-relink-local` mostram diferença em `src/xml/path.cpp` apenas pela base de integração antiga; seus commits próprios são de benchmark, não novas candidatas XML. Não entram na contagem acima.
