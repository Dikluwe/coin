# Validação do builder

Builds Release/Ninja RECORDING, BGFX e RUST_BRIDGE da etapa de isolamento.

```sh
ctest --test-dir /tmp/coin-render-isolation-recording \
  -R '^CoinRender(PlanAssemblyCore|Action|BackendBoundary|Texture|FrameCore|IndexedGeometryCore)Test$' --output-on-failure
ctest --test-dir /tmp/coin-render-first-frame-bgfx \
  -R '^CoinRender(PlanAssemblyCore|Action|BackendBoundary|FrameCore|IndexedGeometryCore)Test$' --output-on-failure
ctest --test-dir /tmp/coin-render-first-frame-wgpu \
  -R '^CoinRender(PlanAssemblyCore|Action|BackendBoundary|FrameCore|IndexedGeometryCore)Test$' --output-on-failure
```

`verify-gpu.py` contém executáveis e ambiente dos dez testes GPU obrigatórios.
`verify-images.py` verifica as quatro variantes na cena de 40 mil edifícios.
Ambos requerem acesso ao display/dispositivos. `gpu.json` e `images.json` registram
os resultados. Os tempos curtos nos logs de benchmark não são uma nova medição
estatística de desempenho.

`stage2-recording-texture-baseline.log` registra a expectativa antiga de SCREEN_DOOR
falhando também com os arquivos do builder/ImageCore de `15c21e097a`. A expectativa
foi corrigida; `recording-core-tests.log` registra a passagem do teste atual.
`core-final-test.log` inclui a verificação final de índices inválidos no template.
