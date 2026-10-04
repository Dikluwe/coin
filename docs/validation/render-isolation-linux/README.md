# Reprodução da validação de isolamento

Fonte: worktree `/tmp/coin-render-first-frame`, branch `codex/coin-render-isolation`.
Builds Release/Ninja com `COIN_BUILD_RENDER=ON`, `COIN_BUILD_TESTS=ON`:

- `/tmp/coin-render-isolation-recording`: `COIN_RENDER_BACKEND=RECORDING`.
- `/tmp/coin-render-first-frame-bgfx`: `COIN_RENDER_BACKEND=BGFX`.
- `/tmp/coin-render-first-frame-wgpu`: `COIN_RENDER_BACKEND=RUST_BRIDGE`.

```sh
ctest --test-dir /tmp/coin-render-isolation-recording \
  -R '^CoinRender(BackendBoundary|Action|Product|FrameCore|FrameReuseCore)Test$' --output-on-failure
```

Nos builds BGFX e wgpu, `boundary-tests.log` corresponde a:

```sh
ctest --test-dir BUILD \
  -R '^CoinRender(BackendBoundary|Action)Test$' --output-on-failure
```

Testes GPU usam as seguintes variáveis; requerem acesso ao display e aos dispositivos:

```sh
export __NV_PRIME_RENDER_OFFLOAD=1
export __GLX_VENDOR_LIBRARY_NAME=nvidia
export VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/nvidia_icd.json
export COIN_GLX_PIXMAP_DIRECT_RENDERING=1
COIN_BGFX_RENDERER=vulkan COIN_RENDER_REQUIRE_BGFX_SHADOW_GPU=1 \
ctest --test-dir /tmp/coin-render-first-frame-bgfx \
  -R '^(CoinRender(Product|FrameCore|FrameReuseCore|ShadowReference)Test|CoinBgfxReadbackModes_vulkan|CoinBgfxSurface_vulkan_object)$' --output-on-failure
WGPU_BACKEND=vulkan COIN_RENDER_REQUIRE_WGPU_SHADOW_GPU=1 \
ctest --test-dir /tmp/coin-render-first-frame-wgpu \
  -R '^CoinRender(Product|FrameCore|FrameReuseCore|ShadowReference|AsyncAction|AsyncReadback)Test$' --output-on-failure
```

`verify-images.py` registra todos os argumentos e o ambiente das quatro verificações
estáticas. Requer a cena `/tmp/coin-render-city-40000.iv` e os benchmarks compilados.
Os logs de benchmark incluem tempos de uma execução curta, sem comparação estatística
de desempenho nesta etapa. `images.json` compara a saída com os checksums anteriores.
