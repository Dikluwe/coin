# P27 — sombras Coin: perfil spot wgpu implementado

P27 exige executar `SoShadowGroup` ativo com a semântica Coin em BGFX e wgpu.
A referência GL e a captura comum estão verificadas. Os perfis de uma luz spot ou direcional opaca antes dos desenhos executam
em wgpu offscreen; os demais perfis e BGFX permanecem bloqueados.
Portanto P27 permanece **aberto**.

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
Com `COIN_RENDER_REQUIRE_WGPU_SHADOW_GPU=1`, também executa a fixture spot
opaca no wgpu e compara seu readback ao GL.

## Fechamentos incrementais

- [x] **P27.1 — wgpu spot opaco:** uma luz spot, geometria opaca do grupo,
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

Cada subetapa exige um quadro renderizado e evidência de comportamento;
shader, captura ou plano isolados não bastam para fechá-la.

P27.1 foi exercitado na GPU AMD Radeon Graphics (RADV RENOIR), Vulkan/radv.
O Core limita a execução a um grupo, uma luz spot visível, triângulos PHONG
opacos sem textura, clipping ou névoa, com a luz antes dos desenhos. O
empacotador C++ transporta casters e receivers escolhidos pelo Core e a ABI
privada C++/Rust 33 carrega índices, matrizes, tipo de distância e parâmetros
VSM. O encoder
wgpu grava momentos RGBA32F com depth e o shader principal multiplica somente
a contribuição da luz selecionada pelo resultado do lookup. `SoShadowStyle`
controla recepção e participação no mapa.

Na fixture 128×128, a amostra alinhada pela orientação vertical dos readbacks
passou de `600` sem sombras para `30` com sombras no wgpu; o GL passou de
`600` para `126`. O teste exige queda de pelo menos `100` e diferença máxima
`130` entre os pixels sombreados, além de conferir o mesmo resultado pela
Action e por submissão direta. Exercita desligar/restaurar a recepção,
redimensionar para 160×160, rejeitar perfil sem caster preservando pixels e
serial, recuperar a renderização e injetar falha na alocação do mapa antes da
submissão, também preservando pixels e serial. O pass isolado de momentos
continua validado por readback com dois triângulos sobrepostos. Reproduzir:

```sh
COIN_RENDER_REQUIRE_WGPU_SHADOW_GPU=1 \
COIN_RENDER_REQUIRE_GL_REFERENCE=1 \
__GLX_VENDOR_LIBRARY_NAME=mesa COIN_GLXGLUE_NO_PBUFFERS=1 \
COIN_GLX_PIXMAP_DIRECT_RENDERING=1 \
xvfb-run -a -s '-screen 0 1280x1024x24 +extension GLX' \
  ctest --test-dir <build-wgpu> -R '^CoinRenderShadowReferenceTest$' --output-on-failure
```

A tolerância visual é da fixture, não uma promessa de equivalência pixel a
pixel em todas as GPUs.

P27.2 está em andamento. O mesmo perfil opaco de uma luz agora admite luz
direcional anterior aos desenhos, com distância axial no pass de momentos e no
lookup VSM. Na fixture Coin capturada com qualidade 1, a diferença espacial
máxima entre wgpu sem/com sombra foi `666`, igual à diferença de estilo da
referência GL. A saída direta do backend e a da Action foram idênticas. A luz
direcional posterior aos desenhos, `maxShadowDistance`, interseção precisa do
frustum e múltiplas luzes ainda não executam. A Action só admite os dois perfis
opacos de uma luz em alvo wgpu offscreen síncrono sem RTT; BGFX, janela, async,
transparência e cenas próprias por luz seguem com `UNSUPPORTED` antes da
submissão.

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
  momentos lineares, spot/directional, e o lookup no shader principal validam
  em Naga. Os perfis opacos de uma luz spot ou direcional anterior aos desenhos
  funcionam no quadro completo; os demais perfis e o executor BGFX continuam
  pendentes.
- [ ] **Shell/capacidades:** seleção explícita de perfil implementado e
  disponível; diagnósticos de limite/formato sem fallback visual implícito.
- [ ] **Qualificação:** comparar spot/directional, todos os estilos, cenas
  próprias por luz, clipping, alpha/transparência, RTT, resize e falhas nas
  mesmas fixtures Coin/GL. Registrar GPU/API/driver e tolerâncias por célula.

O preflight retorna `UNSUPPORTED` para grupos ativos fora dos perfis opacos
spot/direcional de uma luz no wgpu offscreen, antes de submeter o quadro. Assim
os pixels e o serial publicados anteriormente continuam intactos. O restante
de P27.2 exige luz posterior, distância máxima, interseção do frustum e
múltiplas luzes wgpu, comparadas à referência Coin/GL.
