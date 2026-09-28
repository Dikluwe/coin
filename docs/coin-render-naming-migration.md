# Migração de nomes CoinRender, CoinBgfx e CoinWgpu

O commit de base `f749a64527` preserva o checkout existente antes da migração,
incluindo mudanças anteriores de renderização e outras correções Coin. Ele não
é parte da renomeação. A branch de origem `codex/bgfx-evaluation` foi preservada.

A branch `codex/coin-render` contém o contrato, a API e os componentes comuns.
As branches `codex/coin-bgfx` e `codex/coin-wgpu` partem do mesmo commit comum e
contêm suas respectivas adaptações. A integração final ocorre em `coin-render`.
São branches por escopo de mudança, não uma divisão entre agentes.

A API principal é `CoinRenderAction`, `CoinRenderTarget`, `CoinRenderSceneManager`,
`CoinRenderManagerAdapter` e `CoinRenderCapabilities`. Os tipos do contrato e
algoritmos mecânicos comuns usam o prefixo `CoinRender`. O pacote e a biblioteca
são `CoinRender`, com target instalado `CoinRender::CoinRender`.

As opções principais são `COIN_BUILD_RENDER`, `COIN_RENDER_BACKEND`,
`COIN_BUILD_RENDER_WINDOW_EXAMPLE`, `COIN_BUILD_RENDER_BENCHMARKS` e
`COIN_INSTALL_RENDER_EXPERIMENTAL`. A seleção continua aceitando RECORDING,
BGFX, RUST_BRIDGE e os protótipos DAWN/WGPU_NATIVE.

Headers públicos antigos e o pacote `CoinWgpuExperimental` encaminham para
CoinRender como compatibilidade de código-fonte. É necessário recompilar os
consumidores: não se promete compatibilidade binária dos símbolos C++ nem o
nome antigo da biblioteca. Os tipos registrados das actions usam os nomes novos.
Opções CMake antigas inicializam as opções novas quando estas não foram
explicitadas; em conflito, o nome novo prevalece. RTT/profiling reconhecem as
variáveis de ambiente compartilhadas antigas como fallback.

A renomeação não amplia capacidades nem fecha a decisão semântica única. A
extração do Core comum e as pendências funcionais permanecem acompanhadas na
[checklist](rendering-responsibilities-checklist.md). A mudança de nome não deve
ser usada como evidência de equivalência entre executores.

O [mapa de nomes](coin-render-name-map.json) registra símbolos e caminhos
migrados. Os exemplos e o patch FreeCAD distribuído usam os nomes principais.
