# Gate GPU completo de sampling no Mesa privado

Em 2026-10-09, o gate `CoinRenderTextureSamplingTest --gpu` passou nos oito
processos BGFX deste PC: AMD/NVIDIA × Vulkan/OpenGL × política nativa/portátil.
Cada processo percorreu 234 cenas e quatro rejeições, com CoinGL obrigatório.
O controle projetivo `quality=0,5`, `offset=0` teve MAE 0/máximo 0 nas três
comparações CPU/GPU, CPU/CoinGL e GPU/CoinGL. Nenhuma tolerância do teste mudou.

O [ledger e os logs completos](validation/full-gpu-sampling-gate-20261009/summary.json)
registram exit, adaptador, métricas e hashes. Os IDs Vulkan são AMD
`1002:1638` e NVIDIA `10de:2560`. Em OpenGL, o callback BGFX identificou
Renoir/Mesa 25.2.8 e RTX 3060/driver 615.71.09; a referência CoinGL usou
GLX/Mesa separadamente. NVIDIA/OpenGL exigiu EGL surfaceless.

## Causa e correção local

Com o Mesa instalado, o mesmo gate falhava no sampling projetivo por até 78
níveis RGB entre a referência CPU e CoinGL. O [estudo da falha](coin-render-failure-closure-20261008.md)
isolou o filtro misto nearest/mip-linear do Mesa Renoir e qualificou uma
correção NIR em prefixo privado, acionada por
`COIN_MESA_MIXED_FILTER_STUDY=1`. O gate passou usando esse runtime para a
referência CoinGL; na AMD ele também atende o backend GPU. Em NVIDIA, o
backend GPU usa o driver NVIDIA e CoinGL continua usando o Mesa privado.
No [controle com o guard desligado](validation/full-gpu-sampling-gate-20261009/amd-vulkan-native-guard-off.log),
o mesmo binário, Mesa privado e GPU AMD/Vulkan retornaram 1: CPU/CoinGL
MAE 8,08/máximo 78 na fixture projetiva, enquanto GPU/CoinGL ficou em zero.

O prefixo privado conservava as bibliotecas com os hashes da campanha anterior,
mas havia perdido os links `dri/radeonsi_dri.so` e `dri/swrast_dri.so` para
`../libgallium-25.2.8.so`. Os links foram restaurados nesse prefixo, sem
recompilar, alterar os binários ou instalar um driver no sistema. O SHA256 da
`libgallium-25.2.8.so` é o mesmo do [registro anterior](validation/failure-closure-20261008/runtime-hashes.json):
`440268c843b34cbafb756f1add24c3d90a2ecba7faa7671b0e705d9f9b560f41`.

O [runner do gate](../testsuite/reproducers/portable-sampling-study/mesa/run_full_sampling_gate.py)
seleciona os oito processos, desabilita o cache do Mesa, identifica as GPUs e
exige exit 0, 234 cenas, quatro rejeições e o controle projetivo sem diferença.
Uma repetição neste PC usa:

```sh
python3 testsuite/reproducers/portable-sampling-study/mesa/run_full_sampling_gate.py \
  --build /mnt/Laranja/Git/externos/coin-portable-sampling-artifacts/20261009-npot-bgfx/build \
  --mesa-prefix /mnt/Laranja/Git/externos/coin-portable-sampling-artifacts/20261008-failure-fixes/mesa-study/install \
  --output docs/validation/full-gpu-sampling-gate-20261009 \
  --display :0 --xauthority /home/dikluwe/.Xauthority
```

O PASS é condicionado a esse Mesa privado. O driver Mesa instalado ainda
reproduz o FAIL projetivo, conforme os [logs anteriores](validation/texture-scale-policy-20261009/).
O prefixo é um protótipo de estudo com custo de sampling ainda não qualificado;
este resultado não autoriza distribuí-lo como substituto do driver do sistema.
