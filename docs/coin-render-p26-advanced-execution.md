# P26 — execução avançada, contrato a implementar

P26 separa otimizações internas, que precisam preservar pixels/ordem/picking do
Coin, de extensões explícitas. O CoinRender já emite draws convencionais,
agrupa estados e reutiliza buffers/readback; isso não constitui instancing,
indirect draws ou culling de cena. O probe BGFX registra o bit de compute do
hardware, mas esse fato não prova executor de compute CoinRender.

- [ ] Medir custo de travessia, construção do FramePlan, uploads e submissão
  nas cenas P17–P19 antes de escolher um mecanismo.
- [ ] Definir Core comum para visibilidade/ordem e plano de lotes sem alterar
  seleção, transparência, camadas e semântica de nó. Comparar com execução
  convencional no mesmo frame.
- [ ] Implementar instancing e indirect por Infra apenas onde houver suporte
  comprovado; fallback convencional deve ser semanticamente idêntico e medido.
- [ ] Se compute preparar geometria, explicitar ownership, sincronização,
  readback e perda de dispositivo no plano de recursos P12–P14.
- [ ] Ray tracing é extensão opt-in posterior a contrato, orçamento, qualidade
  e matriz de APIs; não substituir o raster Coin silenciosamente.
- [ ] Validar A/B com hashes de imagem, seleção, métricas P17–P19 e células
  físicas P20–P23. Um bit de capacidade da GPU não fecha uma célula.

Nenhum item funcional de P26 está fechado. A implementação depende de P24/P25
quando a otimização interagir com multipass ou efeitos.
