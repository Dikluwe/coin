# Anotações no conector Rust/wgpu

O protocolo privado 20 transporta `CoinRenderDrawPacket::renderLayer` e
`clearDepthBefore` em `CoinWgpuDraw::render_layer` e `clear_depth_before`.
C++ e Rust devem ser reconstruídos juntos: o draw passa de 48 para 56 bytes.
O estado de renderização permanece com 956 bytes, incluindo polygon offset.

A camada zero mantém o perfil de composição existente do Rust. As demais
camadas são executadas por número crescente e preservam a ordem de travessia,
inclusive quando alternam geometria opaca e transparente. A limpeza de depth
ocorre antes do draw marcado, em uma passagem sem attachment de cor, com
scissor na viewport correspondente. A profundidade fora da viewport é
preservada. O pipeline dessa limpeza é reutilizado por device.

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
  ordenação de camadas, mistura opaco/transparente, barreiras, rejeições e frame vazio.
- `CoinRenderDepthContractTest`: transporte, reuso, camera patch e remoção dos metadados.
- `CoinRenderAnnotationTest` (Rust): pixels offscreen para limpeza restrita à viewport,
  preservação da profundidade externa e ordem entre anotações opacas e transparentes.
  Retorna skip 77 se o backend GPU não estiver disponível.

Resultado local em 2026-09-28: sete testes Rust passaram; os três alvos CTest
acima passaram sem skips; `CoinRenderDepthContractTest --gpu` também passou.
A suíte geral `CoinRenderCompositionTest` continua falhando em `GPU source-over`
(canal azul esperado 63, observado 0). Seu fixture coloca a geometria opaca
no far plane (depth 1), enquanto o executor Rust usa LESS contra o clear 1.
A suíte geral e a paridade de transparência continuam pendentes; os testes
específicos de anotações usam geometria dentro do intervalo de profundidade.
