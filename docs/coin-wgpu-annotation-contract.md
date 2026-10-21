# Anotações no conector Rust/wgpu

O protocolo privado 21 transporta `CoinRenderDrawPacket::renderLayer` e
`clearDepthBefore` em `CoinWgpuDraw::render_layer` e `clear_depth_before`.
C++ e Rust devem ser reconstruídos juntos. O draw tem 56 bytes desde a revisão 20;
a revisão 21 usa o antigo campo reservado para o blend resolvido pelo Core.
O estado tinha 956 bytes nessa entrega e 1096 na revisão 25; esses tamanhos
são históricos. A revisão privada vigente é 59, definida em
[CoinWgpuFfi.h](../src/rendering/coinwgpu/CoinWgpuFfi.h); os metadados de
anotações permanecem no draw de 56 bytes. Veja o
[contrato de clipping](coin-render-clipping-contract.md).

A camada zero recebe a composição resolvida pelo Core CoinRender. As demais
camadas são executadas por número crescente e preservam a ordem de travessia,
inclusive quando alternam geometria opaca e transparente. A limpeza de depth
ocorre uma vez na entrada da camada marcada, antes de seus draws, em uma
passagem sem attachment de cor. O draw que transporta `clearDepthBefore`
identifica a viewport original da limpeza; sua posição na sequência resolvida
não cria uma barreira no meio da camada. O scissor conserva essa viewport e
preserva a profundidade externa. O pipeline dessa limpeza é reutilizado por
device.

Viewports inválidas são rejeitadas antes da mutação dos caches e da codificação
de comandos. A viewport FFI com largura e altura zero continua significando o
alvo inteiro. Barreiras em camada zero e valores diferentes de 0/1 são inválidos.
Frames sem draws continuam limpando os attachments.

O empacotamento conserva os metadados em rebuild, reuso e camera patch. A
execução usa o encoder comum aos alvos offscreen e de janela. Isso fecha a perda
de metadados de anotações; não estabelece equivalência das onze modalidades de
transparência Coin, nem qualifica toda a integração FreeCAD no wgpu.

Validação:

- `cargo test --offline --manifest-path src/rendering/coinwgpu/rust_bridge/Cargo.toml`:
  execução sem reclassificação/ordenação, camadas, flags, barreiras, rejeições e frame vazio.
- `CoinRenderDepthContractTest`: transporte, reuso, camera patch e remoção dos metadados.
- `CoinRenderAnnotationTest` (Rust): pixels offscreen para limpeza restrita à viewport,
  preservação da profundidade externa e ordem entre anotações opacas e transparentes.
  Retorna skip 77 se o backend GPU não estiver disponível.

Resultado local em 2026-09-28: a suíte geral `CoinRenderCompositionTest` agora
passa na referência CPU, no wgpu e no BGFX/Vulkan. O Core produz os estados
efetivos de depth e a fixture de source-over fica dentro do far plane sob LESS.
Detalhes, evidências e limites em [composição comum](coin-render-composition-contract.md).
As modalidades não implementadas no wgpu são rejeitadas explicitamente; a
paridade completa das onze modalidades continua pendente.
