# R27 — primeiro perfil MSAA offscreen wgpu

O alvo público agora aceita `CoinRenderOptions.sampleCount` igual a 1, 2 ou 4.
O padrão continua 1. O perfil multiamostra implementado é offscreen wgpu,
RGBA8 linear, cor síncrona e draws opacos. Os pipelines usam a contagem exata;
o encoder mantém RGBA8 e depth32 multiamostra e resolve a cor uma única vez
depois do último passe, antes de copiar o resultado. A ABI privada C++/Rust
avançou para 55 e `CoinWgpuTarget` mede 64 bytes. O device habilita
`TEXTURE_ADAPTER_SPECIFIC_FORMAT_FEATURES` quando disponível para executar 2×.

Não há redução silenciosa de amostras. A bridge consulta o suporte exato do
formato de cor e de depth. A entrada pública recusa 8×; sombras,
peeling/weighted OIT e blend comum exigem perfis próprios. Bump opaco
offscreen tem perfil MSAA 2×/4× qualificado separadamente em R26.4. BGFX recusa MSAA
nesse estágio. O alias zero→1 no pod privado
preserva chamadores internos que inicializam o pod com zeros; a opção pública
envia sempre 1, 2 ou 4.

## Verificação física e regressões

`CoinRenderMsaaTest` usa uma aresta assimétrica e clear transparente. Em
AMD/Vulkan, NVIDIA/Vulkan e AMD/OpenGL, o teste público e o FFI Rust retornaram
respectivamente 0, 1 e 30 pixels com alpha parcial para 1×, 2× e 4×. O teste
público também rejeita 8× sem publicar serial, e percorre
resize 32→64, suspensão zero e restauração 32 com imagem final idêntica à
inicial. Falhas injetadas `NOT_READY`, `OUT_OF_MEMORY` e `DEVICE_LOST`
preservaram a imagem e o serial publicados; `NOT_READY` e `DEVICE_LOST`
recuperaram no frame seguinte. Uma falha determinística adicional, depois
da codificação MSAA e antes da submissão à fila GPU, também preservou pixels
e serial. Todos esses controles passaram nas três células.

Na AMD/Vulkan, os CTests `CoinRenderMsaaTest`, `CoinWgpuFfiFrameTest` e
`CoinRenderOffscreenTest` passaram (3/3). A suíte Rust em série passou 48/48
(quatro testes GPU opt-in ignorados); o novo teste GPU opt-in foi executado
separadamente nas três células. A build BGFX e `CoinRenderSelectionTest`
passaram. `git diff --check` passou.

O readback síncrono de depth MSAA 2×/4× agora resolve pelo menor sample e
transporta os bits IEEE-754 em RGBA8. O teste público passou em AMD/Vulkan e
NVIDIA/Vulkan com interior, borda e fundo; o teste Rust conferiu os quatro
bytes exatos em 2×/4×, e a mutação dirigida `min→max` foi rejeitada (1/1).
Na AMD/OpenGL, o readback de depth multiamostra 2×/4× também resolve pelo
menor sample e preserva os bytes IEEE-754. A correção local de Naga, wgpu-hal
e glow habilita a textura depth multiamostra e aplica a máscara de samples
do pipeline. O teste Rust isolou cada sample 2×/4× e conferiu bytes exatos
de plano e fundo; a mutação dirigida `min→max` foi rejeitada. O teste público
conferiu interior, borda, fundo e recuperação após falha injetada antes da
submissão. Os três CTests MSAA de offscreen, RTT e janela passaram nas três
células físicas (AMD/OpenGL, AMD/Vulkan, NVIDIA/Vulkan); a suíte Rust passou
50/50 em OpenGL e Vulkan, com quatro testes opt-in ignorados em cada execução.

R27 e C18 continuam abertos: faltam BGFX, RTT em formatos/mips adicionais,
OIT e blend. O rollback após submissão efetiva e tickets
assíncronos pertencem aos perfis ainda não implementados. As células
Intel, Windows, Android e Metal não foram qualificadas por estes resultados.
O perfil opaco de janela 1×/2×/4× está documentado separadamente em
[R27 janela](coin-render-r27-window-20261017.md).
O perfil RTT RGBA8 2×/4×, incluindo depth síncrono do consumidor, está em
[R27 RTT](coin-render-r27-rtt-20261017.md).


## Continuação BGFX no Linux disponível

A recusa BGFX descrita neste relatório histórico foi superada pelo perfil R27.2 da [matriz](coin-render-feature-matrix.md). O perfil opaco RGBA8 linear agora executa 1/2/4 em offscreen, janela X11 e RTT DIRECT/STAGED nas quatro células AMD/NVIDIA × Vulkan/OpenGL. Attachments multiamostra têm contagem efetiva verificada por GPU; a cor resolve por média e o depth síncrono pelo mínimo dos samples D32F. O caminho 1× conserva seu formato legado. O orçamento adicional de attachments MSAA é 128 MiB.

Isso não encerra R27/C18: blending/OIT, sombras/bump BGFX, raster canônico/SoImage/bitmap, overlays, tickets e demais formatos/mips/APIs/plataformas mantêm seus perfis próprios abertos. O produto usa a opção pública shaderc --raw no GLSL, preservando a extensão necessária sem modificar o SDK instalado; Vulkan usa o mesmo corpo de resolve compilado para SPIR-V.
