# Evidência Linux — hardware, superfícies e sombras

O relatório é [coin-render-hardware-surfaces-linux.md](../../coin-render-hardware-surfaces-linux.md).
`manifest.json` identifica fontes, bibliotecas/binaries, resultados qualificados,
controles negativos, divergências e tentativas excluídas. O arquivo inclui SHA-256
de cada artefato; `source/` guarda o patch e as fontes novas/modificadas.

- Sombras: `amd-bgfx-vulkan`, `amd-bgfx-opengl-final`, `amd-wgpu-vulkan-final`,
  `nvidia-bgfx-vulkan`, `nvidia-bgfx-opengl-final`, `nvidia-wgpu-vulkan`.
  A célula AMD/BGFX/Vulkan soma seu teste focado `amd-bgfx-vulkan-eight`.
- Pixels P20: `amd-p20`, `nvidia-p20`; 32 PPMs normalizados e métricas completas.
  As duas divergências AMD/Vulkan são resultados válidos fora do gate, não exclusões.
- Oito mapas: `*-eight/run.log` e `*-eight-native-negative/run.log`.
  O teste portátil conserva iluminação; o controle CoinGL recusa nove unidades
  requeridas quando há oito disponíveis.
- Câmera: logs em cada célula e `amd-*-camera-*`; PPMs de erro e
  `amd-raster-diagnostic.json`. O gate GL AMD/Vulkan continua falhando, inclusive
  na travessia completa. O contrato de reúso sem GL passa.
- Wayland: somente `*-wayland-scale1-qualified` e `*-wayland-scale2-qualified`
  integram a matriz atual. Escala observada 1/2, pixels GL/offscreen e três
  recriações são exigidos. `amd-wayland-scale-negative` é uma rejeição esperada.
- `nvidia-wrong-egl-negative` rejeita BGFX/EGL AMD quando GLX/oráculo são NVIDIA.
- `android-*` registra compilação cruzada parcial, hashes e falha de build/link
  restante. Nenhum binário Android foi executado.
- `cpu-*.log`, `cpu-*-details.log`, `python-tests.log`, `isolation-tests.log`
  guardam regressões e testes dos runners. `display-*.log` comprovam restauração.

Tentativas históricas preservadas: `amd-wgpu-vulkan` foi interrompida pelo link
na recompilação do teste e repetida completamente; `*-wayland-scale1/2` falhou
no launcher por ambiente GTK; `*-wayland-scale2-final` executou em escala 1 e
não qualifica escala 2. Os resultados anteriores P20 ficam em `prior-p20-*`.
Os tempos diagnósticos não são medidas de desempenho.

Logs, patches e snapshots preservam espaços finais e CRLF originais; a checagem
de whitespace do código/documentação exclui esse diretório de evidência bruta.
