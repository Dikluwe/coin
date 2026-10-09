# Custo GPU dos mips NPOT diretos no BGFX

Campanha de 2026-10-09 na branch `codex/coin-portable-sampling-study`.
O produtor RTT direto 63×63 do gate de oito sombras e transparência gera
cinco níveis NPOT. A redução já usa um frame BGFX por nível, com blit para
scratch e draw de média por área. Com `COIN_RENDER_TRACE_PHASES=1` e
`COIN_WGPU_GPU_TIMESTAMPS=1`, a instrumentação registra dois tempos por nível:
a view da redução por área e o frame GPU completo do nível, que inclui o blit.
Ela não lê pixels, altera tolerâncias ou modifica o caminho sem tracing.

O profiler BGFX disponibiliza os timestamps com atraso. A medição associa
`gpuFrameNum` ao número retornado por `bgfx::frame()`, mantém views vazias
durante o dreno e só publica um total quando todos os níveis foram resolvidos.
O nome da view não serve para essa associação: BGFX pode atualizá-lo antes de
entregar o timestamp. O dreno é diagnóstico e altera o tempo de parede; ele
não entra na soma dos frames de redução. Um estágio sem timestamps completos
é informado como indisponível.

O [runner](../testsuite/coinrender/run-bgfx-npot-mip-gpu.py) executou
AMD/NVIDIA × Vulkan/OpenGL × peeling/weighted OIT sequencialmente. Cada
processo retornou 0, qualificou a cena e produziu 44 medições completas:
quatro funcionais e 40 do benchmark. As medianas e p95 abaixo usam as 30
iterações após dez aquecimentos por perfil.

| GPU/API | Transparência | Redução por área, mediana ms | Frames de mips, mediana ms | Frames de mips, p95 ms |
| --- | --- | ---: | ---: | ---: |
| AMD/Vulkan | peeling | 0,05092 | 0,09502 | 0,10012 |
| AMD/Vulkan | weighted | 0,05176 | 0,09642 | 0,10396 |
| AMD/OpenGL | peeling | 0,02934 | 0,05118 | 0,05892 |
| AMD/OpenGL | weighted | 0,02968 | 0,04770 | 0,06816 |
| NVIDIA/Vulkan | peeling | 0,06192 | 0,15576 | 0,20208 |
| NVIDIA/Vulkan | weighted | 0,06184 | 0,15322 | 0,21466 |
| NVIDIA/OpenGL | peeling | 0,04096 | 0,07168 | 0,10138 |
| NVIDIA/OpenGL | weighted | 0,03789 | 0,06758 | 0,09933 |

Vulkan identificou AMD `1002:1638` e NVIDIA `10de:2560`. Em OpenGL, o
callback BGFX identificou Radeon Renoir/Mesa 25.2.8 e RTX 3060/driver
615.71.09. Esta matriz usou os drivers instalados, sem o Mesa privado de
sampling. NVIDIA/OpenGL exigiu `EGL_PLATFORM=surfaceless` neste PC.
Os [logs reduzidos e o ledger](validation/bgfx-npot-mip-gpu-20261009/summary.json)
guardam cada amostra, mediana, p95 e hash; os logs brutos ficam em
`/mnt/Laranja/Git/externos/coin-portable-sampling-artifacts/20261009-npot-bgfx/npot-mip-gpu-full`.
Os hashes dos oito logs brutos foram conferidos com o ledger.

```sh
python3 testsuite/coinrender/run-bgfx-npot-mip-gpu.py \
  --build /mnt/Laranja/Git/externos/coin-portable-sampling-artifacts/20261009-npot-bgfx/build \
  --output docs/validation/bgfx-npot-mip-gpu-20261009 \
  --full-logs /mnt/Laranja/Git/externos/coin-portable-sampling-artifacts/20261009-npot-bgfx/npot-mip-gpu-full \
  --display :0 --xauthority /home/dikluwe/.Xauthority
```

Esses tempos isolam os cinco frames de geração de mips, incluindo execução GPU
do blit e da redução. Não incluem alocação/destruição de recursos no CPU,
renderização base, sombras, composição ou readback. A instrumentação e a
diferença de dimensão/filtro impedem atribuir a ela todo o incremento de tempo
de parede do [benchmark anterior](coin-render-bgfx-npot-shadow-oit-20261009.md).
Os números delimitam estes drivers, cenas e 30 amostras; não são estimativa
universal de desempenho nem qualificação DX11.

O gate `CoinRenderAdvancedTextureTest --gpu` sem tracing voltou a passar em
AMD/Vulkan (411 controles) e NVIDIA/OpenGL (426 controles), verificando o
caminho padrão após a instrumentação. A qualificação funcional anterior de
NPOT, sombras e OIT permanece válida; a pendência local de custo GPU foi
resolvida. Windows/DX11 ainda exige sua própria validação.
