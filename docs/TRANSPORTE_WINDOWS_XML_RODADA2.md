# Transporte XML/SCXML para Windows — rodada 2 (2026-10-06)

Estas seis candidatas adicionais passaram os gates Linux descritos abaixo e seguem para validação Windows. A publicação de branches no fork não abre PR. Este manifesto atualiza os estados históricos “somente local/publicação pendente” dos documentos copiados; a validação Windows destes heads continua pendente.

Base master: `674e74267df863dbaf50416c477bc7f918a826d8`. Lab fixo preservado: `b27e37a6dd3ae941bcffcf480a377adc81e3fc27`.

| Branch | Head completo | Base/dependência | Escopo | Gate Linux registrado |
| --- | --- | --- | --- | --- |
| `codex/pr/cc-xml-io-errors` | `de27f9452e7c40ca852287a8fafd42bd0309a610` | `674e74267df863dbaf50416c477bc7f918a826d8` (`674e74267df863dbaf50416c477bc7f918a826d8`) | I/O: escrita, leitura curta e fechamento | 79/79 destino; 106/106 lab |
| `codex/pr/cc-xml-parse-length-guard` | `f2142d0d1a32f8ac59e7b8d04a7be3d0d3dc1fc0` | `codex/pr/cc-xml-parser-transactional` (`162dc998af31291fb2d861b0d6b1431acb1c18da`) | Guarda size_t para int antes de XML_Parse | 79/79 destino; 106/106 lab |
| `codex/pr/scxml-object-unused-map` | `0b58696efdf4f60508ed3eea0ca41be8bee6e69d` | `674e74267df863dbaf50416c477bc7f918a826d8` (`674e74267df863dbaf50416c477bc7f918a826d8`) | Remoção de mapa privado ocioso | 78/78 destino; 105/105 lab |
| `codex/pr/cc-xml-resource-limits` | `41535dbcd95b452be84bbaf018d60bace2053111` | `codex/pr/cc-xml-parse-length-guard` (`f2142d0d1a32f8ac59e7b8d04a7be3d0d3dc1fc0`) | Cotas opt-in do parser | 80/80 destino; 107/107 lab |
| `codex/pr/cc-xml-expat-header-boundary` | `304f28db5764d9a194404e50adc0bcdbeacf0d88` | `674e74267df863dbaf50416c477bc7f918a826d8` (`674e74267df863dbaf50416c477bc7f918a826d8`) | Header e biblioteca Expat correspondentes | 78/78 embarcado e externo; 105/105 lab |
| `codex/pr/scxml-document-defensive-read` | `ef322f1f17d589c46dd898795feec9854d2a6cd3` | `674e74267df863dbaf50416c477bc7f918a826d8` (`674e74267df863dbaf50416c477bc7f918a826d8`) | Leitura defensiva de documento SCXML | 78/78 destino; 105/105 lab |

## Dependências e execução

A guarda de comprimento inclui o parser transacional `162dc998af31291fb2d861b0d6b1431acb1c18da`, já publicado. As cotas incluem a guarda de comprimento: testar a branch completa, sem reaplicar esses commits. As outras quatro candidatas são independentes sobre master.

As correções de alias SCXML `codex/pre-pr/scxml-attribute-alias` (`6ab2915cb7007683b4548c2a6e79cb3b0803001c`) e `codex/pre-pr/scxml-temporary-alias` (`b4204f98594c5fb4eafdeea685ee96fde6cf3c67`) já foram publicadas na rodada CC/Sb. As dez candidatas XML/SCXML originais e suas variantes corrigidas Windows também permanecem no fork. Os resultados anteriores não validam estes seis novos heads.

1. Execute `git fetch --all --prune` e confira o SHA de cada candidata antes de criar sua cópia de teste. Use build separado por candidata/configuração.
2. Compile e execute o conjunto CTest em Debug DLL e Release estático com `COIN_BUILD_TESTS=ON`, `COIN_BUILD_DOCUMENTATION=OFF` e `COIN_THREADSAFE=ON`; registre compilador, arquitetura, configuração, SHA e resultados/JUnit.
3. No link estático, aplique em uma cópia de teste somente o patch do fixture `55dbefa0330ca59a68ff2d47591f1256e7cba7b2` se necessário. Não altere a candidata para incorporar o fixture; registre o SHA adicional usado. Consulte `VALIDACAO_FIXTURE_BUMP.md` e os resultados Windows anteriores.
4. Para a guarda/cotas, execute também `CoinXmlTransactionalRead`, `CoinXmlLimits` e o cliente C `XmlCHeaderDocumentTest` quando presentes. Teste Expat embarcado e externo, identificando a versão externa e os caminhos de header/biblioteca. Zero desabilita cada cota; a API legada permanece ilimitada.
5. Para I/O, execute `XmlLoadFileTest` quando disponível e a regressão `write_to_file_reports_io_errors` de CoinTests. Registre separadamente casos Linux/POSIX, como FIFO/interposição, que não tenham equivalente Windows; não conte sua ausência como aprovação.
6. Para a fronteira de headers Expat, valide tanto o embarcado quanto `USE_EXTERNAL_EXPAT=ON` com pacote Expat Windows. A combinação com a atualização embarcada 2.9.0 foi aprovada no Linux em cópia cumulativa: existe conflito textual em `src/xml/document.cpp`. Preserve `#ifdef HAVE_EXPAT` / `<expat.h>` no modo externo e `XML_STATIC` / `"expat/expat.h"` no modo embarcado; preserve os demais ajustes da atualização 2.9.0. Registre a resolução e seu head de teste.
7. Para a integração conjunta, crie `lab/teste/...` a partir do lab fixo. Aplique somente os commits de contribuição, respeitando parser → guarda → cotas, e preserve todos os alvos de teste ao resolver CMake. Não una as branches inteiras baseadas em master ao lab nem altere `lab/open-prs-integration`. A matriz Windows cumulativa desta rodada ainda não foi executada.

## Material que permanece em estudo

DTD/libxml2 (`codex/work/cc-xml-dtd-libxml2`) continua experimental e fica fora do envio. Faltam proteções de recursos antes do Expat, gates e revisão das duas etapas. As cotas publicadas protegem o parser; não cobrem serialização de DOM manual nem alocações/expansão libxml2 anteriores. O estudo de compactação de associações SCXML não demonstrou benefício que justifique PR. A política de rejeitar todo DOCTYPE foi superada pela decisão de suporte DTD.

A documentação desta branch conserva as rodadas anteriores e acrescenta o checklist XML atual e o estudo de cotas. Fontes experimentais e diretórios locais não rastreados não fazem parte deste transporte. Nenhum novo teste foi executado nesta publicação: os gates acima são os registros do fechamento Linux.
