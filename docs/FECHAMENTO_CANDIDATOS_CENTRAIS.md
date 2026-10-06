# Destino das branches centrais anteriores

Auditoria em 6 de outubro de 2026. Referências fixas: master `674e74267df863dbaf50416c477bc7f918a826d8` e `lab/open-prs-integration` `b27e37a6dd3ae941bcffcf480a377adc81e3fc27` (master mais os 17 PRs registrados em [CONTROLE_BRANCHES.md](CONTROLE_BRANCHES.md)). Nenhuma das cinco branches antigas abaixo deve virar PR diretamente: elas carregam pré-requisitos ou versões anteriores de contribuições já isoladas. Os heads originais foram preservados; não houve push, exclusão de branch nem alteração do lab fixo.

| Fonte auditada | Destino do conteúdo útil | Decisão |
| --- | --- | --- |
| `codex/work/cc-hash-api` `ebae1bd6530e7c0d33fb47c60a94df667754c242` | `codex/pre-pr/cc-hash-complete` `c5dc4c643a48a7f1bf38fe32ac9da7cb24be62a8`; os testes/contratos próprios de SbDict estão em `codex/pre-pr/sbdict-complete` `b5ae0d4ff39f8783648b69b0c2503b1bb5516a69` | Candidato preliminar substituído. Seu histórico parte do lab; não serve como diff contra master. A nova branch cc_hash tem cliente C, teste de falhas, primos e contrato de apply mais completos. |
| `codex/cc-hash-hardening-followup` `a60f2bea89fbacf0494233f68cccbc8e2529b6ec` | PRs #769–772, fechamentos cc_hash e SbDict | Fonte antiga reconciliada. A troca do hash, rollback de falhas, travessia com remoção e o teste público externo constam dos fechamentos. O teste antigo de SbDict foi substituído pela suíte pública e diferencial atual. |
| `codex/sbdict-ccdict-migration` `77ca5b22eb4eea521e4ff08447e8b4baf38200fa` | `codex/pre-pr/sbdict-complete` e `codex/pre-pr/bump-fixture-static` `55dbefa0330ca59a68ff2d47591f1256e7cba7b2` | Migração revisada no fechamento SbDict. O único commit posterior à migração nessa fonte corrige o fixture bump; seu patch-id estável `c06daf27d2f6c577efdf71ade506c84d3d5dd26e`, igual ao da branch independente do fixture; as árvores desse arquivo também são idênticas. |
| `codex/sbdict-hash-hardening` `2ecc31d7cf8d8385cbb913028535fa38ff6a3094` | Fechamentos cc_hash e SbDict | Correção inicial substituída. O teste `CcHashFailureTest.cpp` da fonte verifica relink, falha de buckets e rehash. Os testes atuais cobrem identidade dos nós, falha de buckets/plano, exceções e nova tentativa. A contagem exata de alocações da implementação antiga não foi transportada: o algoritmo atual usa plano auxiliar para hash personalizado. |
| `codex/coin-prime-boundary` `1d087a752dcf8260985e802e53a0720dcb2e8757` | Fechamentos cc_dict `9b9237f13d`, cc_hash `c5dc4c643a` e SbHash `5112bbcaf0` | Política de primos e limites absorvida. `src/primep.h` tem o mesmo blob Git `0835e78c75027166a5eefadb118162e293edaeb3` na fonte e nos três fechamentos. Cada consumidor final usa primo exato na construção e tabela geométrica no crescimento; os testes de fronteira foram distribuídos entre as suítes. |

## Helper legado de primos

`coin_geq_prime_number()` em `src/tidbits.cpp` ainda atende `cc_dict`, `cc_hash` e `SbHash` no master e no lab fixo. A branch antiga troca sua semântica de tabela geométrica por primo exato e introduz outra função para crescimento. Aplicar **somente** essa mudança ao master atual faria os consumidores antigos crescerem pelo próximo primo, em vez do próximo patamar geométrico; por isso ela não é uma contribuição isolada segura. Os três fechamentos substituem as chamadas de produção pelos helpers estáticos de `primep.h`. **Decisão:** preservar `coin_geq_prime_number()` com a semântica histórica e sem novo símbolo privado. Isso evita uma quebra desnecessária para qualquer consumidor privado ainda existente; após o transporte dos três heads, conferir por busca que os componentes de produção já usam `primep.h`. Não há PR separado de `tidbits` neste ciclo.

O teste antigo `CoinPrimeBoundaryTest.cpp` mistura as duas semânticas e os três consumidores. `CcDictResizeTest.cpp`, `CcHashOomTest.cpp` e `SbHashFailureTest.cpp` nos fechamentos verificam os limites nos respectivos componentes; a adição duplicada de `primep.h` deve ser incorporada uma única vez ao preparar os diffs finais para publicação.

## Conferência Linux

Os builds existentes em `/dev/shm` foram produzidos e testados nos registros [cc_dict](VALIDACAO_CC_DICT.md), [cc_hash](FECHAMENTO_CC_HASH.md), [SbDict](FECHAMENTO_SBDICT.md) e [SbHash](FECHAMENTO_SBHASH.md). Nesta auditoria, as regressões dirigidas foram repetidas sem alterar os heads:

```bash
ctest --test-dir /dev/shm/coin-cc-dict-20261006/lab-debug -R '(CcDict|CcMemalloc)' --output-on-failure --parallel 4
ctest --test-dir /dev/shm/coin-cc-hash-complete-20261006/lab-debug-shared -R '(CcHash|CcDict|CcMemalloc|CcSbHash)' --output-on-failure --parallel 4
ctest --test-dir /dev/shm/coin-sbdict-20261006/lab-debug-shared -R '(SbDict|CcDict|CcMemalloc)' --output-on-failure --parallel 4
ctest --test-dir /dev/shm/coin-sbhash-20261006/lab-debug-shared -R '(CcSbHash|SbHash)' --output-on-failure --parallel 4
```

Resultados: **9/9, 9/9, 13/13 e 2/2**, respectivamente, sem falhas. Os conjuntos completos já registrados para esses heads são 108/108 no lab cc_dict, 106/106 no lab cc_hash, 112/112 no lab SbDict com pré-requisito cc_dict e 106/106 no lab SbHash. Não há novo delta de código a aplicar ou uma nova cópia `lab/teste` a criar nesta auditoria.

Windows, transporte exclusivo de cada contribuição ao master de destino e eliminação das cópias temporárias continuam no gate de publicação. As fontes antigas permanecem como referências históricas até essa passagem.
