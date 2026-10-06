# Complexity BOUNDING_BOX — Linux

Campanha iniciada em 2026-10-05 e concluída em 2026-10-06, na branch
`codex/coin-render-transform-performance`, base `bebbd1852c2718346f2270fbd3f39dc5d7f29c51`.
Referência: Coin/OpenGL nativo, NVIDIA GeForce RTX 3060 Laptop / driver 610.57.04.

## Resultado

| Gate | wgpu/Vulkan | BGFX/Vulkan | BGFX/OpenGL |
| --- | ---: | ---: | ---: |
| BOUNDING_BOX GPU/CoinGL | 137 | 137 | 137 |
| BOUNDING_BOX CPU/CoinGL | 134 | 134 | 134 |
| Captura sem GPU | 1 | 1 | — |
| Core CTest | 20 | 19 | 19 |
| UV GPU/CoinGL nativo | 138 | 138 | 138 |
| UV projeção unit-zero equivalente | 4 | 4 | 4 |
| UV composição one-layer equivalente | 5 | 5 | 5 |
| UV GPU/oráculo analítico independente | 50 | 50 | 50 |
| Alpha/RGB REPLACE GPU/CoinGL | 116 | 116 | 116 |
| Texto/imagem GPU/CoinGL | 93 | 93 | 93 |
| MarkerSet GPU/CoinGL | 121 | 121 | 121 |
| ShadowReference nativo | 1 | 1 | 1 |

Os gates finais passaram sem skips. BOUNDING_BOX tem 411 comparações GPU
nativas e 402 CPU nativas; os três controles de mapas por variante exigem GPU.
O máximo erro RGB foi um nível em CPU e GPU; MAE máximo GPU foi 0,333333 e
CPU foi 1, sem pixels com erro acima de três níveis.

Clipping transparente POINTS inclui o ponto criado na diagonal interna do
quad nativo e mantém 100 pixels. LINES conserva os contornos. Os asserts
conferem RGBA8 antes da composição e a classificação transparente original.
NaN/infinito/overflow de extensão são rejeitados sem publicar pixels/serial;
a correção dos bounds restaura a cena. Mapas conservam o caster Sphere de
390 triângulos enquanto o desenho principal captura 12.

Também há três execuções dedicadas de DepthContract após recompilar sua cópia
local do builder. O CMake fixa o DrawStyle bare em Vulkan no build BGFX;
`drawstyle-bgfx-opengl.log` é a execução direta adicional que qualifica OpenGL.
Todos os processos GPU foram serializados. Builds usaram seis jobs.

## Rastreabilidade

[summary.json](summary.json) registra comandos, ambientes, status, métricas,
SHA256 de fontes, binários executados e artefatos. Logs são normalizados apenas
para remover espaços finais e linhas vazias no fim; o SHA256 bruto também é
registrado. Os pilotos preservam as falhas encontradas e sua resolução:
contornos degenerados, cobertura ambígua da fixture FILLED, depth CPU
coplanar e clipping nativo de POINTS. Eles não contam como gates aprovados.

`run.py` reproduz as fases com os builds locais e o helper de ambiente do
repositório. `archive.py` valida e empacota os resultados. Os probes C++ são
investigações locais e incluem a fixture por caminho absoluto deste worktree.
Os hashes de execução registram as bibliotecas efetivamente usadas por cada
fase, incluindo testes que compilam uma cópia própria do builder.

Rust, shaders, protocolo e layout de vértices não mudaram desde a base.
A ABI wgpu continua 45. Os 38 testes Rust da campanha UV anterior não foram
reexecutados nesta campanha; não são contabilizados como testes novos.

## Limites

Qualificação Linux/NVIDIA; Windows e outros drivers precisam de seus gates.
Não há benchmark de desempenho novo nem certificação do alpha geral do
framebuffer. FUNCTION/texgen e unidades adicionais ativas, filtros/formatos
fora dos perfis existentes e slope offset com área projetada zero continuam
recusados. Caixas vazias não geram geometria. Raster original de texto/imagem/
marcadores como caster de mapas continua fora do perfil.

Consulte o [contrato completo](../../coin-render-bounding-box-contract.md).
