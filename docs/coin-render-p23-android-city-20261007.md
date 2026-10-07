# P23 — cidade Android interativa de 40.000 prédios

Em 2026-10-07, o APK x86_64 passou no AVD API 37 deste Linux, por
**wgpu/OpenGL ES**. A janela gráfica do emulador foi aberta e permaneceu na
cidade ao concluir. O launcher abre a cidade por padrão, sem Intent extra;
o cubo do smoke anterior depende de seleção explícita.

## Cena e controles

O pacote inclui `assets/city-40000.iv`: grade 200 × 200, seed 136,
SHA-256 `bb9ccc612cd38ffb749ee599d9350a80974649eab5bf477d2f344538a42b2576`. É o mesmo arquivo das campanhas
anteriores, com PHONG, duas luzes direcionais, materiais e transforms do Coin.
SoDB lê o asset; SoSearchAction confirma **40.001 ocorrências de Cube**:
40.000 prédios e um chão, equivalentes a **480.012 triângulos**. Compartilhar
nós de cena não implica que todo backend use instancing GPU.

Arrastar horizontalmente gira a câmera em torno do ponto focal; arrastar
para cima aproxima e para baixo afasta. No emulador, usar botão esquerdo do
mouse. O zoom limita a distância e atualiza os planos de corte. A posição
final de UP também é aplicada, para preservar arrastes curtos quando eventos
MOVE forem agrupados entre quadros lentos.
A câmera é reenquadrada por `viewAll` ao recriar a janela; a distância de zoom
não é preservada nesse evento.

## Resultado integrado do APK de entrega

- Build/link Android, assinatura e alinhamento 16 KB passaram.
- MAIN/LAUNCHER sem extras carregou 40.000 prédios e renderer 2.
- Giro e zoom chegaram à câmera, incluindo gestos de 600 ms. As capturas
  antes/giro/zoom foram inspecionadas. Houve interação manual durante a
  execução; ela não é uma campanha isolada de desempenho.
- Três HOME/retornos produziram TERM_WINDOW/INIT_WINDOW reais: gerações
  **1 → 2 → 3 → 4**, PID 5471/thread nativa 5497. O alvo offscreen
  independente permaneceu vivo no Host e voltou a capturar após cada retorno.
- 12 checkpoints na primeira Activity e 24 no processo,
  com seriais crescentes, RGBA 64 × 64 completo e conteúdo presente; o quadro
  normal não publicou readback de janela.
- Dimensões reais de janela 1080 × 2400 e 2400 × 1080 foram observadas.
- Duas reaberturas após BACK criaram novas threads no **mesmo PID** e
  apresentaram a cidade. O encerramento liberou o runtime ocioso, com epochs
  1/2/3/4 registrados; IDs globais de tickets/tokens/submissão não são zerados.
- O cubo selecionado explicitamente manteve checksum `dc8224a1b44d3d8d`;
  BACK e launcher normal voltaram à cidade. Quatro encerramentos deram `OK`.
- Cinco controles CPU Linux passaram após recompilar os fontes atuais no
  backend RECORDING; isso não é um gate GPU Linux.

GLES não oferece COPY_SRC de janela neste AVD: captura offscreen e screenshot
são evidências separadas, não equivalência integral de pixels. Avisos EGL e
mensagens `Fake map` do wgpu estão preservados. A execução final não registrou
Fatal signal, falha do smoke ou exceção AndroidRuntime.

## Falha encontrada e fronteira de encerramento

Antes da correção, BACK encerrava a thread nativa mas deixava o device/EGL
compartilhado do processo vivo. Reabrir em outra thread produziu SIGFPE no
`GL2Encoder::s_glBindBufferRange` do GLES do emulador. A pilha está arquivada;
não há um reproducer mínimo independente nem diagnóstico completo do driver.

A correção introduz um hook **privado, somente Android**, chamado após o
último alvo sair do registro e liberar seu backend. O runtime recusa teardown
com superfícies, tickets, aposentadorias pendentes, devices explícitos ou RTT
publicado vivos. No caso ocioso espera a fila, destrói o runtime na thread
que termina e avança o epoch para a próxima Activity. A lib não troca API.
O objeto Infra continua com lifetime do processo; ABI de estruturas permanece
49. O offscreen sobrevivente mantém o registro não vazio durante HOME/retorno.
A retenção de tickets/RTT através da troca de thread ainda exige campanha
específica; não foi qualificada por este smoke síncrono.

A tentativa anterior com launcher padrão do cubo também está preservada.
`ui-diagnostic-full.log`, `launcher-log.log` e `relaunch-crash.log` são falhas
anteriores excluídas dos passes. Não foram apagadas nem agregadas à entrega.

## Desempenho e estudos abertos

As médias funcionais em grupos de dez quadros ficaram próximas de **1 FPS**,
com cerca de **0,9–1,0 s** por render/present. Inicialização, capturas, eventos
ou interação entram em `fps_cpu_wall`; os valores não são timestamps GPU ou
latência até a tela, nem qualificação de desempenho de Android físico.

**Estudo futuro:** perfilar captura/lowering, quantidade de draws, uploads,
instancing GLES e gfxstream antes de escolher a otimização. Também seguem
abertos preservação de câmera entre janelas recriadas, duas janelas, API AUTO,
tickets entre threads, outros formatos/fixtures, Vulkan e Android físico/ARM.

## Entrega e reprodução permanentes

APK de desenvolvimento: `/mnt/Laranja/Git/externos/coin-render-artifacts/p23-city-20261007/apk-controls-x86_64/coin-render-p23-x86_64.apk`.
SHA-256: `aa5e3c4aaaa64bcdcb677d361084258be336c088216b4ea3ec8eaa6f46d5e0df`.
[Evidência e hashes](validation/p23-android-city-20261007/manifest.json).

```sh
python3 .github/scripts/build-coin-render-android-apk.py \
  --sdk "$HOME/Android/Sdk" \
  --ndk "$HOME/Android/Sdk/ndk/30.0.16248370" \
  --build-dir /mnt/Laranja/Git/externos/coin-render-artifacts/p23-apk-20261007/build-x86_64 \
  --output-dir /mnt/Laranja/Git/externos/coin-render-artifacts/p23-city-20261007/apk-new-x86_64 \
  --java-home "$HOME/android-studio/jbr"
adb -s emulator-5554 install --no-incremental -r /path/to/coin-render-p23-x86_64.apk
adb -s emulator-5554 shell am force-stop org.coin3d.coinrender.p23
# Confirmar pidof vazio antes de iniciar.
adb -s emulator-5554 shell am start -W \
  -n org.coin3d.coinrender.p23/android.app.NativeActivity
```

Para o cubo, usar cold-start com `--ez coinrender_city false
--ez coinrender_continuous false`. A Activity é singleTask; mudar fixture
exige encerrá-la antes. Vulkan permanece explícito por `--ei coinrender_renderer 1`.
O roteiro `qualify-final.py` e comandos usados estão arquivados. Ele usa caminhos
deste host e requer um novo output ao reproduzir.

A janela do emulador usa a seleção AMD da campanha anterior, sem `-no-window`.
O teste usa `wm user-rotation lock`; `adb shell wm user-rotation free` restaura
o estado anterior. Interação no toolbar do emulador pode alterar a orientação.
APK/evidência anteriores ficaram em seus próprios diretórios permanentes.
