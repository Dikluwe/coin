# P27 — sombras Coin: perfis BGFX e wgpu qualificados

P27 exige executar `SoShadowGroup` ativo com a semântica Coin em BGFX e wgpu.
A referência GL e a captura comum estão verificadas nos perfis descritos abaixo:
até oito mapas, composição, cenas próprias, transparência, alfa RTT,
peeling/OIT, qualidade ampliada, overrides e caminhos parciais.
A [qualificação Linux](coin-render-p27-linux-validation.md) registra GPUs,
APIs, comandos e resultados. A oitava sombra usa um oráculo equivalente de
sete mapas; a referência GL nativa com oito e as células externas de P27.5
permanecem abertas. P27 permanece **aberto** por esses critérios explícitos.

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
`600` (grupo desligado), `126` (sombra), `30` (luz após os objetos),
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
- [x] **P27.4 — perfil funcional local:** cenas próprias por luz, composição,
  transparência, alfa RTT, peeling/OIT, qualidade, overrides e caminhos parciais
  qualificados em BGFX/wgpu dentro dos limites de cada fixture.
- [ ] **P27.4 — referência nativa da oitava sombra:** exige Coin/GL com nove
  unidades utilizáveis; este host oferece oito. O teste recusa qualificação
  nativa quando essa capacidade falta.
- [x] **P27.5 — células Linux disponíveis:** AMD/NVIDIA BGFX Vulkan e OpenGL,
  AMD/wgpu Vulkan e NVIDIA/wgpu Vulkan; readback,
  resize, falhas injetadas e tolerâncias passam nas mesmas fixtures.
- [x] **P27.5 — Windows/NVIDIA:** GTX 1060, wgpu D3D12 e Vulkan,
  39/39 fixtures de sombras por API com Coin/WGL obrigatório;
  [campanha Windows e seus limites](coin-render-p21-windows-validation.md).
- [ ] **P27.5 — células restantes:** GPU Intel e macOS/Metal.
  NVIDIA/OpenGL PRIME, inclusive o oráculo Coin/GL offscreen, está fechado. Ver arquivo de plataformas pendentes.

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
Coin/GL. A saída direta do backend e a da Action foram idênticas. Para
luzes posteriores, o Core mantém a fonte ausente do estado de cada
desenho anterior à travessia da luz. O Coin/GL agora limpa o slot GL da luz
de sombra até que seu nó seja atravessado; isso evita contribuição de estado
GL residual de quadros anteriores. Com a luz após todos os desenhos, a
diferença ao retirá-la é zero em GL, BGFX e wgpu. Na ordem mista, a luz
participa somente dos desenhos posteriores.
Para uma câmera comum aos
desenhos, o Core reconstrói o frustum capturado, usa a interseção Coin com a
bbox do grupo para ajustar o mapa direcional e leva `maxShadowDistance` ao
shader wgpu, que atenua a sombra pela distância em espaço de vista. Com limite
10 na fixture, o maior clareamento foi `666` no GL e `585` no wgpu. A
referência GL usa um grupo novo porque o shader do grupo já renderizado não
regenerou o ramo do limite quando o campo mudou de negativo para positivo.
Limite anterior ao plano próximo retorna `UNSUPPORTED` no perfil atual e
preserva pixels e serial. Nessa etapa, a ABI privada C++/Rust passou a 34.
A câmera do mapa direcional é capturada na entrada do grupo, como no GL;
câmeras posteriores continuam próprias de cada desenho. Na etapa P27.2, a Action admitia uma ou duas luzes spot/direcionais em alvo
wgpu offscreen síncrono sem RTT; os perfis posteriores estão registrados
abaixo.

