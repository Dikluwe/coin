# P23 — primeiro APK Android e perfil de emulação

Em 2026-10-07, o link completo de Coin, CoinRender/wgpu e NativeActivity passou
no NDK r30/API 26, ABI **x86_64**. O APK de desenvolvimento foi alinhado para
16 KB, assinado, instalado e executado no AVD `Medium_Phone_API_37.0`, Android
17/API 37, em KVM/gfxstream sobre AMD Renoir/Mesa 25.2.8. O perfil inicial do
APK pede **OpenGL ES pelo backend wgpu**, explicitamente. Vulkan permanece
selecionável; não há troca automática de API após falha.

[Relatório e evidência](coin-render-p23-apk-validation-20261007.md).

## Fronteira do Coin base

`COIN_ANDROID_CPU_ONLY` é derivado de Android com
`COIN_BUILD_LEGACY_GL_RENDERER=OFF`. O perfil mantém registro de tipos, nós,
campos, ações CPU e geração de primitivas usados pela travessia CoinRender.
As declarações escalares/tokens da ABI GL são instaláveis, com proveniência
Mesa/libglvnd registrada. As referências GL remanescentes são **indefinidas
fracas**; as bibliotecas não têm DT_NEEDED libGL/libGLES. O backend wgpu carrega
EGL/GLES reais do Android durante sua execução.
Não existe implementação de funções GL para fabricar renderização.

`SoAction::apply` rejeita ações GL antes de `getState()`, porque a inicialização
de elementos GL já executa chamadas do driver. `SoGLRenderAction`,
`SoOffscreenRenderer`, `SoRenderManager` e a descoberta de desktop GL também
tratam a indisponibilidade. O controle negativo no APK exige ação terminada e
render offscreen legado recusado; depois a mesma cena passa por CoinRender.

Isso conclui o primeiro link/APK. **A remoção completa dos fontes e referências
GL da biblioteca base continua aberta**. APIs GL diretas/elementos GL não são
suportados neste perfil; não usar essa ABI como renderer legado no Android.
Os logs arm64 anteriores documentam a compilação parcial; não há APK arm64 ou
validação física Android/ARM nesta entrega.

## Build e pacote em armazenamento permanente

Pré-requisitos: NDK r30, target Rust `x86_64-linux-android`, SDK Platform
`android-37.0`, Build Tools 36.0.0 e Java. O empacotador usa CMake/Ninja,
NativeActivity e aapt2/zipalign/apksigner. Mantém uma chave **de desenvolvimento**
no build-dir e empacota Coin, CoinRender, smoke e libc++ compartilhada. A chave
não é artifact versionado nem chave de publicação.

```sh
python3 .github/scripts/build-coin-render-android-apk.py \
  --sdk "$HOME/Android/Sdk" \
  --ndk "$HOME/Android/Sdk/ndk/30.0.16248370" \
  --abi x86_64 --renderer opengl \
  --build-dir /mnt/Laranja/Git/externos/coin-render-artifacts/p23-apk-20261007/build-x86_64 \
  --output-dir /mnt/Laranja/Git/externos/coin-render-artifacts/p23-apk-20261007/apk-delivery-x86_64 \
  --java-home "$HOME/android-studio/jbr"
```

O script também aceita `--abi arm64-v8a` e `--renderer vulkan`; esses argumentos
não são evidência de build ou execução qualificada dessas células. Usar outro
output-dir para uma campanha nova; não sobrescrever os artifacts arquivados.

```sh
adb -s emulator-5554 install --no-incremental -r /path/to/coin-render-p23-x86_64.apk
adb -s emulator-5554 shell am start -W -f 0x10008000 \
  -n org.coin3d.coinrender.p23/android.app.NativeActivity
```

O default do APK fornecido é OpenGL ES. `--ei coinrender_renderer 1` solicita
Vulkan; `2` solicita OpenGL ES. Para um cold-start verificável, parar o pacote
e aguardar `pidof` vazio antes de iniciar, evitando logs de instâncias anteriores.

## Superfície e captura

O Wiring recebe um `ANativeWindow` emprestado em
`COIN_RENDER_SURFACE_ANDROID_NDK`, e a ponte usa `AndroidNdkWindowHandle`.
A criação Android restringe a instância **sem recursos vivos** à API pedida
antes de criar a superfície. A tentativa anterior de criar EGL e Vulkan na
mesma janela produzia `EGL_BAD_ALLOC`; não se substitui instância/device ativo.

Em OpenGL ES o host configura buffers RGBA8. A superfície anuncia apenas
`RENDER_ATTACHMENT`, sem `COPY_SRC`: o smoke apresenta dois quadros normais,
exige serial crescente/ausência de readback da janela e captura a mesma cena
em offscreen 64×64. O log declara `capture_scope=offscreen`; checksum/centro
vermelho e screenshot de apresentação não são equivalência integral de pixels
janela/offscreen. Vulkan pede captura da janela, quando suportada.

## Lifecycle e limites

Criar, resize, apply e destruir permanecem na thread dos comandos Android.
`TERM_WINDOW` solta action/target antes de invalidar a janela; `INIT_WINDOW`
cria outro target. PAUSE/STOP suspendem submissões, RESUME permite renderizar.
O resize restaura a geometria base da janela GLES antes de consultar dimensões,
incluindo `CONTENT_RECT_CHANGED`; isso evita conservar o tamanho de buffer
anterior após rotação. Rotação e retomada são
qualificadas somente pelas execuções registradas no relatório.

O adapter Vulkan Goldfish não declara conformidade e o wgpu o oculta na
execução normal. O Intent `--ez coinrender_allow_noncompliant_vulkan true`
permite diagnóstico apenas quando `ro.kernel.qemu=1` e Vulkan foi solicitado.
A opção é impressa no log, preserva rejeição de adapter CPU e não habilita um
perfil físico. Nesta imagem, a tentativa explícita falha em
`vulkan.ranchu.so/ResourceTracker::on_vkQueueSubmit`; permanece aberta.

Continuam externos Android/ARM físico, drivers/formatos adicionais, comparação
visual completa, perda real de device e demais fixtures. O
[registro externo](coin-render-platform-validation-pending.md) mantém essas células.
