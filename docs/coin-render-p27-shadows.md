# P27 — sombras Coin: perfis opacos wgpu em andamento

P27 exige executar `SoShadowGroup` ativo com a semântica Coin em BGFX e wgpu.
A referência GL e a captura comum estão verificadas. Os perfis opacos de
uma luz spot ou direcional e de até duas luzes spot/direcionais executam em
wgpu offscreen. O perfil de duas luzes cobre ordem de travessia anterior,
mista e posterior; o Core usa a câmera na entrada do grupo para os mapas
direcionais. Os demais perfis e BGFX permanecem bloqueados.
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
- [x] **P27.2 — wgpu direcional e duas luzes opacas:** câmera direcional
  com recorte do frustum e `maxShadowDistance`, inclusive entrada perspectiva
  e câmeras diferentes dentro do grupo. Dois passes independentes aceitam
  spot+spot, spot+direcional e direcional+direcional. Ambas as ordens do par
  misto foram exercitadas antes, entre e após os desenhos, com readback
  comparado à referência Coin/GL. Resize, falha de mapa, publicação atômica,
  rejeição de três luzes e recuperação também passaram.
- [ ] **P27.3 — BGFX:** executar o mesmo plano comum e as mesmas fixtures
  opacas no BGFX, com shader e recursos próprios, sem reinterpretar o Coin.
- [ ] **P27.4 — contrato ampliado:** três ou mais luzes, cenas próprias por
  luz, transparência, clipping, qualidade, RTT, composição, grupos adicionais
  e alvos múltiplos nos dois executores.
- [ ] **P27.5 — qualificação final:** matriz de GPU/API/driver, perdas, resize,
  falhas e tolerâncias visuais; fechar P27 somente com BGFX e wgpu exercitados.

Cada subetapa exige um quadro renderizado e evidência de comportamento;
shader, captura ou plano isolados não bastam para fechá-la.

P27.1 foi exercitado na GPU AMD Radeon Graphics (RADV RENOIR), Vulkan/radv.
No fechamento P27.1, o Core limitava a execução a um grupo, uma luz spot visível, triângulos PHONG
opacos sem textura, clipping ou névoa, com a luz antes dos desenhos. O
empacotador C++ transporta casters e receivers escolhidos pelo Core e a ABI
privada C++/Rust, agora 34, carrega índices, matrizes, tipo de distância e parâmetros
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

P27.2 está fechado no perfil opaco de até dois passes. Uma luz admite spot ou direcional
antes, entre ou depois dos desenhos, com distância axial no pass direcional
e lookup VSM. Na fixture direcional anterior, a diferença
espacial máxima entre wgpu sem/com sombra foi `666`, igual à diferença de estilo
Coin/GL. A saída direta do backend e a da Action foram idênticas. Para luzes
posteriores, o Core resolve a fonte em cada estado: ela não consta da iluminação
comum capturada antes da sua travessia, mas o shader de `SoShadowGroup` a aplica
nos receivers. O empacotador wgpu adiciona essa fonte somente à contribuição
sombreada. Na fixture, a diferença espacial de estilo da spot tardia foi `735`
no wgpu e no GL; a direcional tardia marcou `666` e `735`, respectivamente.
Na ordem mista (luz entre cubo e plano), o Core marca o cubo antes e o
plano depois da luz; os máximos de diferença de estilo foram `474`/`588`
(spot GL/wgpu) e `666`/`666` (direcional GL/wgpu). A comparação tardia
inclui o pixel de maior diferença da spot: soma RGB `765` no GL e `603`
no wgpu, com tolerância de `180` nesta fixture. Essa diferença mantém a
equivalência visual estreita como trabalho aberto. Para uma câmera comum aos
desenhos, o Core reconstrói o frustum capturado, usa a interseção Coin com a
bbox do grupo para ajustar o mapa direcional e leva `maxShadowDistance` ao
shader wgpu, que atenua a sombra pela distância em espaço de vista. Com limite
10 na fixture, o maior clareamento foi `666` no GL e `585` no wgpu. A
referência GL usa um grupo novo porque o shader do grupo já renderizado não
regenerou o ramo do limite quando o campo mudou de negativo para positivo.
Limite anterior ao plano próximo retorna `UNSUPPORTED` no perfil atual e
preserva pixels e serial. Nessa etapa, a ABI privada C++/Rust passou a 34.
A câmera do mapa direcional é capturada na entrada do grupo, como no GL;
câmeras posteriores continuam próprias de cada desenho. A Action admite uma
ou duas luzes spot/direcionais em alvo wgpu offscreen síncrono sem RTT. BGFX,
janela, async, transparência e cenas próprias por luz seguem com
`UNSUPPORTED` antes da submissão.

