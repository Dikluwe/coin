# Qualificação da API pública de sampling

`run.py` usa opções explícitas native/portable dos testes públicos, executa GPU
sequencialmente e grava comando, ambiente, hashes, código de saída e log. Qualquer
FAIL/SKIP produz saída não zero; o ledger preserva ambos. Requer Linux com display
acessível, builds wgpu/BGFX e bibliotecas em `build-{backend}/{bin,lib}` sob o root
de artefatos. `XAUTHORITY` é herdado. Os perfis padrão são AMD Vulkan/OpenGL e
NVIDIA Vulkan para ambos os backends; use `--profiles` para restringir hardware.

```sh
root=/mnt/Laranja/Git/externos/coin-portable-sampling-artifacts/20261008-sampling-api
python3 testsuite/reproducers/sampling-api/run.py --artifacts "$root" \
  --name stock-portable-repeat --modes portable \
  --cases api,deep,direct,viewport,procedural
python3 testsuite/reproducers/sampling-api/run.py --artifacts "$root" \
  --name native-resources-repeat --cases selection,publication,ownership,large,devices,stress,instancing,shadow-eight,shadow-viewport,shadow-alpha
```

`api` exercita as duas políticas no mesmo processo; seleção/publicação/ownership,
recursos/instancing/sombras são controles nativos independentes. Casos exclusivos
wgpu/BGFX são filtrados. `COIN_SAMPLING_STUDY=fetch` é injetado como conflito
intencional: a nova API deve ignorar o antigo seletor de laboratório.

Quando for necessária a comparação com CoinGL AMD corrigido apenas no laboratório:

```sh
python3 testsuite/reproducers/sampling-api/run.py --artifacts "$root" \
  --name mesa-public-repeat --modes native,portable \
  --cases projective,sampling,advanced,rtt \
  --mesa-prefix /mnt/Laranja/Git/externos/coin-portable-sampling-artifacts/20261008-failure-fixes/mesa-study/install
```

O prefixo Mesa é opcional, externo ao renderer, altera somente o ambiente dos
processos AMD e desativa cache para o estudo. Compare coortes de driver instalado
e driver de estudo separadamente. Não interpretar uma referência CoinGL corrigida
como conserto do driver instalado. Preserve o ambiente, builds e ICDs efetivos.

Runners antigos em `portable-sampling-study` recusam a nova API para impedir que
uma medição rotulada fetch/fine execute silenciosamente native. Use o baseline
congelado da revisão `43f00b0e18830ba79fe8fd19020d1f8ececa4983` para reproduzir
histórico e este runner para a política pública. Builds são mutáveis; logs de cada
coorte identificam os binários realmente usados.

Contrato, limites e ledger: [API](../../../docs/coin-render-texture-sampling-api.md).

## Continuação Linux: janela, desempenho e FreeCAD

```sh
root=/mnt/Laranja/Git/externos/coin-portable-sampling-artifacts/20261008-sampling-api-linux
python3 testsuite/reproducers/sampling-api/run.py --artifacts "$root" \
  --name window-repeat --cases window,api,selection,publication,ownership
python3 testsuite/reproducers/sampling-api/benchmark_linux.py --artifacts "$root" \
  --name cpu-repeat --frames 90 --warmup 30
python3 testsuite/reproducers/sampling-api/benchmark_linux.py --artifacts "$root" \
  --name gpu-repeat --kind gpu --workloads texture-1,texture-4,texture-8,npot-8 \
  --frames 45 --warmup 15
python3 testsuite/reproducers/sampling-api/benchmark_linux.py --artifacts "$root" \
  --name baseline-repeat --baseline --frames 90 --warmup 30
python3 testsuite/reproducers/sampling-api/qualify_freecad_linux.py --artifacts "$root"
python3 testsuite/reproducers/sampling-api/qualify_freecad_linux.py --artifacts "$root" \
  --name freecad-dpr2-repeat --scale 2 --policies portable
```

