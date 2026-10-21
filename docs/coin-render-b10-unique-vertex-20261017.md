# B10 — validação por vértice único no lowering instanciado BGFX

Cada índice continua verificado contra o intervalo do draw. Para os
atributos imutáveis de um vértice, o lowering agora executa os gates de
material, interpolação, fog, posição e normal apenas na primeira referência
àquele vértice dentro do span. O remap existente identifica referências
repetidas; não se confia em revisão ou hash para aceitar uma mutação.

Na cena city de 40 mil objetos, AMD/Vulkan, 512×512 e geometria 100%
dinâmica, o A/B usou três pares AB/BA/AB, cada processo com 10 quadros de
aquecimento e 20 medidos. As seis imagens finais foram idênticas por SHA-256.

| GPU | Mediana das medianas anterior → nova | Mediana dos p95 anterior → nova | Pares |
| --- | ---: | ---: | --- |
| AMD/Vulkan | 179,088 → 174,451 ms (−2,59%) | 185,553 → 179,025 ms (−3,52%) | Mediana e p95 melhores em 3/3 |
| NVIDIA/Vulkan | 182,836 → 181,953 ms (−0,48%) | 186,771 → 188,736 ms (+1,05%) | Mediana e p95 piores em 1/3 |

Na NVIDIA, a amostra é inconclusiva para desempenho; não se declara ganho
universal nem melhora de cauda. O trace separado na AMD reduziu a mediana
instrumentada da validação/montagem temporária de 17,124 para 15,236 ms,
mas esse trace acrescenta custo de relógios por span.

`CoinBgfxCoreTest` passou. `CoinRenderProductTest` e
`CoinBgfxInstancingTest` passaram nas quatro células BGFX Linux:
AMD/NVIDIA × Vulkan/OpenGL, 8/8 processos. Os testes de recusa de vértice
inválido e a comparação visual da cena city preservam o contrato. A mudança
fica mantida pelo ganho repetido na AMD e pela eliminação de trabalho
duplicado, com o resultado NVIDIA explicitamente limitado.

[CSVs, logs, hashes e gates](validation/coin-render-b10-unique-vertex-20261017/verification.json).
