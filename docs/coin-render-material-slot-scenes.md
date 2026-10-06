# Cidades com muitos slots de material

`scripts/coinrender/generate_material_slot_scene.py` prepara cenas para medir a
captura, a tabela de materiais e as animações do CoinRender. O controle continua
sendo o Coin/OpenGL clássico, usando exatamente a mesma cena e os mesmos quadros
lógicos em cada comparação.

## Gerar

Use a cidade determinista existente, criada por
`examples/coinrender/generate_large_scene.py` com `--grid 200 --seed 136`:

```sh
python3 examples/coinrender/generate_large_scene.py /tmp/city-base.iv --grid 200 --seed 136
python3 scripts/coinrender/generate_material_slot_scene.py /tmp/city-materials-40001.iv --source-city /tmp/city-base.iv --objects 40000 --material-slots 40001
```

Repita a segunda chamada com `--material-slots 4097` ou `257` e outro caminho de
saída. `--material-slots` inclui o chão: 40.001 significa 40.000 materiais de
prédios mais um material do chão. Todos os materiais solicitados aparecem em
objetos renderizados. O programa também grava `cena.iv.manifest.json`, com SHA256
da fonte, do gerador, da cena e do layout, parâmetros e contagens esperadas.

`--objects` conserva um prefixo dos prédios da fonte; se omitido, conserva todos.
O chão permanece do tamanho original. O intervalo aceito é de um até 64.000
prédios, e de dois até `objects + 1` slots. Fonte e saídas devem ser arquivos
diferentes; saídas existentes são preservadas.

## O que muda

O gerador aceita somente o formato ASCII restrito da cidade de referência. Ele
valida os `DEF`/`USE`, os valores finitos e positivos e cada objeto com exatamente
`Material`, `Transform` e `Cube`. Rejeita estruturas adicionais, transparência e
shininess diferente de `0.2`, em vez de reescrever uma cena Inventor arbitrária.

Somente as linhas de materiais dos prédios são substituídas. A verificação final
confere por bytes o resto do layout escolhido: luzes, PHONG, chão, posições,
escalas, Cube compartilhado e ordem de travessia. A paleta usa apenas cores
difusas distintas; shininess continua `0.2`, com os demais campos nos mesmos
defaults. Não há identificador escondido em outro campo de material.

Cada componente da paleta é um múltiplo exato de `1/128`, entre `0.25` e
`0.7421875`. A permutação dos inteiros que formam as cores é bijetiva no intervalo
suportado; os literais decimais são conferidos após conversão para float32. Isso
evita que cores inicialmente distintas colapsem por arredondamento do texto. A
paleta tem margem para a variação difusa de ±0,18 do benchmark.

## Contagens e limites da interpretação

Para 40.000 prédios, os três níveis têm a mesma previsão de 40.001 draws lógicos
e 480.012 triângulos:

| Slots estáticos totais | Materiais de prédios | Tabela GPU wgpu nominal |
|---:|---:|---:|
| 257 | 256 | 20.560 bytes |
| 4.097 | 4.096 | 327.760 bytes |
| 40.001 | 40.000 | 3.200.080 bytes |

A captura deduplica os bytes completos de `CoinRenderMaterialSnapshot`, incluindo
cores, shininess e transparência. `SoCallbackAction::getMaterial` obtém o diffuse
float do `SoLazyElement` para estes `SoMaterial`; a deduplicação não usa o nome do
nó nem uma cor RGB de oito bits. O builder usa um índice temporário a partir de
32 materiais, com limite de 65.536 entradas e fallback para a busca existente.
O tamanho nominal acima usa os 80 bytes do `GpuMaterial` atual do wgpu; não é
medição de RSS, capacidade alocada ou tráfego de upload.

O `run_animation_benchmark.py` existente já aceita esta cena com
`materials-10`, `materials-100`, `transforms-10` e `static`. No cenário de 40.000
prédios, os dois casos de material selecionam e clonam respectivamente 4.000 e
40.000 ocorrências antes de medir. A animação altera apenas `diffuseColor`; a
estrutura, a geometria e as camadas permanecem no mesmo perfil opaco da cidade.

**O número estático não é uma garantia durante a animação.** Clones de um
material compartilhado podem adquirir valores próprios. Uma captura completa
deduplica os valores atuais, enquanto um overlay pode manter a tabela e seus
slots anteriores. Confirme a cardinalidade efetiva em cada quadro pelo trace do
backend, incluindo `rust_instances.material_bytes` no wgpu, e guarde a seleção e
os hashes dos quadros. Nunca identifique igualdade pela revisão ou pelo nome do
material.

O único Cube compartilhado no scene graph também não garante ranges de captura
compartilhados nem um único submit GPU. Mais slots podem aumentar os ranges e os
vértices de origem. Observe `action.vertices/indices/draws` e os grupos do lowering
antes de atribuir todo o custo à tabela de materiais.

## Verificar o gerador

```sh
python3 testsuite/coinrender/test_material_slot_scene.py
```

Os oráculos CPU conferem a projeção independente de todos os bytes que não são
linhas de material, determinismo, cardinalidade e uso da paleta, preservação de
aliases do Cube, round-trip float32 até o limite, seleção de prefixo e rejeição
de fontes ou parâmetros inválidos. Eles não executam renderizador ou GPU.
