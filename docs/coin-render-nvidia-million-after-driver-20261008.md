# Milhão de instâncias NVIDIA após atualização do driver

Neste PC, com NVIDIA 615.71.09 e RTX 3060 Laptop (`10de:2560`), repeti a
cena estática `city-1000000.iv` em wgpu/Vulkan e BGFX/Vulkan. São 1.000.001
instâncias, um draw e nenhuma unidade de textura. Os oito processos usaram
1280×720, janela sem readback e ordem native/portable/portable/native em cada
backend. Todos retornaram zero, identificaram a GPU física e produziram o
mesmo `final_state_digest=0x33d99c7f4d6c3900`. O hash da cena foi idêntico
nos oito processos. Os binários e suas SHA-256 constam do
[resultado bruto](validation/nvidia-million-after-driver-20261008/summary.json).

| Backend | Native, medianas por processo (ms) | Portable (ms) | Mediana das medianas native/portable |
| --- | ---: | ---: | ---: |
| wgpu/Vulkan, 120 frames após 30 warmups | 10,064 / 9,231 | 9,378 / 9,261 | 9,647 / 9,320 ms |
| BGFX/Vulkan, 8 frames após 3 warmups | 11,974 / 11,958 | 11,979 / 11,992 | 11,966 / 11,986 ms |

No wgpu, a primeira execução native foi 0,833 ms mais lenta que a última;
essa variação dentro da mesma política excede a diferença agregada entre
políticas (0,328 ms nesta amostra). A coorte anterior de ~25% de diferença
não reapareceu, mas este A/B não identifica a causa: o driver mudou, e não
foram controlados clocks, temperatura ou carga de sistema. O resultado não
mede tempo GPU nem latência de apresentação; o próprio benchmark informa
`timing_scope=cpu-update-and-render-present-call` e `final_sync=none-public-api`.

No BGFX, duas sondas adicionais com tracing, quatro frames após um warmup,
confirmaram `resource_cache_hit=0,1,1,1,1` e
`geometry_buffer_reused=0,1,1,1,1` em native e portable. Após o primeiro
quadro, o lowering caiu de 503,23/491,08 ms para menos de 0,07 ms e o upload
de 130,60/126,49 ms para menos de 0,002 ms. As sondas instrumentadas foram
excluídas das medianas da tabela. Isso amplia a prova local do replay do
milhão da AMD para a NVIDIA/Vulkan; outras cargas e APIs continuam abertas.

O [resumo calculado](validation/nvidia-million-after-driver-20261008/analysis.json),
os CSVs, logs e traces estão no ledger versionado. A cópia integral está em
`/mnt/Laranja/Git/externos/coin-portable-sampling-artifacts/20261008-sampling-api-linux/benchmark-nvidia-after-driver-20261008`.
Comando da campanha sem tracing:

```sh
python3 testsuite/reproducers/sampling-api/benchmark_linux.py \
  --artifacts /mnt/Laranja/Git/externos/coin-portable-sampling-artifacts/20261008-sampling-api-linux \
  --name benchmark-nvidia-after-driver-20261008 \
  --profiles wgpu-nvidia-vulkan,bgfx-nvidia-vulkan \
  --workloads city-1000000 --frames 120 --warmup 30 \
  --large-bgfx-frames 8 --large-bgfx-warmup 3
```