O perfil com uma spot e uma direcional antes dos desenhos, ambas com qualidade
1, gera dois passes independentes no Core vinculados aos índices 0 e 1 da
iluminação capturada. O wgpu codifica dois mapas de momentos e depth no mesmo
command buffer, liga ambos ao shader e soma as contribuições VSM das duas
luzes. Na fixture 128×128, retirar a direcional mudou a soma RGB máxima em
`402` no Coin/GL e `399` no wgpu (tolerância da fixture: `80`). O teste também
confere injeção de falha na alocação, preservação de pixels e serial, resize
para 160×160 e recuperação do quadro original. A ABI privada C++/Rust passou
a 35. O Core rejeita um índice cujo `sourceRevision` não corresponde à luz do
pass. O índice de iluminação vem do estado capturado de cada desenho: com a
direcional entre cubo e plano, a diferença máxima ao retirá-la foi `402`
em GL e wgpu; com ela após ambos, `0` em ambos. A ordem inversa
(direcional→spot) marcou `426`/`429` para a spot anterior ou mista e `0`/`0`
para a spot posterior. Dois spots marcaram `435`/`435`; duas direcionais,
com `maxShadowDistance` na segunda, `402`/`399`.
Com duas câmeras dentro do
grupo e limite direcional ativo, a diferença de estilo foi `81` em ambos os
renderizadores; a câmera perspectiva na entrada também marcou `81`/`81`.
O mapa usa o frustum da entrada do `SoShadowGroup`, preservando as câmeras
distintas dos desenhos. Na etapa P27.2, o teste rejeitava uma terceira luz sem alterar pixels
ou serial, recuperava o quadro de duas luzes e testava falha de alocação e
resize; a terceira luz foi qualificada depois, em P27.4.


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
`0`/`0` para spot posterior (tolerância `100`); dois spots marcaram
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
- [x] **Cinco a oito luzes nos backends:** Core, BGFX e wgpu aceitam até oito passes
  opacos. Os passes cinco a oito usam um segundo draw do receiver com os
  mesmos quatro bindings de mapa e o mesmo depth; staged e direct RTT foram
  exercitados. As fixtures mediram, em Coin/GL e nos dois backends, deltas
  máximos de 291/291 para a quinta luz direcional, 117/174 para a oitava e
  162/159 para a quinta spot e 177/177 (wgpu), 177/174 (BGFX) para a
  sexta spot e 138/138 para a sétima em ambos os backends. Um receiver com textura MODULATE opaca marcou
  81/79 com a quinta luz. Com oito luzes em RTT, staged e direct marcaram
  228/225. O teste verifica submissão e serial da quinta spot e, no wgpu,
  falha de alocação de oito mapas sem alterar pixels nem serial. Ainda faltam
  a comparação GL nativa da oitava spot em um contexto com capacidade maior,
  acompanhada separadamente; o perfil backend e sua composição estão qualificados.
  A oitava spot submeteu e alterou o readback em 117 nos dois backends, mas
  seu delta incremental no Coin/GL desta máquina foi zero. Isso decorre do
  limite de unidades de textura da referência: `updateShadowLights()` usa
  `max_texture_units - numtexunitsinscene` como número máximo de mapas.
  Mesa/GLX reportou 8 unidades, das quais 1 é reservada à cena; o grupo
  portanto criou 7 mapas para 8 caminhos de luz. A oitava spot recebeu o
  índice GL 7 de 8, e sem sombras acrescentou 129 numa cena de intensidades
  baixas, mas seu mapa não entrou no shader do grupo. Com as mesmas
  intensidades, BGFX acrescentou 147 com sombra e GL zero. Forçar
  `shadeFactor=1` no oitavo ramo do shader GL não mudou a imagem, confirmando
  que o ramo nem foi gerado. BGFX e wgpu somam as oito contribuições antes da superfície e do blending
  (o mecanismo inicial usava dois lotes) e conseguem executar o oitavo mapa neste ambiente; a falta de delta GL não
  evidencia erro nos dois backends.

  A divergência inicial da quinta spot veio da normalização VSM: Coin/GL
  usa far de projeção para a câmera e um far distinto para os momentos e
  a recepção. O Core mantém `vsmFarDistance` separado de `farDistance`:
  spot usa `farDistance / cos(2 * min(cutOffAngle, 0,78))`, direcional usa
  `farDistance * 1,1`; BGFX e wgpu transportam esse valor aos shaders de
  mapa e receiver. A regressão RTT durante a correção revelou que
  `updateShadowLights()` desligava o binding da `SoSceneTexture2` herdada
  na unidade 0, embora a textura continuasse no `SoState`. O GL restaura
  as imagens das unidades após gerar os mapas, e a fixture rejeita um
  receiver inteiramente preto. Outra diferença da referência era o slot GL
  de luzes que só aparecem depois dos receivers: o shader podia ler estado
  residual. O GL agora zera esses slots antes de percorrer o grupo; Core
  e Infra respeitam a ausência da luz no estado capturado de cada desenho.
  RTT com uma spot permanece dentro da tolerância: Coin/GL 225, wgpu 213 e
  BGFX 216. Modos de textura não lineares e `SoTextureCombine` ainda são
  rejeitados antes da submissão.
