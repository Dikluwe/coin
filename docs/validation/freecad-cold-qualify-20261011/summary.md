# FreeCAD: primeira borda antes do texto, NVIDIA/wgpu

Em 2026-10-11, o gate `COIN_TEST_COLD_BORDER_QUALIFY=1` executou 16 processos
novos do FreeCAD, intercalando 12 com sampling portátil e quatro com sampling
nativo. Todos rodaram em Xwayland/Weston `desktop-shell.so`, wgpu/Vulkan,
GPU NVIDIA física `10de:2560`, DPR 1, modo object, com `--require-hardware`.
Cada processo criou um perfil privado, abriu o documento e capturou a primeira
borda antes do primeiro uso de `SoText2`; o fixture não forçou nenhum redraw
antes dessa captura. Duas leituras iguais da tela exigiram apresentação estável.

| Política | Processos PASS | Texto visível | Mutação do texto | Diff após ocultar | Diff repetido |
| --- | ---: | ---: | ---: | ---: | ---: |
| Portátil | 12/12 | 700 px | 330 px | 0 px | 0 px |
| Nativa | 4/4 | 700 px | 330 px | 0 px | 0 px |

`cold_start_qualified=true`, `hardware_gpu=true` e `gpu_submitted=true` em
todos os processos. O frame count da primeira captura foi 32–55 no portátil e
58–59 no nativo. Os recibos por processo estão em [summary.json](summary.json);
logs e PNGs completos, incluindo tentativas de inicialização descartadas, em
`/mnt/Laranja/Git/externos/coin-render-artifacts/freecad-cold-qualify-20261011`.
SHA-256 do `libCoinRender.so` testado: `27237089645d378d54f788c427c451349249d23c6bbd691d75d6162c28cb5f87`;
do FreeCAD: `26977d9126fa6270bbff321c184721c2d9d6fe2d8e478ebb21b1a4451c920c27`;
do resumo: `660577e9915aa1be37429836d93b6019d82063c1fed91e75c834e95005860236`.

O primeiro ensaio e a tentativa com `PYTHONNOUSERSITE=1` falharam antes da macro:
o FreeCAD carregava PySide6 6.6 junto com Qt do sistema 6.4. A repetição usou
`LD_LIBRARY_PATH` com `~/.local/lib/python3.12/site-packages/PySide6/Qt/lib`
primeiro, seguido do build atual de Coin e das bibliotecas do host FreeCAD,
como no [recibo anterior](../cold-border-nvidia-after-driver-20261008/portable-standard/result.json).
Essas duas falhas de inicialização não contam como resultados de raster.

Reprodução: definir `LD_LIBRARY_PATH` nessa ordem,
`VK_DRIVER_FILES=VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/nvidia_icd.json`,
`__NV_PRIME_RENDER_OFFLOAD=1`, `__GLX_VENDOR_LIBRARY_NAME=nvidia` e
`__EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/10_nvidia.json`;
executar `testsuite/qt-quarter/run_isolated.py --server xwayland
--weston-shell desktop-shell.so --weston-prefix <prefix> --artifacts <destino>
--exec -- python3 docs/validation/freecad-cold-qualify-20261011/runner.py
<FreeCAD> <destino>`. O script fixa a matriz, o timeout de 120 s e verifica
os recibos de GPU e de imagem.

Este fechamento é delimitado a esse host, driver e sequência. A transição
histórica de 1.476 pixels não reapareceu; sua causa original não foi
identificada, e esta campanha não demonstra uma correção geral para outros
drivers, compositores ou workbenches.
