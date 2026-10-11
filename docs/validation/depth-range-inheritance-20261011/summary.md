# Profundidade: herança de range e limite do depth clamp

Em 2026-10-11, a revisão de `SoDepthBuffer::GLRender` encontrou um erro na
herança de `range` ignorado. O código atribuía o valor anterior ao **campo do
nó**, enquanto continuava a enviar o valor local ao `SoDepthBufferElement`.
Agora atribui ao valor local `depthrange`. A captura CoinRender já consultava
corretamente o elemento anterior. O novo caso `depth-range/ignored-inherits`
confere o range observado pelas ações callback e GL, a permanência do campo
ignorado no nó e a comparação CPU/GPU/CoinGL sem alterar o gate RGB 4.

| Execução física da regressão de geometria | Resultado | Novo caso, RGB máximo |
| --- | ---: | ---: |
| BGFX/OpenGL, AMD Renoir | 1.800/1.800 | 1 |
| BGFX/Vulkan, NVIDIA RTX 3060 Laptop | 1.800/1.800 | 1 |
| wgpu/Vulkan, AMD Renoir | 1.800/1.800 | 1 |
| Captura CPU | 1.800/1.800 | — |

`CoinRenderDepthContractTest` passou em CPU e nos mesmos três perfis físicos.
O controle legado `CoinRenderBoundingBoxTest --probe-gl-depth-clamp` na AMD
continua distinguindo clipping normal de clamp geométrico: em 96×80, cada
plano near/far passou de 1.536 para 1.728 pixels cobertos, diferença de 192.
O novo reparo de `range` não implementa `GL_DEPTH_CLAMP`.

`vulkaninfo` com o ICD Radeon e o ICD NVIDIA anunciou `depthClamp = true`,
`depthClipEnable = true` e `depthClipControl = true` em ambas as GPUs. O SDK
BGFX usado neste build tem o parâmetro `_depthClamp` em `setDepthControl`.
No wgpu atual, o device não solicita `DEPTH_CLIP_CONTROL` e o pipeline usa
`unclipped_depth: false`. `SoDepthBuffer` expõe apenas `test`, `write`,
`function` e `range`; nenhuma dessas propriedades solicita clamp geométrico.

Uma implementação portátil precisa transportar a intenção por nó até o
frame, preservar push/pop e cache, negociar o recurso no wgpu, aplicar o estado
por draw no BGFX e rejeitar perfis sem suporte antes da publicação. Depois
deve comparar near/far, ranges normais/reversos, transparência, sombras, RTT e
formatos de depth. A disponibilidade física observada aqui não qualifica esse
contrato; a caixa principal do checklist permanece aberta.