- [x] **Cenas próprias por luz — captura independente:** `shadowMapScene`
  agora é percorrido pelo Wiring no estado de entrada do `SoShadowGroup`,
  sem exigir que seja filho do grupo ou que cada forma apareça uma só vez.
  Como em `SoSceneTexture2` e no callback SHADOWMAP do Coin/GL, a captura
  reinicia material, texturas e modelo; fixa BASE_COLOR, qualidade de textura
  zero e binding de material OVERALL. O restante do estado Coin é preservado.
  Uma cena própria que seja `SoShadowGroup` é percorrida pelos filhos, como
  faz a referência, sem criar outro grupo de sombras nessa captura.
  Core agrega os snapshots, converte modelo e clipping local ao espaço global,
  preserva planos herdados já globais e atribui cada draw ao mapa de uma luz.
  Geometria exclusiva do mapa não entra na composição principal nem na bbox
  do grupo usada para ajustar a câmera. BGFX baixa esses draws em uma lista
  própria de momentos; wgpu consome as mesmas listas de casters do Core.
  Foram qualificados shape direto, subárvore interna após outras geometrias,
  cena externa, instâncias repetidas de uma mesma forma com transformações
  distintas, materiais, `SoShadowStyle`, clipping e transformação herdada.
  Dois mapas spot e dois direcionais verificam seleção independente: alternar
  uma cena para vazia ou compartilhar a cena entre as duas luzes produz os
  deltas Coin/GL/GPU registrados pela fixture `independent shadow scenes`.
  Cenas vazias, transparentes ou `NO_SHADOWING` mantêm mapa limpo; mudanças
  de cena atualizam o próximo quadro. As fixtures anteriores de rotação,
  escala e clipping também continuam passando nos dois executores.
  O perfil usa a câmera calculada pela luz: uma câmera dentro da cena própria
  retorna `UNSUPPORTED`, preservando pixels e serial e permitindo recuperação
  após removê-la. Anotações são excluídas da travessia do mapa, assim como
  overlays excluídos dos casters no perfil de composição abaixo. Efeitos sem
  contrato portátil continuam fora do perfil. Grupos ativos dentro da cena
  do mapa são atravessados como geometria caster, sem substituir o VSM por
  outro programa de iluminação; a raiz e o filho ativo vazio preservam os pixels.
  A fixture ampliada revelou uma omissão da referência GL: o vertex shader
  VSM não escrevia `gl_ClipVertex`. A referência agora escreve a posição no
  espaço de olho, sem modificar a tolerância. Clipping da cena externa com
  modelo de entrada e plano herdado marcou delta 543/531/534 para spot
  (Coin/GL, wgpu, BGFX) e 666/666 para direcional nos dois executores; a equação do plano herdado permanece global
  e a equação adicionada à cena própria recebe a transformação de entrada.
- [x] **Clipping de casters opacos:** os planos já capturados e resolvidos
  pelo Core entram também no pass de momentos; BGFX e wgpu descartam o
  fragmento no mapa antes da recepção. A fixture com `SoClipPlane` no caster
  mediu diferença máxima 531/531 em Coin/GL e nos dois executores (tolerância
  200 nesta cena). Um readback direto do mapa wgpu confirmou branco no lado
  descartado e momentos no lado mantido.
- [x] **Transparência de material no mecanismo de objetos:** Wiring captura
  `SoLazyElement::isTransparent()` para todo o array de materiais; Core exclui
  a forma transparente dos casters, reproduzindo `SoShape::shouldGLRender()`
  no modo SHADOWMAP. A exclusão independe de `SoShadowStyle` e do modo de
  transparência. BGFX compõe o shader de receiver com seu pass transparente
  existente; wgpu duplica mecanicamente os receivers quando a composição
  deriva um estado para alterar a política de profundidade. A fixture
  renderizada validou SCREEN_DOOR, ADD, DELAYED_ADD, SORTED_OBJECT_ADD, BLEND,
  DELAYED_BLEND, SORTED_OBJECT_BLEND, os dois modos de triângulos ordenados e
  NONE. Para o receiver com transparência 0,5 e uma spot, os deltas máximos
  Coin/GL/GPU foram 456/474 em SCREEN_DOOR, 237/237 em ADD/BLEND imediato e
  nos modos ADD adiados, 120/120 em BLEND adiado e 474/474 em NONE, nos dois
  executores. Alternar CASTS_SHADOW/NO_SHADOWING numa segunda forma
  transparente preservou pixels idênticos nos três renderizadores, nos dez
  modos. A mesma fixture foi repetida com quatro spots de intensidade
  0,25: todos os modos passaram com as mesmas métricas, exercitando os
  quatro receivers e seus estados de profundidade derivados. A extensão de
  cinco a oito mapas está qualificada na caixa abaixo; uma nona luz é rejeitada
  antes de publicar o quadro. O perfil admite o mecanismo de objetos e até oito mapas;
  peeling/OIT está qualificado no perfil descrito abaixo. A fixture também
  repetiu os dez modos sem nenhum caster opaco; a exclusão manteve pixels
  idênticos em Coin/GL e nos dois backends.
