# B10 — decomposição da preparação de spans BGFX

O trace de `lowerInstanced` separou a preparação dos 40.001 spans da cena
city dinâmica em AMD/Vulkan. O processo teve três quadros de aquecimento e
cinco medidos; as medianas abaixo incluem os oito registros. Há relógios
por span, portanto estes valores contêm custo da instrumentação e **não**
devem ser somados à linha de base sem trace.

| Trabalho | Mediana instrumentada |
| --- | ---: |
| Busca da faixa | 1,994 ms |
| Validação, remap e montagem temporária | **17,124 ms** |
| Normalização diagonal | **10,310 ms** |
| Comparação/deduplicação da malha | 1,801 ms |
| Inserção da faixa | 3,273 ms |

Todos os 40.001 spans são distintos, mas resultam numa única malha. O maior
custo está na leitura, validação e normalização de cada fonte mutável.
O próximo A/B deve reduzir passagens ou dados intermediários **sem perder a
validação por quadro** nem a igualdade da imagem; otimizar só o hash ou a
reserva do mapa tem pouco espaço neste perfil. A instrumentação foi mantida
apenas sob `COIN_RENDER_TRACE_PHASES`. `CoinBgfxCoreTest` passou.

[Trace e métricas](validation/coin-render-b10-range-detail-20261017/results.json).
