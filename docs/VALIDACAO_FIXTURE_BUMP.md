# Revisão do fixture bump para link estático

Revisão concluída em 6 de outubro de 2026. A contribuição necessária é o rename do global `bumphack` apenas na cópia do renderer compilada pelos testes. Não foi encontrada outra correção relacionada que precise entrar junto.

## Referências

| Referência | SHA |
| --- | --- |
| Master de destino usado | `674e74267df863dbaf50416c477bc7f918a826d8` |
| Pré-PR `codex/pre-pr/bump-fixture-static` | `55dbefa0330ca59a68ff2d47591f1256e7cba7b2` |
| Lab fixo, preservado | `b27e37a6dd3ae941bcffcf480a377adc81e3fc27` |
| Cópia `lab/teste/bump-fixture-static` | `f780ab9a44a2a5cf0df58b3fa09a4935fb8da176` |

Worktrees: `/home/dikluwe/.codex/worktrees/bump-fixture-review/coin` e `/home/dikluwe/.codex/worktrees/bump-fixture-test/coin`.

O diff contra master contém **um arquivo, três linhas adicionadas**, em `testsuite/bumprender/TestAdapter.h`. Essa branch é independente dos PRs de `cc_dict`. O título do commit descreve link estático em geral: a falha também ocorre no Linux.

A branch anterior `codex/pre-pr/bump-static-testfix`, em `c9c9234768`, conserva o mesmo patch sobre #772 por ser o pré-requisito local de `cc_dict`. Ela foi preservada; a contribuição isolada para publicar o fixture é `codex/pre-pr/bump-fixture-static`.

## Revisão e evidências

- Os dois fixtures incluem `soshape_bumprender.cpp`: `FailureTest.cpp` e `GLXTest.cpp`. O adapter já dá um nome próprio à classe, mas faltava fazer o mesmo com seu global.
- A versão anterior do adapter foi compilada em uma cópia em RAM com os mesmos comandos e a biblioteca estática do master. **Ambos os fixtures falharam no link por definição duplicada de `bumphack`**. As compilações dos objetos passaram; a falha foi no linker.
- Com o patch, a biblioteca conserva `bumphack` e cada executável define `coinBumpTestHack`. A interseção dos símbolos fortes definidos pela biblioteca e por cada fixture ficou vazia, conferida com `nm -C --defined-only`.
- Os demais dados e helpers livres do renderer são estáticos ou inline; os métodos ficam sob a classe própria do fixture. Não foi encontrada outra colisão a corrigir.
- Foram conferidos os adapters de cleanup, helpers privados de GL, `COIN_INTERNAL`/Pimpl e `NOMINMAX`. A proteção existente para imports Windows foi preservada; não houve execução nativa Windows.
- Os fontes do fixture e do renderer são iguais entre master e os pré-requisitos de #772. A correção não precisa dessa pilha. O diff aplicado no lab é idêntico ao aplicado sobre master, conferido com `cmp`; `git diff --check` passou.

Os testes existentes de falhas/lifetime e de integração GLX já verificam os dois executáveis afetados. O próprio link estático é a regressão para esta falha; não foi acrescentado um teste que apenas repita o rename.

## Builds e testes em RAM

```bash
mkdir -p /dev/shm/coin-bump-fixture-20261006/compiler-tmp
export TMPDIR=/dev/shm/coin-bump-fixture-20261006/compiler-tmp

cmake -S /home/dikluwe/.codex/worktrees/bump-fixture-review/coin \
  -B /dev/shm/coin-bump-fixture-20261006/master-release-static -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCOIN_BUILD_SHARED_LIBS=OFF \
  -DCOIN_BUILD_TESTS=ON -DCOIN_BUILD_DOCUMENTATION=OFF -DCOIN_THREADSAFE=OFF
cmake --build /dev/shm/coin-bump-fixture-20261006/master-release-static --parallel 8

cmake -S /home/dikluwe/.codex/worktrees/bump-fixture-test/coin \
  -B /dev/shm/coin-bump-fixture-20261006/lab-debug-shared -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DCOIN_BUILD_SHARED_LIBS=ON \
  -DCOIN_BUILD_TESTS=ON -DCOIN_BUILD_DOCUMENTATION=OFF -DCOIN_THREADSAFE=OFF
cmake --build /dev/shm/coin-bump-fixture-20261006/lab-debug-shared --parallel 6

for fixture_build in \
  /dev/shm/coin-bump-fixture-20261006/master-release-static \
  /dev/shm/coin-bump-fixture-20261006/lab-debug-shared; do
  xvfb-run -a -s '-screen 0 1280x1024x24 +extension GLX' \
    env LIBGL_ALWAYS_SOFTWARE=1 COIN_GLX_PIXMAP_DIRECT_RENDERING=1 \
    ctest --test-dir "$fixture_build" --output-on-failure --parallel 4
done
```

| Base e configuração | Resultado |
| --- | --- |
| Master + somente o patch, Release estático | **78/78 aprovados** |
| Lab + somente o patch, Debug compartilhado | **105/105 aprovados** |

Sem falhas ou testes ignorados. `BumpProgramFailuresAndLifetime` e `BumpProgramGLX` passaram em ambos. Builds e temporários ficaram no `tmpfs` de `/dev/shm`.

Logs: `/tmp/bump-fixture-master-{configure,build,full}-20261006.log`, `/tmp/bump-fixture-lab-{configure,build,full}-20261006.log`, `/tmp/bump-fixture-baseline-link-20261006.log` e `/tmp/bump-fixture-symbol-audit-20261006.log`.

## Próxima etapa

Validar o head `55dbefa033` no Windows, especialmente no link estático e na compilação DLL de `CoinBumpProgramTest`, antes de publicar. A branch não foi enviada ao remoto e nenhum PR foi aberto. O gate Linux sobre master já passou; se essa base mudar, conferir o novo diff e repetir a validação pertinente.

A cópia temporária permanece até encerrar o ciclo Windows/publicação. Depois, arquivar seu worktree e eliminar `lab/teste/bump-fixture-static`, preservando a contribuição na branch pré-PR. O lab fixo permaneceu inalterado.
