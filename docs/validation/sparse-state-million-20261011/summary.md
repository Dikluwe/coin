# Milhão: estado de texturas disperso — 2026-10-11

Branch `codex/coin-render`. Os oito programas de combinação e as sete unidades
extras não ocupam mais espaço dentro de cada `CoinRenderRenderStateSnapshot`
quando não são usados. Uma escrita materializa o payload; cópias do plano
compartilham a versão imutável até a primeira escrita, preservando rollback.
O teste Core cobre leitura sem alocação, cópia, escrita e destacamento posterior.
Os executores recebem os mesmos bytes de textura; o layout do FFI Rust e o
protocolo não mudaram.

| Medida no mesmo `city-1000000.iv`, AMD `0x1002:0x1638`/Vulkan | Build anterior | Build com payload disperso |
| --- | ---: | ---: |
| Estados/draws | 1.000.001 / 1.000.001 | 1.000.001 / 1.000.001 |
| `sizeof` de estado | 1.648 B | 496 B |
| Bytes usados por estados | 1.648.001.648 | 496.000.496 |
| Capacidade reservada de estados | 1.728.053.248 B | 520.093.696 B |
| RSS máximo do processo (`/usr/bin/time -v`) | 4.410.844 KiB | 3.263.360 KiB |
| Travessia do primeiro quadro | 4.819,35 ms | 3.629,96 ms |
| `final_state_digest` | `0x33d99c7f4d6c3900` | `0x33d99c7f4d6c3900` |

O ganho estrutural é 1.152 B por estado sem texturas. O RSS caiu 1.147.484
KiB nesta execução. As duas builds não são commits adjacentes: a anterior é
de 2026-10-07, e o teste mede uma execução de oito quadros após dois warmups.
Portanto os tempos são observações, não uma atribuição causal ou um benchmark
de cauda. Cena, resolução 1280×720, API, GPU e digest foram os mesmos.
O binário anterior tem SHA-256
`a49fd65c4b07edca1ea2e020b1ac6a1f77049830c4de512d2a467f92fce8b0e1`;
o novo, `ebf05c56d2f3caa5f678ac210628960f618cd39a4e5bd2237d6c8139bf463af0`.

`CoinRenderFrameCoreTest`, `CoinRenderFrameReuseCoreTest`,
`CoinWgpuFfiFrameTest`, `CoinBgfxCoreTest` e o gate GPU de texturas avançadas
BGFX/Vulkan passaram; este último executou 413 controles na AMD física.
A suíte wgpu/Vulkan com oracle CoinGL GLX NVIDIA passou 219/219 CTests
(incluindo skips declarados). O gate de geometria aprovou 1.795/1.795 cenas.
Na suíte BGFX com Vulkan/OpenGL NVIDIA, 289/290 CTests passaram; o único
desvio foi um pixel de instancing em OpenGL NVIDIA. Esse teste passou isolado
em OpenGL AMD com CoinGL NVIDIA. Uma suíte BGFX mista AMD/NVIDIA teve três
desvios de driver/oracle; ela não é contada como aprovação completa. O CTest
`CoinRenderRttProfileMipsDirectTest` agora classifica seu retorno 77 como skip,
igual aos demais RTT condicionais, quando falta a referência GL obrigatória.

APK x86_64 recompilado com SHA-256
`0cc3105e9c9afb67f53273b7715e3af3a2c9790f7ec8e081fd1033a9f1f622e2`.
O AVD API 37 aumentou a solicitação de 2 GiB para 4 GiB. Em `-gpu host`,
o emulador saiu com SIGSEGV durante o boot, antes do app, tanto com 4 quanto
com 8 GiB. Desabilitar o Vulkan da imagem com `-feature -Vulkan` estabilizou
o mesmo AVD em **4 e 8 GiB com GLES da GPU host AMD**. Em 4 GiB com SwiftShader,
o APK carregou e contou 1.000.001 cubos, mas o renderer recusou corretamente
o adaptador não físico. Só a execução GLES/AMD conta como gate de renderização.

No perfil de 4 GiB/GLES/AMD, o APK carregou 1.000.001 cubos e 12.000.012 triângulos.
A captura offscreen inicial deu `067c1efbc25642eb`. Após um arraste horizontal
e três ciclos HOME/retomada, as gerações 2, 3 e 4 deram o mesmo checksum
`ef0cf0ba19f72741`, com PID 3455 preservado, sem erro de renderização.
Depois de um arraste vertical, o focal passou de 9.260,209 para 12.396,241;
a quinta superfície manteve esse valor e capturou `7a6a8c0c986435bf`.
A primeira captura veio 28,366 s depois da criação da superfície; as quatro
recapturas levaram 19,025, 19,858, 17,559 e 15,889 s. O trace registrou
três reconstruções completas por geração. A cena parada manteve grupos
próximos de 35 FPS.

