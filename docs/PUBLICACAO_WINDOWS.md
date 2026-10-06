# Branches corrigidas e validação Windows

Transporte para o fork Dikluwe/coin em 6 de outubro de 2026. As novas refs preservam os snapshots publicados anteriormente. Não houve force-push, abertura de PR ou merge upstream. As demais candidatas aprovadas sem patch continuam nas refs do manifesto original.

| Nova branch corrigida | SHA completo | SHA base validado | Alteração |
| --- | --- | --- | --- |
| `codex/windows-smallmap-test-link` | `689d62859983c794216ec4e5947f00212e2ae607` | `ff1d09ddf3d458bb73023aa3fc001afce1e54fd6` | `testsuite/SbSmallMapTests.cmake` |
| `codex/windows/gl-contexts-small-complete` | `4e05d0e423dbfe42deffec30470270a5fde4d7bd` | `87cc9003e9adbacc702effe666d0e3b34b377983` | `testsuite/SbSmallMapTests.cmake` |
| `codex/windows/cc-xml-expat-2.9.0` | `9c2f7ed12bdcade59e9303fc738dfb130f88b412` | `aef999cb3642638c61a82128f0db69ad9e532702` | `testsuite/bumprender/TestAdapter.h` |
| `codex/windows/cc-xml-entity-abi` | `1ffb6252ec67957db9099c935672868de2b7cf43` | `21dc59664b2ece234822901e9498ab86c539c6d6` | `testsuite/bumprender/TestAdapter.h` |
| `codex/windows/cc-xml-path-loops-truncate` | `8f1220bfe2846a6b6d1ef0822777662fb0ba1c78` | `bffa176d456a623078d1ded247d8b04bd3f7d898` | `testsuite/bumprender/TestAdapter.h` |
| `codex/windows/cc-xml-escaping-inttypes` | `1965d1543bfc0dca203bc79863e855ca5735e7f2` | `0935125576bf039ce59f257a634549affc4137cc` | `testsuite/bumprender/TestAdapter.h` |
| `codex/windows/cc-xml-parser-transactional` | `01abac5366be030736f52e8de3a4126c0bb67788` | `162dc998af31291fb2d861b0d6b1431acb1c18da` | `testsuite/bumprender/TestAdapter.h` |
| `codex/windows/cc-xml-coalesce-cdata` | `72da5e67e47cbe21fe9a32efc6872dac80208020` | `5c4a34e04561eaef39abecf9acbca9609d39ddd5` | `testsuite/bumprender/TestAdapter.h` |
| `codex/windows/cc-xml-dom-ownership` | `53046efad26e1ba724ea0d4482f80738bfa9f12a` | `28d74b9982bf0791d50abd60a1dcbbd7817fe1f0` | `testsuite/bumprender/TestAdapter.h` |
| `codex/windows/cc-xml-dom-regressions-fuzz` | `26840751bd5c9861c69302f975312474f95dc2b1` | `dcb54791bd3609458f7df5c5e22e83682b68358a` | `testsuite/bumprender/TestAdapter.h` |
| `codex/windows/scxml-sendelt-tokenize` | `0cd119ebb9a23439ccbc6e5a81aa00319df153b3` | `5e8945f2086049850f8638bed144112fd85e1eb0` | `testsuite/bumprender/TestAdapter.h` |
| `codex/windows/scxml-event-association-ownership` | `40b2e49da7cd2d6365fa9c3fb8cef5c2961a327b` | `a4fc778e7c07fd416d0799e4b002bdb090b92f8f` | `testsuite/bumprender/TestAdapter.h` |

A correção de link foi aplicada exatamente como na validação de SbSmallMap/caches GL. As dez correções XML/SCXML usam o patch de fixture já publicado em 55dbefa033; o código do adapter foi comparado com o overlay testado, desconsiderando apenas comentários e linhas vazias. Os diffs das novas candidatas tocam um único arquivo de teste e não alteram fontes de produção. A validação completa anterior permanece válida para esse delta, sem repetir a matriz após apenas trocar comentários do fixture.

Resultados: [RESULTADOS_WINDOWS.md](RESULTADOS_WINDOWS.md), [resultados-windows.json](resultados-windows.json). Evidência JUnit da matriz final e dos drivers WGL: [windows-validation-20261006/junit](windows-validation-20261006/junit). Os caminhos absolutos H:/Git/coin nos XML representam a máquina onde os testes foram executados.
