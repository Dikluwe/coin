# Android: reutilização sem varredura VRML por quadro

A qualificação de presença VRML agora usa um `SoNodeSensor` imediato na raiz
junto da revisão do nó. Uma ausência comprovada pode ser reaproveitada sem
percorrer novamente o grafo. Notificações estruturais e mudanças relevantes
invalidam essa prova; uma atualização de câmera só a preserva quando as
qualificações existentes e a ausência de conexões tornam isso seguro.

Uma cena identificada como VRML continua sendo examinada e recapturada,
inclusive para observar alterações silenciosas que o caminho anterior já
observava. Referências `SoSFNode`/`SoMFNode`, troca e destruição de raiz,
múltiplas ações e falhas de submissão permanecem cobertas. Editar uma cena
com notificações deliberadamente desabilitadas exige o `touch()` do chamador
para invalidar caches, conforme o contrato de notificação do Coin.

`SoNode::setNodeType()` passa a notificar quando o tipo muda; repetir o mesmo
valor é inerte. Isso impede que uma transição semântica para VRML deixe uma
prova negativa válida. Não houve alteração de headers públicos ou layouts.

## Origem da regressão

A busca foi acrescentada no suporte às semânticas visuais VRML para impedir
reutilização indevida do plano. Entretanto, ficou antes da decisão de cache
e reconstruía uma tabela de nós visitados em cada `apply()`. Assim, mesmo
um frame com `plan_cache_hit=1` pagava uma travessia global. O perfil do milhão
atribuiu 96,68% das amostras de CPU à busca e suas chamadas. Os novos testes
verificam trabalho estrutural por contagem, além de imagem/payload correto;
um teste apenas funcional não detectava esse desperdício.

## Comparação do milhão no emulador

Mesma sessão do AVD x86_64 API 37, emulator 37.2.12, 8.192 MiB temporários,
GPU host AMD Renoir/Mesa 25.2.8, wgpu/OpenGL ES explícito, MSAA 1×,
1080×2400, trace desligado, câmera inicial parada e assets idênticos.
A cena contém 1.000.000 prédios mais chão, 12.000.012 triângulos.

Histórico de 07/10 e candidato foram executados na ordem histórico,
candidato, candidato, histórico, em quatro processos novos. Em cada processo,
os primeiros dez quadros foram descartados e os três grupos seguintes de
dez foram medidos: 60 quadros por variante. Não havia compilação concorrente
na rodada final. A mediana é calculada sobre as seis médias de dez quadros,
não sobre latências individuais.

| Versão | Mediana render/present CPU/wall | FPS dos grupos |
|---|---:|---:|
| APK histórico de 07/10, repetido nesta rodada | 31,31 ms | 31,30–32,62 |
| Branch antes desta correção, comparação anterior no mesmo AVD/configuração | 2.510,39 ms | 0,38–0,41 |
| Branch com qualificação por notificações | 98,27 ms | 8,87–10,55 |

A correção reduz em aproximadamente **25,5×** o tempo por quadro em relação
à medição anterior da regressão. A versão anterior à correção não foi uma
terceira variante intercalada nesta rodada. O histórico apresentou alguma
variação entre campanhas; os valores acima são os da rodada indicada.

As quatro capturas offscreen 64×64 da rodada final tiveram o mesmo conteúdo
verificado, e screenshots Android confirmaram apresentação. A comparação
offscreen usa um alvo independente, não uma leitura da janela GLES. Os tempos são
CPU/wall em `apply`/present, não timestamps GPU nem latência até a tela.
A leitura/qualificação inicial continua levando dezenas de segundos e ficou
fora dos grupos medidos. Ensaios com input durante a qualificação ou com
Activity já iniciada foram descartados e repetidos em processo novo.

No trace separado do candidato, a região de preparação da ação teve mediana
**0,01071 ms** em 27 quadros reutilizados. O plano e as instâncias GPU foram
reaproveitados, sem novo upload das instâncias. O tempo restante está
concentrado na preparação da bridge, com varreduras de
`hasPortableShaderDraws()` e comparações no transporte do frame. A versão
corrigida ainda leva aproximadamente **3,1×** o tempo do histórico; esta
mudança não encerra essa diferença nem qualifica desempenho com interação,
Android físico ou G7 ThinQ.

## Repetição

Construir o APK com `.github/scripts/build-coin-render-android-apk.py`,
`--abi x86_64 --renderer opengl --include-million-city`, Release, e alternar
com o APK histórico de 07/10. A instalação atualiza o mesmo pacote; APKs
precisam de assinaturas compatíveis, preservando código e assets do histórico.
Após cada instalação, forçar encerramento antes do lançamento para evitar
aceitar uma Activity restaurada como processo novo. Acordar a tela e fixar
retrato antes de iniciar:

```sh
adb -s emulator-5554 shell am force-stop org.coin3d.coinrender.p23
adb -s emulator-5554 shell input keyevent KEYCODE_WAKEUP
adb -s emulator-5554 shell wm dismiss-keyguard
adb -s emulator-5554 shell wm user-rotation lock 0
adb -s emulator-5554 shell am start -W \
  -n org.coin3d.coinrender.p23/android.app.NativeActivity \
  --ei coinrender_renderer 2 --ei coinrender_samples 1 \
  --ez coinrender_city true --ez coinrender_million_city true \
  --ez coinrender_continuous true --ez coinrender_trace false
```

Exigir contagem da cena, adapter `(Gl)`, tamanho e checksum esperados e
nenhum `camera_touch`. Coletar `fps_cpu_wall` e `render_present_mean_ms` dos
grupos terminados nos frames 20, 30 e 40 pelo tag `CoinRenderP23`. Não tocar
na janela durante a carga ou medição. Ao terminar, reinstalar o candidato e
restaurar `adb shell wm user-rotation free`.

Os contratos CPU estão em `CoinRenderActionTest`, incluindo aplicações
estáveis e várias alterações silenciosas consecutivas com VRML presente,
para detectar um cache incorreto que só se arma após a primeira captura.

A diferença restante foi tratada na [comparação posterior da bridge](coin-render-android-bridge-reuse-20261023.md), com nova rodada intercalada das três variantes.
