# B10 — dimensão do cubo e A/B da normalização especializada

No trace atual da cidade dinâmica, 40.001 spans de fonte resultam em
40.001 instâncias e uma malha BGFX de 24 vértices/36 índices. O cache de
templates de cubo teve 39.969 expulsões porque a animação modifica as
dimensões de cada cubo; não é uma colisão da chave. O custo deve ser
tratado no preparo/captura e lowering com validação por quadro, sem
assumir que uma única dimensão represente todos os objetos.

Foi testado um candidato que, para spans de 24 vértices, usava os valores
absolutos do primeiro vértice como dimensões e verificava todos os demais
antes de normalizar. Malhas que não satisfaziam a prova mantinham o caminho
geral. Três pares AB/BA/AB executaram a mesma cena city 40 mil, geometria
100% dinâmica, AMD/Vulkan, 512², 10 frames de aquecimento e 20 medidos por
processo. As seis imagens finais tiveram SHA-256 idêntico.

| Métrica, mediana dos três processos | Antes | Candidato |
| --- | ---: | ---: |
| Mediana do tempo total por frame | 178,635 ms | 179,705 ms |
| p95 do tempo total por frame | 184,817 ms | 188,264 ms |

O candidato foi **revertido**: não melhorou a mediana nem a cauda nesta
campanha. O `CoinBgfxCoreTest` passou após a reversão. Ficou apenas o campo
diagnóstico `first_mesh_vertices/indices` sob `COIN_RENDER_TRACE_PHASES`,
para orientar experimentos posteriores. O trace usa poucos frames e não
é estimativa de cauda.

[A/B, imagens, cena e trace](validation/coin-render-b10-cube-normalize-20261017/verification.json).
