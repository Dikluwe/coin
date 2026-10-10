# FreeCAD cold start: transição da borda NVIDIA/wgpu

Esta nota examina o FAIL preservado em `freecad-qualified-final` para
`wgpu-nvidia-vulkan-portable`, DPR 1. A captura inicial `screen-bare.png` e a
captura `screen-text-removed.png` têm 1100 × 641 pixels. O diff RGB contém
exatamente 1.476 pixels, todos na moldura de um retângulo de 370 × 370 pixels:
`x=365..734`, `y=135..504`. Em cada pixel alterado, o valor passa de
`(10,10,10)` para `(146,152,158)`. A contagem coincide com
`2 × 370 + 2 × 370 - 4`: a borda de um pixel desapareceu; não restou nenhum
pixel do texto amarelo na área alterada. As quatro capturas foram copiadas para
o ledger versionado: [baseline](validation/sampling-api-linux-20261008/freecad-qualified-final/wgpu-nvidia-vulkan-portable/vulkan-object-opaque-1x-freecad-sampling-policy/screen-bare.png),
[texto inicial](validation/sampling-api-linux-20261008/freecad-qualified-final/wgpu-nvidia-vulkan-portable/vulkan-object-opaque-1x-freecad-sampling-policy/screen-text.png),
[texto alterado](validation/sampling-api-linux-20261008/freecad-qualified-final/wgpu-nvidia-vulkan-portable/vulkan-object-opaque-1x-freecad-sampling-policy/screen-text-updated.png),
[após remoção](validation/sampling-api-linux-20261008/freecad-qualified-final/wgpu-nvidia-vulkan-portable/vulkan-object-opaque-1x-freecad-sampling-policy/screen-text-removed.png)
e [resultado](validation/sampling-api-linux-20261008/freecad-qualified-final/wgpu-nvidia-vulkan-portable/vulkan-object-opaque-1x-freecad-sampling-policy/result.json).
SHA-256: `661d2a6998b1a14813aaacde80bd31b4187b341f5db6543c6b20d638c7aa566c`
e `ee744cb039325107e3873cf1c7554e4685021f466436be71ae1aaa7e5358a061`.

O fixture cria primeiro a cena FreeCAD e captura `screen-bare`; em seguida
habilita o SoText2, altera sua string e oculta o switch antes de comparar com
o baseline. Na sequência registrada, os 1.476 pixels da moldura ainda são
pretos com o texto inicial. Após a mutação da string, nenhum deles continua
preto; 1.475 já têm o cinza final e um está coberto pelo texto alterado.
Após ocultar o texto, todos os 1.476 têm o cinza final. Portanto a transição
ocorre até o primeiro redraw após a mutação da string, antes da remoção.
O teste de remoção detectou a diferença corretamente. A captura
de tela estável em duas leituras descarta um frame de apresentação isolado,
mas não prova que o desenho inicial do documento já estivesse estabilizado.
O teste posterior aquecido `sampling-startup-before-text` versus
`sampling-startup-after-text` tem diff zero na mesma célula
([resultado](validation/sampling-api-linux-20261008/freecad-warm-qualified-final/wgpu-nvidia-vulkan-portable/vulkan-object-opaque-1x-freecad-sampling-policy/result.json)). Isso delimita a
transição ao primeiro uso do host; não identifica se a origem é estado de
seleção/estilo do FreeCAD, cache de geometria ou apresentação inicial.

Plano de controle definido após o FAIL histórico: repetir a sequência sem
mutar a string, mutar a string com
o switch oculto e mutar um nó não textual, capturando a moldura após cada
redraw. Registrar o estado de seleção e o estilo do objeto do documento em
cada captura. Comparar native/portable com o mesmo host e sequência de frames.
Manter o gate `remaining_changes <= 30` e
`cold_start_qualified=false` até haver qualificação fria controlada. Os
controles executados neste PC estão registrados abaixo.

## Controles executados neste PC

O fixture ganhou a opção isolada `COIN_TEST_COLD_BORDER_DIAGNOSTIC`, que sai
após capturas de diagnóstico e não altera o caminho qualificado quando a opção
está ausente. Quatro processos wgpu/Vulkan/portable em compositor privado AMD
terminaram com `PASS`, GPU física `0x1002:0x1638` e política ativa `1`:

| Controle | Sequência | Diff RGB final contra `cold-before` |
| --- | --- | ---: |
| `visible-no-mutation` | mostrar/ocultar texto | 0 |
| `visible-mutation` | mostrar, mutar e ocultar texto | 0 |
| `hidden-mutation` | mutar texto oculto | 0 |
| `hidden-image-mutation` | mutar imagem oculta | 0 |