- [x] **Mapas sem casters:** Core conserva o pass e a recepção quando a
  lista de casters está vazia; BGFX e wgpu limpam o mapa de momentos e
  continuam iluminando. O protocolo privado wgpu 39 usa `mapSize` como
  marcador de presença, aceitando contagem de casters zero em qualquer um
  dos oito passes. A fixture verifica que o Core escolheu listas vazias,
  submete um a oito mapas e exige avanço do serial. A segunda, terceira,
  quarta, sexta e sétima luzes marcaram delta incremental máximo 63/63;
  a quinta marcou 63/60, Coin/GL e GPU, nos dois executores. A oitava marcou
  63 em BGFX e wgpu; sua comparação Coin/GL continua dependente do contexto
  externo descrito na pendência de oito mapas. O build da ponte acompanha
  também `shadow.rs` e `coin_shadow.wgsl` como dependências explícitas.
- [x] **Alfa de textura estática MODULATE:** o Core aceita texels RGBA com
  alfa variável no mecanismo de objetos, até oito mapas, sem unidades
  extras nem `SoTextureCombine`. A mesma evidência exclui a forma texturizada
  dos casters, como `TRANSP_TEXTURE` no Coin/GL. Uma imagem 2×2 com alfa
  0/128/255 e UVs explícitas passou nos dez modos Coin nos dois executores:
  SCREEN_DOOR e NONE marcaram 474/474, ADD/BLEND imediato e ADD adiado
  174/174, BLEND adiado 102/102 no wgpu e 102/99 no BGFX, e triângulos
  ordenados em BLEND 174/171. O mesmo teste com quatro spots passou nos
  dez modos, com as mesmas métricas e bindings dos quatro mapas. Alternar
  o estilo de casting da forma com
  textura alfa manteve pixels idênticos nos três renderizadores, inclusive
  sem nenhum caster opaco. Alfa com DECAL é rejeitado antes da submissão,
  preservando pixels e serial.
- [x] **Alfa RTT NONE/ALPHA_BLEND:** qualificado no perfil de objetos, RGBA8
  MODULATE, sem unidades extras nem `SoTextureCombine`, com um, quatro e cinco a oito mapas,
  nos dez modos Coin e nas rotas staged/direct. Composição e sombras consultam
  a mesma decisão Core: `NONE` força classificação opaca; `ALPHA_BLEND` força
  transparência, inclusive quando o produtor entrega somente texels alfa 255.
  Casters transparentes são excluídos como no Coin/GL, inclusive em mapas sem
  casters opacos. A captura de cenas próprias por luz preserva a política RTT
  com qualidade de textura zero sem executar o produtor durante essa travessia.
  A referência Coin/GL foi corrigida: o produtor FBO limpa o estado de sombras
  e overrides herdados do consumidor; a cena interna VSM estabelece seu próprio
  papel no callback do mapa. A fixture `--alpha-rtt` passou com referência GL
  obrigatória em BGFX/Vulkan, BGFX/OpenGL e wgpu. SCREEN_DOOR/NONE mediram
  deltas GL/GPU 381/381; ADD/BLEND imediato, ADD adiado e triângulos ordenados
  228/138; BLEND adiado 126/81 no BGFX e 126/78 no wgpu, dentro da tolerância
  existente de 180 para o delta de recepção. Isso não exige igualdade de pixels
  entre renderizadores. Alternar casting manteve pixels idênticos em cada
  renderizador; alterar o alfa do produtor atualizou o quadro e restaurá-lo
  reproduziu os pixels anteriores. Falha de alocação do mapa preservou pixels
  e serial; após reconfigurar o alvo, a recuperação reproduziu o quadro anterior.
  A regressão de sombras, composição, transparência, RTT e reúso de plano
  passou nos dois backends; a rota RTT direct também passou.
  A extensão de cinco a oito mapas e a política `ALPHA_TEST`
  estão descritas abaixo.
