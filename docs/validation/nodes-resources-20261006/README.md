# Nós, FreeCAD e recursos — qualificação de 2026-10-06

Esta entrega qualifica um perfil Linux de nós e RTT. Windows, consumidores
completos de workbenches, nós GL-only restantes, formatos/mips RTT adicionais
e RTT de janela continuam abertos. Os contratos de shaders próprios, volume,
cubo e MSAA/multipass são definições de arquitetura; não têm executor publicado.

## Reproduzir

Builds Coin independentes: `COIN_RENDER_BACKEND=RUST_BRIDGE`, `BGFX` e
`RECORDING`; `COIN_BUILD_LEGACY_GL_RENDERER=ON`. FreeCAD usa o guard experimental,
a biblioteca correspondente via `LD_LIBRARY_PATH` e os dois patches incrementais
sobre a integração experimental P16 existente, em
`examples/coinrender`. A fixture está em `testsuite/qt-quarter`.

O runner usa Weston/Xwayland privado e `--require-hardware`; não há fallback
CoinGL contado como passe. A referência usa `--reference-gl` e tem status
REFERENCE_PASS. Viewport nativo exige submit de surface; NaviCube usa offscreen,
cuja prova identifica adapter e serial depois da conclusão/publicação verificadas.
O parser preserva renderer desconhecido como falha e normaliza os nomes oficiais
BGFX OpenGL/Vulkan. Testes unitários verificam as duas fronteiras.

Comando de host: `python3 testsuite/qt-quarter/run_isolated.py --server xwayland
--weston-prefix /tmp/coin-p16-weston --harness /tmp/coin-front3-qt/QtQuarterRegression
--freecad /tmp/freecad-p16-build/bin/FreeCAD --artifacts /tmp/resultado
--backend bgfx --renderer vulkan --case freecad-screen-content
--require-hardware --timeout 120`. Por padrão: object/weighted_oit × DPR 1/2.
Para NaviCube: `--case navicube`, `COIN_TEST_NPOT_LABELS=1`;
object/weighted_oit × opaque/translucent, sete orientações, máscaras de rótulos,
controle sem rótulos e picking. Mesa/RADV: `__GLX_VENDOR_LIBRARY_NAME=mesa`,
`VK_DRIVER_FILES=/usr/share/vulkan/icd.d/radeon_icd.json`.

Controles RTT: `ctest --output-on-failure -R RttProfile`, com
`COIN_RENDER_REQUIRE_GL_REFERENCE=1`, `DISPLAY=:0`,
`__NV_PRIME_RENDER_OFFLOAD=1`, `__GLX_VENDOR_LIBRARY_NAME=nvidia`,
`VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/nvidia_icd.json` e
`COIN_GLX_PIXMAP_DIRECT_RENDERING=1`. Cada execução contém 288 casos,
rejeição RGBA16F sem publicação e recuperação. Neste contexto, quatro unidades
fixed-function dão 144 referências nativas; 144 têm equações independentes.

## Diagnósticos preservados

- Marcadores finos: 29 pixels e zero amostras numa grade de passo 2. A fixture
  agora mede cada pixel, mantendo a contagem antiga como diagnóstico.
- Idle: havia um único quadro pendente na remoção. A fixture assenta os sensores
  antes de iniciar a medição e exige contador idêntico após 1,2 s.
- Primeiros runs foram rejeitados pelo parser de `OpenGL 4.3` e pelo gate de
  surface aplicado ao NaviCube offscreen. Não entram no total de passes.
- BGFX/OpenGL AMD, DPR 2: timeout antes da primeira captura. Não é passe nem
  atribuído a um driver sem diagnóstico suficiente; outros DPR/APIs têm células
  próprias. Backtrace por attach ficou indisponível sob a política ptrace do host.
- Rust em paralelo teve SIGSEGV; a execução serial, `cargo test --locked --
  --test-threads=1`, passou 40 testes. Serial não certifica concorrência.
- Uma invocação sem DISPLAY do teste Lighting falhou ao abrir GLX; o primeiro
  teste FFI ainda esperava revisão 48. A revisão privada é 49 e o assert foi
  atualizado. As regressões finais são executadas com ambiente GPU explícito.

Resultados finais e hashes ficam em `summary.json`, resultados de host por
célula e logs dos gates. Capturas originais grandes permanecem nos diretórios
`/tmp/coin-front3-qualified-*` registrados nos resultados; os controles JSON e
logs essenciais são versionados aqui.

Resultado: 19 CTests wgpu, 27 BGFX e três Recording aprovados; sete execuções
RTT/2.016 controles (1.008 referências CoinGL e 1.008 equações escalares), com
rejeição e recuperação por controle. Dez células nativas em hardware, duas
referências CoinGL e 12 células NaviCube NPOT em hardware aprovadas. As células
OpenGL DPR 2 de diagnóstico não entram nesses totais.
