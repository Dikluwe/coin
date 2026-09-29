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
Cada alvo preparado reserva 16 views; o máximo depende de `caps.maxViews`.
Destruição devolve o bloco e excesso retorna `UNSUPPORTED`. RTT staged mantém o
consumidor preparado durante a vida dos filhos, evitando desligar o runtime
entre passes. Isso substitui a liberação antiga do consumidor, inadequada ao
runtime compartilhado. O initializer pode ser destruído sem interromper peers.

Os alvos públicos wgpu usam o dispositivo default; a bridge privada já possui
IDs de dispositivos extras e testes de isolamento, perda e destruição. P14
qualifica essa bridge e o default compartilhado; não adiciona seleção pública
de dispositivos por alvo. BGFX não passa a oferecer múltiplos dispositivos.

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
ampliados e ALPHA_TEST seguem F14; profiling físico P18; persistência/pools e
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