| Trace da Activity, 4 GiB | Mediana | P95 | Máximo |
| --- | ---: | ---: | ---: |
| `frame_plan_ms`, 28 patches de câmera | 31,758 ms | 149,930 ms | 232,470 ms |
| `backend_ms`, 28 patches de câmera | 42,562 ms | 55,989 ms | 68,066 ms |
| `backend_ms`, 5.256 reusos | 28,581 ms | 34,770 ms | 133,400 ms |

Após a quinta geração, Android informou 3.831.269 KiB de PSS e
1.299.492 KiB de swap PSS. Três arrastes adicionais levaram o sistema a
encerrar o PID 3455 por `lowmemorykiller` às 08:09:58: memória livre abaixo
do mínimo e swap insuficiente. O registro do kernel informa liberação de
2.219.512 KiB de RSS e 1.677.864 KiB de swap. Portanto as cinco superfícies
são apenas um smoke funcional: **o milhão não passou resistência no AVD de
4 GiB**.

Repetimos com o mesmo APK e GLES/AMD no AVD de 8 GiB (8.130.248 KiB em
`MemTotal`). A captura inicial repetiu `067c1efbc25642eb`. Quatro arrastes
seguidos e dois ciclos HOME/retomada conservaram o PID 3171, produziram
capturas nas gerações 2 e 3 (`7cd195103e553869` e `9a7e2d9373eb01d1`)
e não usaram swap. O arraste vertical entre as retomadas mudou o focal para
12.396,244; a geração 3 preservou esse valor. O trace conteve nove
reconstruções completas, 101 patches de câmera e 1.422 reusos.

| Trace da Activity, 8 GiB | Mediana | P95 | Máximo |
| --- | ---: | ---: | ---: |
| `frame_plan_ms`, 101 patches de câmera | 30,969 ms | 31,773 ms | 33,114 ms |
| `backend_ms`, 101 patches de câmera | 42,662 ms | 43,622 ms | 58,091 ms |
| `backend_ms`, 1.422 reusos | 28,402 ms | 30,031 ms | 37,043 ms |

Ao final, o app tinha 3.845.081 KiB de PSS, 3.971.712 KiB de RSS e zero
swap. Isso aprova somente o ensaio delimitado de interação e retomada no AVD
local de 8 GiB. A imagem API 37 local não permite testar 2 GiB efetivos;
o PSS observado também excede esse orçamento. O perfil de 4 GiB falhou;
**dispositivos menores que 8 GiB não estão qualificados para esta cena**.
Não há medição de Android físico arm64, nem garantia geral para aparelhos
de 8 GiB com outras GPUs e pressão de memória.

Comandos principais: `cmake --build /tmp/coin-render-closure-wgpu -j 8`,
`ctest --test-dir /tmp/coin-render-closure-wgpu -j 4 --output-on-failure`
com `WGPU_BACKEND=vulkan`, ICD NVIDIA, `__NV_PRIME_RENDER_OFFLOAD=1`,
`__GLX_VENDOR_LIBRARY_NAME=nvidia` e
`COIN_GLX_PIXMAP_DIRECT_RENDERING=1`; o benchmark usou
`coin_render_window_benchmark --backend wgpu-vulkan --scene city-1000000.iv
--width 1280 --height 720 --frames 8 --warmup 2 --animation static`
com ICD RADV, `COIN_RENDER_TRACE_PHASES=1` e `/usr/bin/time -v`.

Os logs integrais e os hashes SHA-256 de cada log estão em
`/mnt/Laranja/Git/externos/coin-render-artifacts/sparse-state-20261011/`.
O AVD foi iniciado com
`emulator -avd Medium_Phone_API_37.0 -no-window -gpu host -feature -Vulkan
-no-audio -no-boot-anim -memory 4096 -no-snapshot`; o segundo ensaio trocou
`-memory 4096` por `-memory 8192`. Os registros decisivos são
`android-logcat-full-4g-host.log` (encerramento por falta de memória),
`android-phases-4g-host-final.log`, `android-logcat-8g-host.log`,
`android-phases-8g-host.log` e `android-meminfo-8g-host.txt` no diretório acima.
