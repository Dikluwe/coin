# P23 — link, primeiro APK e validação emulada — 2026-10-07

O primeiro APK Android **x86_64** foi gerado, alinhado, assinado e instalado
no AVD deste PC. A NativeActivity abre sem extras em **wgpu/OpenGL ES** e
apresenta o cubo BASE_COLOR vermelho. Captura offscreen, retomada, rotação e
encerramento passaram. O trabalho continua em `codex/coin-render`, no checkout
permanente `/mnt/Laranja/Git/externos/coin-render`.

## Ambiente e pacote

- Source base: `07088f3165b6390a0b0868fceceb39de0f6116dd` mais o patch/snapshot
  identificado no manifesto desta campanha; ponte/protocolo continuam em 49.
- NDK r30 `30.0.16248370`, CMake/Ninja Release, Rust x86_64-linux-android,
  min API 26, target API 37, Build Tools 36.0.0 e Java do Android Studio.
- AVD `Medium_Phone_API_37.0`, Android 17/API 37, KVM utilizável,
  Emulator 37.2.12, gfxstream/AMD Renoir, Mesa 25.2.8.
- Quatro bibliotecas: `libCoin.so`, `libCoinRender.so`,
  `libcoin_render_android_smoke.so`, `libc++_shared.so`; SONAMEs Android reais.
- O ELF Coin contém **112 imports GL fracos, zero fortes**, sem DT_NEEDED
  libGL/libGLES. A remoção completa dos fontes/referências legados continua
  aberta; o perfil rejeita APIs legadas antes de inicializar estado GL.
- zipalign `-P 16` e apksigner verify passaram. A assinatura é exclusivamente
  de desenvolvimento; a chave permanece local e não é versionada.

APK local entregue:
`/mnt/Laranja/Git/externos/coin-render-artifacts/p23-apk-20261007/apk-delivery-x86_64/coin-render-p23-x86_64.apk`.
Builds, intermediários, logs e APKs de preparação também permanecem nesse
armazenamento permanente. Hashes dos binários/APK e fontes estão versionados;
o APK e as chaves não são armazenados no Git.

## Resultado da versão final

Processo qualificado: PID 8456, thread NativeActivity 8503; execução fria
após confirmar que o processo anterior terminou. O APK pediu renderer **2**
explicitamente, com `emulator_noncompliant_vulkan_opt_in=0` e
`legacy_gl_unavailable=1`. O adapter foi Android Emulator OpenGL ES Translator
sobre AMD, backend Gl, vendor 1002, device 0000. Essa identidade é emulada.

| Verificação | Resultado |
|---|---|
| Carregamento NativeActivity/bibliotecas | Passou |
| Ação GL terminada e offscreen legado recusado | Passou, depois CoinRender renderizou |
| Apresentação OpenGL ES | Cubo visível, screenshot preservada |
| Captura da mesma cena offscreen 64×64 | Centro vermelho; FNV64 `dc8224a1b44d3d8d` estável |
| Quadro normal da janela | Serial crescente e readback vazio |
| HOME e retomada | Novos quadros sem erro |
| Rotação e resize físico observado | 1080×2400 → 2400×1080 → 1080×2400 |
| Encerramento por BACK | `P23 smoke finished: OK` |
| Regressões CPU Linux com GL legado ligado | 5/5, sem falhas/skips |

A execução final produziu 11 checkpoints, com seriais
3/6/9/12/15/18/21/24/27/30/33, geração 1 e o mesmo checksum offscreen.
Geração 1 significa que não se demonstrou recriação repetida de ANativeWindow
na mesma execução. A rotação mudou o tamanho e reconfigurou a superfície.
O modo de rotação original do AVD (`free`) foi restaurado.

A superfície GLES anuncia somente `RENDER_ATTACHMENT`, sem COPY_SRC. O host
não pede captura da janela nesse perfil. A captura é **offscreen**, com alvo
separado na API pedida; screenshots evidenciam apresentação, sem constituir
comparação integral janela/offscreen ou equivalência com CoinGL. Nenhuma API
alternativa é contada como aprovação após falha de Vulkan.

## Correções integradas e tentativas preservadas

1. Coin base ainda incluía headers GL desktop. A fronteira Android instala
   declarações ABI Mesa, conserva tipos/ações CPU e usa referências GL opcionais.
   A descoberta obrigatória de desktop GL foi removida desse perfil. Ações GL,
   OffscreenRenderer e RenderManager são recusados explicitamente.
2. O primeiro controle negativo GL revelou que elementos GL eram inicializados
   antes de beginTraversal. A rejeição foi antecipada para SoAction::apply;
   o controle negativo final passou e a cena pôde seguir pelo CoinRender.
3. A criação wgpu de EGL e Vulkan na mesma ANativeWindow causava EGL_BAD_ALLOC.
   A instância Android sem recursos vivos é limitada à API explicitamente
   pedida antes de criar a superfície; instâncias/devices vivos são preservados.
4. A geometria de buffer GLES conservava o tamanho antigo após rotação. O host
   restaura a geometria base em WINDOW_RESIZED/CONFIG_CHANGED/CONTENT_RECT_CHANGED
   e depois redimensiona o target. A primeira observação de rotação foi rejeitada;
   o APK corrigido passou o critério de dimensão e retorno.
5. O APK final configura OpenGL como API inicial no CMake. O Intent permite
   solicitar Vulkan/GL explicitamente. Logs Android foram adicionados para
   distinguir adapter/capacidade/driver, respeitando logger já instalado pelo host.

Vulkan Goldfish não declara conformidade. A execução normal o rejeitou com
`No physical adapter for the requested renderer and surface`, sem fallback.
O diagnóstico opt-in, restrito a `ro.kernel.qemu=1`, alcançou o device/superfície,
mas falhou em `vulkan.ranchu.so/ResourceTracker::on_vkQueueSubmit`, também depois
de restringir a instância a Vulkan. Esse teste **falhou**, não qualifica Vulkan
no AVD nem autoriza relaxar gates físicos. O stack identifica o local da falha;
a causa completa/reprodução mínima do driver permanece estudo.

## Reprodução e evidência

O [contrato/build P23](coin-render-p23-android.md) descreve a fronteira, os
argumentos do empacotador e os comandos de execução. As dimensões base seguem
a semântica de [ANativeWindow_setBuffersGeometry](https://developer.android.com/ndk/reference/group/a-native-window#anativewindow_setbuffersgeometry),
que restaura o tamanho base quando width/height são zero.

[validation/p23-apk-linux-20261007](validation/p23-apk-linux-20261007/)
contém comandos, logs de preparação/falhas e versão final, XML Linux, ELF,
assinatura/alinhamento, screenshots e hashes. Scripts usam paths deste host;
reproduzir em diretórios novos sem substituir o pacote arquivado. O arquivo
keystore não está incluído.

Continuam abertos: APK/build arm64 desta revisão, dispositivo Android físico,
Vulkan desta imagem, API automática no Android, recriações repetidas/múltiplos
alvos, demais fixtures/formatos e retirada completa de componentes GL legados.
Não houve benchmark, qualificação de todos os nós ou equivalência visual completa.
