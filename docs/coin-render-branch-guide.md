# CoinRender: branch de integração

Etapa 1 de inventário e reconciliação **encerrada em 16/10/2026**: [auditoria, inventários e critérios](coin-render-stage1-audit.md), com [ledger individual das campanhas visuais](coin-render-visual-case-ledger.md). Recursos e combinações continuam acompanhados na matriz.

## Continuação publicada em 21/10/2026

A continuação revisável está em `codex/stage2-review-20261021`, no repositório
[Dikluwe/coin](https://github.com/Dikluwe/coin/tree/codex/stage2-review-20261021).
A árvore anterior `codex/coin-render` foi preservada. A [matriz](coin-render-feature-matrix.md)
registra os commits de baseline/candidato e os comandos de reprodução de B90–B92.
Código, testes e matriz foram separados. Os bloqueios históricos da etapa2 seguem
abertos e etapas3/4 permanecem suspensas. As referências ao checkout canônico
abaixo descrevem a organização anterior a esta publicação.

## Acompanhamento funcional

Use a [matriz consolidada](coin-render-feature-matrix.md) para acompanhar recursos, combinações, plataformas e falhas até a cobertura completa. Ela separa implementação de qualificação e remete aos contratos e relatórios históricos. PBR permanece fora dessa matriz.

Este checkout canônico está na branch `codex/coin-render` e organiza a
implementação CoinRender para revisão. O checkout original de desenvolvimento
evidenciado pelos relatórios foi preservado no bundle da consolidação, no
estado local de referência `c28812338b8c6d51f7a1a8bd7eea6d997a291013`. Os links
para recibos excluídos deste checkout apontam para caminhos absolutos dentro
desse arquivo local; eles não estão publicados em um destino remoto. Antes de
submeter os relatórios a um repositório upstream, publique esses recibos em um
local durável e atualize os links. As correções do núcleo CoinGL e os patches
do driver Mesa têm escopos e revisões próprios. O
[mapa de arquitetura](coin-render-architecture.md) registra a intenção atual:
CoinRender conecta a cena Coin a BGFX, wgpu e futuros executores; CoinGL é o
oráculo de compatibilidade e RECORDING/CPU é auxiliar de teste.

## Onde está cada parte

| Parte | Caminho |
| --- | --- |
| Plano, estado comum, alvos e API pública experimental | `src/rendering/coinrender/`, `include/Inventor/rendering/` |
| Executor BGFX e shaders | `src/rendering/coinbgfx/` |
| Executor wgpu e ponte Rust | `src/rendering/coinwgpu/` |
| Integração com nós e ações Coin | `src/actions/`, `src/nodes/`, `src/elements/`, `src/shapenodes/` |
| Testes de contratos e backends | `testsuite/coinrender/`, `testsuite/coinbgfx/`, `testsuite/coinwgpu/` |
| Consumidor instalado e exemplos | `examples/coinrender/` |
| FreeCAD/Quarter | `testsuite/qt-quarter/` |
| Estudos isolados de GPU e Mesa | `testsuite/reproducers/` |
| Relatórios e recibos de execução | `docs/coin-render-*.md`; recibos completos no arquivo local original |

O [plano geral](coin-render-work-plan.md) define as frentes P/F/A; o
[checklist](coin-render-next-fronts-checklist.md) registra o estado e as
pendências. Para o contrato inicial, veja o
[perfil experimental](coin-render-experimental-profile.md). Para a API pública
de sampling, veja o [contrato](coin-render-texture-sampling-api.md).

## Campanha de integração de 2026-10-10

A branch de origem reuniu a implementação original de CoinRender, a API de
sampling native/portable, a continuação Windows e o teste das mesmas cenas no
Linux.
Os relatórios visuais são [Windows](coin-render-windows-visual-scenes-20261010.md)
e [Linux](</home/dikluwe/Área de trabalho/Estudo coin/consolidacao-frentes-20261016/renderer-history/docs/validation/linux-visual-20261010/README.md>). O Linux registrou 16
passes e 32 falhas em 48 processos sob igualdade RGB exata; o Windows registrou
30 passes e 42 falhas em 72. Essas falhas continuam abertas no checklist.

A pesquisa de recorte AMD está documentada no
[estudo Mesa](coin-render-mesa-candidate-20261010.md), com patches e controles
sob `docs/validation/raster-clip-edge-mesa-fix-20261009/`. O código do driver
Mesa não faz parte do CoinRender nem foi instalado como correção geral. O
[estudo EGL/AMD](coin-sampling-amd-origin-study-20261007.md) e seus fixtures
estão em `testsuite/reproducers/coin-sampling-amd-study/`.

## Preparação do PR

Este checkout mantém os relatórios de validação em `docs/coin-render-*.md` e
remete os recibos detalhados ausentes para o arquivo local indicado acima;
eles precisam de publicação durável antes do envio upstream.
Antes de abrir um PR revisável, atualize a base de `origin/master`, execute os
gates no estado final e explicite as falhas ainda abertas. A organização local
do código e dos documentos não equivale a essa aprovação.

A configuração é opt-in: `COIN_BUILD_RENDER=ON`, com
`COIN_RENDER_BACKEND=BGFX` ou `RUST_BRIDGE`; a instalação experimental exige
`COIN_INSTALL_RENDER_EXPERIMENTAL=ON`. Os relatórios indicam os comandos e
os GPUs específicos usados em cada campanha.


## Frente única após a consolidação de 16/10/2026

Continuar CoinRender somente na branch `codex/coin-render`, neste checkout.
A antiga branch `codex/coin-render-clean` foi absorvida pelo nome canônico.
O estado local original, incluindo as 17 modificações e a documentação nova,
foi preservado em snapshot e bundle restaurado independentemente antes da troca.
A organização modular e as correções Windows posteriores são a base atual.

As versões arquivadas de limites de textura e oito mapas CoinGL foram
integradas por seus deltas de código/teste, conservando os testes posteriores.
O relatório de oito mapas é histórico e não qualifica automaticamente esta árvore.
Windows, Linux e demais sistemas são colunas da mesma matriz por backend/GPU.
PBR continua como frente separada, que usa CoinRender e está em
`/home/dikluwe/.codex/worktrees/pbr-clean/coin`, branch `codex/pbr-clean`.

Manifesto, fontes originais e evidências completas:
`/home/dikluwe/Área de trabalho/Estudo coin/consolidacao-frentes-20261016/LEIA-ME.md`.
