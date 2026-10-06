# SoGLRenderAction sob exceção — fechamento Linux

## Branches e composição

- Master de origem `674e74267df863dbaf50416c477bc7f918a826d8`. A pré-PR `codex/pre-pr/gl-render-exceptions` está em `2f47f7fcf338190da9754ae9cd5564530d6985b6`, empilhada sobre `codex/pre-pr/action-apply-exceptions` `b6afd3f1d71f16b46a68d11f9795c65269aa4b49`. O diff próprio contra esse pré-requisito contém apenas `SoGLRenderAction.cpp`, a inclusão de testes no CMake e dois arquivos novos de teste GLX.
- Lab fixo `lab/open-prs-integration` `b27e37a6dd3ae941bcffcf480a377adc81e3fc27`, master mais 17 PRs abertos conforme [controle](CONTROLE_BRANCHES.md). Cópia separada `lab/teste/gl-render-exceptions` `06bcd67d50a0037db6f7c2fc3ec62d81561f3a40`: commits de SoAction `433628a1dc` e `575b270ae8`, seguidos somente dos dois commits GL `ee83b36405` e `06bcd67d50`. A resolução de `testsuite/CMakeLists.txt` conserva as duas inclusões de testes.
- Worktrees `/home/dikluwe/.codex/worktrees/gl-render-exceptions-work/coin` e `/home/dikluwe/.codex/worktrees/gl-render-exceptions-test/coin`. A base fixa permaneceu no mesmo SHA.

## Correção e regressão

`SoGLRenderActionP::render()` restaura, inclusive sob exceção, `isrendering`, o indicador de overlay, a profundidade de `SoState`, o passe atual, flags de transparência/renderização adiada/backfaces/WBOIT e as listas temporárias de caminhos. O valor anterior de `isrendering` também é preservado no retorno normal de uma chamada recursiva de fallback. A desativação temporária do profiler no overlay volta ao estado habilitado em caso de falha.

Quando uma exceção ocorre durante o passe WBOIT, a ação desvincula o programa de geometria, restaura o framebuffer anterior e o draw buffer do framebuffer não padrão, além de liberar a máscara de escrita em profundidade. A exceção original continua propagando para `SoAction::apply`, que restaura o lock e as referências no pré-requisito separado.

A regressão roda em contexto GLX nativo e injeta exceções em callback pré-render, callback da cena, passe de transparência, passe adiado e passe WBOIT. Confere a reutilização da mesma ação, a profundidade do estado, flags públicas e o framebuffer. No ambiente Mesa/llvmpipe testado, o depurador confirmou framebuffer WBOIT `1` durante a exceção, e o teste confirmou o vínculo restaurado depois. A execução contra a biblioteca com SoAction corrigido, mas GL anterior, abortou com `pre-render callback was skipped after failure` (exit 134); a candidata passou.

## Validação

| Configuração | Resultado |
| --- | --- |
| Lab temporário, Debug compartilhado, `COIN_THREADSAFE=ON` | 107/107 CTests, incluindo regressão GLX |
| Pré-PR sobre SoAction, Debug compartilhado, `COIN_THREADSAFE=ON` | 80/80 CTests, incluindo regressão GLX |
| Biblioteca e regressão GLX estáticas, ASan/UBSan | Sem erro de acesso ou comportamento indefinido com `detect_leaks=0`; com LeakSanitizer ativo, a base anterior ao glue retém 13.682 bytes em dez alocações de `cc_glglue` |
| Biblioteca GL + fontes da pré-PR do glue, ASan/UBSan/LeakSanitizer | Regressão GLX aprovada com `detect_leaks=1:halt_on_error=1`, sem supressões |

Compilação, temporários e executáveis em `/dev/shm/coin-gl-render-exceptions-20261006`, logs em `/tmp/gl-render-exceptions-*-20261006.log`. Comandos do conjunto:

```sh
TMPDIR=/dev/shm/coin-gl-render-exceptions-20261006/compiler-tmp cmake -S "$WORKTREE" -B "/dev/shm/coin-gl-render-exceptions-20261006/$BUILD" -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DCOIN_BUILD_SHARED_LIBS=ON \
  -DCOIN_BUILD_TESTS=ON -DCOIN_BUILD_DOCUMENTATION=OFF -DCOIN_THREADSAFE=ON
TMPDIR=/dev/shm/coin-gl-render-exceptions-20261006/compiler-tmp cmake --build "/dev/shm/coin-gl-render-exceptions-20261006/$BUILD" --parallel 8
xvfb-run -a -s '-screen 0 1280x1024x24 +extension GLX' \
  env LIBGL_ALWAYS_SOFTWARE=1 COIN_GLX_PIXMAP_DIRECT_RENDERING=1 \
  ctest --test-dir "/dev/shm/coin-gl-render-exceptions-20261006/$BUILD" --output-on-failure --parallel 4
```

`BUILD` foi `lab-debug-shared` ou `destination-debug-shared`. Logs finais `/tmp/gl-render-exceptions-{lab,destination}-full-20261006.log`; caso vermelho `/tmp/gl-render-exceptions-old-library-20261006.log`. `git diff --check` passou.

A variante instrumentada foi configurada em `asan-static` com `-fsanitize=address,undefined -fno-omit-frame-pointer -O1 -g` em C/C++ e `-fsanitize=address,undefined` no link. O teste isolado com `ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1` apontou somente a retenção conhecida do glue antigo: `/tmp/gl-render-exceptions-asan-test-20261006.log`. O mesmo executável passou com `detect_leaks=0`: `/tmp/gl-render-exceptions-asan-no-leaks-20261006.log`.

Para verificar vazamentos sem mascará-los, `/tmp/run-gl-render-glue-integration.py` recompilou **somente** `src/glue/gl.cpp` e `src/misc/SoContextHandler.cpp` do head fechado `codex/pre-pr/glglue-lifetime-complete` `0d31242881b0a2bc996ae348c3768fa8730c7a98`, usando os flags reais do build instrumentado. Os dois objetos foram ligados antes da biblioteca estática ao mesmo teste GLX, sem editar as branches. Essa integração passou com LeakSanitizer ativo; logs `/tmp/gl-render-exceptions-glue-integration-{build,test}-20261006.log`. O build isolado da pré-PR GL continua dependente da publicação separada do glue para não reter aquele registro.

Windows/WGL e transporte do pré-requisito SoAction permanecem pendentes antes de publicar. Nenhum push ou PR novo foi feito. Conservar a cópia lab até encerrar Windows/publicação; depois arquivar o worktree gerenciado e remover a branch temporária.
