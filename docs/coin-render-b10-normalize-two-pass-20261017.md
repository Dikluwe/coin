# B10 — normalização diagonal em duas passagens: revertida

O normalizador de spans BGFX usa três passagens: determina a extensão de
cada eixo, valida todos os vértices sem mutar e, só após aprovação, grava
as posições normalizadas. Testei reunir extensão e validação na primeira
passagem e manter a escrita na segunda. `CoinBgfxCoreTest` passou, incluindo
geometria com zeros sinalizados, fatores subnormais e coordenadas fora da
grade; as imagens A/B foram idênticas.

Na cena city de 40 mil objetos, AMD/Vulkan, 512×512 e geometria 100%
dinâmica, foram três pares AB/BA/AB com 10 quadros de aquecimento e 20
medidos por processo.

| Par | Mediana anterior → duas passagens | p95 anterior → duas passagens |
| --- | ---: | ---: |
| 1 | 182,798 → 187,654 ms | 190,142 → 192,033 ms |
| 2 | 207,592 → 200,399 ms | 247,171 → 243,419 ms |
| 3 | 185,868 → 187,400 ms | 196,560 → 200,007 ms |

A mediana e o p95 pioraram em dois pares. O trace isolado também não mostrou
redução confiável da fase de normalização. **Reverti** a mudança; permanece
a validação por vértice único qualificada anteriormente. Reduzir esta fase
provavelmente exige evitar trabalho redundante na representação dos spans,
não apenas trocar a ordem das passagens.

[CSVs, logs, imagens por hash e trace](validation/coin-render-b10-normalize-two-pass-20261017/verification.json).
