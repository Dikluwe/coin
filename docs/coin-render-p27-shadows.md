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
fornecer `shadowMapScene` próprio. A política de qualidade muda a iluminação das luzes de mapa (spot e
shadow directional) para por fragmento acima de 0,3; o limiar 0,7 se aplica
às demais luzes da cena.
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
- [ ] **P27.4 — contrato ampliado:** cinco a oito luzes, cenas próprias
  complexas por luz, transparência, demais níveis de qualidade, RTT direct,
  composição e grupos aninhados. Três/quatro luzes opacas, clipping, alvos
  múltiplos, dois grupos irmãos, cena própria como shape direto, qualidade
  direcional plana e RTT staged já têm perfis qualificados nos dois executores.
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
  Coin/GL: deltas máximos 666/723 (terceira) e 228/228 (quarta), GL/BGFX.
  O shader de vértice exclui as quatro luzes de mapa da iluminação base antes
  de o fragmento somar cada contribuição com VSM; a terceira e a quarta
  deixaram de ser contadas duas vezes.
  O perfil anterior de até quatro passes permanece qualificado em
  Vulkan/NVIDIA e GL/Mesa; o lote adicional direcional está descrito abaixo.
- [x] **wgpu, três/quatro luzes opacas:** a ABI privada 38 conserva os
  quatro passes iniciais, o encoder escreve mapas independentes e o shader
  aplica VSM por contribuição da luz resolvida no Core. A fixture submeteu
  três e quatro luzes com readback: deltas máximos 666/723 e 228/228,
  Coin/GL e wgpu. O segundo lote direcional está descrito abaixo.
- [ ] **Cinco a oito luzes:** o Core produz e valida planos de cinco e
  oito passes no contrato de iluminação capturada. BGFX compõe os passes
  cinco a oito em um segundo draw opaco, reutilizando quatro samplers e o
  depth buffer; o mesmo mecanismo funciona no produtor RTT direto e no
  staged. Com as luzes adicionais direcionais, a fixture comparou os deltas
  de cinco luzes (291/291) e oito (117/174), Coin/GL/BGFX. O produtor RTT
  com oito luzes marcou 228/369 nos modos staged e direct. Uma quinta luz
  spot revelou divergência VSM: a referência muda 162, mas o BGFX não muda
  a imagem. wgpu também compõe os passes cinco a oito num segundo draw,
  reutilizando quatro bindings de mapa via ABI 38. As comparações Coin/GL e
  wgpu mediram 291/291 (quinta) e 117/174 (oitava); staged e direct RTT de
  oito luzes mediram 228/369, como no BGFX. A textura opaca MODULATE no
  receiver também passou com a quinta luz: a contribuição isolada ao
  comparar quatro contra cinco luzes foi 81/79 em Coin/GL e em cada
  executor. Modos de textura não lineares e `SoTextureCombine` permanecem
  fora do segundo lote; o Core os rejeita antes da submissão, preservando
  pixels e serial. A quinta luz spot divergiu também
  no wgpu (162/0 após corrigir a matriz de recepção do lote) e ambos os
  executores a rejeitam antes de publicar pixels ou serial. A referência
  Coin/GL foi reavaliada em cópias novas da cena: a quinta spot mudou 162
  com sombra ativa; fixar sua intensidade em zero anulou esse delta; retirar
  os casters do mapa mudou a imagem em 369. Sem sombra, as quatro luzes
  anteriores saturavam os pixels (765/765), explicando o delta zero da
  quinta; ao baixar suas intensidades, a spot mudou a imagem em 471. Isso
  afasta um falso positivo simples da referência.
  Uma sonda temporária liberou a quinta spot apenas para diagnóstico:
  BGFX e wgpu mediram delta 0 como quinta luz e 99 quando a mesma spot
  ocupou o quarto passe. O Core manteve câmera, projeção, near/far
  (3,34088/8,34746), mapa 1024² e o mesmo caster selecionado nas duas
  posições. Em wgpu, forçar visibilidade 1 no lookup do lote adicional
  produziu delta 579; usar o primeiro momento do mapa produziu 198,
  usar `1 - distância normalizada` produziu 129, e a probabilidade VSM
  antes do limiar ainda produziu delta zero. Isso localiza a perda na
  avaliação de visibilidade do segundo lote ou nos dados que chegam a ela,
  sem demonstrar ainda qual coordenada/momento diverge. As sondas foram
  retiradas; a rejeição atômica da spot tardia permanece ativa. O Coin/GL
  seleciona a quantidade de mapas pelas unidades de textura disponíveis
  menos as usadas pela cena (`SoShadowGroupP::updateShadowLights`); seu
  limite não é fixo em quatro. Uma falha de alocação de oito mapas no wgpu
  preservou a imagem e o serial do alvo. Faltam spots tardios e outros
  perfis de luzes.
