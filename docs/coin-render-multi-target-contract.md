# CoinRender — múltiplos alvos e recuperação (P14)

## Contrato Coin e divisão de responsabilidades

O Coin aplica uma action ao grafo com viewport e estado próprios. No caminho GL,
`SoOffscreenRendererP::renderFromBase` ativa o canvas, troca o cache context da
`SoGLRenderAction` e restaura o contexto anterior. A imagem pertence ao renderer;
a existência de outro canvas não permite trocar silenciosamente o resultado.
Referências locais: `src/rendering/SoOffscreenRenderer.cpp` e
`src/actions/SoGLRenderAction.cpp`. CoinRender preserva essa responsabilidade,
usando planos capturados e recursos com owner/gerações do P12 e publicação do P13.

- **Wiring:** aplicação ordenada das actions; tamanho, suspensão e destruição do
  alvo; coordenação dos consumidores de uma geração perdida; publicação.
- **Core:** admissão da fila pelo número de tickets e bytes de saída; stamps de
  owner/alvo/dispositivo; composição e dependências RTT já definidas no P12/P13.
- **Infra:** ocupação concreta das filas, fences, views, texturas e buffers;
  liberação segura da geração perdida; criação do próximo dispositivo/superfície.
- **Shell:** seleção tipada/defaults e diagnósticos. A falha informa o mecanismo
  e mantém o último resultado CPU válido.

## Agendamento e isolamento

O chamador ordena `apply`/`applyAsync` por alvo. O grafo RTT executa produtores
antes de consumidores; o ticket corresponde ao frame e às dimensões capturadas
na submissão. Resize/destruição do alvo não alteram tickets independentes já
aceitos. Mutações de produtor e submissões de outro alvo não publicam nesse alvo.

No BGFX existe um runtime por processo, um renderer ativo e uma thread de API.
Cada alvo preparado começa com um bloco de 16 views. Composições maiores
reservam blocos contíguos adicionais conforme o plano; o limite depende de
`caps.maxViews`, dos outros alvos vivos e da disponibilidade de um intervalo
contíguo. Destruição devolve todos os blocos do alvo; falta de capacidade retorna
`UNSUPPORTED`. O limite antigo de 16 por frame foi ampliado em B51, cuja
fronteira de recursos foi requalificada nas quatro células BGFX Linux em B51.2. O preflight consulta o pool sem
reservar views: DIRECT verifica o maior span compartilhado entre raiz e
produtores; STAGED verifica spans contíguos distintos para o pai e o maior
filho sequencial, respeitando os outros alvos vivos. Não soma filhos que já
foram destruídos antes do próximo produtor. A recusa deve anteceder o primeiro
produtor e preservar RGBA, profundidade e serial; a reserva real ocorre na
submissão. RTT staged mantém o consumidor preparado durante a vida dos
filhos, evitando desligar o runtime entre passes. Isso substitui a liberação antiga do consumidor, inadequada ao
runtime compartilhado. O initializer pode ser destruído sem interromper peers.

Os alvos públicos wgpu usam o dispositivo default; a bridge privada já possui
IDs de dispositivos extras e testes de isolamento, perda e destruição. P14
qualifica essa bridge e o default compartilhado; não adiciona seleção pública
de dispositivos por alvo. BGFX não passa a oferecer múltiplos dispositivos.

