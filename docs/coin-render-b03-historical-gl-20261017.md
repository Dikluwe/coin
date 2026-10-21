# B03.2 — recorrência histórica wgpu/OpenGL AMD

O B03.2 **continua aberto**. A falha original mostrou a primeira captura X11
preta depois de 50 ms, apesar de a captura offscreen da mesma cena ter 401466
pixels visíveis. O quadro final passou. O log original permanece em
`/mnt/Laranja/Git/externos/coin/build-coinrender-terrain-results/light-final-wgpu-opengl-native-terrain-million.log`.

Nova execução de submissão única na branch atual: quatro processos independentes
do `CoinRenderFirstPresentTest --terrain --managed` em AMD/OpenGL, com um milhão
de triângulos, tiveram **primeira leitura imediata preta**, embora o serial já
tivesse avançado para 1. Nos quatro, a leitura aos 50 ms identificou o frame
novo e comparou RGB exatamente com a referência offscreen produzida somente
depois da observação X11. As revisões seguintes e o resize também passaram,
sem render adicional entre submissão e observação. Portanto, o retorno da
submissão não equivale a frame apresentado; uma leitura imediata preta é
reproduzível, mas **não reproduz** o atraso histórico além dos 50 ms.
[Três recibos arquivados](validation/coin-render-b03-terrain-repeat-20261017/sha256.txt)
com logs completos; o quarto foi executado antes da série arquivada.

Em 17/10, a fixture visual original foi recompilada sem alteração e executada
30 vezes em processos independentes na AMD Renoir, wgpu/OpenGL, X11, terreno
de um milhão de triângulos, política nativa, 1280×960, um quadro de aquecimento
e dois medidos. Os 30 processos passaram com RGB exato entre janela e
offscreen nas capturas inicial e final. Todas as capturas iniciais tiveram
401466 pixels visíveis; as finais, 400710. Os comandos, identificação física,
hash do executável, logs e hashes das 120 imagens constam em
[results.json](validation/coin-render-b03-historical-gl-20261017/results.json).

Um controle separado forçou a primeira leitura XGetImage a ficar vazia. O
oráculo reprovou esse quadro, e quatro leituras X11 subsequentes, sem novo
render, observaram cada uma 401466 pixels visíveis. O controle confirma que o
observador detecta o quadro preto e registra a recuperação quando ela ocorre;
não demonstra que a falha histórica teve a mesma causa. Os PPMs do controle
foram preservados comprimidos. [Verificação e hashes](validation/coin-render-b03-historical-gl-20261017/verification.json),
[runner inicial](validation/coin-render-b03-historical-gl-20261017/run_initial.py)
e [continuação da série](validation/coin-render-b03-historical-gl-20261017/run_resume.py).

A superfície wgpu/OpenGL não oferece `COPY_SRC` neste perfil. Portanto, a
comparação de janela depende de XGetImage. Nem 30 passes nem o controle
injetado identificam se a ocorrência original foi uma apresentação omitida
ou uma leitura antes da atualização da janela. Para fechar B03.2, é preciso
capturar uma recorrência natural com leituras repetidas sem render adicional
e um recibo de apresentação/lifecycle no mesmo processo; então corrigir a
causa demonstrada e repetir o gate físico. B03.1 e B03.3 têm qualificações
próprias e não transferem um passe a B03.2.

Uma rodada adicional na revisão atual usou `--trace-observer` para carimbar o
início e o fim de cada `XGetImage`, com `COIN_RENDER_TRACE_PHASES=1` no wgpu.
Seis processos AMD/OpenGL com o terreno completo produziram 18/18 imagens RGB
exatas e 18 recibos de `surface_texture.present()`. Seis leituras imediatas
foram stale; as 18 leituras fixas começaram entre 50,064 e 59,477 ms após o
retorno da submissão e terminaram até 83,565 ms, todas com o frame novo. Um
controle sem a segunda submissão ficou stale na leitura fixa e até o limite de
5 s, confirmando que a instrumentação distingue ausência real de frame. O
recibo de `present()` é uma chamada da API, não uma confirmação de scanout.
[Logs e tempos por processo](validation/coin-render-b03-observer-timing-20261017/summary.json).
Essa rodada melhora a observabilidade, mas não reproduz o quadro preto natural
após 50 ms e não altera o estado aberto de B03.2.

Uma série limitada adicional com a fixture atual executou oito processos
AMD/OpenGL independentes: quatro janelas gerenciadas pelo WM e quatro com
`override_redirect`. Os 24 frames de terreno de um milhão de triângulos
tiveram leitura X11 iniciada após 50 ms fresca, imagem RGB exata contra
offscreen posterior, serial estável e recibo de `present()` em cada submissão.
As oito primeiras leituras imediatas foram stale, inclusive quando a chamada
`XGetImage` só terminou entre 55,595 e 70,532 ms após o retorno: o instante
em que a leitura **começa** importa mais que seu término. A leitura seguinte,
iniciada após 50 ms, foi fresca nos oito casos. Isto reforça a distinção
entre retorno de submissão e visibilidade X11, mas ainda não reproduz a
falha histórica na leitura iniciada após 50 ms. [Resumo e logs por processo](validation/coin-render-b03-present-stress-bounded-20261017/summary.json);
[runner reproduzível](../scripts/coinrender/run_b03_present_stress.py).