- [ ] **Cenas próprias por luz:** estão qualificados dois recortes de
  `shadowMapScene`: um `SoShape` filho direto do grupo, desenhado uma só vez
  com modelo identidade e estilo caster; e um `SoSeparator` filho direto,
  após apenas luzes no grupo, com modelo de entrada capturado e descendentes
  formados só por separadores, translações, rotações, escalas,
  `SoTransform` e formas caster. Wiring identifica
  as formas da subárvore; Core exige um draw por forma e escolhe somente esses
  casters, mantendo a bbox do grupo para a câmera do mapa, como no Coin/GL.
  A fixture da subárvore conta duas formas em separadores aninhados, verifica
  a seleção de ambas e a matriz capturada com rotação e escala. Ao trocar
  cena própria por grupo inteiro, mediu diferenças máximas 420/576 para a
  forma direta e 426/531 para a subárvore transformada, em Coin/GL e BGFX
  ou wgpu (tolerância 180). Uma subárvore com `SoShadowStyle` interno continua
  `UNSUPPORTED` sem publicar pixels nem serial. A mesma subárvore com
  `SoTranslation` herdada antes do grupo passou em ambos os executores:
  deltas 441/531, Coin/GL e GPU (tolerância 180). Faltam os demais nós e
  estados de cena própria e subárvores que precisem de travessia separada.
- [x] **Clipping de casters opacos:** os planos já capturados e resolvidos
  pelo Core entram também no pass de momentos; BGFX e wgpu descartam o
  fragmento no mapa antes da recepção. A fixture com `SoClipPlane` no caster
  mediu diferença máxima 496/663 em Coin/GL e nos dois executores (tolerância
  200 nesta cena). Um readback direto do mapa wgpu confirmou branco no lado
  descartado e momentos no lado mantido.
- [ ] **Transparência e qualidade:** qualificar por modo os casters e
  receivers transparentes e os demais perfis de qualidade. O subperfil
  direcional opaco difuso com normais planas em `quality=0,2` está qualificado
  nos dois executores; o Core rejeita especular e normais interpoladas.
  O Coin/GL precisou corrigir a geração do shader para esse perfil: a luz
  direcional no caminho por vértice era tratada como spot, e a função
  `DirectionalLight` não era registrada no fragmento para `quality=0,5`.
  A fixture de qualidade baixa mediu delta máximo de recepção 735 no GL e
  666 em BGFX e wgpu, com tolerância 120.
- [x] **Receiver com textura estática opaca:** a textura primária é aceita
  quando seus texels RGBA capturados têm alfa 255 e não há unidades
  adicionais. O mesmo draw recebe sombra e modulação de textura nos
  dois executores; alternar a textura verde da fixture mudou a imagem em 485
  no Coin/GL, BGFX e wgpu. Uma imagem com alfa 128 retorna `UNSUPPORTED`
  preservando pixels e serial. Um `SoSceneTexture2` também pode modular o
  receiver se a captura registrar fundo opaco e `transparencyFunction=NONE`;
  a Action valida o perfil no plano lógico antes de executar o produtor.
  A fixture com luz spot, `quality=0,5` e sombra no grupo consumidor
  passou em staged e direct: deltas máximos 474/291 no Coin/GL e em BGFX
  ou wgpu (tolerância 200).
  `ALPHA_BLEND` e fundo não opaco foram rejeitados antes de mudar pixels
  ou serial. Alfa variável continua aberto.
