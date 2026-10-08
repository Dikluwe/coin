# Sampling portátil — laboratório isolado, 2026-10-07

Branch `codex/coin-portable-sampling-study`, base `f94a6c2898582d6dfd5796771392c38a541f4fb2`.
Nenhuma mudança em Coin original, nenhum relaxamento dos gates.

`COIN_SAMPLING_STUDY` é apenas um seletor experimental, lido uma vez por processo:

- `native`/ausente: sampler nativo.
- `nearest`: reconstrução nearest/trilinear por fetch, só filtro 2.
- `fetch`: reconstrução de nearest/bilinear/trilinear isotrópica.
- `fine`: centro no mip `floor(LOD)`, uma amostra nearest/trilinear em POT;
  NPOT usa os dois centros. Só filtro 2 isotrópico, magnificação linear.
- `fine_uniform`: mesmo algoritmo, dimensões dos mips derivadas de uniforme com
  o tamanho **do recurso ligado**, inclusive RTT.
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
`base`/`base_uniform` são nomes exclusivos destas sondas. `fine`/`fine_uniform`
também foram integrados na bridge nesta continuação isolada. Tamanho base não basta para correspondência segura POT em mips grossos
na AMD; `fine` centraliza no mip `floor(LOD)`. As variantes usam dois centros em
NPOT. Consulte `docs/coin-render-base-sampling-counterexample-20261008.md`.
O controle `--derived` calcula LOD8 das UV originais e valida todos os pixels.
Não se mediu desempenho nem magnificação nesta sonda; são próximos controles.


## Campanha integrada fine/fine_uniform

```sh
python3 testsuite/reproducers/portable-sampling-study/run.py --artifacts "$study_artifacts" --modes native,center,fine,fine_uniform --deep --output-name quality-deep
python3 testsuite/reproducers/portable-sampling-study/run.py --artifacts "$study_artifacts" --modes native,center,fine,fine_uniform --rtt-study --output-name quality-direct
python3 testsuite/reproducers/portable-sampling-study/run.py --artifacts "$study_artifacts" --modes native,center,fine,fine_uniform --viewport-study --output-name quality-viewport
python3 testsuite/reproducers/portable-sampling-study/run.py --artifacts "$study_artifacts" --modes native,center,fine,fine_uniform
python3 testsuite/reproducers/portable-sampling-study/run.py --artifacts "$study_artifacts" --modes fine,fine_uniform --extra --output-name quality-extra
python3 testsuite/reproducers/portable-sampling-study/run.py --artifacts "$study_artifacts" --modes fine_uniform --resources --output-name quality-resources
c++ -O2 -std=c++17 testsuite/reproducers/portable-sampling-study/gpu_benchmark.cpp -o "$study_artifacts/gpu-benchmark-fine" -lEGL -lOpenGL
python3 testsuite/reproducers/portable-sampling-study/benchmark_fine.py --artifacts "$study_artifacts" --kind gpu
python3 testsuite/reproducers/portable-sampling-study/benchmark_fine.py --artifacts "$study_artifacts" --kind window
python3 testsuite/reproducers/portable-sampling-study/benchmark_fine.py --artifacts "$study_artifacts" --kind city --backends wgpu
python3 testsuite/reproducers/portable-sampling-study/summarize_fine.py --artifacts "$study_artifacts"
```

`--deep`: 162 casos por processo, mip profundo/fractional, magnificação, wrap,
clamp, unidade0/7/ambas, POT quadrado/retangular, NPOT, RGBA8/SRGB/HDR/BC3.
`--rtt-study`: 48 consumidores privados explícitos filter2 com recursos diretos
RGBA8/RGBA16F e mudança de tamanho. SoSceneTexture2 público usa linear/trilinear
(filter1/3), e não ativa fine: o gate público RTT continua separado e intacto.
GPU/EGL: modos0/3/5/6 = native/center/fine/fine_uniform; 30 warmups, 5×60 draws.
Janela: baseline da produção, native do estudo e três candidatos, ordem direta
mais inversa; 45 warmups e150 quadros medidos, wgpu/BGFX Vulkan AMD/NVIDIA.
`--viewport-study` repete36 casos POT/NPOT com viewport24×18 na origem(4,5),
comparando o interior físico sem mudar os gates.
Os CSV contêm warmups: o agregador exclui `warmup=1`, mede `render_present_ms`.
Cenas POT128² e NPOT129×127, 1/8 unidades. EGL reutiliza uma imagem1024² entre
as unidades; cache quente, não representa streaming nem texturas gerais.
Não execute workloads GPU simultaneamente. As cenas e baseline paths são locais:
`scenes-fine`, builds wgpu/BGFX e executável EGL devem existir antes da campanha.

