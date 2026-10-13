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
- [ ] M1 — nó, estado, serialização, captura, invalidação e perfis.
- [ ] M2 — shading GGX por fragmento, materiais opacos, HDR e saída.
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

Próximo: M1, contrato e estado SoPBRMaterial, com registro, serialização,
herança/override, captura tipada e rejeição explícita em executores sem PBR.