- [x] **Transparência com cinco a oito mapas:** os dez modos Coin (0–9)
  passaram em BGFX/Vulkan, BGFX/OpenGL e wgpu, com alfa de material, textura
  estática e RTT `ALPHA_BLEND` staged/direct. O Core conserva a decisão única
  de transparência e exclusão de casters, ampliando o perfil de objetos até oito
  mapas; MODULATE sem unidades extras nem `SoTextureCombine` continua sendo
  o contrato qualificado. Os executores somam todas as contribuições antes da
  modulação da superfície e do blending, em uma submissão por desenho; o fundo
  é atenuado apenas uma vez. BGFX compõe a variante de oito mapas a partir do
  shader de quatro e usa stages 8–15. wgpu compõe o shader padrão com os quatro
  receivers adicionais, retirando quatro bindings de unidades de textura da
  cena que este perfil não usa; o pipeline permanece dentro do limite padrão
  de 16 texturas. A revisão privada wgpu 40 registra a nova convenção: somente
  os dois primeiros receivers usam matrizes de objeto, e os demais usam espaço
  de vista, incluindo os mapas cinco e seis.
  A fixture compara quatro e cinco/seis/sete/oito luzes coincidentes com a
  mesma intensidade total, exigindo erro máximo de três níveis RGBA; os 160
  casos mediram erro zero em cada uma das três rotas. Desligar a última luz
  deve alterar os pixels. Isso detecta perda de contribuição,
  aplicação repetida do alfa e mudanças de ordem. Casters transparentes
  continuam excluídos, inclusive sem casters opacos; a nona luz preserva pixels
  e serial ao ser rejeitada. Os casos RTT também verificam mutação/restauração
  do alfa, falha e recuperação; receivers transparentes dentro de produtores
  RTT staged/direct continuam recebendo sombra nos dez modos (160 casos por
  rota). A regressão de sombras, composição, transparência, RTT, anotações
  e reúso de plano passou nos executores; Naga validou o shader composto e
  o teste FFI confirmou a revisão privada 40.
  Cinco, seis e sete mapas têm referência Coin/GL nativa. Para oito, este host
  compara com sete mapas coincidentes de intensidade total equivalente e
  verifica a execução de oito mapas na GPU; isso não fecha a comparação com
  oito mapas Coin/GL nativos, registrada no arquivo de plataformas pendentes.
  A matriz está em `CoinRenderShadowTransparency5Test` a `8Test`, ou
  `CoinRenderShadowReferenceTest --transparent-maps N`. Exigir o executor GPU
  e `COIN_RENDER_REQUIRE_GL_REFERENCE=1`; sem executor explícito, os testes
  retornam skip 77. No contexto externo capaz de criar oito mapas Coin/GL,
  `COIN_RENDER_REQUIRE_GL_EIGHT_MAP_REFERENCE=1` desativa o oráculo equivalente.
- [x] **Peeling/OIT com sombras:** BGFX/Vulkan, BGFX/OpenGL e wgpu executam
  os dois mecanismos com até oito mapas. A matriz registrada cobre 1, 4 e 8
  mapas, alfa de material/textura estática/RTT e produtores RTT staged/direct.
  Peeling conserva o contrato limitado de camadas; weighted OIT é extensão
  explícita nos dois executores e usa o mesmo peso numérico, acumulação e
  revelação. O Core mantém classificação, exclusão dos casters transparentes,
  ordenação e orçamento; a Infra soma toda a iluminação antes da superfície
  e executa o compositor. BGFX reutiliza esses compositores no RTT direto com
  anexos locais, dimensões e IDs de view explícitos. A revisão privada wgpu 41
  acrescenta o flag de execução weighted; os limites também são validados na
  ABI Rust e o profiling encerra o intervalo no resolve.
  Foram corrigidos dois mecanismos da referência/execução: o shader GLSL de
  sombras Coin/GL precisa descartar as profundidades anteriores porque substitui
  o programa ARB de peeling; no BGFX, a paleta de limpeza dos mapas VSM não
  pode ser sobrescrita pelos anexos de transparência. Oito mapas com peeling
  retiram samplers de texturas extras que o perfil proíbe, evitando ultrapassar
  os dezesseis samplers da reflexão BGFX.
  `CoinRenderShadowOitN_MTest` verifica recepção, particionamento das luzes,
  casters, aditivos após o compositor, mutação, falha e recuperação; M=1 peeling, M=2 weighted.
  `CoinRenderShadowLayersN_MTest` usa expectativas numéricas independentes
  para seis superfícies, ordem invertida, materiais por face, limite de 2/8
  camadas, oclusão e orçamento insuficiente sem alteração de pixels/serial.
  O peeling de material/textura estática mede deltas GL/GPU 231/237 e 168/171
  com tolerância 30; RTT usa tolerância 120 (222/138). Weighted tem sua matriz
  numérica própria, incluindo alfa extremo e draws aditivos; não exige igualdade
  com o algoritmo de peeling GL. A referência nativa GL de oito mapas continua
  pendente: este host usa sete luzes coincidentes de mesma intensidade total.
  Validação local: 12/12 casos da matriz final em cada rota BGFX/Vulkan,
  BGFX/OpenGL e wgpu; regressões relacionadas 40/40 BGFX e 28/28 wgpu,
  sem skips, mais 18 testes unitários Rust. A matriz final inclui o teste
  aditivo acrescentado depois da campanha de regressão.
