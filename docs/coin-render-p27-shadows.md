# P27 — sombras Coin: referência e implementação pendente

P27 exige executar `SoShadowGroup` ativo com a semântica Coin em BGFX e wgpu.
A referência GL, a captura comum e o bloqueio seguro estão verificados. Os
executores de sombras ainda não existem. Portanto P27 permanece **aberto**.

## O que o Coin/GL faz

`SoShadowGroup` usa um mapa por `SoSpotLight` ou `SoShadowDirectionalLight`
habilitada e Variance Shadow Maps (VSM); `SoDirectionalLight` comum e
`SoPointLight` não geram mapa.
Quando GL 2.0, framebuffer object ou textura float faltam, ele atravessa o
grupo sem sombras e emite aviso. Um grupo inativo também atravessa os filhos
normalmente. O caminho com sombras consulta a viabilidade de mapas RGBA16F na GL
(ou depth se o programa VSM não existir), calcula câmeras para luzes spot/directional e faz passes de
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
`600` (grupo desligado), `126` (sombra), `33` (luz após os objetos),
`600` (receiver desligado), `654`
(caster desligado), em soma RGB; a região central do cubo variou só `1`. Para
directional, a maior diferença de estilo foi `666`. O teste usa relações e
tolerâncias, sem gravar esses valores de um driver como golden universal.

```sh
env __GLX_VENDOR_LIBRARY_NAME=mesa COIN_GLXGLUE_NO_PBUFFERS=1 \
  COIN_GLX_PIXMAP_DIRECT_RENDERING=1 COIN_RENDER_REQUIRE_GL_REFERENCE=1 \
  xvfb-run -a -s '-screen 0 1280x1024x24 +extension GLX' \
  ctest --test-dir <build> -R '^CoinRenderShadowReferenceTest$' --output-on-failure
```

Sem `COIN_RENDER_REQUIRE_GL_REFERENCE`, o teste exerce a captura e o contrato
de falha do CoinRender em CPU: após um quadro válido, a mesma cena com grupo ativo retorna
`UNSUPPORTED`, preserva pixels e serial e aceita um quadro seguinte válido.
Ele confere grupos, luzes spot/directional, estilos efetivos, listas de
casters/receivers, vínculo da luz com o estado de iluminação (inclusive luz
posterior aos objetos), projeção direcional cobrindo a geometria e precedência de
`nearDistance`/`farDistance` para spot no plano comum.
O teste passou nas builds RECORDING e wgpu; a referência GL passou em Xvfb.
Isso verifica diagnóstico/publicação e o oráculo; não verifica sombras em GPU.

## Fechamentos incrementais

- [ ] **P27.1 — wgpu spot opaco:** uma luz spot, geometria opaca do grupo,
  `SoShadowStyle` nos casters/receivers, mapa de momentos com depth, lookup
  VSM aplicado somente à contribuição da luz e readback comparado à mesma
  fixture Coin/GL. Inclui resize, erro de recurso e preservação da publicação.
- [ ] **P27.2 — wgpu direcional e múltiplas luzes:** câmera direcional com
  interseção do frustum, `maxShadowDistance`, mapa por luz e soma correta das
  contribuições; comparar spot/directional e luz posterior à geometria ao GL.
- [ ] **P27.3 — BGFX:** executar o mesmo plano comum e as mesmas fixtures
  opacas no BGFX, com shader e recursos próprios, sem reinterpretar o Coin.
- [ ] **P27.4 — contrato ampliado:** cenas próprias por luz, transparência,
  clipping, qualidade, RTT, composição e alvos múltiplos nos dois executores.
- [ ] **P27.5 — qualificação final:** matriz de GPU/API/driver, perdas, resize,
  falhas e tolerâncias visuais; fechar P27 somente com BGFX e wgpu exercitados.

Cada subetapa exige um quadro renderizado e evidência de comportamento. A
existência de shader, captura ou plano isolados não fecha P27.1.

Dentro de P27.1, o pass de momentos spot foi exercitado isoladamente na GPU:
dois triângulos opacos sobrepostos foram renderizados em RGBA32F com depth,
lidos de volta e os momentos no pixel central conferidos com a distância
linear do caster frontal, mesmo com o traseiro submetido por último. Passou
em AMD Radeon Graphics (RADV RENOIR), Vulkan/radv, com
`COIN_RENDER_REQUIRE_WGPU_SHADOW_GPU=1 cargo test --manifest-path
src/rendering/coinwgpu/rust_bridge/Cargo.toml --offline --test shadow_map_gpu`.
O Core agora delimita esse primeiro perfil: um grupo, uma luz spot visível,
triângulos PHONG opacos sem textura, clipping ou névoa, com a luz antes dos
desenhos. A fixture opaca entra; transparência e luz posterior ficam fora.
Faltam o transporte dos casters do plano Coin, o lookup VSM na composição dos
receivers e a comparação do quadro final à fixture GL.

## Trabalho funcional para fechar

- [x] **Wiring inicial:** capturar grupo ativo, campos, `SoShadowStyle`, luzes
  spot/directional, elegibilidade Coin, matrizes e indicação de
  `shadowMapScene`; respeitar o escopo
  da travessia e os separadores sem chamar `GLRender`.
- [ ] **Wiring completo:** qualificar overrides, caminhos parciais, cenas próprias
  por luz e todos os modos de composição com fixtures Coin/GL.
- [x] **Core inicial:** gerar um pass por luz spot/directional habilitada,
  separar desenhos caster/receiver pelos bits de `SoShadowStyle`, dimensionar
  mapa por `precision`, calcular câmeras spot/directional a partir da geometria
  capturada, resolver os parâmetros de VSM/qualidade e o índice da luz em cada
  estado de desenho, resolver a contribuição dessa luz no espaço de vista
  inclusive quando ela aparece após a geometria, e limitar a memória planejada.
- [ ] **Core completo:** qualificar a interseção com o frustum principal e
  `maxShadowDistance`, qualidade, transparência, cenas próprias, múltiplas luzes
  e dependências RTT.
  Reusar ownership e publicação de P12–P14, inclusive múltiplos alvos.
- [ ] **Infra BGFX/wgpu:** mapas de momentos e depth, VSM, bias, textura,
  passes, sincronização, resize e reconstrução após perda, com shader específico
  por API. Mapas da Infra não entram no estado Coin. O shader wgpu que grava
  momentos lineares, spot/directional, já valida em Naga, mas ainda não está
  ligado ao encoder nem produz mapa.
- [ ] **Shell/capacidades:** seleção explícita de perfil implementado e
  disponível; diagnósticos de limite/formato sem fallback visual implícito.
- [ ] **Qualificação:** comparar spot/directional, todos os estilos, cenas
  próprias por luz, clipping, alpha/transparência, RTT, resize e falhas nas
  mesmas fixtures Coin/GL. Registrar GPU/API/driver e tolerâncias por célula.

O preflight ainda retorna `UNSUPPORTED` para qualquer grupo ativo depois de
construir e validar o plano comum, antes de submeter o quadro. Assim os pixels
e o serial publicados anteriormente continuam intactos. A próxima entrega
precisa materializar os passes e mapas em BGFX e wgpu; somente então o bloqueio
poderá ser retirado após comparação visual com o oráculo GL.
