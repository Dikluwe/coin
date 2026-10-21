# B06 — ciclo de vida paralelo da ponte Rust (Linux, 2026-10-17)

**Resultado:** encerrado para a concorrência admitida pelo CoinRender nas três células físicas disponíveis: AMD/Vulkan, NVIDIA/Vulkan e AMD/OpenGL. A API pública admite chamadas concorrentes de criação, submissão, readback, destruição e recuperação de dispositivos; o mutex do runtime serializa as transições de estado. Cada dispositivo extra tem `Device`/`Queue` próprios e agora compartilha a `wgpu::Instance` do runtime. O teste Rust paralelo usa uma instância comum no perfil Vulkan; no perfil OpenGL, cada teste mantém a sua instância porque o contexto EGL é vinculado à thread. Isso não qualifica Windows, Metal, Android ou aplicações que usem diretamente wgpu fora da ponte CoinRender.

## Diagnóstico preservado

A falha histórica do teste Rust paralelo aparecia após as asserções, com salto para endereço nulo a partir de `libvulkan.so.1` em `vkEnumerateInstanceExtensionProperties` e `ash::prelude::read_into_uninitialized_vector`. O material histórico está em `validation/coin-render-b06-20261017/historical/` e não continha um core aberto.

Um programa de integração que usa apenas wgpu, sem ponte Rust ou FFI CoinRender, reproduziu SIGSEGV (`returncode=-11`) na NVIDIA/Vulkan ao sobrepor a criação e a destruição de instâncias independentes com renderização em dois workers. O GDB mostrou o mesmo percurso e endereço nulo, durante `wgpu_core::instance::Instance::new`. Isso localiza a falha no ciclo de vida da instância/loader; não identifica sozinho a biblioteca ou camada que introduz a corrida. O controle que só enumerou extensões 250 vezes, inclusive em paralelo, não reproduziu a falha. As campanhas iniciais de 43 testes Rust (18 processos) e do teste direto sincronizado (5 processos, 160 ciclos de dispositivo) também passaram, mostrando que o interleaving livre foi essencial para reproduzir este caso.

O mesmo teste direto com uma única instância compartilhada passou em AMD/Vulkan, NVIDIA/Vulkan (cinco repetições) e na seleção mista AMD+NVIDIA/Vulkan, sempre com renderização e leitura exatas de 64 pixels por ciclo. **Este controle vale para Vulkan.** Compartilhar essa instância entre os dois workers do teste direto OpenGL provocou `EGL_BAD_ACCESS` no destrutor do adapter; a seleção mista incluindo GL também falhou. Esses recibos negativos permanecem arquivados. O CoinRender usa o mutex de seu runtime nas chamadas públicas; a campanha pública AMD/OpenGL passou com instância compartilhada e lifecycle concorrente.

## Correção

`make_extra_device` clonava um `RuntimeContext` temporário criando uma nova `wgpu::Instance` para cada dispositivo. Agora clona a instância do runtime existente e ainda solicita um `Device`/`Queue` lógico próprio. O runtime e os dispositivos extras têm identidade e gerações próprias, e todos os caminhos públicos continuam sob `RUNTIME_CTX`.

Os testes GPU da biblioteca usam uma instância de processo compartilhada apenas quando o backend selecionado é Vulkan. Para OpenGL cada teste cria a sua própria instância. A seleção de API no teste de shadow também foi corrigida: `WGPU_BACKEND=gl` antes executava Vulkan sem avisar; o log anterior e as duas versões da fonte foram preservados.

Um teste C++ novo exercita três threads: duas criam, submetem uma imagem com pixel exato e destroem oito dispositivos cada; a terceira submete 32 imagens no dispositivo padrão. Depois das threads, uma nova submissão verifica a recuperação do dispositivo padrão. O teste de estresse existente percorre 512 iterações, com tickets antigos invalidados, peer vivo e recriação de dispositivo.

## Validação

- **Suíte Rust final:** 18 processos × 43 testes, 1 e 8 threads, três repetições por perfil AMD/Vulkan, NVIDIA/Vulkan e AMD/OpenGL. 18/18 passaram, com recibos de GPU e API selecionada. Os 18 processos antes da correção de fixture também passaram; isso não foi tratado como prova de ausência da corrida histórica.
- **Caminho público final:** testes normal e estresse em cada perfil, 6/6 processos, GPU/API explícitas. O teste normal inclui a nova criação/destruição concorrente; o estresse inclui 512 iterações e recuperação.
- **Controle direto compartilhado Vulkan:** 7/7 processos (AMD 1, NVIDIA 5, mista 1), 16 ciclos com pixels de render exatos por processo. O controle independente falhou na NVIDIA/Vulkan com stack histórica reproduzida; os dois controles GL compartilhados falharam e estão registrados.
- **Escopo externo:** a criação/destruição livre de instâncias independentes diretamente no wgpu/NVIDIA Vulkan ainda pode provocar SIGSEGV neste host. O CoinRender não oferece esse padrão por sua API pública. Qualificação Windows/Android/Metal permanece nas etapas de plataforma.

Executar a auditoria arquivada com `python3 scripts/coinrender/audit_wgpu_lifecycle.py docs/validation/coin-render-b06-20261017`. `manifest.json` fixa os hashes dos logs, scripts, fontes e drivers observados. Os binários de build em `/dev/shm` são mutáveis; cada resultado registra seu hash no momento da execução. Os logs negativos e os snapshots anteriores preservam os contratos dos respectivos controles.
