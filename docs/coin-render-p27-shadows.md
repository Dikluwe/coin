# P27 — sombras Coin: perfis opacos BGFX e wgpu executáveis

P27 exige executar `SoShadowGroup` ativo com a semântica Coin em BGFX e wgpu.
A referência GL e a captura comum estão verificadas. Os perfis opacos de
uma ou duas luzes spot/direcionais executam em BGFX e wgpu offscreen síncrono.
O perfil de duas luzes cobre ordem de travessia anterior, mista e posterior;
o Core usa a câmera na entrada do grupo para os mapas direcionais. Os
perfis ampliados e a matriz final de plataformas permanecem abertos em
P27.4–P27.5; P27 permanece **aberto**.

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
- [x] **P27.3 — BGFX:** o plano comum executa uma ou duas luzes spot/direcionais
  em BGFX offscreen síncrono, com mapas RGBA32F/D32F, shaders de momentos e
  receiver próprios, VSM e soma por contribuição de luz. As fixtures de
  estilos, pares e ordens de travessia foram comparadas por readback com
  Coin/GL. Resize, falha de mapa, rejeição de terceira luz e publicação
  atômica passaram. A semântica Coin permanece no Core.
- [ ] **P27.4 — contrato ampliado:** três ou mais luzes, cenas próprias por
  luz, transparência, clipping, qualidade, RTT, composição, grupos adicionais
  e alvos múltiplos nos dois executores. Os perfis de três/quatro luzes
  opacas estão qualificados nos dois; cinco ou mais e os demais casos
  continuam abertos.
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


P27.3 foi exercitado no BGFX/Vulkan sobre NVIDIA GeForce RTX 3060 Laptop GPU
(`vendor_id=0x10de`, `device_id=0x2560`, driver NVIDIA 610.57.04); o oráculo
Coin/GL usou Mesa GLX em Xvfb. O executor reserva dois views para os mapas
antes do quadro principal, codifica apenas os casters indicados pelo Core e
combina cada contribuição PHONG com o resultado VSM de seu próprio mapa.
A direção vertical do lookup foi conferida com o readback, para não espelhar
a sombra no plano. Na amostra spot alinhada, BGFX marcou `30` e Coin/GL
`126` em soma RGB (tolerância `130`). A contribuição máxima da segunda luz
no par spot→direcional foi `402` em ambos. Na ordem direcional→spot, as
relações ficaram `426`/`336` (GL/BGFX) para spot anterior ou mista e
`336`/`336` para spot posterior (tolerância `100`); dois spots marcaram
`435`/`438` e duas direcionais `402`/`402` (tolerância `120`). As variações
remanescentes de pixels e outras GPUs/APIs pertencem à matriz P27.5.
A Action e a submissão direta produziram o mesmo quadro. A falha injetada
antes da alocação preservou pixels e serial; o alvo entra em estado de erro
por `OUT_OF_MEMORY`, enquanto outro alvo BGFX continua executando. O teste
BGFX offscreen também verifica que perda do runtime compartilhado aposenta
os handles dos alvos pares antes da recuperação.

Reproduzir no build BGFX com GPU e referência GL:

```sh
env __GLX_VENDOR_LIBRARY_NAME=mesa COIN_GLXGLUE_NO_PBUFFERS=1 \
  COIN_GLX_PIXMAP_DIRECT_RENDERING=1 COIN_RENDER_REQUIRE_GL_REFERENCE=1 \
  COIN_RENDER_REQUIRE_BGFX_SHADOW_GPU=1 \
  xvfb-run -a -s '-screen 0 1280x1024x24 +extension GLX' \
  ctest --test-dir <build-bgfx> -R '^CoinRenderShadowReferenceTest$' --output-on-failure
```

## P27.4 — acompanhamento do contrato ampliado

- [x] **BGFX, três/quatro luzes opacas:** o Core aceita até quatro passes
  independentes para um grupo. O BGFX usa uma variante do shader de receiver
  para quatro mapas; a variante anterior continua disponível para uma/duas
  luzes e dispositivos com doze unidades de textura. A fixture submeteu três
  e quatro luzes com readback e comparou a contribuição incremental com
  Coin/GL: deltas máximos 666/735 (terceira) e 228/195 (quarta), GL/BGFX.
  A quinta luz foi rejeitada antes da publicação e o quadro de duas luzes
  recuperado. A qualificação desta célula usa Vulkan/NVIDIA e GL/Mesa.
- [x] **wgpu, três/quatro luzes opacas:** a ABI privada 36 carrega até
  quatro passes, o encoder escreve mapas independentes e o shader aplica
  VSM por contribuição da luz resolvida no Core. A fixture submeteu três e
  quatro luzes com readback: deltas máximos 666/723 e 228/228, Coin/GL e
  wgpu. A quinta luz foi rejeitada antes de alterar pixels/serial, seguida
  por recuperação do quadro de duas luzes. Os 17 testes Rust passaram.
- [ ] **Cinco a oito luzes:** compor contribuições em mais de um passo sem
  exceder as unidades de textura nem repetir semântica Coin no backend.
- [ ] **Cenas próprias por luz:** capturar `shadowMapScene` como geometria de
  caster separada, respeitando a travessia e o bbox do grupo no Coin/GL.
- [x] **Clipping de casters opacos:** os planos já capturados e resolvidos
  pelo Core entram também no pass de momentos; BGFX e wgpu descartam o
  fragmento no mapa antes da recepção. A fixture com `SoClipPlane` no caster
  mediu diferença máxima 496/663 em Coin/GL e nos dois executores (tolerância
  200 nesta cena). Um readback direto do mapa wgpu confirmou branco no lado
  descartado e momentos no lado mantido.
- [ ] **Transparência e qualidade:** qualificar por modo os casters,
  receivers e o limiar de iluminação por fragmento.
- [ ] **RTT, composição, grupos adicionais e múltiplos alvos:** ordenar os
  passes com as dependências de textura e camadas, sem fundir grupos ou
  estados Coin independentes; validar resize, falha e recuperação.

Cada caixa acima requer uma fixture renderizada nos dois executores e sua
referência Coin/GL antes de marcar P27.4 concluído.

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
  em Naga. BGFX usa shaders BGFX separados e recursos próprios, mas os dois
  executores recebem os mesmos passes opacos qualificados do Core. Mapas e
  lookup de perfis ampliados continuam em P27.4.
- [ ] **Shell/capacidades:** seleção explícita de perfil implementado e
  disponível; diagnósticos de limite/formato sem fallback visual implícito.
- [ ] **Qualificação:** comparar spot/directional, todos os estilos, cenas
  próprias por luz, alpha/transparência, RTT, resize e falhas nas
  mesmas fixtures Coin/GL. Registrar GPU/API/driver e tolerâncias por célula.

O preflight retorna `UNSUPPORTED` para grupos ativos fora dos perfis opacos
de uma a quatro luzes spot/direcionais em BGFX ou wgpu offscreen síncrono,
antes de submeter o quadro. Assim os pixels e o serial publicados
anteriormente continuam intactos. Cinco ou mais luzes e os demais perfis
pertencem à ampliação P27.4.