O perfil com uma spot e uma direcional antes dos desenhos, ambas com qualidade
1, gera dois passes independentes no Core vinculados aos índices 0 e 1 da
iluminação capturada. O wgpu codifica dois mapas de momentos e depth no mesmo
command buffer, liga ambos ao shader e soma as contribuições VSM das duas
luzes. Na fixture 128×128, retirar a direcional mudou a soma RGB máxima em
`402` no Coin/GL e `399` no wgpu (tolerância da fixture: `80`). O teste também
confere injeção de falha na alocação, preservação de pixels e serial, resize
para 160×160 e recuperação do quadro original. A ABI privada C++/Rust passou
a 35. O Core rejeita um índice cujo `sourceRevision` não corresponde à luz do
pass. O empacotador reserva índices por estado para luzes vistas depois dos
receivers. Com a direcional entre cubo e plano, as diferenças máximas ao
retirá-la foram `402` no GL e `399` no wgpu; com a direcional após ambos,
`459` e `399`; com ambas as luzes após os desenhos, `456` e `399`.
A tolerância de `100` nesta fixture cobre esses máximos e não é uma
qualificação pixel a pixel. Na ordem direcional→spot, as diferenças GL/wgpu
foram `426`/`336` com a spot antes ou entre os desenhos e `336`/`336`
com a spot posterior. Dois spots marcaram `435`/`438`; duas direcionais,
com `maxShadowDistance` na segunda, `402`/`399`. Com duas câmeras dentro do
grupo e limite direcional ativo, a diferença de estilo foi `81` em ambos os
renderizadores; a câmera perspectiva na entrada também marcou `81`/`81`.
O mapa usa o frustum da entrada do `SoShadowGroup`, preservando as câmeras
distintas dos desenhos. O teste rejeita uma terceira luz sem alterar pixels
ou serial, recupera o quadro de duas luzes e testa falha de alocação e resize.

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
  inclusive quando ela aparece após a geometria, validar dois passes opacos
  spot/direcional e limitar a memória planejada.
- [ ] **Core completo:** ampliar qualidade, transparência, cenas próprias,
  três ou mais luzes, grupos adicionais e dependências RTT.
  Reusar ownership e publicação de P12–P14, inclusive múltiplos alvos.
- [ ] **Infra BGFX/wgpu:** mapas de momentos e depth, VSM, bias, textura,
  passes, sincronização, resize e reconstrução após perda, com shader específico
  por API. Mapas da Infra não entram no estado Coin. O shader wgpu que grava
  momentos lineares, spot/directional, e o lookup no shader principal validam
  em Naga. Os perfis opacos de uma luz spot ou direcional anterior, mista ou
  posterior aos desenhos, e dois passes spot/direcionais no perfil opaco
  qualificado funcionam no quadro completo; os demais perfis e o executor
  BGFX continuam pendentes.
- [ ] **Shell/capacidades:** seleção explícita de perfil implementado e
  disponível; diagnósticos de limite/formato sem fallback visual implícito.
- [ ] **Qualificação:** comparar spot/directional, todos os estilos, cenas
  próprias por luz, clipping, alpha/transparência, RTT, resize e falhas nas
  mesmas fixtures Coin/GL. Registrar GPU/API/driver e tolerâncias por célula.

O preflight retorna `UNSUPPORTED` para grupos ativos fora dos perfis opacos
de uma ou duas luzes spot/direcionais no wgpu offscreen síncrono, antes de
submeter o quadro. Assim os pixels e o serial publicados anteriormente
continuam intactos. Três ou mais luzes e os demais perfis pertencem à
ampliação P27.4; P27.3 ainda exige execução BGFX do plano comum.
