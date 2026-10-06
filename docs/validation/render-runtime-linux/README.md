# Reprodução da segunda etapa

Fonte: `/tmp/coin-render-first-frame`, branch `codex/coin-render-isolation`.
Builds Release/Ninja com testes ligados:

- RECORDING: `/tmp/coin-render-isolation-recording`.
- BGFX: `/tmp/coin-render-first-frame-bgfx`.
- RUST_BRIDGE: `/tmp/coin-render-first-frame-wgpu`.

Em cada build, os logs `boundary-tests` correspondem a:

```sh
ctest --test-dir BUILD \
  -R '^CoinRender(Action|BackendBoundary|RuntimeBoundary|Product)Test$' --output-on-failure
```

`lifetime-test` registra a execução final do teste de runtime, incluindo a ordem de
liberação de superfície/executor, nos três builds.

`verify-gpu-tests.py` contém os executáveis, argumentos e variáveis de ambiente das
17 execuções de testes GPU e dos dois exemplos de janela. Requer acesso aos
dispositivos e ao display X11 do host. `gpu-tests.json` registra cada resultado.
Os logs não contêm skips ou erros de renderização do exemplo.

`verify-images.py` contém os comandos das quatro variantes de benchmark. Requer
`/tmp/coin-render-city-40000.iv`, os benchmarks compilados e acesso à GPU/display.
`images.json` compara seus checksums com a etapa anterior. Tempos nos logs vêm de
uma execução curta de preservação da saída, sem avaliação estatística de desempenho.
