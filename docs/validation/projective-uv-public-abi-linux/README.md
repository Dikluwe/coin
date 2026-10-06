# UV projetivo com ABI pública Coin 4 — Linux

Data: 2026-10-06. Branch: `codex/coin-render-transform-performance`, parent
`b4543b0dc5cef6f127a40b64527bf06b9c811145`.

A implementação UV `bebbd1852c` já usa ST/R/Q em contratos privados e não
modifica headers públicos. A correção desta campanha restaura
`SoLazyElement::LightModel` como enum canônico, conservando o nome experimental
como typedef do mesmo tipo. Não há mudanças de valores, layout ou shaders.
A branch Coin 5 permanece separada.

## ABI binária

Baseline: release `v4.0.10` em `b3ccdc8c5cc22774377b0baa5543990055f28ba2`.
Os dois builds usam GCC, C++11, RelWithDebInfo, `-g`, biblioteca compartilhada,
instalações próprias e SONAME `libCoin.so.80`. O candidato inclui CoinRender
wgpu; o baseline é a libCoin oficial. O protocolo privado wgpu continua 45.

`abidiff-public.log` usa libabigail 2.4, headers instalados, tipos privados
opacos (`--drop-private-types`) e diferenças diretas (`--leaf-changes-only`).
Não há suppressions customizadas. Resultado: zero tipos públicos alterados,
zero funções removidas ou alteradas, zero variáveis existentes removidas ou
alteradas, 40 funções adicionais e quatro símbolos de variáveis adicionais.
Essas adições incluem trabalho anterior da branch e não são mudanças do UV.
Exit 4 significa adições; o bit de incompatibilidade não está presente.

`abidiff.log` conserva o relatório completo. Ele aponta duas diferenças
indiretas em `CoinVrmlJsMFHandler::field2jsval` e no destrutor
`SbList<SoNode*>`, ambas sem mudança de tamanho/membros públicos. O header
público SoField é idêntico ao da release, e os membros físicos de SbList
permanecem iguais; as alterações de SbList são nos corpos inline de segurança
de crescimento. O relatório público resolve essas diferenças de implementação
sem esconder uma classe pública específica.

## Consumidor antigo, sem recompilação

`legacy-consumer.cpp` é compilado exclusivamente com os headers e biblioteca
instalados do baseline. O mesmo executável passa 14 checks em duas execuções:
primeiro com a release, depois com `LD_LIBRARY_PATH` apontando para a biblioteca
candidata. O binário não é recompilado entre elas. Os logs registram a biblioteca
real obtida por `dladdr` e os tamanhos compilados antigos de Input, Callback,
PickStyle e RenderManager. Exercita `readHex` virtual, leitura numérica,
assinaturas antigas por valor, nomes, enums e objetos com layout antigo.

`canonical-types.cpp` também compila com os headers atuais e verifica o tipo
original e seu alias, incluindo os valores BASE_COLOR/PHONG.

## UV e integração

| Oracle GPU | wgpu/Vulkan | BGFX/Vulkan | BGFX/OpenGL |
| --- | ---: | ---: | ---: |
| CoinGL nativo | 138 | 138 | 138 |
| Projeção unit-zero equivalente | 4 | 4 | 4 |
| Composição de uma camada equivalente | 5 | 5 | 5 |
| Oito estágios analíticos independentes | 50 | 50 | 50 |

Todos passaram sem skips: 414 comparações nativas, 27 equivalentes e 150
analíticas. Também passaram seis gates CTest (CoinTests, configuração,
preparação de frame, action, captura UV e captura BOUNDING_BOX) e a captura
UV no build BGFX. Rust, FFI, shaders e structs privados não mudaram nesta
correção; a ponte Rust 45 existente foi reutilizada.

`summary.json` registra comandos, hashes de fontes, binários e logs. Builds
ABI são independentes dos builds Release anteriores. Logs são normalizados
somente em whitespace final, mantendo hashes brutos. `run.py` reproduz os gates
GPU usando o helper de ambiente do repositório.

Qualificação Linux/NVIDIA; MSVC/macOS exigem seus gates. Isto não certifica
compatibilidade geral de fonte de outras alterações herdadas, nem alpha geral
de framebuffer ou os perfis de textura ainda não admitidos.

Consulte o [contrato UV](../../coin-render-projective-uv-contract.md).
