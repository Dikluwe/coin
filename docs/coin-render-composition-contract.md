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
  também é resolvido no Core; sua aplicação é específica do executor.
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

## Transporte wgpu

A revisão privada 21 introduziu esse transporte com draw de 56 bytes e estado
de 956. A revisão 22 conservou o draw e ampliou o estado para 1088 bytes
com equações de clipping. A revisão atual 28 acrescenta opções ao frame view
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

Esta mudança conserva a ABI privada atual 43. As revisões 21–28 na seção de
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