`window` exige imagens de dois alvos simultâneos e oracle após resize/suspensão/
remap, independência de políticas e recuperação; também exercita manager e
adapter. wgpu/OpenGL não oferece COPY_SRC nessa superfície: o teste inspeciona
os pixels X11 apresentados. Os outros perfis usam captura RGBA do renderer.
O fixture confirma extent e MapNotify antes da submissão; corridas do WM não
são resultados válidos de resize ou exposição.

O benchmark usa ordem ABBA e não lê pixels. Os resultados CPU representam
chamadas render/present com possível backpressure, sem promessa de latência de
display ou GPU concluído. A sonda GPU é uma campanha separada, síncrona e com
escopos por backend; timestamp indisponível permanece indisponível e não qualifica a sonda GPU.
O runner exige recibo do adaptador físico solicitado. BGFX/OpenGL usa o nome
do contexto GL com trace apenas no setup, pois capabilities pode informar
vendor/device zero. Os recibos e seu audit aparecem no ledger.
wgpu/OpenGL usa `--allow-vsync`, pois esta superfície não oferece Immediate/
Mailbox; não comparar sua taxa como se estivesse sem vsync. O milhão BGFX usa
12 amostras/4 warmup para delimitar seu custo elevado, com os mesmos números
nas políticas e baseline. Comandos individuais guardam a configuração efetiva.
`--resume` conserva checkpoints válidos; não misturar revisões de runtime numa
coorte de qualificação final. O runner histórico nunca foi usado para medir
política portátil desta API.

A qualificação FreeCAD exige uma cópia privada recompilada do host em
`freecad-host/build`, com
[patch Qt por viewport](../../../examples/coinrender/freecad_sampling_policy.patch).
O patch usa a propriedade Qt booleana `coinRenderPortableSampling` e publica
`coinRenderActiveSamplingPolicy`; trocar a propriedade recria o adapter.
`freecad_sampling_policy.FCMacro` reaplica os gates existentes de screen-content
sem reduzir tolerâncias. O caso projetivo usa uma imagem POT 256×256 e
qualidade 0,5 para exercer NEAREST_MIPMAP_LINEAR/minificação no host, além do
conteúdo legado. A câmera é estabelecida por dois redraws antes do
baseline; um ciclo visível/oculto prepara os recursos de texto antes do
baseline principal. O perfil qualificado é aquecido e declara
`cold_start_qualified=false`; não certifica o primeiro frame absoluto nem corrige
a transição inicial de borda registrada no estudo NVIDIA/wgpu portable.
A macro bloqueia input interativo somente no host privado, fecha menus/tooltip
próprios, solicita ABOVE por EWMH (`wmctrl`/`xprop`) sem recriar a superfície
Qt e exige dois renders/leitura de tela idênticos em até 0,5 s. Isso delimita
apresentação sem reduzir os gates de imagem. Cleanup deve estabilizar em até
5 s antes do gate idle original de 1,2 s. Requer WM X11 com suporte a ABOVE.
O runner congela as três macros em `fixture/` antes de iniciar o host e grava
um manifest SHA-256 por execução.
O runner exige exatamente um resultado por processo, submissão física do backend
e a política ativa pedida. Usa object/DPR 1 ou 2 (`QT_SCALE_FACTOR` forçado;
não certifica troca de monitores/DPI físico distinto). AMD/Vulkan recebe sessão Xwayland
privada; OpenGL e NVIDIA usam o desktop acelerado, porque a sessão headless
desta máquina não prova DRI3 GL e o WM NVIDIA encerrou antes do host. Preferências são privadas em ambos os casos. Não usar um
FreeCAD genérico sem esse patch como evidência da política portátil.

O build do host reaproveitado, seus comandos de compile/link, fonte alterada,
patch, hashes e logs ficam no root de artefatos e no ledger versionado. O host e
fonte FreeCAD originais não são modificados. A receita suplementar de build
preserva o snapshot do CMake/objetos do host anterior e recompila Quarter;
para outra árvore, reconfigure o host completo contra CoinRender antes de usar
a macro. Não copiar apenas o executável sem Part/Material/lib/recursos/Pivy.

Contrato e resultados: [continuação Linux](../../../docs/coin-render-sampling-api-linux-20261008.md).
