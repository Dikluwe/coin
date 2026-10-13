# Sombras PBR no CoinRender

Branch `codex/sombras-pbr`, iniciada em `6d2f8ecdb2be1be84bdc12069db39e88cbc7a802`.
Objetivo: implementar os seis itens solicitados, em sequência, mantendo os nós,
estado, matemática Sb e infraestrutura cc do Coin. A política de qualidade
orientada pela view pertence ao CoinRender; propriedades físicas permanecem no
material e nas luzes. Perfil inicial de execução: BGFX/Vulkan e cenas CAD.

- [x] S1 — orçamento completo, admissão conjunta de recursos e diagnóstico dos mapas.
- [x] S2 — receptor PBR opaco com visibilidade por luz, GGX, HDR/IBL e views CAD.
- [ ] S3 — contato/detalhe: bias separado da variância, momentos e culling.
- [ ] S4 — estabilidade da projeção direcional durante órbita/zoom.
- [ ] S5 — persistência dos attachments e cache correto por conteúdo/projeção/vista.
- [ ] S6 — filtros VSM/PCF e PCSS, incluindo contrato de emissor/penumbra e orçamento.

Cada fechamento deve registrar implementação, comandos, testes discriminantes,
imagens reais e limitações. Os controles legados de Phong e de qualidade Coin
continuam válidos; eles não são um oracle de radiância GGX. O orçamento completo
de momentos/depth passa a explicitar o teto físico de 160 MiB antes admitido
pela contagem incompleta de 128 MiB somente de momentos. Não reduzir resolução
ou remover asserções existentes para fazer controles passar.

## Estado

S1 concluído: seis controles passaram na AMD física Vulkan `1002:1638`, incluindo
orçamento completo, fronteiras/rollback, modos de diagnóstico, falha real de
attachment com reconfiguração e os controles CoinGL atuais. O lookup VSM legado
foi extraído sem alterar sua matemática. Novos controles de API recusam modos,
índices e orçamentos inválidos. A admissão conjunta conta mapas, cena, overlays
e diagnóstico antes de alocar os attachments da submissão.

Evidências: [summary](validation/shadows/s1/summary.json),
[CTest](validation/shadows/s1/ctest.log), [comandos](validation/shadows/s1/commands.txt)
e [diagnósticos reais](validation/shadows/s1/gallery.png).
S2 concluído: receptor GGX por fragmento usa a visibilidade do mapa associado a
cada luz, preserva HDR, emissão e IBL. Linhas técnicas BASE_COLOR são compostas
após tone mapping e não entram nos mapas. Os 12 controles passaram, incluindo
igualdade entre janela Xlib e offscreen, os oracles GGX/IBL existentes e CoinGL.
[Evidências S2](validation/shadows/s2/summary.json) e
[captura real](validation/shadows/s2/pbr-shadows.png).
PBR continua opaco, sem texturas de material e sem readback assíncrono qualificado.
O diagnóstico de visibilidade para PBR ainda é feito por radiância linear;
profundidade/momentos estão disponíveis. S3 em implementação; S4–S6 abertos.
