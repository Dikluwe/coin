# Transformações comuns e lowering específico

Quarta e última fase desta campanha em `codex/coin-render-isolation`, sobre
`f25e6e4110`. Fecha a extração planejada após action, infraestrutura do target e builder.

## Fronteira implementada

`CoinRenderTransformCore` é o dono comum dos cálculos de conversão da projeção Coin
para depth `[0,1]`, matriz de normais, projeção do viewport e interseção do scissor,
além da verificação de matrizes finitas. Usa matrizes e snapshots Coin; não depende
de action/estado, layout de vértice, GPU ou nomes de backend.

`CoinBgfxLowering` usa esse Core no lowering completo, clipping do viewport e patch
de câmera. `CoinWgpuFfiFrame` usa o mesmo Core para projeção e normais, inclusive
no caminho de batching. Ordem das multiplicações, tolerância de singularidade,
convenções e diagnósticos anteriores foram preservados.

O lowering BGFX mantém layout compacto/completo, empacotamento de uniformes,
agregação específica de draws, assinaturas de agrupamento, patches de buffers e
limites dos mecanismos BGFX. Transparência, ordenação Coin, alpha, screen door e
depth efetivo continuam no Core comum de composição. A adaptação para os recursos
concretos permanece nos conectores.

`CoinBgfxLowering.cpp` também deixou a lista incondicional de fontes do módulo:
agora só integra `libCoinRender` no build BGFX. Os testes de contrato que qualificam
os dois lowering/packers ainda compilam os adaptadores necessários como fontes
privadas de teste. Inspeção dos símbolos locais confirmou presença no módulo BGFX
e ausência nos módulos wgpu/RECORDING.

## Validação

32 casos selecionados aprovados, sem skips: 16 de Core/empacotamento nos três builds
e 16 execuções GPU nos dois backends, incluindo clipping, estilos, texturas,
composição, sombras e depth real em Vulkan/OpenGL.

O novo `CoinRenderTransformCoreTest` liga somente `Coin`, sem `CoinRender`/GPU.
Verifica limites de depth, normais com escala não uniforme, fallback singular,
matrizes não finitas, viewport parcial sem alterar a projeção, interseção vazia
sem modificar saída e soma de bounds sem overflow de 32 bits.

Quatro verificações estáticas (40 mil edifícios, 1024×1024, Vulkan/OpenGL em BGFX e
wgpu) preservaram os checksums anteriores. Quatro verificações dinâmicas
(10 mil edifícios, alteração de câmera/material, Vulkan nos dois backends) também
preservaram os checksums: `0x972ec92989e3abdc` para câmera e
`0x9147e63e47d4fcaf` para material.

[Evidências e scripts](validation/render-transform-linux/). Não se declara ganho
de desempenho nesta campanha. Windows e os protótipos não foram executados.
A conclusão é sobre os limites de organização descritos, sem declarar resolvidas
as pendências de equivalência funcional ou todos os pontos da checklist de render.
