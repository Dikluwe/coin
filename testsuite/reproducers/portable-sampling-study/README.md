# Sampling portátil — laboratório isolado, 2026-10-07

Branch `codex/coin-portable-sampling-study`, base `f94a6c2898582d6dfd5796771392c38a541f4fb2`.
Nenhuma mudança em Coin original, nenhum relaxamento dos gates.

`COIN_SAMPLING_STUDY` é apenas um seletor experimental, lido uma vez por processo:

- `native`/ausente: sampler nativo.
- `nearest`: reconstrução nearest/trilinear por fetch, só filtro 2.
- `fetch`: reconstrução de nearest/bilinear/trilinear isotrópica.
- `center`: LOD explícito, escolha `floor` do texel, centros com sampler nativo,
  mistura explícita de dois mips; só filtro 2. Magnificação permanece linear.

Todos os modos preservam anisotropia nativa acima de 1, formatos tipados,
SRGB/HDR/BC3, estado desligado e matriz/flip RTT. Não são promessa de pixels
idênticos em qualquer geometria/GPU: raster, interpolação e derivadas ainda têm
limites. O GLSL 330 BGFX usa derivadas comuns; Naga 24 também faz fallback de
fine para comum em GLSL sem suporte. A representação temporária em tex_params.x
não é contrato público nem proposta final de API.

## Repetir

Configure builds Release com CMake e `COIN_BUILD_RENDER=ON`, `COIN_BUILD_TESTS=ON`,
`COIN_BUILD_RENDER_BENCHMARKS=ON`, `COIN_BUILD_RENDER_WINDOW_EXAMPLE=ON` e
`COIN_RENDER_BACKEND=RUST_BRIDGE`/`BGFX`. BGFX exige shaderc/package instalado.
Use `build-wgpu` e `build-bgfx` sob um diretório **permanente** de artefatos.

```sh
python3 testsuite/reproducers/portable-sampling-study/run.py --artifacts "$study_artifacts" --modes native,center,fetch
python3 testsuite/reproducers/portable-sampling-study/run.py --artifacts "$study_artifacts" --modes native,center,fetch --extra
python3 testsuite/reproducers/portable-sampling-study/run.py --artifacts "$study_artifacts" --modes native,center,fetch --alpha --output-name quality-alpha
c++ -O2 -std=c++17 testsuite/reproducers/portable-sampling-study/gpu_benchmark.cpp -o "$study_artifacts/gpu-benchmark-optimized" -lEGL -lOpenGL
python3 testsuite/reproducers/portable-sampling-study/benchmark.py --artifacts "$study_artifacts" --kind gpu --optimized
```

A fase inicial usa shader com módulo inteiro para wrap. Seus fontes/patch e
binário não devem ser misturados com a fase otimizada. Os manifests registram
hashes dos executáveis. O baseline das cidades é o build anterior da branch
coin-render; a execução de janela recebe cenas externas e registra paths exatos.
`benchmark.py` usa paths do PC e deve ser adaptado ao repetir em outro sistema.

GPU/EGL: alvo RGBA8 1280×720, 1024² checker, 1/4/8 unidades, 30 warmups,
5 grupos ×60 draws por processo, sequência intercalada, GL_TIME_ELAPSED sem
readback. Mede GPU; não mede apresentação. Na fase otimizada modo 3 é centro
para nearest e native para linear; modo 4 usa fetch com wrap por comparações.
Janela/wgpu Vulkan: 1280×720, 45 warmups, 150 quadros, amostras CSV; tempo da
chamada CPU de render/present, sem timestamps GPU ou latência de tela. Cidades
usam perspectiva/viewAll, sem texturas. A cena texturizada cobre o alvo com
plano ortográfico, textura128² e UV0..96, 1/4/8 unidades. Não compare estes
números diretamente com o AVD/Android2400×1080.

Os testes completos podem sair com 1 mesmo quando CPU/GPU coincide: a referência
CoinGL AMD diverge. FAIL continua FAIL; extrações por subcontrato são explícitas.
Falhas de linha CPU/CoinGL e esfera/UV OpenGL são preservadas, não contornadas.
Veja o relatório versionado em `docs/coin-render-portable-sampling-study-20261007.md`.

`boundary.cpp` compila com `-lEGL -lOpenGL`; recebe prefixo de saída e verifica
4.096 pixels por modo no mip3. Execute separadamente sob cada EGL vendor para
registrar native/centro/fetch e identificação real da GPU.

## Candidatos de uma amostra — 2026-10-08

`base_probe.cpp` e `run_base.py` são sondas independentes EGL/OpenGL, sem Coin.
`base`/`base_uniform`/`fine` são nomes destas sondas e não seletores implementados
na bridge. Tamanho base não basta para correspondência segura POT em mips grossos
na AMD; `fine` centraliza no mip `floor(LOD)`. As variantes usam dois centros em
NPOT. Consulte `docs/coin-render-base-sampling-counterexample-20261008.md`.
O controle `--derived` calcula LOD8 das UV originais e valida todos os pixels.
Não se mediu desempenho nem magnificação nesta sonda; são próximos controles.
