# B10 — custo interno do lowering instanciado BGFX

Um trace diagnóstico delimitou as fases de `lowerInstanced` na cena city
de 40 mil objetos, geometria 100% dinâmica, AMD/Vulkan, 512×512. Cada
processo teve três quadros de aquecimento e cinco medidos. As medianas
abaixo incluem os oito registros e o custo da instrumentação.

| Fase | Mediana na implementação mantida |
| --- | ---: |
| Pré-checagem e composição | 2,439 ms |
| Matrizes/qualificação por draw | 6,139 ms |
| Preparação e deduplicação de spans | **28,377 ms** |
| Empacotamento de instâncias | 3,757 ms |

São **40.001 spans distintos** de origem, deduplicados para **uma malha** e
40.001 instâncias. O trecho dos spans é o maior alvo CPU medido, mas inclui
validação, normalização, comparação e inserção no mapa; ainda não atribuí
o tempo a uma única operação.

Testei reutilizar os vetores temporários de vértices/índices e depois
reservar antecipadamente o mapa de spans. A mediana instrumentada desse
trecho foi 29,201 ms com reuso e 27,968 ms com reuso+reserva, contra
28,377 ms antes. As diferenças são pequenas nessa amostra de um processo;
as duas micro-otimizações foram **revertidas**. Mantive apenas o trace de
fases, ativo com `COIN_RENDER_TRACE_PHASES`, para guiar uma mudança mais
substancial. `CoinBgfxCoreTest` passou após a reversão.

[Traces e métricas](validation/coin-render-b10-lowering-detail-20261017/results.json).
