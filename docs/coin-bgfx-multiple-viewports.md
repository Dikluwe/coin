# Múltiplas viewports BGFX

## Arquitetura

O runtime BGFX é global, mas não pertence à primeira janela. Ele é inicializado
sem uma janela de apresentação principal e permanece vivo enquanto houver
alvos preparados. Cada janela tem sua própria swapchain, framebuffer,
dimensões, cache de geometria/texturas e composição transparente.

Todos os alvos compartilham a mesma thread de API, normalmente a thread de GUI
do Qt. A thread de renderização interna do BGFX continua separada. Múltiplas
janelas não significam que seja permitido chamar `bgfx::frame()` ou alterar
views arbitrariamente em threads de trabalho. Travessia de cenas mutáveis do
Coin também não ganha uma garantia de thread safety por causa desta mudança.

Os IDs de view são reservados por alvo para impedir que clear, overlays,
weighted OIT, depth peeling e readback de uma viewport alterem outra. O limite
de alvos é `maxViews / 16`: o build padrão com 256 views permite dezesseis alvos
ativos, incluindo offscreen e sondagens. O build experimental com 1024 views
permite 64 alvos (também aumenta os pools de framebuffers e shaders).
Um alvo além da capacidade é rejeitado explicitamente;
fechar um alvo libera a reserva. O orçamento local também limita a quantidade
de camadas de overlay por frame; não há colisão silenciosa entre alvos.

Trocas de composição opaco/OIT/depth peeling também mantêm o framebuffer
nativo: recriam apenas attachments auxiliares. Isso evita duas superfícies EGL
para o mesmo XID enquanto a destruição anterior ainda está na fila do BGFX.
O formato da swapchain é escolhido nativamente, evitando inversão vermelho/azul
na apresentação Vulkan.

Resize atualiza apenas a swapchain da janela correspondente, sem reset global
do dispositivo. Destruir uma viewport libera somente seus recursos; shutdown
acontece quando o último alvo sai. Uma nova viewport pode reutilizar a reserva
liberada.

Alvos offscreen e sondagens de capacidades compartilham o mesmo runtime.
O renderer não pode mudar de Vulkan para OpenGL enquanto houver alvos ativos.
Uma falha fatal do dispositivo pertence ao runtime compartilhado e deve ser
observada pelos alvos restantes, nunca escondida como sucesso.

## Escopo

O suporte de apresentação permanece Xlib/Linux. O conector não coordena
outras bibliotecas que inicializem BGFX diretamente no mesmo processo.
Cada atualização apresenta um frame global; isto não é um agendador de
apresentação sincronizada de todas as janelas, nem paralelização da travessia.

## Validação

### Viewports externas e limites OpenGL

Uma viewport pode ter origem negativa ou exceder o framebuffer. Isso acontece
quando o NaviCube é maior que a célula do mosaico. O plano preserva esse retângulo
na transformação de projeção e no cache de câmera. BGFX recorta somente scissor
e clear de profundidade; draws sem interseção não são submetidos. O backend CPU
também limita acesso aos buffers. Dimensões nulas/negativas continuam inválidas.

O driver local reportou GL_MAX_VIEWPORTS=16 e
GL_MAX_VIEWPORT_DIMS=(16384,16384). O primeiro é o tamanho do array de
viewports indexadas, selecionadas pelo shader, não a quantidade de janelas.
Uma aplicação pode desenhar 64 regiões no mesmo contexto trocando glViewport
entre draws, ou usar contextos/janelas independentes. BGFX views são passes
lógicos sequenciais e não exigem 64 entradas nesse array GL.
A especificação exige no mínimo 16; o limite efetivo é definido pelo driver e
não é uma configuração que a aplicação possa aumentar.

Referência: https://registry.khronos.org/OpenGL/specs/gl/glspec46.core.pdf
(seção 13.8.1 e tabela de limites de viewport).

Regressões do recorte: 17/17 testes Coin passaram em Vulkan/OpenGL, incluindo
pixels em viewports parcialmente externas dos quatro lados, viewport totalmente
externa, preservação de projeção, camera patch e modos transparentes.
COIN_DEMO_VALIDATE_CLIPPING=1 faz a macro verificar novamente após reduzir
e restaurar a janela; a execução de teste termina automaticamente.

O teste real FreeCAD no desktop X11 passou 64/64 nas três verificações
(abertura, redução para 1200x800 e restauração para 1700x1000), sem fallback.
Log: /tmp/freecad-64-clipping-desktop.log.
O ensaio Xvfb/RADV não é válido para apresentação: reportou ausência de DRI3
e abortou no driver Vulkan; o teste desktop acima foi usado para validar.

Em 2026-09-27, a matriz Coin passou 12/12 no desktop X11: Product e Offscreen
em Vulkan/OpenGL, mais duas ordens de inicialização (janela/offscreen primeiro)
para cada renderer, além dos modos weighted OIT e sorted layers.
Foram verificadas cores e câmeras independentes, resize,
suspensão de tamanho zero, fechamento/recriação do primeiro alvo, limite de
oito alvos, reutilização de reserva, rejeição de outra thread de API e perda
compartilhada do dispositivo com recuperação após liberar os alvos afetados.

As 13 regressões de preparação de frame, action, core, profundidade,
transparência, depth peeling, weighted OIT e composição passaram sem skips.
O teste Qt/Quarter de duas viewports passou em ambos os renderers, incluindo
fechamento da primeira janela e reabertura enquanto a segunda permanece ativa.

O smoke PartDesignExample no FreeCAD passou no desktop em Vulkan e OpenGL,
incluindo primeira exposição, pré-seleção, painel inferior, resize,
maximizar/restaurar e minimizar/restaurar, sem fallback. Os probes gdb registram
zero chamadas auxiliares Qt/Coin GL. Logs:

- `/tmp/freecad-multitarget-vulkan-smoke.log`
- `/tmp/freecad-multitarget-opengl-fixed-smoke.log`
- `/tmp/coin-multitarget-regressions.log`
- `/tmp/coin-multitarget-final-matrix.log`

Dois documentos reais do FreeCAD passam em ambos os renderers, nos modos
object e weighted OIT (4/4), com câmera e pré-seleção independentes,
frames novos em ambas as viewports após resize/maximizar/restaurar e
minimizar/restaurar, e sobrevivência após fechar o primeiro documento.
O harness de hardware passa 4/4 com a segunda viewport inteiramente
transparente. Resultados detalhados em `testsuite/qt-quarter/EXECUTION.md`.

OpenGL por software sob Xvfb também passa 2/2. A captura aguarda pixels por

## Demonstração 4 × 4

`examples/coinrender/freecad_16_viewports.FCMacro` abre dezesseis documentos reais
em grade 4 × 4, com câmeras e cores independentes. Usa o perfil temporário
informado na inicialização do FreeCAD, verifica dezesseis presenters nativos
sem fallback e grava `/tmp/freecad-16-viewports.json` e
`/tmp/freecad-16-viewports.png`. A janela fica aberta para interação.
até cinco segundos, sem solicitar frames artificiais nem relaxar o limiar
de geometria. Uma espera fixa de 180 ms confundia submissão CPU com
apresentação concluída durante a compilação inicial de shaders/MRT.

## Recuperação compartilhada P14

RTT staged mantém o consumidor preparado durante os passes filhos. Uma perda
retira todos os conectores e readbacks da geração compartilhada; tickets antigos
preservam um diagnóstico de perda sem reter staging/runtime. As mesmas janelas
e alvos offscreen reconstroem recursos na próxima aplicação. Ver
[contrato e checklist P14](coin-render-multi-target-contract.md).
