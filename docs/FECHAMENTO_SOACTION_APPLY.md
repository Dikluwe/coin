# SoAction::apply sob exceção — fechamento Linux

## Base e contribuição

- Master de destino: `674e74267df863dbaf50416c477bc7f918a826d8`.
- Pré-PR: `codex/pre-pr/action-apply-exceptions` em `b6afd3f1d71f16b46a68d11f9795c65269aa4b49`, com commits `10206c8cad196b2f50a9d546341ecd819cb3e2b6` e `b6afd3f1d71f16b46a68d11f9795c65269aa4b49`. Pai da contribuição: o próprio master, sem o fixture estático.
- Lab fixo: `lab/open-prs-integration` em `b27e37a6dd3ae941bcffcf480a377adc81e3fc27`, master mais 17 PRs abertos conforme [controle](CONTROLE_BRANCHES.md). Cópia em worktree separado: `lab/teste/action-apply-exceptions` em `c1aae576bf31376f552496ff17ccb150f9accf99`, contendo somente os dois commits candidatos aplicados ao lab.
- Worktrees: `/home/dikluwe/.codex/worktrees/action-apply-work/coin` e `/home/dikluwe/.codex/worktrees/action-apply-test/coin`. Base fixa preservada.

O reprodutor anterior em `/tmp/gl-map-action-exception-repro.cpp` confirmou que uma exceção de callback deixava a referência da raiz em 2 quando era 1 antes da chamada. O diff final do candidato contra master toca apenas `src/actions/SoAction.cpp`, `testsuite/CMakeLists.txt` e dois arquivos novos de regressão. Os outros PRs do lab não entram no diff da pré-PR.

## Contrato corrigido

As três sobrecargas de `apply` liberam o read lock por escopo, inclusive quando setup, criação de estado, callback ou travessia lança. No caminho de exceção, restauram alvo/código aplicados e código do caminho atual; as formas nó e caminho liberam a referência temporária, e a lista libera a representação compactada. A pilha de `SoState` volta à profundidade de entrada. A travessia do overlay e das estatísticas também restaura a habilitação do profiler ao sair por exceção. A exceção original é propagada.

A regressão usa uma ação de callback real sobre nó, caminho e listas ordenada/não ordenada. Confere referências, alvo aplicado, profundidade de estado, propagação da exceção, liberação do read lock por aquisição de write lock em outra thread e reutilização da ação após as falhas. Os objetos do fixture são destruídos antes de `SoDB::finish()`.

## Comandos e resultados

Builds, temporários do compilador e executáveis ficam em `/dev/shm/coin-action-apply-20261006`; logs duráveis ficam em `/tmp/action-apply-*-20261006.log`.

```sh
TMPDIR=/dev/shm/coin-action-apply-20261006/compiler-tmp cmake -S "$WORKTREE" -B "/dev/shm/coin-action-apply-20261006/$BUILD" -G Ninja \
  -DCMAKE_BUILD_TYPE="$TYPE" -DCOIN_BUILD_SHARED_LIBS="$SHARED" \
  -DCOIN_BUILD_TESTS=ON -DCOIN_BUILD_DOCUMENTATION=OFF -DCOIN_THREADSAFE=ON
TMPDIR=/dev/shm/coin-action-apply-20261006/compiler-tmp cmake --build "/dev/shm/coin-action-apply-20261006/$BUILD" --parallel 8
xvfb-run -a -s '-screen 0 1280x1024x24 +extension GLX' \
  env LIBGL_ALWAYS_SOFTWARE=1 COIN_GLX_PIXMAP_DIRECT_RENDERING=1 \
  ctest --test-dir "/dev/shm/coin-action-apply-20261006/$BUILD" --output-on-failure --parallel 4
```

| Configuração | Resultado |
| --- | --- |
| Master + fixture estático, Release estática, `COIN_THREADSAFE=ON` | 79/79 testes após o head funcional e o ajuste do profiler; fixture testado como pré-requisito separado |
| Lab temporário, Debug compartilhado, `COIN_THREADSAFE=ON` | 106/106 testes no head final |
| Master puro, Debug compartilhado, `COIN_THREADSAFE=ON` | 79/79 testes no head final; diff da pré-PR contém somente os quatro arquivos da contribuição |
| Master puro, Debug estática, ASan/UBSan | Regressão específica passou com `detect_leaks=1:halt_on_error=1` e `halt_on_error=1` em UBSan |

`git diff --check` passou nas duas branches. A pré-PR não modifica headers públicos ou layout de `SoAction`. Windows permanece pendente antes da publicação; nenhum push ou PR novo foi feito. Após o ciclo Windows/publicação, arquivar o worktree temporário e eliminar `lab/teste/action-apply-exceptions`.

Os logs finais do conjunto são `/tmp/action-apply-master-destination-full-20261006.log`, `/tmp/action-apply-master-head-full-20261006.log` e `/tmp/action-apply-lab-head-full-20261006.log`. A variante instrumentada usou `-fsanitize=address,undefined -fno-omit-frame-pointer -O1 -g` em C/C++, `-fsanitize=address,undefined` no link e `ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1`; após o rebase sobre master puro, a configuração e a regressão foram repetidas. Logs finais `/tmp/action-apply-asan-final-{configure,build,test}-20261006.log`.

`SoGLRenderActionP::render()` mantém estado próprio (`isrendering` e `isrenderingoverlay`) durante a travessia. Essa restauração sob exceção exige um ciclo separado, com teste GL, antes de declarar a ação GL reutilizável após falhas.