Antes/depois de cada benchmark de janela, registra-se DPMS. Monitor Off é
reativado com `xset dpms force on`, sem alterar preferências ou desbloquear a
sessão. Medições cujo monitor não permaneceu On são excluídas do agregador;
o ensaio anterior com DPMS Off foi preservado separadamente. `--resume` evita
repetir logs concluídos quando uma configuração interrompe uma campanha.

## Separação do caminho nativo — 2026-10-08

O laboratório agora compila native sem helpers de sampling nem o apêndice de
128 bytes. Wgpu liga o prefixo original de3040 bytes (stride3072 quando o
alinhamento é256); o perfil experimental usa3168/3328. Instancing sem textura
sempre usa o perfil nativo, inclusive sob fine_uniform. BGFX compila fragmentos
nativos e experimentais separados, não cria/envia o uniforme extra no nativo
e remove o apêndice dos draws. Só fine_uniform consulta os tamanhos ligados.
O seletor continua privado e fixo por processo; isto ainda não é API pública.

```sh
python3 testsuite/reproducers/portable-sampling-study/run.py --artifacts "$study_artifacts" --modes native,fine_uniform --resources --output-name quality-resources
python3 testsuite/reproducers/portable-sampling-study/run.py --artifacts "$study_artifacts" --modes native,fine_uniform --shadow --output-name quality-shadow
python3 testsuite/reproducers/portable-sampling-study/benchmark_fine.py --artifacts "$study_artifacts" --kind window --modes baseline,original-native,native,fine_uniform
python3 testsuite/reproducers/portable-sampling-study/investigate_native.py --artifacts "$study_artifacts" --phase before
python3 testsuite/reproducers/portable-sampling-study/investigate_native.py --artifacts "$study_artifacts" --phase after --candidate-build "$study_artifacts/build-wgpu"
python3 testsuite/reproducers/portable-sampling-study/analyze_native.py --artifacts "$study_artifacts"
```

Antes de rebuild, copie o executável e suas bibliotecas para original-study/bin
e original-study/lib, registrando hashes/revisão. O runner fixa LD_LIBRARY_PATH
por filho; sem isso o RUNPATH absoluto do executável copiado carrega o rebuild.
O baseline usa os builds de produção registrados nos comandos. O controle do
milhão executa todas as seis permutações dos três modos (18 processos por fase),
45 warmups/150 quadros e telemetria NVIDIA. --resume preserva o ensaio rejeitado
por DPMS/erro em rejected-monitor-off e repete apenas controles inválidos.

COIN_SAMPLING_AUDIT imprime hash/bytes dos uniformes, instâncias, draws e
unidades. Para a sonda GPU de janela, habilite também COIN_WGPU_TRACE_PHASES=1
e COIN_WGPU_GPU_TIMESTAMPS=1. Requer TIMESTAMP_QUERY e
TIMESTAMP_QUERY_INSIDE_ENCODERS; a saída COIN_SAMPLING_AUDIT_GPU mede o trecho
do encoder. Resolve/copia timestamps e espera sincronicamente: use processos
separados e exclua-os das medianas normais de render/present. Não é medição
de latência de tela nem profiler assíncrono de produção.

Relatório: docs/coin-render-native-sampling-path-study-20261008.md.

## Correção das 28 falhas — 2026-10-08

A continuação mantém as tolerâncias e separa correções de geometria/profundidade
do workaround **externo do Mesa**, salvo em [mesa/README.md](mesa/README.md).
O ledger individual e os controles A/B ficam no relatório
`docs/coin-render-failure-closure-20261008.md`. Coin original, a branch principal
coin-render e o driver instalado continuam intactos.