- [x] **ALPHA_TEST de SceneTexture2:** Wiring captura a política em RTT
  staged/direct; Core resolve classificação transparente, composição e
  exclusão de casters, inclusive em `shadowMapScene`. Coin/GL define
  `FORCE_TRANSPARENCY_TRUE|FORCE_ALPHA_TEST_TRUE`, mas nenhum renderizador
  consome `SoGLImage::useAlphaTest()` nesta versão. O comportamento real
  acompanha ALPHA_BLEND: não há descarte alfa automático. BGFX e wgpu
  preservam esse contrato sem criar limiar próprio ou alterar o GL.
  As fixtures `--alpha-test N M` cobrem N=1,4,5,6,7,8 e M=0 (dez modos
  Coin), 1 (peeling), 2 (weighted OIT), nos dois caminhos RTT. Comparam
  pixels ALPHA_TEST/ALPHA_BLEND em GL e GPU, recepção de sombras, exclusão
  de casters, mapas vazios, partição de intensidade, contribuição da última
  luz, composição RTT, atualização do produtor e recuperação de falhas.
  Política desconhecida preserva pixels/serial e retorna UNSUPPORTED.
  Teste alfa explícito por `SoAlphaTest` não integra este fechamento.
  Validação em 2026-10-02: 18 testes ALPHA_TEST por rota, 54 no total em
  BGFX/Vulkan, BGFX/OpenGL e wgpu. Regressões de SceneTexture2,
  ownership e sombras passaram. O preflight BGFX foi corrigido para não
  exigir peeling/OIT de desenho opaco só pelo modo Coin declarado.
- [x] **Qualidade ampliada:** Core resolve a iluminação por vértice ou por
  fragmento; BGFX e wgpu executam essas decisões para spot e direcional,
  com especular, normais interpoladas e atenuação posicional. As contribuições
  das luzes por vértice são interpoladas separadamente da visibilidade VSM,
  calculada por fragmento. Luzes comuns herdadas mudam de estágio acima de
  `0,7`; luzes de mapa mudam acima de `0,3`. Os limites preservam as comparações
  em precisão dupla do Coin/GL: o campo `float` com valor textual `0,3` já
  seleciona fragmento. Shapes sem SHADOWED mantêm iluminação comum por vértice.
  Especular por fragmento é somado depois da modulação da textura primária,
  preservando o contrato GL. O protocolo privado wgpu 42 transporta os flags
  resolvidos no campo existente, sem alterar o tamanho das estruturas.
  `CoinRenderShadowQuality{1,4,8}Test` compara sete valores de qualidade,
  superfícies com normais suaves e especular, com/sem textura colorida e
  ambos os tipos de luz. O limite continua sendo oito luzes ativas: uma e
  quatro sombras incluem uma luz comum; oito sombras ocupam todo esse limite.
  O oráculo de oito mapas usa sete luzes coincidentes com intensidade total
  equivalente neste host; referência GL nativa com oito continua pendente.
  Validação em 2026-10-02: 84 combinações de qualidade por rota GPU
  (252 em BGFX/Vulkan, BGFX/OpenGL e wgpu), com erro médio RGB limitado a
  2 níveis em 255. As regressões cobriram 41 testes BGFX, 30 wgpu e
  9 BGFX/OpenGL; 18 testes Rust e três validadores Naga passaram.
  `smoothBorder=1`
  passou em Coin/GL, BGFX e wgpu com pixels idênticos a `0`: a suavização
  gaussiana está desativada na implementação Coin/GL atual. O Core aceita
  somente os valores 0 e 1. O Coin/GL precisou corrigir a
  geração do shader para o perfil direcional: a luz
  direcional no caminho por vértice era tratada como spot, e a função
  `DirectionalLight` não era registrada no fragmento para `quality=0,5`.
  A matriz ampliada mantém as verificações de recepção de baixa qualidade e
  as regressões de transparência, peeling/OIT e RTT staged/direct.
