# P27 — sombras Coin: referência e implementação pendente

P27 exige executar `SoShadowGroup` ativo com a semântica Coin em BGFX e wgpu.
A referência e o bloqueio seguro estão verificados; nenhum executor de sombras
foi implementado. Portanto P27 permanece **aberto**.

## O que o Coin/GL faz

`SoShadowGroup` usa um mapa por luz suportada e Variance Shadow Maps (VSM).
Quando GL 2.0, framebuffer object ou textura float faltam, ele atravessa o
grupo sem sombras e emite aviso. Um grupo inativo também atravessa os filhos
normalmente. O caminho com sombras cria mapas RGBA32F (ou depth se o programa
VSM não existir), calcula câmeras para luzes spot/directional e faz passes de
casters antes da composição dos receivers. `SoShadowStyle` só implementa
`GLRender`: `NO_SHADOWING`, `CASTS_SHADOW`, `SHADOWED` e ambos controlam
participação por shape. `SoShadowSpotLight` e `SoShadowDirectionalLight` podem
fornecer `shadowMapScene` próprio. A política de qualidade muda iluminação
por fragmento acima de 0,3 para spots e acima de 0,7 para outras luzes.
O lookup VSM usa momentos, `epsilon` e `threshold`; `precision`, raio de
visibilidade e bounding box alteram resolução/câmera. Esses campos pertencem
ao contrato Coin, não à interpretação isolada de cada backend.

## Referência reproduzível

`CoinRenderShadowReferenceTest` constrói uma cena com cubo vermelho, plano branco,
spot ou luz direcional e `SoShadowGroup`. Sob Mesa llvmpipe/GLX com suporte a
sombras, testa grupo desligado/ligado, caster e receiver desligados, restauração
de estilo, e diferença espacial da luz direcional. A amostra spot no plano foi
`600` (grupo desligado), `126` (sombra), `600` (receiver desligado), `654`
(caster desligado), em soma RGB; a região central do cubo variou só `1`. Para
directional, a maior diferença de estilo foi `666`. O teste usa relações e
tolerâncias, sem gravar esses valores de um driver como golden universal.

```sh
env __GLX_VENDOR_LIBRARY_NAME=mesa COIN_GLXGLUE_NO_PBUFFERS=1 \
  COIN_GLX_PIXMAP_DIRECT_RENDERING=1 COIN_RENDER_REQUIRE_GL_REFERENCE=1 \
  xvfb-run -a -s '-screen 0 1280x1024x24 +extension GLX' \
  ctest --test-dir <build> -R '^CoinRenderShadowReferenceTest$' --output-on-failure
```

Sem `COIN_RENDER_REQUIRE_GL_REFERENCE`, o teste exerce o contrato de falha do
CoinRender em CPU: após um quadro válido, a mesma cena com grupo ativo retorna
`UNSUPPORTED`, preserva pixels e serial e aceita um quadro seguinte válido.
O teste passou nas builds RECORDING e wgpu; a referência GL passou em Xvfb.
Isso verifica diagnóstico/publicação e o oráculo; não verifica sombras em GPU.

## Trabalho funcional para fechar

- [ ] **Wiring:** capturar grupo, `SoShadowStyle` (não tem callback), luzes,
  matrizes, `shadowMapScene`, overrides e estado `SoState`; preservar escopo de
  separadores e caminhos de ação. Não executar `GLRender` como fonte de captura.
- [ ] **Core:** resolver casters/receivers, câmeras e pass graph com limites de
  unidades, visibilidade, mapa, qualidade, transparência e ciclo de dependências.
  Reusar ownership e publicação de P12–P14, inclusive múltiplos alvos.
- [ ] **Infra BGFX/wgpu:** mapas de momentos e depth, VSM, bias, textura,
  passes, sincronização, resize e reconstrução após perda, com shader específico
  por API. Mapas da Infra não entram no estado Coin.
- [ ] **Shell/capacidades:** seleção explícita de perfil implementado e
  disponível; diagnósticos de limite/formato sem fallback visual implícito.
- [ ] **Qualificação:** comparar spot/directional, todos os estilos, cenas
  próprias por luz, clipping, alpha/transparência, RTT, resize e falhas nas
  mesmas fixtures Coin/GL. Registrar GPU/API/driver e tolerâncias por célula.

O bloqueio P25 de `SoShadowGroup` ativo deve permanecer até que o preflight
consiga garantir que todos os passes e recursos exigidos são suportados antes
que qualquer frame seja publicado. A dependência imediata é a representação
de multipass e shadow map no plano comum; a capacidade de GPU sozinha não fecha
nenhum item.
