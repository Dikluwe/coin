# Validação Linux das atualizações feitas no Windows

As 12 branches corrigidas no Windows foram baixadas de `Dikluwe/coin` e validadas no Linux em 6 de outubro de 2026. As 24 configurações passaram: Debug compartilhado e Release estático de cada head, com `COIN_THREADSAFE=ON`, sem falhas ou testes ignorados.

Foram executadas 1884 entradas de CTest, contando repetições entre branches e configurações; isso não representa testes únicos. Os testes gráficos registrados, incluindo os caches com contextos GLX, foram executados sob Xvfb/llvmpipe.

## Atualizações baixadas

- Duas branches acrescentam `target_link_libraries(SbSmallMapFailureTest PRIVATE Coin)` ao alvo de teste: SbSmallMap e seu consumidor de caches GL.
- Dez branches XML/SCXML incorporam o pré-requisito do fixture estático em `testsuite/bumprender/TestAdapter.h`, isolando o símbolo `bumphack` da biblioteca.
- A branch de resultados Windows está em `c3b4e6cd9688c4b664d9998403813afa7eeb2598`. Seus registros, JUnit e suporte WGL foram baixados pelo fetch; a documentação está em [RESULTADOS_WINDOWS.md](https://github.com/Dikluwe/coin/blob/c3b4e6cd9688c4b664d9998403813afa7eeb2598/docs/RESULTADOS_WINDOWS.md).

Os heads originais das candidatas, o lab fixo, os worktrees ativos e os arquivos locais não rastreados foram preservados. Os testes usaram snapshots dos SHAs remotos em uma pasta de execução separada; não houve push, merge nem atualização dos PRs nesta rodada.

## Matriz Linux

| Ref remota testada | SHA completo | Debug compartilhado | Release estático |
| --- | --- | --- | --- |
| `fork/codex/windows-smallmap-test-link` | `689d62859983c794216ec4e5947f00212e2ae607` | 79/79 | 79/79 |
| `fork/codex/windows/gl-contexts-small-complete` | `4e05d0e423dbfe42deffec30470270a5fde4d7bd` | 80/80 | 80/80 |
| `fork/codex/windows/cc-xml-coalesce-cdata` | `72da5e67e47cbe21fe9a32efc6872dac80208020` | 78/78 | 78/78 |
| `fork/codex/windows/cc-xml-dom-ownership` | `53046efad26e1ba724ea0d4482f80738bfa9f12a` | 78/78 | 78/78 |
| `fork/codex/windows/cc-xml-dom-regressions-fuzz` | `26840751bd5c9861c69302f975312474f95dc2b1` | 78/78 | 78/78 |
| `fork/codex/windows/cc-xml-entity-abi` | `1ffb6252ec67957db9099c935672868de2b7cf43` | 80/80 | 80/80 |
| `fork/codex/windows/cc-xml-escaping-inttypes` | `1965d1543bfc0dca203bc79863e855ca5735e7f2` | 78/78 | 78/78 |
| `fork/codex/windows/cc-xml-expat-2.9.0` | `9c2f7ed12bdcade59e9303fc738dfb130f88b412` | 78/78 | 78/78 |
| `fork/codex/windows/cc-xml-parser-transactional` | `01abac5366be030736f52e8de3a4126c0bb67788` | 79/79 | 79/79 |
| `fork/codex/windows/cc-xml-path-loops-truncate` | `8f1220bfe2846a6b6d1ef0822777662fb0ba1c78` | 78/78 | 78/78 |
| `fork/codex/windows/scxml-event-association-ownership` | `40b2e49da7cd2d6365fa9c3fb8cef5c2961a327b` | 78/78 | 78/78 |
| `fork/codex/windows/scxml-sendelt-tokenize` | `0cd119ebb9a23439ccbc6e5a81aa00319df153b3` | 78/78 | 78/78 |

## Ambiente e comandos

Compilador: `c++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0`. Generator Ninja, renderer OpenGL legado e testes habilitados; documentação e fuzzers desabilitados. Threads habilitadas nas duas configurações.

Os snapshots foram obtidos por `git archive <SHA>`. A pasta de fonte compartilhada entre rodadas atualiza apenas arquivos cujo conteúdo muda e remove fontes ausentes no próximo snapshot. Antes de cada configuração, os snippets de testes gerados são removidos e recriados pelo CMake, evitando reutilizar testes de outra candidata.

```bash
cmake -S <snapshot> -B <build> -G Ninja \
  -DCMAKE_BUILD_TYPE=<Debug|Release> \
  -DCOIN_BUILD_SHARED_LIBS=<ON|OFF> \
  -DCOIN_BUILD_TESTS=ON -DCOIN_THREADSAFE=ON \
  -DCOIN_BUILD_DOCUMENTATION=OFF -DCOIN_BUILD_FUZZERS=OFF
cmake --build <build> --parallel 8
xvfb-run -a -s '-screen 0 1280x1024x24 +extension GLX' \
  env LIBGL_ALWAYS_SOFTWARE=1 COIN_GLX_PIXMAP_DIRECT_RENDERING=1 \
  ctest --test-dir <build> --output-on-failure --timeout 60 --parallel 4 \
  --output-junit <resultado.xml>
```

## Evidência e limites

- [Manifesto dos heads](/tmp/coin-linux-windows-roundtrip-20261006/heads.json).
- [Resultados por configuração](/tmp/coin-linux-windows-roundtrip-20261006/results.json).
- [Runner da rodada](/tmp/coin-linux-windows-roundtrip-20261006/run.py).
- Logs de configure/build/test e JUnit por branch em `/tmp/coin-linux-windows-roundtrip-20261006/logs/<assunto>/<configuração>/`.

Cada candidata foi testada isoladamente. A integração cumulativa de todas elas não faz parte desta rodada. Os drivers Windows WGL não são executáveis no Linux; o teste GLX disponível nos consumidores foi executado. ASan, UBSan, LeakSanitizer, TSan e libFuzzer não foram repetidos nesta validação de portabilidade; seus resultados anteriores continuam identificados nas checklists e registros próprios.

Os heads `codex/windows/*` e `codex/windows-smallmap-test-link` contêm os deltas aprovados nesta rodada. A consolidação desses deltas nas branches principais e o encerramento das referências auxiliares ainda precisam seguir o método de organização, com conferência de dependências e trabalhos ativos.
