# Transporte CC e Sb para Windows da segunda rodada

Referência de 6 de outubro de 2026. Este lote transporta 13 contribuições concluídas no Linux para validação Windows no fork `Dikluwe/coin`. Os heads anteriores, o lab fixo e os trabalhos ativos são preservados. A publicação destas refs é transporte para testes; abertura/atualização de PRs e merge upstream continuam etapas separadas.

A documentação está em `codex/docs/cc-sb-windows-round2-20261006`, baseada no registro Windows anterior `c3b4e6cd9688c4b664d9998403813afa7eeb2598`. Ela conserva os resultados e arquivos JUnit daquele lote e atualiza os registros locais de fechamento. As menções históricas a “não houve push” descrevem o ciclo Linux anterior a este transporte.

## Heads para a nova rodada

| Branch | SHA completo | Base ou dependência |
| --- | --- | --- |
| `codex/pre-pr/action-apply-exceptions` | `b6afd3f1d71f16b46a68d11f9795c65269aa4b49` | `origin/master` |
| `codex/pre-pr/gl-render-exceptions` | `2f47f7fcf338190da9754ae9cd5564530d6985b6` | `codex/pre-pr/action-apply-exceptions` |
| `codex/pre-pr/scxml-attribute-alias` | `6ab2915cb7007683b4548c2a6e79cb3b0803001c` | `origin/master` |
| `codex/pre-pr/scxml-temporary-alias` | `b4204f98594c5fb4eafdeea685ee96fde6cf3c67` | `origin/master` |
| `codex/pre-pr/profiler-stats-lifetime` | `bb32da5fc2041e56bf75e158c8df63d6bdb66243` | `origin/master` |
| `codex/pre-pr/cc-list-bounds` | `ea8e90543611087e1c85fa468c671175343c323d` | `origin/master` |
| `codex/pre-pr/sblist-value-bounds` | `af3ae606439805f1cc34fdebea3a40f6c26cf2e9` | `origin/master` |
| `codex/pre-pr/sbplist-bounds` | `969a37276e931f20cd56c7d2f8c9f97f9113d8c0` | `origin/master` |
| `codex/pre-pr/callback-list-copy-stress` | `23ccce4b84b1adea848518f033ca35319b7e1234` | `origin/master` |
| `codex/pre-pr/bump-cache-ready-path` | `9d9c16f53af802d5af165a810f69144b38cc814c` | `origin/master` |
| `codex/pre-pr/bump-cache-diagnostics-memory` | `1a373951dd4140ce7e42907a721b82154b0b0447` | `origin/master` |
| `codex/pre-pr/bump-shared-programs` | `9b772453d41cf7a38595db480c91e6251becb148` | `origin/master` |
| `codex/pre-pr/sbheap-cancel-contract` | `6436c4d1936b17fbd81978e10048b6443fb1b100` | `origin/master` |

Master de referência: `674e74267df863dbaf50416c477bc7f918a826d8`. Lab fixo para as cópias de integração: `b27e37a6dd3ae941bcffcf480a377adc81e3fc27`, já publicado em `lab/open-prs-integration`.

## Dependências e cuidados de teste

- Testar primeiro `action-apply-exceptions`; `gl-render-exceptions` incorpora esse pré-requisito. O gate GL inclui casos GLX Linux; validar WGL equivalente no Windows, sem declarar equivalência apenas pelo CTest não gráfico.
- As outras 11 candidatas são independentes sobre o master indicado. Os três candidatos bump alteram o mesmo arquivo e podem conflitar entre si; validar cada head isolado antes de planejar integração cumulativa.
- Builds estáticos do master têm o problema conhecido do fixture `bumphack`. O pré-requisito corrigido já está no fork em `codex/pre-pr/bump-fixture-static`, SHA `55dbefa0330ca59a68ff2d47591f1256e7cba7b2`. Aplicar somente seu delta de `testsuite/bumprender/TestAdapter.h` à cópia estática quando necessário; registrar claramente o head original e o overlay, como na rodada Windows anterior. Não copiar uma versão completa de outro candidato.
- Manter a modificação local existente no checkout Windows. Usar snapshots/worktrees separados e fazer fetch, sem reset/clean do checkout de trabalho.
- Configurações de destino: x64 DLL Debug e estático Release, `COIN_THREADSAFE=ON`, com regressões dirigidas e suíte completa; conferir LLP64, ciclo de contexto e comportamento após exceção.
- Regenerar os snippets de teste entre snapshots para evitar resíduos de outra branch. Registrar SHA, comandos, testes e limitações por configuração.
- O lab fixo fica preservado; integração cumulativa usa cópia `lab/teste/<assunto>`. A aprovação individual Linux não comprova que todos os candidatos funcionem juntos.