- [x] **Receiver com textura estática opaca:** a textura primária é aceita
  quando seus texels RGBA capturados têm alfa 255 e não há unidades
  adicionais. O mesmo draw recebe sombra e modulação de textura nos
  dois executores; alternar a textura verde da fixture mudou a imagem em 485
  no Coin/GL, BGFX e wgpu. Alfa estático MODULATE foi qualificado depois,
  como descrito acima; alfa DECAL retorna `UNSUPPORTED`, preservando pixels
  e serial. Um `SoSceneTexture2` também pode modular o
  receiver se a captura registrar fundo opaco e `transparencyFunction=NONE`;
  a Action valida o perfil no plano lógico antes de executar o produtor.
  A fixture com luz spot, `quality=0,5` e sombra no grupo consumidor
  passou em staged e direct: após restaurar o binding RTT no Coin/GL,
  os deltas máximos são 225/213 (GL/wgpu) e 225/216 (GL/BGFX)
  (tolerância 200).
  `ALPHA_BLEND` e fundo não opaco foram rejeitados antes de mudar pixels
  ou serial. Alfa variável no produtor RTT continua aberto.
- [x] **Luz direcional comum herdada:** uma `SoDirectionalLight` anterior ao
  `SoShadowGroup` compõe sua contribuição opaca com um mapa spot; Core mantém
  a identidade da luz sem criar pass de sombra para ela. O recorte exige luz
  presente antes de todos os desenhos, material difuso sem especular e normais
  planas. Ligar essa luz mudou a cena em 152 no Coin/GL, BGFX e wgpu. A luz
  comum inserida dentro do grupo ativo também executa: o shader Coin/GL
  é montado com as luzes presentes na entrada do grupo e ignora essa luz
  interna. O Core identifica o índice capturado e ambos os backends zeram
  somente sua intensidade; ligá-la ou desligá-la preservou pixels idênticos
  em Coin/GL, BGFX e wgpu. Uma `SoPointLight` herdada com localização (0,2,4)
  também compôs com o mapa spot: ligá-la mudou a cena em 594 no Coin/GL e
  603 em BGFX e wgpu (tolerância 180). A `SoPointLight` inserida dentro
  do grupo ativo também foi comparada ligada/desligada: os três renderizadores
  produziram pixels idênticos, pois o shader Coin/GL é montado na entrada
  do grupo. Outras atenuações e combinações permanecem abertas.
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
- [x] **Composição opaca: RTT, grupos e camadas no perfil qualificado:** o produtor `SoSceneTexture2`
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
  calibração visual.
  A ampliação atual qualifica dois níveis de grupos aninhados, qualidade 1,
  três mapas, transforms de entrada e parâmetros distintos. O Core possui a
  árvore de ancestrais; cada grupo procura as luzes descendentes e recebe os
  próprios passes. A captura de mapa é independente da captura principal:
  grupos internos são geometria caster no mapa ancestral e mantêm o contrato
  próprio como receivers na cena. Desativar o grupo interno ou externo,
  alterar somente a recepção interna e restaurar o estado passaram nos três
  modos: offscreen, RTT staged e RTT direct. A alteração interna preserva os
  pixels do grupo externo. Ancestralidade cíclica é rejeitada no plano.
  Anotações opacas foram qualificadas com um e cinco mapas nas mesmas rotas.
  São receivers desenhados por último, sem testar/escrever depth e sem limpar
  o depth da cena; não viram casters, mesmo com `CASTS_SHADOW`. BGFX reutiliza
  o executor de camadas no produtor direct; a acumulação adicional preserva
  o depth efetivo e a iluminação das shapes sem recepção de sombras.
  Com a extensão a oito mapas, a soma de iluminação precede o blending
  na própria submissão do desenho. No BGFX, tanto
  os uniforms quanto o vertex shader mantêm a contribuição da luz adicional
  quando o lookup VSM está desligado.
  A comparação por pixel normaliza GL de baixo para cima e o readback GPU de
  cima para baixo. A fixture mede a diferença entre alterações sucessivas na
  região 88×88, com erro médio por canal limitado a 20 níveis RGB, além de
  exigir efeito de sombra e isolamento. Anotações usam uma superfície aberta,
  para que faces traseiras de uma caixa não escondam a iluminação sem depth.
  Quatro renderizações GL por estado conferem estabilidade do cache, com até
  três níveis RGB de arredondamento em display lists comuns. A referência GL
  foi corrigida: mapas não reiniciam grupos internos, `SoShadowStyle` não
  troca o programa VSM, anotações não geram delayed paths nos mapas e caches
  GL não encapsulam grupos nem passes SHADOWMAP/SHADOWS. A busca estrutural
  para proteger o cache ancestral é guardada pela revisão Coin do separator.
  A matriz focada passou em BGFX/Vulkan, BGFX/OpenGL e wgpu. A anotação com
  cinco mapas mediu deltas GL/GPU de 477/480 no BGFX e 477/477 no wgpu;
  no RTT, 384/384 nos dois executores. O erro médio da alteração ficou em
  1,17 no BGFX e 0,67 no wgpu, abaixo do limite declarado.
  A classificação alfa dos casters usa bytes capturados no Core: o elemento
  genérico Coin marca qualquer imagem de 2/4 componentes como transparente,
  enquanto o elemento GL consulta os texels efetivos. A evidência permanece
  no estado mesmo com qualidade de textura zero e invalida o reúso do plano.
  Falha injetada no mapa preservou pixels e serial em todas as rotas.
  Após resize explícito do alvo para suas dimensões atuais, a recuperação
  reproduziu exatamente o quadro GPU anterior. OOM do alvo offscreen mantém
  o protocolo existente de `TARGET_ERROR` até sua reconfiguração.
  A execução focada está em `CoinRenderShadowReferenceTest --composition`;
  é necessário exigir a referência GL e o executor GPU pelos mesmos flags
  da suíte completa. Alfa RTT fora do perfil acima, a referência GL com oito mapas e
  combinações além deste perfil permanecem nas caixas correspondentes.

