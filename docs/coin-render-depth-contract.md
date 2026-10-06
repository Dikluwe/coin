# Contrato de profundidade WGPU/BGFX

`CoinRenderAction` captura `SoDepthBufferElement` e `SoPolygonOffsetElement`
em cada `CoinRenderRenderStateSnapshot`. O padrão é depth test/write habilitados, função
LESS, range [0,1] e offset desabilitado, factor/units zero e estilo FILLED.
Um nó `SoPolygonOffset` habilitado preserva factor, units e a máscara de estilos.
O estilo original de linhas/pontos permanece no estado após expansão em
triângulos. Só draws cujo estilo intersecta a máscara recebem bias; estados
seguintes e irmãos de `SoSeparator` não herdam uniform de um submit anterior.

Com teste desligado, a execução também desliga a escrita efetiva, mesmo com
`write = TRUE`. A captura conserva os campos originais. O contrato e os gates
focados OFF/ON/OFF estão em
[profundidade e política de fragmentos](coin-render-fragment-policy-contract.md#profundidade).

## BGFX

O shader calcula profundidade de janela por fragmento, depois do clipping:

```
depth = near + (far - near) * gl_FragCoord.z
slope = max(abs(dFdx(depth)), abs(dFdy(depth)))
unitScale = rendererIsOpenGL ? 1 / 8388608 : 1 / 16777216
depth = clamp(depth + factor * slope + units * unitScale, 0, 1)
```

O uniform é configurado para todo draw, com factor/units zero quando o offset
está desabilitado ou fora do estilo selecionado. O mesmo cálculo alimenta
`gl_FragDepth`, a comparação manual de depth peeling e os pesos de weighted OIT.
A transformação de range no fragmento preserva o volume de clipping original;
alterar apenas a matriz de projeção permitiria que geometria fora desse volume
voltasse a aparecer. Depth test, write e função continuam no estado BGFX. O agrupamento opaco só
reordena o contrato padrão (test/write, LESS, range [0,1], sem offset); qualquer
estado de profundidade não padrão preserva a ordem de travessia inteira.

A API BGFX instalada oferece `setDepthControl(constant, slopeScale, clamp)` por
draw e o renderer GL usa `glPolygonOffset` com FILLED. Esse bias nativo seria
sobrescrito por `gl_FragDepth` ao implementar o range em shader; além disso,
`gl_FragCoord.z` usado pelo peeling não inclui o bias nativo. Por isso a
implementação usa o cálculo compartilhado de profundidade no shader.

O termo constante usa uma unidade de profundidade normalizada D24 (2^-24).
No renderer OpenGL são usados dois LSBs D24: um LSB pode arredondar de volta
para o mesmo valor ao percorrer `gl_FragCoord` e `gl_FragDepth`. Em D32F, e
nos drivers cujo fator mínimo de `glPolygonOffset` difere, isso continua sendo
uma aproximação, não paridade bit a bit com OpenGL. A derivada de slope
é calculada sobre a profundidade já remapeada. Linhas e pontos expandidos
recebem a semântica do estilo original; isso também permite offset em
primitivas independentes, uma extensão em relação ao GL_POLYGON_OFFSET_LINE.
Escrever profundidade no shader pode reduzir a eficiência de early depth test;
não há medição de desempenho desta mudança no FreeCAD.

## Rust e ABI

Rust mantém depth range no viewport e usa `wgpu::DepthBiasState`, com bias
constante arredondado para i32 (limitação da API wgpu) e slope em float.
O cache de pipelines inclui ambos; a máscara é resolvida antes da escolha do
pipeline. A precisão do termo constante segue o formato Depth32Float do wgpu,
portanto pode diferir da aproximação D24 usada no BGFX.

O protocolo **privado** C++/Rust passa de 18 para 19. Os novos campos foram
anexados ao estado, preservando offsets anteriores, com assertions de tamanho
e offset dos dois lados. Bridges de revisão diferente são rejeitadas antes da
leitura dos estados. A ABI pública de libCoin e dos nós existentes não muda.

## Verificação

`CoinRenderDepthContractTest` cobre captura por travessia, defaults, máscaras de
estilo após expansão, preservação das configurações de depth, FFI e invalidação
de cache. Os testes GPU Vulkan/OpenGL verificam overlays coplanares com
factor/units de ambos os sinais, desabilitação entre frames, máscaras de estilo,
range não padrão e preservação de test/write/function por leitura de pixels.
As suítes existentes de transparência/overlays devem passar com profundidade
padrão. A validação visual da viewport real do NaviCube no FreeCAD ainda é uma
etapa externa; os testes não afirmam ter executado o aplicativo.

O backend CPU de referência não é a implementação de bias/range validada nesta
mudança; o escopo de execução é BGFX e o contrato/pipeline Rust.
O CoinRenderFramePlan continua aceitando apenas ranges crescentes dentro de [0,1];
range invertido e clamping de entradas fora desse intervalo não foram adicionados.

## Relatório da entrega (2026-09-26)

Worktree: `/tmp/coin-wgpu-depth-offset`, branch `codex/wgpu-depth-offset`.
O checkout original não foi alterado. As mudanças preexistentes foram copiadas
para um commit-base separado (`41fe27af62`); o commit seguinte contém somente
esta implementação de profundidade.

Arquivos alterados (caminhos relativos ao repositório):

- `src/rendering/coinrender/CoinRenderFramePlan.h`, `CoinRenderFramePlan.cpp`,
  `CoinRenderFramePlanBuilder.cpp`: estado, validação e captura por draw/estilo.
- `src/rendering/coinrender/CoinRenderFrameReuseCore.cpp`: invalidação por offset/range/depth.
- `src/rendering/coinbgfx/CoinBgfxLowering.h`, `CoinBgfxLowering.cpp`: lowering,
  filtragem de estilos, agrupamento e proteção do camera patch.
- `src/rendering/coinbgfx/CoinBgfxBackend.h`, `CoinBgfxBackend.cpp`:
  ciclo de vida do uniform e aplicação por draw.
- `src/rendering/coinwgpu/CoinWgpuFfi.h`, `CoinWgpuFfiFrame.cpp`,
  `rust_bridge/src/lib.rs`: protocolo privado 19, payload e pipeline Rust.
- `src/rendering/coinrender/CoinRenderRecordingBackend.cpp`: diagnóstico do estado capturado.
- `src/rendering/coinbgfx/shaders/coin_depth.sh`, `fs_base_color.sc`,
  `fs_peel_next.sc`, `fs_weighted_oit.sc`: profundidade compartilhada dos shaders.
- `src/rendering/coinrender/CMakeLists.txt`: dependência do include dos shaders.
- `testsuite/coinrender/CoinRenderDepthContractTest.cpp`, `CoinRenderLightingTest.cpp`,
  `testsuite/CMakeLists.txt`: testes e revisão do protocolo.
- `docs/coin-bgfx-evaluation.md`, `docs/coin-render-depth-contract.md`: suporte e limitações.

Validações executadas:

```
cmake --build /tmp/coin-wgpu-depth-build -j 6
ctest --test-dir /tmp/coin-wgpu-depth-build --output-on-failure
cargo check --offline --locked
cargo test --offline --locked --lib
git diff --check
```

Build C++/BGFX e shaders GLSL/SPIR-V concluídos. CTest: **47/47 passaram**,
sem skips, incluindo os testes GPU de profundidade em Vulkan e OpenGL,
transparência (object/weighted OIT/sorted layers), overlays, texturas, iluminação,
RTT, cache e os testes C++ existentes. Rust: check aprovado e **3/3 testes**
passaram; permanecem cinco warnings preexistentes de APIs deprecated/dead code.
O teste Rust novo cobre sinais, arredondamento de units, máscaras, desabilitação
e rejeição de NaN; a execução GPU do Rust não foi validada nesta entrega.

Limitações restantes: precisão D24 aproximada no BGFX versus bias nativo D32F
no Rust; custo de early-Z não medido; validação visual do NaviCube/textos/
wireframes na aplicação FreeCAD ainda pendente. Os overlays coplanares do
teste GPU passam, mas não substituem essa validação de integração.

## Ampliação de geometria/viewport em 2026-10-06

Veja o [perfil P02/P04/P05/P06](coin-render-geometry-viewport-contract.md):
viewport externo/vazio no Core, correção de bindings de normais e fixtures
compartilhadas com resize, alpha, luzes/fog e estilos com UV procedural.
Os limites de depth/offset e a matriz completa por shape continuam abertos.
