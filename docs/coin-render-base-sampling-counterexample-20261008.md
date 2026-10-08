# Uma amostra nearest/trilinear: centro base versus centro do mip ativo

**POT não é condição suficiente para centralizar no mip base com segurança na AMD.**
O algoritmo `base`/`base_uniform` descrito foi reproduzido numa sonda EGL sem
Coin. Ele funciona matematicamente com seleção nearest exata, mas a precisão
nativa pode selecionar o texel vizinho nos mips grossos. O novo candidato
`fine` centraliza no mip `floor(LOD)` e conserva uma amostra/hardware trilinear;
passou estes controles. A integração e medição foram concluídas na continuação
vinculada abaixo.

O estudo existente não tinha seletores `base`/`base_uniform` na bridge.
Os nomes abaixo descrevem **variantes da sonda**, sem promover renderer ou mudar
sampling padrão. Os resultados complementam a
[avaliação anterior](coin-render-portable-sampling-study-20261007.md).

**Continuação concluída:** [fine/fine_uniform integrados e medidos](coin-render-fine-sampling-study-20261008.md)
na branch isolada, com POT/NPOT, formatos, viewport e RTT. O restante deste texto
preserva os resultados da sonda inicial e sua limitação de alcance.

## Experimento e resultados

18 processos, 624 imagens de64×64, 2.506.752 pixels verificados contra oracle
CPU autoral de precisão double, com cores40/160 e último mip100.
AMD Renoir/radeonsi Mesa25.2.8 e NVIDIA3060Laptop610.57.04.
Texturas POT128/256/512/1024/2048/4096 e NPOT257/511; cadeia completa RGBA8
uploadada por nível, `NEAREST_MIPMAP_LINEAR`, MAG_LINEAR e repeat.
As cadeias autorais isolam sampling; o teste não qualifica geração de mips NPOT.

Cada combinação tem seis variantes: native UV; centro base; centro base com
n0 uniforme; centro em `floor(LOD)`; centro em cada um dos dois mips; fetch inteiro
com mistura explícita. `base`, `base_uniform` e `fine` usam duas amostras em NPOT,
como fallback solicitado. LODs1,25/3,5/5,5/7,25/8/8,25/8,5/9,25/10,75, limitados
pela cadeia; os valores são explícitos para isolar a escolha de texel.

| Variante | Imagens comparadas | Divergências >1 RGB | Erro máximo RGB |
|---|---:|---:|---:|
| native | 104 | 43 | 120 |
| base | 104 | 22 | 120 |
| base_uniform | 104 | 22 | 120 |
| fine | 104 | 0 | 0 |
| centro por mip, duas amostras | 104 | 0 | 0 |
| fetch inteiro por mip | 104 | 0 | 0 |

As divergências estão na AMD. NVIDIA passou as seis variantes. POT128/256
passaram `base`; POT512–4096 falharam em mips profundos. A primeira divergência
**entre os LODs amostrados** ocorre em7,25, que mistura mip7 com mip8: max30
na textura4096². Em LOD8 o máximo é120. `base_uniform` teve as mesmas métricas
que `base`: uniformizar o tamanho não corrige a correspondência de texels.
NPOT passou pelo fallback de duas amostras; não é pass da aproximação de uma
amostra com centro base em NPOT.

No contracontrole adicional, o LOD8 foi **calculado das derivadas das UV originais**:
`du/dx = 1/16`, textura4096², footprint256 texels/pixel. O shader verifica o
LOD calculado e a cor do oracle verifica cada pixel, incluindo repeat negativo.
Na AMD, `base`/`base_uniform` erraram512 de4.096 pixels (MAE15/max120).
Pixel31 deveria ser40 e saiu160. `fine`, duas amostras e fetch deramMAE0/max0.
NVIDIA deuMAE0/max0 em todos. Logo, a divergência também ocorre num footprint
coerente com o LOD, sem depender de um LOD artificial incompatível com as UV.

Os18 processos retornam0 porque os controles obrigatórios fine/centro/fetch e
integridade RGBA/GL passaram. Isso **não** transforma as variantes base em PASS:
seus44 resultados divergentes, somando base e base_uniform, constam separadamente.
O oracle permite no máximo1 RGB para conversão UNORM; os caminhos corretos
medidos tiveram erro0. Os gates anteriores de CoinRender não foram modificados.

## Por que POT não basta

Em aritmética exata, um centro base continua dentro do texel ancestral correto.
Porém, para um texel base imediatamente antes de uma fronteira ancestral, a
margem no mip `L` é apenas `0,5 / 2^L` texel. Ela diminui conforme o mip engrossa.

No caso4096², o centro base selecionado é `2047,5 / 4096 = 0,4998779296875`.
No mip8 (16texels), isso vira `7,998046875`, só1/512texel abaixo de8.
A sonda AMD escolhe a região do texel8, apesar de a coordenada ideal pertencer
ao texel7. É consistente com a quantização já medida no estudo de origem;
não é uma regra universal declarada sobre toda GPU/driver.

Centralizar no mip fino **ativo**, `lo = floor(LOD)`, evita a margem que encolhe
por toda a cadeia. Em POT, o centro do texel desse mip fica a0,25 ou0,75texel
no mip seguinte: distância mínima de0,25 de sua fronteira. Há apenas uma redução
entre os dois mips participantes. O hardware ainda pode fazer a mistura numa
única chamada com LOD fracionário:

```text
LOD = calcular pelas UV originais
lo = floor(LOD)
POT e minificação:
  n = dimensões do mip lo
  uv_centro = (floor(uv_endereçada * n) + 0.5) / n
  cor = sampler(uv_centro, LOD)
NPOT:
  centros por mip + duas amostras + mistura explícita
magnificação:
  sampling linear nas UV originais
```

A margem matemática explica o resultado, mas não constitui qualificação universal
do candidato: faltam outras geometrias, formatos, transição de magnificação,
wrapping/clamp completo, wgpu/BGFX integrados e outras plataformas.
É possível estudar `fine_uniform` calculando dimensões POT a partir do uniforme
base e do mip lo. Isso preservaria a correspondência do mip ativo; não foi
medido nesta rodada. A economia de uma chamada é uma hipótese de desempenho,
não um ganho de FPS demonstrado aqui.

## Reprodução e evidências

Fontes na branch de estudo:
[base_probe.cpp](https://github.com/Dikluwe/coin/blob/codex/coin-portable-sampling-study/testsuite/reproducers/portable-sampling-study/base_probe.cpp)
e [run_base.py](https://github.com/Dikluwe/coin/blob/codex/coin-portable-sampling-study/testsuite/reproducers/portable-sampling-study/run_base.py).

```sh
c++ -O2 -std=c++17 testsuite/reproducers/portable-sampling-study/base_probe.cpp \
  -o "$study_artifacts/base-probe" -lEGL -lOpenGL
python3 testsuite/reproducers/portable-sampling-study/run_base.py \
  --artifacts "$study_artifacts"
```

[Oracle/contagens](validation/base-sampling-counterexample-20261008/results.json),
[AMD com derivadas](validation/base-sampling-counterexample-20261008/amd-4096-derived.log),
[NVIDIA com derivadas](validation/base-sampling-counterexample-20261008/nvidia-4096-derived.log),
[manifest/hashes](validation/base-sampling-counterexample-20261008/manifest.json).
Logs, dumps RGBA e recibos são versionados. Build/executável ficam em
`/mnt/Laranja/Git/externos/coin-portable-sampling-artifacts/20261008-base`.
O binário linka EGL/OpenGL e dependências C++, sem Coin.
