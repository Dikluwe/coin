# Android: preparação da bridge com planos imutáveis do Coin

A diferença restante de 3,1× foi removida no cenário estável do milhão.
A preparação e a submissão agora reutilizam a prova de imutabilidade do plano
privado da ação, cuja validade depende das notificações e revisões do Coin.
Antes, mesmo com um plano reaproveitado, a bridge voltava a examinar estados,
draws e payloads que não tinham mudado.

A autoridade vale somente durante a execução síncrona do snapshot privado.
Revisões e hints enviados por consumidores independentes continuam exigindo
validação. A prova é renovada após recaptura e invalidada em falhas ou mudanças
de configuração, tamanho, recurso ou dispositivo. O caminho VRML conserva a
recaptura necessária para observar as alterações silenciosas já suportadas.

A bridge usa o caminho existente de payload pertencente ao Rust para cenas
instanciadas elegíveis. Se outro alvo substituiu esse payload, repete a chamada
com dados completos somente após a rejeição específica que ocorre antes de
qualquer trabalho GPU. Não houve mudança da ABI raw nem dos headers/layouts
públicos do Coin; a comparação dos símbolos exportados da biblioteca legada
não encontrou símbolos removidos ou acrescentados.

## Resultado no mesmo emulador

AVD Medium_Phone_API_37.0 x86_64, emulator 37.2.12, 8.192 MiB temporários,
host AMD Renoir/Mesa 25.2.8, wgpu/OpenGL ES explícito, MSAA 1×, 1080×2400.
Cena com 1.000.000 prédios mais chão e 12.000.012 triângulos, câmera inicial
parada, assets idênticos e trace desligado durante a comparação.

Foram seis processos novos, na ordem histórico, baseline, candidato,
candidato, baseline, histórico. Em cada processo foram descartados os dez
primeiros quadros e medidos os três grupos seguintes de dez: 60 quadros por
variante, sem compilações concorrentes. A mediana abaixo é das seis médias de
grupos, não das latências individuais.

| Versão | Mediana render/present CPU/wall | FPS dos grupos |
|---|---:|---:|
| Histórico de 07/10, repetido nesta rodada | 30,684 ms | 31,76–32,99 |
| Baseline com correção VRML, `bcabed423f` | 97,592 ms | 7,49–10,50 |
| Candidato com reutilização da bridge, `1cc511b2d6` | 20,676 ms | 45,80–48,73 |

O candidato reduz o tempo por quadro em **4,72×** frente ao baseline desta
rodada e em **1,48×** frente ao histórico. Todas as seis verificações offscreen
produziram conteúdo idêntico; screenshots também confirmaram apresentação.
A leitura offscreen é independente da janela GLES. Os tempos incluem espera
no render/present e não são timestamps GPU ou latência até aparecer na tela.
A carga inicial continua levando dezenas de segundos e não integra esses
grupos; a correção mede a execução estável, sem interação ou recaptura.

## Orçamento para 120 FPS

120 FPS exige processar um quadro em 8,33 ms, independentemente da frequência
que a tela do emulador anuncia. Um trace separado, sem compilações concorrentes,
confirmou 77 submissões com plano imutável, payload Rust reaproveitado e nenhuma
recuperação por payload ausente. Não houve nova validação por estado/draw,
comparação de bytes de textura ou upload das instâncias nesse caminho.

As medianas de regiões CPU/wall foram: preparação da ação 0,0134 ms;
validação do alvo 0,0020 ms; preparação do alvo 0,00032 ms; custo C++ da bridge,
estimado subtraindo a região Rust da região do alvo, 0,1213 ms; região Rust
excluindo present 3,2785 ms. A preparação está abaixo de 8,33 ms. As medianas
são de regiões distintas e não devem ser somadas como latência medida de um
único quadro. O present teve mediana de 17,7559 ms e inclui espera no driver,
na GPU ou na apresentação; retirá-lo não prova throughput GPU de 120 FPS.

O ensaio adicional com `COIN_RENDER_BENCH_NO_VSYNC=1`, injetado por `wrap.sh`
em APK temporário mantendo os binários e assets, foi recusado explicitamente:
a superfície GLES não oferece Immediate nem Mailbox. Não houve fallback
silencioso para um modo sem vsync. O APK normal foi reinstalado para a comparação
final e a rotação foi restaurada. Logo, **120 FPS de processamento completo
não foi demonstrado**; o resultado completo medido é 45,80–48,73 FPS. O tempo
restante pede investigação do render/present, sem confundi-lo com o custo da
preparação ou com os Hz anunciados pela tela. Android físico/G7 ThinQ e
comportamento sob interação permanecem fora desta medição.

## Repetição e cobertura

Usar o procedimento e os extras de lançamento do
[ensaio anterior](coin-render-android-vrml-reuse-20261023.md#repetição), com
as três variantes acima, encerrando o pacote após cada instalação e antes
de cada lançamento. Exigir adapter `(Gl)`, contagem da cena, câmera inicial
sem input e dimensão esperada. O APK histórico foi assinado com a mesma chave
de desenvolvimento, mantendo idênticas todas as entradas fora de META-INF.

A validação final passou em 43 testes CTest do backend real RUST_BRIDGE,
no controle portable adicional e em 61 testes Rust (quatro ignorados pela
suíte). Inclui VRML contra referência CoinGL, entradas mutáveis com mesma
revisão, textura/mip, alternância de alvos, publicação e tickets assíncronos.
Os seis mutantes direcionados foram detectados: autoridade por revisão,
classificação portable, mip ignorado, opção de instancing ignorada, payload
não nulo aceito sem comparação e remoção da recuperação entre alvos.
O Rust foi executado serialmente com Mesa; a tentativa paralela inicial teve
conflito de contexto EGL em teste GPU, eliminado na execução serial.
