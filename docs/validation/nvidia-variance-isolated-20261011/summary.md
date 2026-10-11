# Variação NVIDIA/wgpu em compositor isolado — 2026-10-11

A sessão X11 principal introduziu pausas de 750–1000 ms na submissão/
apresentação NVIDIA, tanto em wgpu quanto em BGFX e inclusive em
`texture-8.iv` ([trace](../nvidia-present-stall-20261011/summary.md)).
O controle `texture-8.iv` em Weston headless GL + Xwayland privado,
shell `desktop-shell.so`, voltou a mediana de 0,641 ms em wgpu/Vulkan
NVIDIA, sem essas pausas. A GPU foi identificada como RTX 3060 Laptop
`10de:2560`, não como renderer de software.

No **mesmo compositor isolado**, oito processos físicos executaram
`city-1000000.iv`, 1280×720, janela sem readback, ordem
native/portable/portable/native duas vezes. Cada processo mediu 120
quadros após 30 warmups, retornou zero e relatou o mesmo
`final_state_digest=0x33d99c7f4d6c3900`. Nenhuma das 960 amostras
medidas ultrapassou 500 ms.

| Política | Medianas por processo (ms) | Mediana das medianas | Amplitude entre processos |
| --- | --- | ---: | ---: |
| native | 15,039 / 15,470 / 15,137 / 15,407 | 15,272 ms | 2,82% |
| portable | 15,692 / 15,103 / 15,176 / 15,212 | 15,194 ms | 3,85% |

A diferença agregada portable/native foi **−0,51%**, menor que a
amplitude dentro de cada política. O salto histórico de aproximadamente
25% não reapareceu. O benchmark mede a chamada CPU de atualização/render/
present, sem tempo GPU nem latência de exibição; este resultado não é uma
previsão universal de FPS. A reprodução isolada demonstra que as pausas
de quase um segundo da sessão atual dependiam do caminho de apresentação
do X11 principal. Não distingue qual parte de driver, PRIME, compositor
ou estado da janela produziu aquelas pausas, nem prova a causa do outlier
histórico de 25%.

O replay BGFX foi qualificado separadamente no milhão AMD e NVIDIA e em
`texture-8`/`npot-8` nas duas GPUs
([recibo](../replay-textures-dual-gpu-20261011/summary.md)). Essa
combinação encerra o **estudo local** da variação e ampliação do replay.
Uma recorrência de regressão em um novo perfil exigirá diagnóstico próprio;
nenhuma política de sampling foi promovida por este estudo.

Comando da sessão: `testsuite/qt-quarter/run_isolated.py --server
xwayland --weston-prefix
/mnt/Laranja/Git/externos/coin-portable-sampling-artifacts/20261008-sampling-api-linux/weston/prefix
--weston-shell desktop-shell.so --artifacts
/mnt/Laranja/Git/externos/coin-render-artifacts/nvidia-isolated-abba-20261011
--exec -- python3 docs/validation/nvidia-variance-isolated-20261011/runner.py`.
A execução usou
`COIN_SAMPLING_STUDY=fetch`, ICD NVIDIA, `WGPU_BACKEND=vulkan` e o
binário de `/tmp/coin-render-closure-wgpu`. O
[runner exato](runner.py) está versionado; os comandos de
cada processo, relatórios, CSVs, logs, SHA-256 de binário/cena/log/CSV
e informações da sessão isolada estão em
`/mnt/Laranja/Git/externos/coin-render-artifacts/nvidia-isolated-abba-20261011/`.
