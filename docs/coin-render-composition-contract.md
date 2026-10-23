# Composição resolvida no CoinRender

Fechamento de A03 em 2026-09-28: `CoinRenderComposition.h` é o dono comum da
classificação de alpha, da modalidade Coin, da ordenação e dos estados efetivos
de profundidade. O parâmetro `exactCoin` foi eliminado. BGFX, o empacotador wgpu
e a referência CPU consomem a mesma política; Rust valida o transporte e
executa sua sequência, sem examinar alpha para escolher o passe ou ordenar
objetos pela câmera.

## Contrato e referência Coin/GL

A referência é `SoGLRenderAction::handleTransparency`, `renderSingle` e
`doPathSort`, junto de `SoDepthBuffer`, `SoDepthBufferElement` e `SoAnnotation`.
Wiring captura o centro de sorting para todos os backends, removendo a antiga
condição BGFX. O Core recebe snapshots e arrays; não lê `SoState`, atravessa paths ou emite
comandos GPU.

- `NONE` e `SCREEN_DOOR` não habilitam blend. O nível de stipple do screen door
  também é resolvido no Core; sua aplicação é específica do executor. O Bayer
  32×32 preserva a ordem MSB do bitmap CoinGL, sem espelhamento horizontal,
  e permanece ancorado à origem inferior esquerda do framebuffer. O stipple
  poligonal aplica-se somente a faces preenchidas. `SoDrawStyle::LINES` e
  `POINTS`, assim como `SoLineSet` e `SoPointSet`, conservam sua geometria sem
  esse stipple, seguindo [OpenGL 1.4, §3.5.4](https://registry.khronos.org/OpenGL/specs/gl/glspec14.pdf).
  O CoinGL instalado diverge entre Mesa e NVIDIA nesse cruzamento; a comparação
  nativa continua estrita e a divergência é acompanhada em B09.1 na matriz.
- `ADD` e `BLEND` executam imediatamente na ordem de travessia.
- `DELAYED_*` adiam draws transparentes, mantendo sua ordem relativa.
- `SORTED_OBJECT_*` ordenam os transparentes de trás para frente, estavelmente,
  antes da lista adiada sem sorting. Usam o centro capturado do bounding box;
  fixtures sem centro usam o centro do intervalo de profundidade.
- `SORTED_OBJECT_SORTED_TRIANGLE_*` solicita também sorting de triângulos.
  O Core produz intervalos de triângulos ordenados, incluindo materiais da mesma shape/instância. Essa solicitação não é substituída por weighted OIT.
- `SORTED_LAYERS_BLEND` solicita a estratégia específica de camadas.
- Anotações são organizadas por camada e preservam a ordem de travessia dentro
  da camada, inclusive entre draws opacos e transparentes. Sua barreira limpa
  somente a profundidade na viewport marcada.
- No passe transparente adiado, os padrões são test=true, write=false,
  função=LEQUAL e range=[0,1]. Campos explícitos de `SoDepthBuffer` prevalecem,
  como ocorre ao reaplicar o path no GL. Wiring captura a máscara dos campos
  explícitos para todos os backends. Anotações usam os estados capturados.

`CoinRenderCompositionItem` contém índice do draw, blend, adiamento, adição,
sorting, estratégia, screen door e profundidade efetiva. O lowering BGFX apenas
adapta esses valores. A CPU aplica função, range e escrita de depth, inclusive
para transparência imediata; peeling e mecanismos GPU têm o [perfil P09](coin-render-transparency-contract.md).

## Manager mono RGB e camadas autorais

`CoinRenderManagerAdapter` delimita a cena base e cada superimposition como
travessias autorais distintas. Backgrounds precedem a base; foregrounds a
seguem na ordem de inserção. Os seis modos técnicos e `setTexturesEnabled`
afetam somente a base. Flags, transparência e recursos das camadas conservam
seu estado próprio, inclusive em ocorrências repetidas do mesmo subtree.
`ZBUFFERON` controla o teste; não inventa uma máscara de escrita de depth.
`CLEARZBUFFER` limpa a viewport da camada mesmo quando ela não emite geometria.
Uma base ausente não inventa clear em `AS_IS`; `HIDDEN_LINE` conserva seu clear
próprio de cor/depth após a travessia dos backgrounds. O primeiro background registrado consome a decisão de clear do CoinGL, mesmo
quando desativado. Sem dono efetivo de clear, compor exigiria preservar a cor
anterior: estes targets não oferecem color LOAD persistente e recusam com
`UNSUPPORTED` antes de captura/GPU. Isso inclui base nula sem background e
primeiro background desativado, mesmo com base ou outro background ativo.
`HIDDEN_LINE` conserva seu clear próprio, inclusive com base nula e foreground;
manager inteiramente vazio conserva `INVALID_SCENE`. Reativar/remover o
background inicial ou restaurar a base, conforme o caso, recupera no mesmo target.

O depth de backgrounds sem clear pertence ao target e pode depender do frame
anterior. Targets distintos, resize, produtores RTT e o cache opcional de
attachments não compartilham esse histórico. MSAA preserva as amostras de
depth, sem reconstruí-las a partir do readback resolvido. A troca de
`doubleBuffer` em MONO não muda o resultado: a apresentação pertence ao target.
O adaptador não cria contexto GL, chama a action GL do host ou acrescenta um
render para atualizar a janela.

Passes técnicos sintéticos da mesma ocorrência base reutilizam a captura de
RTT. Ocorrências autorais distintas preservam seus efeitos de captura;
produtores com payload e revisão idênticos podem compartilhar execução GPU
após essa captura. Recusas de admissão conservam RGBA, depth e serial publicados, antes de
executar produtores ou submeter a raiz, e admitem recuperação no mesmo target.

Este perfil é mono RGB no Linux. Color-index e overlays de framebuffer sem
correspondência RGBA continuam recusados. Limites reais de formato, alpha-test,
sombras e prova de opacidade de RTT na base técnica não são removidos por
haver uma superimposition. Amostragem e derivadas do cascade de texturas
precedem o descarte por clip/stipple, para preservar o LOD nos quads com
SCREEN_DOOR; a máscara e o alpha-test continuam sendo aplicados antes da
publicação da cor. A chegada física do frame em NVIDIA/Vulkan, nos perfis BGFX com
duas imagens IMMEDIATE e wgpu FIFO, conserva a limitação externa
[B95](coin-render-feature-matrix.md#b95--imagem-antiga-após-present-bem-sucedido--22102026);
present bem-sucedido não comprova visibilidade. R29 e R35 mantêm seus escopos próprios; a comparação
de junções de linhas/pontos com CoinGL conserva os limites do
[perfil de geometria](coin-render-geometry-viewport-contract.md).

## Manager estéreo em um target RGBA

`CoinRenderManagerAdapter` admite ANAGLYPH, INTERLEAVED_ROWS e
INTERLEAVED_COLUMNS no perfil Linux descrito na [matriz R29](coin-render-feature-matrix.md#r29--manager-estéreo-no-linux--23102026).
BG é uma ocorrência autoral antes do par; a base tem uma ocorrência por olho;
FG é uma ocorrência monoscópica depois da composição, com todos os canais
habilitados. Modos técnicos e `texturesEnabled` continuam restritos à base.
O clear estéreo nativo pode apagar pixels de BG, mas não elimina sua travessia
ou seus recursos. Passes técnicos não repetem callbacks ou produtores autorais.

Cada olho conclui sua composição, incluindo os recursos admitidos no mesmo
perfil mono, em attachments GPU privados. Só depois são escolhidos os canais
ou pixels de saída. ANAGLYPH usa R de LEFT, G/B e depth de RIGHT. Seu alpha
segue as duas escritas nativas: LEFT inicializa o alpha que RIGHT recebe,
sem transportar RGB ou depth entre os olhos. Essa dependência conserva cada
amostra MSAA até a composição final e a aplicação de FG. As modalidades
intercaladas escolhem RGBA/depth completos por pixel; a fase é relativa à
viewport lógica do manager, inclusive com viewport aninhada e tamanho ímpar.
Não há readback/reupload CPU para compor as vistas ou apresentar a janela normal.

A admissão cobre ambos os olhos e todos os produtores antes de trabalho GPU.
Recusa de autoria ativa apenas em RIGHT preserva RGBA, depth, armazenamento
emprestado, serial e câmera, e permite nova tentativa no mesmo target.
Exceções autorais são contidas antes de sair da travessia legada para permitir
sua limpeza normal. A câmera temporária conserva modo e ajuste anteriores;
setters sem notificação não autorizam reutilizar uma captura de outro olho.
Prepare/finish, sensores e hooks pertencem ao frame público. Seu serial avança
uma vez por publicação; seriais de fila e tickets assíncronos pertencem a outro
domínio. Nenhuma action GL do host executa a produção.

Os produtores STAGED continuam executando por ocorrência. Um lease exclusivo
reutiliza somente o target de execução, depois de copiar os pixels para a imagem
própria dessa ocorrência. O dono retém até quatro targets ociosos por LRU,
com chave completa de tamanho, formato/mips, opções e domínio/geração. Resize,
perda e destruição invalidam o reuso sem destruir filhos sob o lock do registro.
Não é cache de conteúdo: revisões novas continuam sendo renderizadas. Descritores
adicionais podem causar eviction e nova compilação; o limite não recusa recursos.
A retenção inclui os backends, attachments e buffers desses quatro alvos.
O scratch exige o readback da sua execução atual mesmo com pipeline opcional
2/3, usando os slots e o limite de espera existentes; a política pública do
consumidor permanece própria.

Os tokens privados pertencem ao backend/dispositivo e têm duração limitada à
transação. Recursos de olhos, ocorrências autorais, produtores e peers não
compartilham slots por coincidência numérica. A ABI privada C++/Rust 70 usa
`CoinWgpuFrameView` de 600 bytes, com tokens, modalidade, origem e extensão
estéreo na cauda. As duas partes devem ser reconstruídas juntas; isso não
altera a ABI legada do Coin.

SEPARATE_OUTPUT e QUAD_BUFFER são aliases de um pedido de buffers nativos
LEFT/RIGHT. Um target RGBA único não fornece esses buffers: a recusa explícita
continua `UNSUPPORTED`, sem emulação silenciosa. Limites reais do perfil mono,
formatos e plataformas permanecem próprios. Highlight BOX/LINE ativo com
estéreo ou modos técnicos continua sob R35, com recusa e recuperação mono.
A separação de vistas permite inserir futuramente o replay gerado em cada
olho, antes da escolha dos canais e antes do FG autoral, preservando os
propósitos BASE/HIGHLIGHT e a identidade dos recursos.

A referência GL tem limites explícitos. Ela desativa display lists que não
acompanham setters estéreo sem notificação e normaliza a representação do
upload de stencil, a origem das imagens e a fase Bayer quando alinha o
raster MSAA em UPPER_LEFT. O produtor RTT opaco da fixture
é independente da máscara cromática externa. Esses controles não corrigem
nem qualificam as APIs legadas alteradas pela referência. HIDDEN_LINE é
comparado por olhos completos: seu clear global e a restauração de
`glColorMask` no manager GL legado não definem a composição desejada. A
comparação numérica de strokes e bordas conserva o perfil P02/P07 do mono;
a composição dos olhos tem adicionalmente um oráculo independente em todos
os pixels. Present bem-sucedido conserva o limite de visibilidade B95.

## Transporte wgpu

A revisão privada 21 introduziu esse transporte com draw de 56 bytes e estado
de 956. A revisão 22 conservou o draw e ampliou o estado para 1088 bytes
com equações de clipping. A revisão 28 acrescentou opções ao frame view
de 176 bytes e conserva o draw de 56 bytes, o vértice de 100 e o estado de 2280
da revisão 26 de multitextura. O campo no offset 36 é
`composition_flags`: blend no bit 0, aditivo no 1, screen door no 2, peeling no
3 e nível de stipple nos bits 8–14. O array de draws já chega na ordem resolvida. Estados
variantes são empacotados quando o passe adiado modifica a profundidade; um
estado compartilhado da captura não é alterado para afetar outros draws.

Camera patch recalcula a composição. Se a ordem ou os metadados dos draws
mudarem, o empacotador retira o hint de geometria imutável, impedindo que Rust
reutilize a sequência anterior do device. Falha de empacotamento invalida a
revisão em cache para que a próxima preparação reconstrua armazenamento válido.
C++ e Rust devem ser reconstruídos juntos; essa ABI permanece privada.

P09 amplia o wgpu e a CPU para os onze modos, incluindo aditivo, screen door,
triângulos ordenados pelo Core e peeling, ampliado para 1..8 camadas pelo
[contrato P10](coin-render-peeling-contract.md). BGFX consome os
mesmos intervalos e conserva os mecanismos concretos do seu perfil.
Pedidos fora das capacidades preservam imagem publicada e serial e permitem
um pedido válido seguinte; `UNSUPPORTED` de um frame não é erro fatal de recurso.
O [contrato P09](coin-render-transparency-contract.md) delimita qualificação,
overrides de peeling e particularidades da referência GL.

## Empréstimo da composição opaca durante a submissão

A classificação comum pode publicar uma prova de ordem opaca sem expansão
para planos com pelo menos 256 draws de triângulos, sem texturas, samplers,
sombras, anotações, strokes, screen door ou passes transparentes. A prova é
acumulada na classificação existente e só é publicada após seu sucesso.
Flags inertes de sorting e os campos efetivos de profundidade permanecem
exatamente como foram resolvidos pelo Core.

Target conserva as verificações de perfil e admissão. Quando a prova capturada
corresponde ao endereço, revisão e política do plano, usa a própria receipt
durante sua chamada. O schedule de instanciação BGFX e os schedules de
instanciação/agrupamento opaco wgpu podem ler essa ordem por uma view imutável
local. O escopo restaura a receipt ativa ao retornar, inclusive em falha; nenhuma
referência de composição entra em cache de backend, transporte Rust ou ticket
assíncrono. Revogação da receipt cancela a prova.

Os demais perfis continuam usando os vetores e o algoritmo de composição
geral. `COIN_RENDER_DISABLE_COMPOSITION_BORROW=1` restaura as duas cópias para
comparação. O trace distingue itens/bytes lógicos copiados, emprestados e
calculados; esses bytes não representam capacidade do vetor ou tráfego do
alocador. O tempo `composition_identity.qualify_ms` inclui a classificação
existente e não mede o custo incremental da prova.

Esta entrega histórica conservou a ABI privada 43. As revisões 21–28 na seção de
transporte acima registram etapas anteriores da sua evolução.

[Medições e validação local](coin-render-composition-copy-linux.md).

## Evidências e limites

`CoinRenderCompositionTest` verifica os onze pedidos Coin no Core, a ordem
imediata/adiada, padrões e overrides de depth, source-over, alpha 0/1, textura
com alpha, empates, viewports, anotações e preservação após rejeição. As mesmas
fixtures de pixels são executadas na CPU, no wgpu e no BGFX. A disponibilidade
BGFX agora é reconhecida pela fachada comum, evitando testes restritos à CPU.

`CoinRenderDepthContractTest` verifica captura da máscara explícita, transporte,
reuso e execução de profundidade. `CoinWgpuFfiFrameTest` verifica ordem/blend/depth
resolvidos, inversão da ordem por câmera, transporte de sorting de triângulos e
invalidação do empacotamento rejeitado. Os testes Rust verificam execução sem
reclassificação, sequência de camadas, flags inválidos, barreiras e frame vazio.

A falha antiga de source-over vinha também de uma fixture opaca em depth=1
sob LESS contra clear=1, aceita indevidamente pela referência CPU. O fundo agora
fica em depth=.875; as comparações respeitam a função declarada. Os passes
transparentes usam a profundidade efetiva do contrato comum.

O registro A03 abaixo é histórico. P09 acrescenta alpha aditivo, sorting por
triângulo e peeling no perfil delimitado, com comparação GL obrigatória.
Equivalência universal GL, FreeCAD e dependências RTT comuns permanecem
nas etapas próprias; os limites atuais constam dos contratos P09 e P10.

A matriz ampliada expôs fixtures anteriores incorretas em materiais/multidevice.
A biblioteca instalada da etapa anterior reproduziu os dois erros: expectativa
INVALID_SCENE para um índice positivo que a captura limita ao material disponível,
e estado FFI com estilo de polygon offset zero (o protocolo exige 1/2/4).
As fixtures agora verificam o slot limitado e inicializam estilo válido sem offset.
O teste direto de alpha também declara `composition_flags`: Infra não escolhe
blend novamente. A referência CPU quantiza alpha opaco com clamp e arredondamento,
evita produzir 254 para alpha 1 após interpolação e preserva alpha não opaco.


## Registro de validação local

- wgpu/Rust Debug: 16 casos CTest passaram, incluindo materiais, texturas,
  fog, cache, multidevice, RTT staged/budget, anotações e publicação assíncrona.
- BGFX/Vulkan Release: 9 casos CTest passaram, incluindo composição comum,
  profundidade, readback, offscreen, object/sorted layers/weighted OIT.
- BGFX/OpenGL Release sob Xvfb: 4 casos CTest passaram (composição comum,
  profundidade, readback e transparência).
- Rust: 7 testes unitários e 1 teste de validação do shader passaram, offline.
- `CoinRenderDepthContractTest --gpu` passou diretamente no wgpu.

Nenhum dos 29 casos CTest retornou skip. As subcomparações opcionais Coin/GL
de `CoinRenderLightingTest` e `CoinRenderFogTest` registraram, respectivamente,
`[SKIP] Coin/GL offscreen reference unavailable` e
`[SKIP] Coin/GL fog reference unavailable`, mesmo sob Xvfb. A referência GL da política de composição nesta entrega é o estudo
do código; não há certificação universal por comparação de imagens GL.

Probe wgpu: NVIDIA GeForce RTX 3060 Laptop GPU (Vulkan). Probe BGFX/Vulkan:
NVIDIA, vendor=0x10de/device=0x2560. Probe BGFX/OpenGL sob Xvfb: vendor/device
não identificados (0/0); a execução não qualifica um dispositivo físico OpenGL.
Alvos de composição: RGBA8 offscreen, depth `Depth32Float` no wgpu;
BGFX prefere D24S8 quando disponível, usando D32F nas camadas de peeling.
As tolerâncias estão nas fixtures: até 6 níveis de byte para composição
(3–6 conforme o caso) e .01–.02 de depth, quando a rota oferece readback.
Os demais testes mantêm suas próprias tolerâncias e limites de perfil.
O spike native/Dawn conserva a rejeição de transparência/stipple e não foi
qualificado como executor nesta matriz.
