# P23 — Android/NDK (rota preparada, sem qualificação nativa)

P23 compõe o lifecycle do host Android com o `CoinRenderTarget` existente. O
Wiring aceita um `ANativeWindow` emprestado em
`COIN_RENDER_SURFACE_ANDROID_NDK`; a ponte wgpu o transforma em
`RawWindowHandle::AndroidNdk`. O Core continua dono de travessia capturada,
composição e publicação; nenhum estado Coin é reinterpretado para Android.
`COIN_RENDER_EXPERIMENTAL_ANDROID_WINDOW` permite consultar o adaptador ativo,
sem transformar o probe em qualificação de apresentação.

O host deve executar criação, resize, apply e destruição na thread que processa
os comandos de janela. Em `APP_CMD_TERM_WINDOW`, solta a referência da action
e destrói o target **antes** de o NDK invalidar `ANativeWindow`. Um novo
`APP_CMD_INIT_WINDOW` cria outro target para a janela nova. Em `APP_CMD_PAUSE` ou
`APP_CMD_STOP`, suspende os submits; em `APP_CMD_RESUME`, renderiza se ainda há
janela. O resize usa `ANativeWindow_getWidth/Height`, em pixels físicos. Não é
necessária uma API paralela de detach/reattach: o novo target recebe a mesma
cena e cria seus próprios recursos e estado de apresentação.

O [smoke NativeActivity](../examples/coinrender/coin_render_android_smoke.cpp)
implementa esse fluxo com `android_native_app_glue`. Pede Vulkan, apresenta
uma cena opaca, captura RGBA8 e verifica cor central/checksum, submete outro
quadro sem readback, registra geração/serial e consulta renderer/vendor/device.
O [manifesto de exemplo](../examples/coinrender/AndroidManifest-p23.xml) identifica
a biblioteca nativa. A ponte Rust/CMake mapeia `arm64-v8a`, `armeabi-v7a`,
`x86_64` e `x86` para seus targets Rust e usa o compilador C do NDK como linker
Cargo. O app precisa empacotar `libCoinRender.so`, `libCoin.so` e suas
dependências para a ABI escolhida.

Para tentar o build em um host com Android NDK e Rust target instalados:

```sh
rustup target add aarch64-linux-android
cmake -S . -B build-android \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-26 \
  -DCOIN_BUILD_RENDER=ON -DCOIN_RENDER_BACKEND=RUST_BRIDGE \
  -DCOIN_BUILD_RENDER_WINDOW_EXAMPLE=ON \
  -DCOIN_BUILD_LEGACY_GL_RENDERER=OFF
cmake --build build-android --target coin_render_android_smoke
```

Neste host não há NDK, target Rust Android nem `adb`. A checagem sintática do
smoke com headers NDK mínimos simulados passou; CoinRender wgpu e os testes de
seleção/superfície passaram no Linux. Esses resultados **não** provam build
Android nem execução GPU. O build cross-target, link final, empacotamento APK,
instalação e execução ainda precisam ser verificados em host/dispositivo Android.

## Fechamento por evidência

- Compilar e empacotar a ABI do dispositivo; registrar Android API, SoC/GPU,
  driver Vulkan, formato de superfície e hashes dos binários.
- Forçar foreground/background, rotação e perda/recriação de `ANativeWindow`.
  Verificar que cada geração apresenta e captura, sem usar handle destruído nem
  publicar frame incompleto; testar `APP_CMD_TERM_WINDOW` antes/depois de pause.
- Conferir serial, ausência de readback no quadro normal, resize em pixels,
  memória/recursos após ciclos repetidos e diagnóstico de device/surface lost.
- Comparar fixtures Coin com um oráculo válido e tolerâncias por combinação
  dispositivo/API/alvo. Skips explícitos para APIs/formatos indisponíveis.

A célula que exige Android/ARM permanece no
[registro de validação externa](coin-render-platform-validation-pending.md).
