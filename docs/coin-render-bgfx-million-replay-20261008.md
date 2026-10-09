# BGFX: replay do milhão após upload de instâncias

Estudo local em `codex/coin-portable-sampling-study`, AMD Renoir (`0x1002:0x1638`),
BGFX/Vulkan, 1280×720, cena `city-1000000.iv` com 1.000.001 instâncias.
A mudança mantém o orçamento CPU de 128 MiB. Depois de copiar o payload para o
buffer dinâmico BGFX, `retainForReuse` pode liberar o vetor CPU de instâncias
quando ele sozinho impede a admissão do plano. O plano conserva a contagem e
os intervalos de draws; replay e patch de câmera usam o buffer GPU. Antes do
upload, o mesmo orçamento ainda recusa a retenção. Falha de admissão não publica
um plano parcial.

## Controles

- `CoinBgfxCoreTest` PASS: orçamento reduzido recusa a retenção antes do upload,
  admite depois de liberar o payload e mantém o patch de câmera.
- `CoinBgfxInstancingTest` PASS em AMD/Vulkan e AMD/OpenGL: pixels e depth contra
  lowering geral, replay da mesma revisão, atualização de câmera/material,
  resize, fallback e recuperação. O renderer GL físico foi confirmado por
  `glxinfo -B` (`AMD Radeon Graphics`, radeonsi/Renoir).
- Sondas de janela, native e portable: quatro quadros cada (um warmup, três
  medidos), sem readback. Ambas mostram `resource_cache_hit=0,1,1,1`,
  `geometry_buffer_reused=0,1,1,1` e 1.000.001 instâncias. Em native,
  `lower_ms` passa de 504,48 no primeiro quadro para 0,08/0,03/0,03 nos
  seguintes; `upload_ms` de 140,58 para menos de 0,001. Em portable, o mesmo
  padrão ocorre (500,51 → 0,18/0,04/0,04 ms de lowering). Essas sondas têm
  tracing e não substituem o benchmark CPU comum.

## Janela sem readback

Quatro processos na ordem baseline/native/native/baseline, todos válidos,
oito frames medidos após três de warmup por processo. A mediana de
`render_present_ms` por processo foi:

| Processo | Mediana (ms) |
| --- | ---: |
| Baseline 1 | 1195,65 |
| Native novo 1 | 17,62 |
| Native novo 2 | 17,88 |
| Baseline 2 | 1159,64 |

O controle baseline é o binário congelado anterior à API pública de sampling;
portanto essa razão de aproximadamente 66× nesta amostra não isola sozinha
uma única alteração de fonte. O teste de orçamento e os traces demonstram o
mecanismo: antes o plano de 160.000.160 bytes não era retido e repetia lowering/
upload; agora o replay usa o buffer GPU após a primeira submissão. A medição
representa chamadas CPU com possível backpressure, não latência de monitor,
tempo GPU ou FPS universal. A cena é estática; arraste, mutação de geometria,
outras GPUs e APIs exigem células próprias.

Ledger versionado: [resumo dos processos](validation/bgfx-million-replay-20261008/summary.json),
[trace native](validation/bgfx-million-replay-20261008/native-replay-trace.log),
[trace portable](validation/bgfx-million-replay-20261008/portable-replay-trace.log)
e quatro CSVs no mesmo diretório. Os logs de
[Core](validation/bgfx-million-replay-20261008/core.log),
[instancing Vulkan](validation/bgfx-million-replay-20261008/instancing-vulkan.log),
[instancing OpenGL](validation/bgfx-million-replay-20261008/instancing-opengl.log)
e o [recibo OpenGL](validation/bgfx-million-replay-20261008/opengl-adapter.txt)
também estão versionados. Logs completos e builds permanecem em
`/mnt/Laranja/Git/externos/coin-portable-sampling-artifacts/20261008-sampling-api-linux/bgfx-million-replay-20261008`.
Comando do A/B:

```sh
python3 testsuite/reproducers/sampling-api/benchmark_linux.py \
  --artifacts /mnt/Laranja/Git/externos/coin-portable-sampling-artifacts/20261008-sampling-api-linux \
  --name bgfx-million-replay-20261008 --profiles bgfx-amd-vulkan \
  --workloads city-1000000 --baseline --large-bgfx-frames 8 \
  --large-bgfx-warmup 3
```