No perfil Linux Xlib/Vulkan, o `Display` e a janela pertencem ao chamador.
Depois de destruir o alvo, ele pode destruir a janela e fechar sua conexão,
enquanto outro alvo mantém o runtime ativo. Endereços de `Display`/XCB podem
ser reutilizados; o endereço não comprova que uma conexão já foi preparada.
BGFX e o ingresso Vulkan Xlib do HAL wgpu negociam XFIXES na conexão concreta
antes do WSI. O perfil de apresentação requer versão 2 ou superior, com
resposta compatível com o pedido 6.0; ausência ou resposta inválida retorna
recusa com diagnóstico antes de publicação, preservando o peer offscreen.
OpenGL e alvos offscreen não adquirem esse pré-requisito de apresentação.
O [protocolo XFIXES, seção 4](https://xorg.freedesktop.org/archive/current/doc/fixesproto/fixesproto.txt)
exige negociar a versão antes dos demais pedidos da extensão.
A qualificação Linux e os limites de recriação estão em
[B54/B34 na matriz](coin-render-feature-matrix.md); esses casos são distintos.

A fila comum admite até **16 tickets e 128 MiB de saída empacotada pendente**,
cor e depth somados. O cálculo é do Core; Infra informa contagem/bytes. A action
verifica antes de executar produtores RTT, e Target verifica antes da submissão
raiz. O BGFX mantém a mesma defesa na entrada do executor. A bridge Rust mantém
seu limite concreto de jobs; sua consulta inclui cancelados ainda em retirement e agrega os dispositivos do
runtime, seguindo a fila global já existente na bridge.
Descritores de gerações perdidas sem staging não ocupam o orçamento de saída.

A admissão é uma verificação de ocupação, não uma reserva para ações concorrentes.
As aplicações Coin deste perfil são ordenadas pelo chamador. Não há promessa de
thread safety para mutação/travessia do grafo nem para uso simultâneo de um alvo.
BGFX exige também polling/cancel/destruição na thread de API. Alinhamento de
staging, caches e memória física do driver são medidos separadamente em P18/P19;
128 MiB não representa um teto para toda a memória GPU.

## Perda e reconstrução

Uma perda BGFX invalida o dispositivo compartilhado. Resetar somente o alvo que
observou a falha deixava peers/tickets prendendo o runtime morto. Agora Wiring
aposenta todos os conectores BGFX registrados, incrementa as gerações dos alvos
e os marca `TARGET_LOST`. Infra retém buffers de readback até `bgfx::shutdown`,
libera suas referências e guarda somente descritores de perda. Tickets antigos
retornam `READBACK_DEVICE_LOST`, preservam os vetores do chamador e podem ser
cancelados; eles não impedem a reconstrução. A próxima aplicação em um alvo
existente recria seus recursos e o dispositivo, com novo epoch.

No wgpu a bridge descarta recursos da geração perdida e recria o default; peers
reconstroem seus recursos ao voltar a executar. CoinRender observa o domínio
preparado/concluído e avança a geração de recursos do alvo quando o epoch muda.
Um ticket de geração descartada retorna perda ou invalidade, sem publicar dados.
Tokens RTT de outro owner/epoch continuam rejeitados pelo contrato P12. Uma
mudança de epoch observada entre produtores rejeita o grafo e libera os tokens
temporários, sem presumir que o novo dispositivo saudável precise ser destruído.

Perda preserva a última cor/depth CPU, serial, revisão validada e empréstimos
publicados. A revisão de validação é uma propriedade do plano capturado, não do
handle GPU; resize continua invalidando-a. A próxima publicação bem-sucedida
substitui esses resultados pelo contrato P13. Um erro de configuração, OOM ou
falha geral de backend não ganha um retry infinito: continua com seu diagnóstico
e política explícita de recuperação já definida pelo alvo.

## Checklist e evidência

- [x] Isolamento de tamanhos, imagens, serial e mutação de produtor RTT.
- [x] Aplicações intercaladas; resize de um alvo preserva o peer.
- [x] Ticket RTT sobrevive à destruição do alvo e à execução de um peer.
- [x] Admissão no limite de jobs/bytes e proteção contra overflow.
- [x] 16 tickets repartidos entre alvos; 17º rejeitado antes de produtores GPU;
  cancelamento restaura a capacidade.
- [x] View budget BGFX esgotado, rejeitado e devolvido por destruição.
- [x] Perda com dois alvos e ticket pendente; publicação anterior preservada;
  mesmos alvos voltam a executar RTT num epoch novo.
- [x] Janelas BGFX simultâneas e offscreen: resize, suspensão, troca/destruição do
  initializer e reconstrução dos mesmos alvos depois de perda compartilhada.
- [x] Superfícies wgpu e bridge de dispositivos extras requalificadas.

Fixture nova: `testsuite/coinrender/CoinRenderMultiTargetTest.cpp`, em wgpu
staged/direto e BGFX Vulkan/OpenGL staged/direto. A rejeição antes dos produtores
é observada pelo serial global wgpu e por um fault BGFX não consumido. A fixture
BGFX também esgota o limite real de views. `CoinBgfxMultiWindowTest` acrescenta
perda/reconstrução ao seu perfil de janelas, opaco/OIT/peeling e inicialização
por janela ou offscreen. Regressões anteriores de publicação, ownership,
SoSceneTexture2, async, superfície e composição continuam parte da campanha.

Execução Linux/Xvfb/Mesa. Esta evidência fecha P14 no perfil experimental atual;
matriz física de drivers/plataformas/FreeCAD continua P16/P17/P20. Formatos RTT
ampliados seguem F14; ALPHA_TEST foi qualificado em P27.4 conforme Coin/GL;
profiling físico P18; persistência/pools e
readback de janela P19. Inventário de nós/workbenches é a próxima entrega P15.

### Campanhas de validação

Builds C++11 BGFX/CPU e Debug wgpu. Rust: 16 testes unitários e 2 de shaders
aprovados. CPU: 4/4, incluindo publicação e ownership. A campanha wgpu corrigida
passou 33/33, incluindo stress de múltiplos dispositivos e RTT/readback. A
assertiva final exige um ticket real pendente antes da perda: wgpu 2/2 e BGFX 4/4
nos modos staged/direto, Vulkan/OpenGL. A campanha ampla BGFX
exercitou 74 casos; a rodada final focada passou 18/18, incluindo os cinco casos
que falharam inicialmente. Três eram a regressão de tratamento de epoch corrigida;
dois retornaram falta de capacidades OpenGL depois de múltiplas inicializações
do contexto e passaram na reexecução com Xvfb novo. Essa intermitência do ambiente
Mesa/Xvfb fica registrada, sem ampliar a certificação de drivers.

Logs locais: `/tmp/coin-p14-bgfx-regression.log`,
`/tmp/coin-p14-bgfx-final-focused.log`, `/tmp/coin-p14-wgpu-regression.log`,
`/tmp/coin-p14-recording-tests.log` e `/tmp/coin-p14-rust-tests.log`.

No perfil Linux X11/BGFX/OpenGL, o runtime possui uma conexão X11 privada,
aberta no servidor do primeiro alvo (ou no display default para offscreen).
Os recursos EGL do chamador permanecem independentes: destruir todos os alvos
não deve invalidar seu contexto/pbuffer nem impedir unbind/rebind. A conexão
privada vive até o shutdown efetivo do BGFX, incluindo tickets assíncronos que
sobrevivem ao alvo, e então é fechada. A janela continua pertencendo ao chamador;
a criação é sincronizada antes de o renderer acessar seu XID pela outra conexão.
Isso evita tratar criação de contexto como propriedade exclusiva do EGLDisplay:
[EGL 1.5, seção 3.2](https://registry.khronos.org/EGL/specs/eglspec.1.5.pdf)
não dá uma referência adicional a `eglInitialize` sobre display já inicializado.
O perfil de preservação B61 não encerra as falhas de recriação NVIDIA B34.

Quando a janela BGFX recebe um pedido explícito de readback, a submissão
aguarda o término do callback de captura da cena corrente. `frame()` sozinho
sincroniza o frame anterior; por isso B63 usa um frame FLUSH vazio como fence,
sem repetir a cena ou apresentar o frame vazio. Preparação fria e desenho
permanecem dentro do `apply`; o prazo do callback não é ampliado. A publicação
lógica e o serial avançam uma única vez. Isso não encerra B34 nem implica
ganho de desempenho medido.

Conclusão do readback e apresentação física são eventos distintos. O callback
BGFX pode terminar antes de `flip()`; em Vulkan, enfileirar o present não
comprova sua conclusão no servidor X11. O retorno de `apply` com readback
corrente não promete que o primeiro `XGetImage` já observe esse frame. Os
testes físicos observam a imagem corrente dentro do prazo X11 existente de
cinco segundos, sem reaplicar a cena; comparam o framebuffer inteiro ao
readback do mesmo apply e também conservam os oráculos de cor e MSAA. Esse
prazo pertence ao observador do teste e não amplia o prazo do callback nem
adiciona uma espera fixa ao produto.
