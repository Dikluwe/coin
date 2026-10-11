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
com 8 GiB. Em 4 GiB com SwiftShader, o APK carregou e contou 1.000.001
cubos, mas o renderer recusou o adaptador não físico, como exige o contrato.
Assim, **não há aprovação Android** desta versão: captura, memória estabilizada,
longas caudas, arraste, HOME/retomada e 2 GiB continuam abertos.

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
