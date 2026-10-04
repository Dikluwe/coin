# Validação das transformações compartilhadas

Builds Release/Ninja da branch `codex/coin-render-isolation`:
`/tmp/coin-render-isolation-recording`, `/tmp/coin-render-first-frame-bgfx` e
`/tmp/coin-render-first-frame-wgpu`.

Os logs `core-tests` correspondem a este filtro CTest (testes inexistentes no build
são omitidos; FfiFrame só está no RUST_BRIDGE):

```sh
ctest --test-dir BUILD \
  -R '^(CoinBgfxCoreTest|CoinWgpuFfiFrameTest|CoinRender(TransformCore|PlanAssemblyCore|Action|FrameCore)Test)$' --output-on-failure
```

`verify-gpu.py` contém argumentos e ambiente das 16 execuções de testes GPU.
`verify-images.py` contém quatro verificações estáticas; `verify-dynamic.py` contém
quatro comparações dinâmicas com os checksums anteriormente registrados em
`/tmp/coin-render-first-frame-results/controls.json`. Todos requerem acesso ao
display/dispositivos e aos arquivos de cena indicados. Os arquivos JSON registram
cada resultado; não houve skips nesses conjuntos.

O módulo foi inspecionado com `nm -C --defined-only libCoinRender.so`: símbolos
locais de `CoinBgfxLowering` presentes no BGFX e ausentes em wgpu/RECORDING.
Os testes do adaptador continuam compilando a implementação como fonte privada.
