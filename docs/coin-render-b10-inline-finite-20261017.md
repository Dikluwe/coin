# B10 — classificação finita dentro da validação de vértices

O validador de `CoinRenderFramePlan` examinava todos os campos numéricos de
cada vértice por meio de uma chamada externa por componente. Na cena city de
40 mil objetos, o plano tem 960024 vértices; o trace anterior media cerca de
72 ms somente nessa fase. A função exportada `coin_render_is_finite` continua
disponível. O loop de validação usa agora uma implementação inline com a mesma
regra de bits IEEE-754 e o mesmo fallback `std::isfinite`. Nenhuma validação
foi removida: posições, normais, UVs, coordenadas extras, R/Q, W, fog e slots
de material continuam examinados em cada invocação.

## A/B físico

O binário BGFX anterior foi congelado antes da alteração. O A/B usou a mesma
cena, NVIDIA RTX 3060 Laptop, Vulkan, 512×512, transparência por objeto,
10 quadros de aquecimento e 40 medidos por processo. Três processos por
variante/caso foram intercalados. A estatística é a mediana das medianas de
processo; p95 é a mediana dos três p95 de processo.

| Caso | Antes: mediana / p95 | Inline: mediana / p95 |
| --- | ---: | ---: |
| Geometria 100% | 278,321 / 292,349 ms | 212,857 / 224,561 ms |
| Estático | 0,906 / 0,970 ms | 0,909 / 0,998 ms |

Na geometria dinâmica, a mediana caiu **23,52%**. O trace separado reduziu a
mediana da validação de vértices de 71,868 para 9,211 ms. A mediana de update
ficou em aproximadamente 61 ms nas duas variantes, o que localiza o ganho na
validação/renderização, sem atribuir mudança ao overlay. Os traces são
instrumentados e não entram na estatística de latência.

## Correção e limites

Três quadros dinâmicos A/B foram idênticos byte a byte em PPM. O teste
`CoinRenderFrameCoreTest` passou nos builds BGFX e wgpu: cobre os 40 campos
de vértice com NaN/infinito, extremos finitos, subnormais e zero sinalizado,
além das recusas do plano. `CoinRenderAdvancedCoverageTest` passou nas sete
células Linux disponíveis (BGFX AMD/NVIDIA × Vulkan/OpenGL; wgpu AMD/NVIDIA
Vulkan e AMD OpenGL). Os logs, CSVs, comandos, hashes de binários e imagens
comprimidas estão no [pacote verificável](validation/coin-render-b10-inline-finite-20261017/verification.json).

Este ganho é do perfil dinâmico medido, não um fechamento geral de B10.
Primeiro frame, Windows/Android/Metal, outras cenas, memória, GPU timings,
janela, política canônica, custo LESS e o caso BGFX de 16 milhões de
triângulos mantêm campanhas próprias. A validação ainda custa cerca de 9 ms
neste plano; reuso incremental exigiria prova de identidade e invalidação
para cada mutação, sem confiar apenas em revisão ou checksum.