Isso confirma que os controles produzem capturas estáveis na AMD; não reproduz
a transição histórica. Uma tentativa NVIDIA/wgpu/Vulkan foi recusada antes da
primeira captura: `No physical adapter for the requested renderer and surface`.
Nesta sessão, o módulo NVIDIA carregado é 610.57.04 e a biblioteca NVML
instalada informa 615.71 (`Driver/library version mismatch`). Esse processo
não conta como teste de raster ou de sampling. A matriz NVIDIA foi repetida
após restaurar a compatibilidade do driver, sem trocar o oracle ou o gate;
os resultados aparecem abaixo.

Resultados e imagens completos estão no root durável
`/mnt/Laranja/Git/externos/coin-portable-sampling-artifacts/20261008-sampling-api-linux/cold-border-controls-20261008`.
Os [cinco resultados e oito capturas selecionadas](validation/cold-border-controls-20261008/amd-portable-visible-mutation/result.json)
também foram copiados para `docs/validation/cold-border-controls-20261008`,
com subdiretórios por controle; o quinto resultado documenta a falha de
inicialização NVIDIA.
O runner usado foi `testsuite/qt-quarter/run_isolated.py` com
`--server xwayland`, `--weston-prefix` desse mesmo root, FreeCAD privado em
`freecad-host/build/bin/FreeCAD`, `--case freecad-sampling-policy`,
`--renderer vulkan`, `--backend wgpu`, `--mode object`, `--scale 1` e
`--require-hardware`; a variável de diagnóstico selecionou cada linha.

## Após atualização do driver neste PC

Depois da reinicialização, módulo carregado, biblioteca e `nvidia-smi` passaram
a reportar NVIDIA 615.71.09. O compositor privado Xwayland/Weston identificou
GLX/EGL NVIDIA RTX 3060, e `CoinRenderAdvancedTextureTest --gpu` em BGFX/Vulkan
identificou `10de:2560` e passou 380 controles. Portanto o bloqueio anterior
de versão do driver foi removido.

A primeira repetição com o shell padrão `kiosk-shell.so` abortou antes da macro:
Qt informou que o plugin `xcb` não pôde ser inicializado (exit `-6`). O trace
mostrou `ECONNREFUSED` no socket X11 e o coredump do Weston apontou SIGSEGV
em `weston_view_move_to_layer` chamado por `kiosk-shell.so` quando a janela do
FreeCAD surgiu. O erro Qt foi consequência da queda do compositor. Com
`--weston-shell desktop-shell.so`, o mesmo host iniciou e renderizou na RTX 3060.

Oito processos de diagnóstico `wgpu`/Vulkan em GPU física `10de:2560` passaram:
os quatro cenários abaixo com política portable (`1`) e native (`0`). Em ambos,
o diff RGB final contra `cold-before` foi zero para todos os cenários. A
moldura histórica `x=365..734`, `y=135..504` também teve diff zero em todas as
capturas intermediárias. O texto visível alterou 700 pixels e a mutação de sua
string alterou 712, conforme esperado; após ocultá-lo, o diff voltou a zero.

| Cenário | Portable | Native | Diff final |
| --- | --- | --- | ---: |
| `visible-no-mutation` | PASS | PASS | 0 |
| `visible-mutation` | PASS | PASS | 0 |
| `hidden-mutation` | PASS | PASS | 0 |
| `hidden-image-mutation` | PASS | PASS | 0 |

O gate padrão `freecad-sampling-policy`, sem a variável de diagnóstico, também
passou em portable e native no host NVIDIA. As primeiras execuções terminaram
no timeout de 45 s; a repetição com `--timeout 120` concluiu com `PASS` nas duas
políticas e `startup_root_changes=0`. O fixture padrão segue registrando
`cold_start_qualified=false`. Os processos de diagnóstico exercitam a sequência
fria, mas não substituem a qualificação do gate histórico: a borda de 1.476
pixels não reapareceu, e driver e shell mudaram entre as coletas. A causa
daquela transição permanece aberta; não há base para atribuí-la ao sampling,
ao driver ou ao shell isoladamente. Oracle e tolerância não foram alterados.

[Resumo calculado, resultados e capturas](validation/cold-border-nvidia-after-driver-20261008/summary.json)
estão no ledger versionado. Para repetir neste host, usar
`testsuite/qt-quarter/run_isolated.py --server xwayland --weston-shell desktop-shell.so`
com o mesmo `--weston-prefix`, host privado, `--require-hardware` e seletor
`COIN_TEST_COLD_BORDER_DIAGNOSTIC` dos quatro cenários; definir
`COIN_TEST_PORTABLE_SAMPLING=1` ou `0`. Para o gate padrão, omitir o seletor
de diagnóstico e usar `--timeout 120`.

Logs, resultados integrais e sondas desta sessão estão em
`/mnt/Laranja/Git/externos/coin-portable-sampling-artifacts/20261008-sampling-api-linux/cold-border-controls-after-driver-20261008`.
O checkout original do estudo não persistiu após o reboot; a mesma branch foi
restaurada em um novo worktree no commit `ff267411f6` para estas tentativas.
