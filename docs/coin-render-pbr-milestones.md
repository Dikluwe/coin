# SoPBRMaterial — execução de M0 a M5

Meta: implementar SoPBRMaterial no CoinRender usando as bibliotecas básicas
`Sb*` e `cc_*` do Coin para matemática, estruturas, strings e utilitários
existentes, além da integração normal com nós, estado e actions. Evitar
equivalentes paralelos onde essa base já oferece a operação. `SbString` já usa
`cc_string`; algoritmos de geometria/câmera usarão `SbVec*`, `SbMatrix`,
`SbRotation`, `SbBox*` e `SbList`, e a instrumentação usa `cc_time`.
A implementação inicial usa
BGFX/Vulkan e o viewer CoinRender; imagens devem vir do renderer real.

## Estado verificável

- [x] M0 — baseline Coin, fixtures CAD, câmera e captura reproduzível.
- [x] M1 — nó, estado, serialização, captura, invalidação e perfis.
- [x] M2 — shading GGX por fragmento, materiais opacos, HDR e saída.
- [ ] M3 — IBL pré-calculada, cubemap, irradiância, LUT e lifetime.
- [ ] M4 — views CAD, seleção, cortes, arestas, labels e estabilidade.
- [ ] M5 — SDK instalado, consumidor, regressões e campanha Release.

Cada conclusão deve apontar comandos, código e resultados que provem o alcance
do marco. Uma etapa parcial não será marcada como concluída.

## Baseline

Worktree isolado da branch CoinRender do mesmo repositório Coin. Commit de
origem: c28812338b8c6d51f7a1a8bd7eea6d997a291013. As alterações locais de integração
serão preservadas como entrada identificada, sem editar o checkout compartilhado.

## Critérios finais

O contrato e o pipeline seguirão o plano `plano-sopbrmaterial-coinrender.tex`
preparado neste chat. Isso inclui o nó opaco, estado Open Inventor, FramePlan
tipado, GGX/HDR/saída, ambiente, 72 views CAD, órbita, ferramentas técnicas,
controles independentes e SDK reproduzível. O programa não substitui a base
Coin por uma cena, travessia ou renderer externo.

## M0 — fechado

- Build Release de Coin + CoinRender/BGFX e viewer, com SDK BGFX existente.
- Fixtures construídas com Sb*/nós Coin; teste independente de furo, normais,
  picking e seis presets de câmera.
- 26/26 casos do viewer na AMD física Vulkan: três cenas CAD, seis câmeras,
  controles janela/offscreen, primeiro frame/replay, corte, montagem real
  exportada do FreeCAD e três rejeições de entrada.
- 9/9 CTests de baseline. O drawable de um teste legado estava desmapeado pelo
  window manager; diagnóstico independente mostrou map=0. O teste agora controla
  tamanho/visibilidade do seu drawable, mantendo as mesmas asserções de pixels,
  resize, suspensão, recuperação e readback.
- As imagens deste marco são **legadas**, ainda sem PBR. São a baseline real
  para comparar geometria, câmeras e captura durante os próximos marcos.
- Evidências: [summary](validation/pbr/m0/summary.json),
  [matriz](validation/pbr/m0/results.json),
  [CTest](validation/pbr/m0/contract-tests-final.log) e
  [gallery](validation/pbr/m0/gallery.png).

## M1 — fechado

- SoPBRMaterial experimental: fatores lineares, estado e serialização Coin,
  binding OVERALL, isolamento, campos ignorados e override.
- FramePlan/Recording tipados, validação de fatores e invalidação de materiais.
  Rejeição explícita por executores ainda sem shading PBR; publicação preservada.
- 13/13 CTests BGFX, 10/10 Recording, 11/11 Rust. Regressões legadas de action e
  overlays passam; provas de revisão respeitam notificações e rollback.
- Matriz CAD M0 repetida: 26/26, sem mudança nos hashes dos pixels.
- Contrato: [material](coin-render-pbr-material.md). Evidências:
  [summary](validation/pbr/m1/summary.json), [comandos](validation/pbr/m1/commands.json).

## M2 — fechado

- GGX/Smith correlacionado/Schlick por fragmento, piso perceptual 0,045,
  câmeras ortográfica/perspectiva e transformação de normais Coin.
- Radiância RGBA16F, emissão HDR, exposição por target, Khronos Neutral e uma
  codificação sRGB. Clipping e profundidade vêm do alvo da geometria.
- 18/18 CTests BGFX, 10/10 Recording, 11/11 Rust; oracle independente de
  radiância, câmera/normais, luzes pontuais/spot, saída e rejeições.
- Viewer PBR: 27/27 casos, incluindo 48 imagens primeiro frame/replay,
  igualdade janela/offscreen nas três cenas, corte, grade e exposição +2.
- Matriz legada M0: 26/26, hashes de pixels preservados.
- Contrato: [shading](coin-render-pbr-shading.md). Evidências:
  [summary](validation/pbr/m2/summary.json), [gallery](validation/pbr/m2/gallery.png).

Próximo: M3, ambiente HDR procedural, SH9, prefilter GGX e LUT coerente.
M2 usa luz direta e interação única; IBL e compensação continuam pendentes.