- [x] **Luz direcional comum herdada:** uma `SoDirectionalLight` anterior ao
  `SoShadowGroup` compõe sua contribuição opaca com um mapa spot; Core mantém
  a identidade da luz sem criar pass de sombra para ela. O recorte exige luz
  presente antes de todos os desenhos, material difuso sem especular e normais
  planas. Ligar essa luz mudou a cena em 152 no Coin/GL, BGFX e wgpu. A luz
  comum inserida dentro do grupo continua `UNSUPPORTED`: nessa fixture o
  Coin/GL não alterou pixels, enquanto o caminho de iluminação comum dos
  executores a somaria. Uma `SoPointLight` herdada com localização (0,2,4)
  também compôs com o mapa spot: ligá-la mudou a cena em 594 no Coin/GL e
  603 em BGFX e wgpu (tolerância 180). Outras atenuações e combinações
  permanecem abertas.
- [x] **Múltiplos alvos offscreen:** dois alvos simultâneos de 128×128 e
  160×160 executam sombra ativa em BGFX e wgpu; intercalar submissões não
  altera os pixels do primeiro alvo e cada serial avança independentemente.
- [x] **Dois grupos irmãos ativos e opacos:** o Core associa cada pass ao
  `SoShadowGroup` capturado e só atribui a contribuição da luz aos draws
  daquele grupo. BGFX e wgpu renderizaram dois grupos separados, com o estilo
  de recepção do segundo alternado: delta máximo 483 no Coin/GL e 588 em
  ambos os executores; os pixels do primeiro grupo permaneceram idênticos.
  `epsilon` e `threshold` agora são resolvidos por pass no Core e
  transportados separadamente em BGFX e na ABI wgpu 38. Alternar apenas o
  segundo grupo para `epsilon=0,00002` e `threshold=0,12` mudou a região
  desse grupo em 138 no Coin/GL, 6 no BGFX e 12 no wgpu (tolerância 150);
  a região do primeiro grupo manteve pixels idênticos. Essa diferença de
  magnitude permanece registrada para uma calibração visual posterior.
  Um grupo ativo ao lado de um inativo agora também executa como composição
  opaca: o Core mantém os desenhos do irmão inativo na iluminação Coin comum,
  sem atribuir-lhes pass de sombra. Alternar o segundo grupo para inativo
  marcou delta máximo 483 no Coin/GL e 588 em BGFX e wgpu; os pixels do
  primeiro grupo permaneceram idênticos. A antiga sondagem com chão de 2,2
  (18 no GL, 654 nos executores) continua fora deste perfil visual.
- [ ] **RTT, composição e demais grupos:** o produtor `SoSceneTexture2`
  em modo staged pode conter um grupo opaco do perfil qualificado. A Action
  captura seu plano sem alvo e o executor RTT existente renderiza o produtor
  num alvo offscreen antes do consumidor texturizado. A fixture 128×128,
  alternando sombra no produtor, mediu delta máximo 306 no Coin/GL e 414 em
  BGFX e wgpu (tolerância 180; a execução atual mede 405). Falha injetada na alocação do mapa do
  produtor preservou pixels e serial do consumidor; a recuperação reproduziu
  o quadro, e o resize do consumidor para 160×160 manteve a diferença de
  sombra. O mesmo produtor opaco também foi qualificado em modo direct:
  a Action captura o plano, BGFX e wgpu executam o mapa antes do framebuffer
  de textura e o consumidor recebe o token GPU. Alternar a sombra produziu
  delta máximo 306 no Coin/GL e 405 em BGFX e wgpu (tolerância 180);
  falha injetada no mapa preservou pixels e serial, e a recuperação reproduziu
  o quadro. Redimensionar o consumidor direto para 160×160 manteve o efeito
  (delta GPU 405 em BGFX; ambos executores passaram). O consumidor com
  sombra opaca e textura RTT também passou em staged e direct, como descrito
  acima. Dois grupos irmãos dentro do produtor RTT mantiveram `epsilon`
  e `threshold` distintos nos modos staged e direct: alterar apenas o segundo
  deixou o primeiro inalterado; deltas máximos 102/3 no Coin/GL e BGFX
  (tolerância 150; wgpu também passou). A diferença de magnitude exige
  calibração visual. Camadas e grupos aninhados ainda não estão qualificados.

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
- [ ] **Core completo:** ampliar qualidade, transparência, cenas próprias
  complexas, cinco a oito luzes, grupos aninhados e dependências RTT direct.
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
