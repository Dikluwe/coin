# CoinRender: primeiro perfil WebAssembly / wgpu WebGPU

A cena de cubo opaco com câmera perspectiva e material é criada e capturada
pelo Coin/CoinRender C++ compilado com Emscripten. A ação sem target fornece o
FramePlan; não se executa o rasterizador de referência do backend RECORDING.
A ponte transfere posições homogêneas, normais, cores e índices para outro
módulo WASM, que usa Rust wgpu 24 com BROWSER_WEBGPU apenas. O shader é um
perfil pequeno de demonstração, não o conjunto completo do backend nativo.
Não há cubo hardcoded no renderer nem fallback WebGL/software/imagem pronta.

No Chrome 156 em Linux, o cubo foi renderizado com movimento, resize e conteúdo
não uniforme confirmado por capturas. O adapter efetivo informou vendor `amd`,
arquitetura `gcn-5`, `isFallbackAdapter=false`. Vendor/device/description são os
dados expostos pelo navegador; campos vazios não identificam uma placa exata.
A inspeção observa o mesmo adapter/device entregue ao wgpu, sem sondar outro.

O Chrome padrão desse host não ofereceu adapter. O ensaio de hardware usou
`--enable-unsafe-webgpu --enable-features=Vulkan --use-angle=vulkan`, com ICD
Mesa Radeon explícito. Portanto o resultado não qualifica a configuração
padrão de todos os navegadores nem demonstra suporte na TV. Os testes aceitaram
o certificado de desenvolvimento; dispositivos remotos precisam de HTTPS
confiável e de suporte WebGPU próprio.

A API `on_submitted_work_done` do wgpu 24 não está implementada no backend web.
O renderer Rust faz encode/submit/present e validação; o host aguarda
`GPUQueue.onSubmittedWorkDone()` da mesma queue antes de publicar sucesso.
Perda de device e erros impedem a publicação. Isso confirma conclusão da fila,
não mede latência até a tela ou throughput de 120 FPS.

A suíte independente tem 18 testes de navegador/distribuição: imagem,
movimento, resize, conclusão da fila retida por gate, ausência de WebGPU/contexto
seguro, falhas de adapter/device/pipeline/submissão, perda de device,
identidades software e flags de fallback contraditórias. Três mutações
(aceitar software, aceitar fallback e publicar sem confirmação) foram
rejeitadas. A suíte final foi repetida pelo IP da LAN.

Para construir e servir, consultar
[README do exemplo](../examples/coinrender/webgpu/README.md). O servidor aceita
somente os seis arquivos finais, recusa symlinks e listagem e mantém o material
TLS fora da raiz pública. Abrir em outro dispositivo é uma nova qualificação:
se o navegador não oferecer adapter verificável, a página apresenta erro e
não troca de backend. O teste do milhão, os modelos FreeCAD e a paridade do
backend nativo não fazem parte deste primeiro perfil.
