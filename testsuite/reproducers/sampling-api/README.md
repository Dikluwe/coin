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
