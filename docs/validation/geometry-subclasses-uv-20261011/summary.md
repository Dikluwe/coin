# Geometria côncava, subclasses e UV — 2026-10-11

Branch `codex/coin-render`, NVIDIA física (vendor `0x10de`, device `0x2560`),
oráculo CoinGL GLX. Com a build corrente de `CoinRenderGeometryViewportTest`:

| Rota | Sonda `--gpu --probe-concave` | Suíte `--gpu` | SHA-256 da sonda |
| --- | ---: | ---: | --- |
| BGFX/Vulkan | 64/64, RGB máximo 1 | 1.354/1.354 | `6f1381477cf87cc42136c26e8047cc6191c8d6c078e68f607e10369e51f74b4f` |
| BGFX/OpenGL | 64/64, RGB máximo 1 | 1.354/1.354 | `503863c02ac103a361ea5e2e3e2265a44d470e64f3fd42051281a1042cedfe2a` |
| wgpu/Vulkan | 64/64, RGB máximo 1 | 1.354/1.354 | `8c8e04ab56c449a6bf2b38faecc1e435a75f4aaf56727aeb1e5ddf20ba954f52` |

Os 64 casos cruzam quatro tipos (faces e faces indexadas, bases e subclasses),
fast path, alpha, viewport externo e UV texturizado. Comparam CPU/GPU/CoinGL e
verificam cobertura separada do notch. Os três logs completos da suíte tiveram
SHA-256 `84bad6166466c72925ab85c6b4e3c4b383c8a37b3277fd50bc1afd2e548038da`.
Logs completos e sondas:
`/mnt/Laranja/Git/externos/coin-render-artifacts/geometry-subclasses-uv-20261011/`.

Depois da sonda côncava, os 49 pares de material/normal foram testados em
FaceSet, IndexedFaceSet, QuadMesh, TriangleStripSet,
IndexedTriangleStripSet e subclasses de faces: nove formas, **441 células**.
A primeira execução BGFX/Vulkan falhou em TriangleStripSet, material PER_FACE
e normal PER_VERTEX, CPU/CoinGL RGB máximo 11 contra limite 4. O capturador
foi corrigido para reconstruir os materiais dos vértices compartilhados na
ordem de emissão do strip. A matriz de 441 células foi integrada à regressão;
as três rotas passaram **1.795/1.795** casos, e o máximo RGB da matriz cruzada
foi 1. Logs físicos completos, inclusive a falha original, em
`/mnt/Laranja/Git/externos/coin-render-artifacts/geometry-binding-cross-20261011/`.
SHA-256 BGFX/Vulkan e wgpu/Vulkan:
`15b95087fdc23940a2e1e412482dd95315ee019bd23f14b145c7e661d1d06eac`;
BGFX/OpenGL:
`c02fdb3bcaf8ff54fe650adf7739836e6024553b6ab425713f71101cc1ea14f7`;
falha anterior:
`f39edb0744af34706694595ef8b546afed7da11853a4ce9f42cbf40ff25bd3f1`.

O teste de offset não planar em `CoinRenderDrawStyleTest` já demonstrava
`UNSUPPORTED`, alvo e serial preservados, seguido de recuperação. Este
contrato de rejeição continua válido; a matriz não promete renderizar offset
não planar nem formas arbitrárias de subclasses.
