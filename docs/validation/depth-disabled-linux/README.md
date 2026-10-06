# Profundidade OFF com escrita ON — Linux, 2026-10-06

Base: `c8ee75fd4bc5014c1a7deeb238fea435f4cc6d1f`, branch
`codex/coin-render-transform-performance`. GPU NVIDIA RTX 3060 Laptop,
driver 610.57.04. Esta entrega qualifica o comportamento já implementado;
altera testes, registro CTest e documentação. Produção e ABI permanecem iguais.

## Caso discriminante

Três quads: azul distante com teste/escrita ON; vermelho próximo com teste OFF
e escrita ON; verde intermediário com teste/escrita ON. CoinGL termina verde:
a escrita não ocorre sem teste. Se o vermelho escrevesse depth indevidamente,
o verde seria rejeitado. Ligar teste/escrita no vermelho produz vermelho e
armazena profundidade menor que a do verde.

O gate verifica seis frames, incluindo repetição, escrita OFF, função NEVER
ignorada quando teste OFF e restauração OFF/ON/OFF. A captura deve conservar
os valores explícitos originais test/write, sem normalizá-los no plano.

## Resultados

| Execução | Comparações RGB GPU/CoinGL | Erro máximo /255 |
| --- | ---: | ---: |
| wgpu/Vulkan, profundidade | 6 | 0 |
| BGFX/Vulkan, profundidade | 6 | 0 |
| BGFX/OpenGL, profundidade | 6 | 0 |
| wgpu/Vulkan, fragmentos completos | 119 | 1 |

Cada execução focada GPU também passou seis comparações CPU/CoinGL. Captura/CPU
passou separadamente nos dois builds. Todas as seis execuções terminaram com
código zero; não houve skip. O gate completo teve ROI MAE máximo 0,666667/255
e zero pixels acima de erro 3/255.

A leitura de depth da GPU é obrigatória: buffer completo, valores finitos em
[0,1], amostra discriminante diferente do clear e controle ON mais próximo.
Os controles OFF e a restauração exigem igualdade exata de cor e depth dentro
de cada executor. A referência nativa CoinGL compara RGB; não foi lido o buffer
de profundidade GL. Não se afirma igualdade numérica CPU/GPU de depth.

## Reprodução e evidência

Os comandos, ambiente, duração, hashes dos binários executados e contagens estão
em [summary.json](summary.json); os logs correspondentes estão neste diretório.
`build-wgpu.log` e `build-bgfx.log` registram a recompilação do gate. Os logs foram
normalizados apenas para retirar espaços ao final das linhas; hashes originais
e arquivados estão no resumo.

Com os builds configurados para o executor desejado:

```sh
cmake --build "$build" --target CoinRenderFragmentPolicyTest -j 4
LD_LIBRARY_PATH="$build/lib" "$build/bin/CoinRenderFragmentPolicyTest" --capture --depth-only
```

Para GPU, use o ambiente da execução correspondente no resumo e execute:

```sh
"$build/bin/CoinRenderFragmentPolicyTest" --gpu --depth-only
"$build/bin/CoinRenderFragmentPolicyTest" --gpu
```

Os gates registrados em CTest são `CoinRenderDepthDisabledCaptureTest`,
`CoinRenderDepthDisabledGpuTest` (wgpu) e `CoinRenderDepthDisabled_<renderer>`
(BGFX). Os testes GPU mantêm o resource lock comum. Indisponibilidade do executor
pode retornar 77; ausência de referência CoinGL válida faz o gate falhar.
A campanha usou Linux/NVIDIA; não executou Windows ou outros drivers.
