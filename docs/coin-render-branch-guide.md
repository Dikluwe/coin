# CoinRender: branch de integração

A branch de desenvolvimento da funcionalidade é `codex/coin-render`, no checkout
`/mnt/Laranja/Git/externos/coin-render`. Correções, novos testes e registros de
validação do CoinRender devem partir dela. Branches de investigação podem ser
usadas temporariamente e integradas aqui antes de preparar o PR. As correções
do núcleo CoinGL e os patches do driver Mesa têm escopos e revisões próprios.

## Onde está cada parte

| Parte | Caminho |
| --- | --- |
| Plano, estado comum, alvos e API | `src/rendering/coinrender/`, `experimental/include/Inventor/rendering/` |
| Executor BGFX e shaders | `src/rendering/coinbgfx/` |
| Executor wgpu e ponte Rust | `src/rendering/coinwgpu/` |
| Integração com nós e ações Coin | `src/actions/`, `src/nodes/`, `src/elements/`, `src/shapenodes/` |
| Testes de contratos e backends | `testsuite/coinrender/`, `testsuite/coinbgfx/`, `testsuite/coinwgpu/` |
| Consumidor instalado e exemplos | `examples/coinrender/` |
| FreeCAD/Quarter | `testsuite/qt-quarter/` |
| Estudos isolados de GPU e Mesa | `testsuite/reproducers/` |
| Relatórios e recibos de execução | `docs/coin-render-*.md`, `docs/validation/` |

O [plano geral](coin-render-work-plan.md) define as frentes P/F/A; o
[checklist](coin-render-next-fronts-checklist.md) registra o estado e as
pendências. Para o contrato inicial, veja o
[perfil experimental](coin-render-experimental-profile.md). Para a API pública
de sampling, veja o [contrato](coin-render-texture-sampling-api.md).

## Estado da integração em 2026-10-10

A branch reúne a implementação original de CoinRender, a API de sampling
native/portable, a continuação Windows e o teste das mesmas cenas no Linux.
Os relatórios visuais são [Windows](coin-render-windows-visual-scenes-20261010.md)
e [Linux](validation/linux-visual-20261010/README.md). O Linux registrou 16
passes e 32 falhas em 48 processos sob igualdade RGB exata; o Windows registrou
30 passes e 42 falhas em 72. Essas falhas continuam abertas no checklist.

A pesquisa de recorte AMD está documentada no
[estudo Mesa](coin-render-mesa-candidate-20261010.md), com patches e controles
sob `docs/validation/raster-clip-edge-mesa-fix-20261009/`. O código do driver
Mesa não faz parte do CoinRender nem foi instalado como correção geral. O
[estudo EGL/AMD](coin-sampling-amd-origin-study-20261007.md) e seus fixtures
estão em `testsuite/reproducers/coin-sampling-amd-study/`.

## Preparação do PR

Esta branch conserva recibos completos de validação para reprodução. Em
2026-10-10, a comparação com `origin/master` abrange cerca de 25 mil arquivos,
dos quais a maior parte é evidência em `docs/validation/`. Antes de abrir um PR
revisável, é preciso selecionar um conjunto representativo de recibos e
referenciar os demais por um meio durável; a limpeza deve preservar o histórico
e as referências dos relatórios. Também é preciso atualizar a base de
`origin/master`, executar os gates no estado final e explicitar as falhas ainda
abertas. A integração local nesta branch não equivale a essa aprovação.

A configuração é opt-in: `COIN_BUILD_RENDER=ON`, com
`COIN_RENDER_BACKEND=BGFX` ou `RUST_BRIDGE`; a instalação experimental exige
`COIN_INSTALL_RENDER_EXPERIMENTAL=ON`. Os relatórios indicam os comandos e
os GPUs específicos usados em cada campanha.