## Material que continua fora do lote

AdaptiveMap, SequentialMap, migrações experimentais e pool compacto de SbName não são enviados como candidatas prontas. Benchmarks, arquivos históricos, fontes substituídas e labs temporários também não entram neste push.

As primeiras candidatas CC/Sb/XML já têm resultados Windows em `docs/RESULTADOS_WINDOWS.md` e `docs/PUBLICACAO_WINDOWS.md` desta branch. As 12 versões corrigidas daquele ciclo passaram no retorno Linux: 24 configurações, 1.884 entradas de CTest, sem falhas ou skips; ver `docs/VALIDACAO_RETORNO_WINDOWS.md`. Este lote não sobrescreve aquelas refs.

## Documentação atualizada

| Arquivo | SHA256 do snapshot |
| --- | --- |
| `docs/CHECKLIST_DICT_CC_SB.md` | `68c3db1305b540796fd32c771dd137edbdc96e7d3dc61c84ec83d7f8ef4a0c87` |
| `docs/CONTROLE_BRANCHES.md` | `6c13b2b0bf37354af494abc0bd0acb8c52c390e4b2ff6f75a917e66bd15858d8` |
| `docs/DICT_CC_SB.md` | `b9c2e621057068b097c436382874575ce6e9a6ea0663370bbdde1c13d6abac15` |
| `docs/METODO_BRANCHES.md` | `6ec8022a0da1d777d4ac2744f6098928ed0724f10b0777058d954ee9c0985496` |
| `docs/FECHAMENTO_ADAPTIVE_MAP.md` | `3bc5312b73576e3dddb9c06a054bf7bb386721ee03c319721b0c46f010c13ea3` |
| `docs/FECHAMENTO_APOIO_CONSUMIDORES.md` | `a8e03dcc6c31a4a7ccef85163598b2e8e5abf243327c927d82f9121739699ad5` |
| `docs/FECHAMENTO_CANDIDATOS_CENTRAIS.md` | `1a6a9c6c161af14e60fb6b618e0bbbfd2b994e35b184cb5a7c9aeb1a7ad76c1a` |
| `docs/FECHAMENTO_CC_HASH.md` | `91a807c918e3df205f2090cd023f37cffa0040209448c9f66e7a8de50187c952` |
| `docs/FECHAMENTO_GLGLUE_LIFETIME.md` | `5dc061a3381f76f510ad9d40e8e24d84581022c09791f81bf8167eb29f5cd0f5` |
| `docs/FECHAMENTO_GL_CONTEXT_MAPS.md` | `1a5ef06ea49112aca17e169012975d6739e76405225750d2f45e6bc6741c3a2b` |
| `docs/FECHAMENTO_GL_RENDER_EXCEPTIONS.md` | `8debbada5d8ea0e8d3946622a0701c15f3857c75b2667c7362d57c9369cf4d2c` |
| `docs/FECHAMENTO_MAPAS_CONSUMIDORES.md` | `4b220ec23ab9359e0fb8a516baddd439951dda5de8d029d605b99958c1c22019` |
| `docs/FECHAMENTO_SBDICT.md` | `8eef7710e48406d8da94723a7b951aa9f371114601b4f2ba575fab62ab6c54ff` |
| `docs/FECHAMENTO_SBHASH.md` | `baeaa1c869952e4efc407d5951da47ba3979fd11001e506e57e6b8c74cbf2a7d` |
| `docs/FECHAMENTO_SBSMALLMAP.md` | `7c6ff71274e59b89bb3da08175a5e248e589c0569f4f0b82cc80a25cd576e7c5` |
| `docs/FECHAMENTO_SOACTION_APPLY.md` | `53365152d70c3fbc3b0046c68056c7ac8dfe22261d919f4066591b37eb9ee8f5` |
| `docs/VALIDACAO_CC_DICT.md` | `6e8dfa88e347bdac690328a13de32a102eb49afb45a5b58be4b95781902b95dc` |
| `docs/VALIDACAO_CC_HASH.md` | `390a423e16e4b82b9e5b8d03905b72d5a18059fe5cb680571d7b8802eafec734` |
| `docs/VALIDACAO_FIXTURE_BUMP.md` | `128a937e104723c7e9ef8a23010f23b72a2256827e83c5b3019d34db26a16905` |
| `docs/VALIDACAO_RETORNO_WINDOWS.md` | `f7b9988a980b19f639a38f5c7969028c13dca36febb3ef300924190019b19443` |