Cada caixa acima requer uma fixture renderizada nos dois executores e sua
referência Coin/GL antes de marcar P27.4 concluído.

## Responsabilidades e estado

- [x] **Wiring inicial:** capturar grupo ativo, campos, `SoShadowStyle`, luzes
  spot/directional, elegibilidade Coin, matrizes e indicação de
  `shadowMapScene`; respeitar o escopo
  da travessia e os separadores sem chamar `GLRender`.
- [x] **Wiring do perfil:** overrides de material e qualidade de textura,
  caminho até o grupo e caminho até um receiver. `--wiring` verifica que o
  caster fora do caminho ainda contribui para a sombra e para os bounds.
  O callback de `SoComplexity` respeita o mesmo override de qualidade do GL.
  Para qualidade zero, o oráculo do nó usa GL sem sombras: o shader GL de
  `SoShadowGroup` continua amostrando a imagem instalada. Isso não é usado
  como referência de desativação de textura no perfil sombreado.
- [x] **Core inicial:** gerar um pass por luz spot/directional habilitada,
  separar desenhos caster/receiver pelos bits de `SoShadowStyle`, dimensionar
  mapa por `precision`, calcular câmeras spot/directional a partir da geometria
  capturada, resolver os parâmetros de VSM/qualidade e o índice da luz em cada
  estado de desenho, resolver a contribuição dessa luz no espaço de vista
  inclusive quando ela aparece após a geometria sem inseri-la nos estados
  anteriores, validar dois passes opacos
  spot/direcional e limitar a memória planejada.
- [x] **Core do perfil:** até oito mapas, grupos aninhados, RTT direct,
  composição e bounds de casters omitidos na travessia principal. Ownership
  e publicação reutilizam P12–P14, inclusive múltiplos alvos. Extensões fora
  das fixtures descritas não fazem parte deste fechamento.
- [x] **Infra BGFX/wgpu do perfil Linux:** mapas de momentos e depth, VSM, bias, textura,
  passes, sincronização, resize e reconstrução após perda, com shader específico
  por API. Mapas da Infra não entram no estado Coin. O shader wgpu que grava
  momentos lineares, spot/directional, e o lookup no shader principal validam
  em Naga. BGFX usa shaders BGFX separados e recursos próprios, mas os dois
  executores recebem os mesmos passes qualificados do Core. A campanha Linux
  cobre os perfis ampliados, resize e recuperação de falhas injetadas; perda
  física real do dispositivo não foi provocada.
- [x] **Shell/capacidades do perfil:** seleção explícita de perfil implementado e
  disponível; diagnósticos de limite/formato sem fallback visual implícito.
- [x] **Qualificação Linux do perfil:** comparar spot/directional, todos os estilos, cenas
  próprias por luz, alpha/transparência, RTT, resize e falhas nas
  mesmas fixtures Coin/GL. GPU/API/driver e resultados estão preservados na
  campanha de seis células físicas, com as tolerâncias declaradas nas fixtures.
- [ ] **Qualificação restante:** oito mapas GL nativos, GPU Intel
  e macOS/Metal, conforme o arquivo de pendências.

O preflight retorna `UNSUPPORTED` antes de submeter o quadro para grupos
ativos fora dos perfis qualificados de composição por objetos. O perfil atual aceita até oito
passes spot/direcionais. A quinta, sexta e sétima spots têm comparação
Coin/GL; a oitava tem readback e publicação verificados nos dois backends,
mas ainda requer referência GL com oito mapas. Alfa RTT NONE/ALPHA_BLEND
está qualificado até oito mapas, incluindo alfa de material e textura estática;
Combinações além dos perfis descritos são extensões não qualificadas.
Nos casos rejeitados, pixels e serial publicados ficam intactos.
