# CoinRender examples

Build the common window/viewer examples with `COIN_BUILD_RENDER=ON` and
`COIN_BUILD_RENDER_WINDOW_EXAMPLE=ON`. Comparison/benchmark clients require
`COIN_BUILD_RENDER_BENCHMARKS=ON`; keep real CoinGL reference availability and
requested GPU/API visible when interpreting them.

- `coin_render_window_cone` and `coin_render_viewer`: common CoinRender window clients.
- `coin_render_win32_smoke`, `coin_render_appkit_smoke`, `coin_render_wayland_smoke`:
  platform-specific smoke clients, built only for applicable configurations.
- `coin_render_window_benchmark` and `coin_render_gl_benchmark`: comparison tools;
  image validation must precede throughput claims.
- [Installed SDK consumer](sdk-consumer/README.md): standalone C++/C11 package smoke.
- [Browser WebGPU demo](webgpu/README.md): separate WASM capture/browser pipeline,
  with a small explicit capability profile and its own measured limitations.

See [build/tests](../../docs/coin-render-build.md),
[current scope](../../docs/coin-render-feature-matrix.md) and
[evidence packaging](../../docs/coin-render-evidence.md). Host integration patches,
macros and campaign receipts remain outside this generic source submission.
