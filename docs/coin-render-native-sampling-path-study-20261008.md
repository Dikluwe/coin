# Caminho nativo e controle do milhão — estudo de 2026-10-08

**A separação dos perfis remove o custo extra do sampling experimental do
shader/bloco nativo. A regressão anterior NVIDIA/1milhão não se reproduziu como
efeito persistente do seletor.** O milhão sem texturas usa o mesmo caminho
instanciado, bytes e hash de uniformes em native/fine_uniform; a sonda mede
≈7,207 ms GPU em ambos. Isso não demonstra ganho de sampling para prédios.

Implementação somente em `codex/coin-portable-sampling-study`, commit
`eff6103da599b8dc6b2ee4c92a2ff23568dc4661`, sobre
`ccd43105f0fcb55d599589516f1ff66f910711f4`. Coin original permanece intacto;
`codex/coin-render` recebe docs/checklist/evidências. Política pública portátil,
Android, Windows e FreeCAD continuam pendentes de proposta e qualificação.

## Mudança delimitada

Wgpu compila o perfil native sem helpers/branches/derivadas do estudo e sem
`study_texture_sizes`. Uniformes, binding range, arena e orçamento usam o
prefixo original de3040 bytes, em vez de3168; com alinhamento256, stride3072 em
vez de3328. Instancing sem textura sempre usa o perfil nativo, mesmo quando o
processo seleciona fine_uniform. O transporte de instâncias continua96 bytes.
Só fine_uniform consulta as dimensões dos recursos ligados.

BGFX compila fragmentos distintos native/study para superfície, peeling,
weighted OIT e sombras4/8. Native não cria/envia o uniforme de tamanhos, nem
compila os helpers. Os128 bytes extras por Draw e sua comparação de agrupamento
foram removidos. Fine_uniform mantém dimensões dos recursos ligados no backend,
incluindo RTT. Instancing conserva o shader dedicado.

O seletor experimental continua privado e fixo por processo, com fallback
native para valores ausentes/desconhecidos. Nenhum contrato público, tolerância,
matriz, formato, publicação ou regra de recuperação foi alterado.

## Qualidade e recursos

AMD Renoir/Mesa25.2.8 e RTX3060 Laptop/NVIDIA610.57.04: wgpu Vulkan AMD/NVIDIA,
wgpu OpenGL AMD, BGFX Vulkan AMD/NVIDIA e BGFX/OpenGL. Este último informa
vendor/device0: validação funcional, sem certificar a GPU física.
Native e fine_uniform foram exercitados em todas as coortes; o piloto é wgpu.

| Coorte | Processos | PASS | FAIL |
|---|---:|---:|---:|
| Piloto de texturas avançadas | 6 | 6 | 0 |
| Mip profundo, formatos, unidade0/7 e wrap | 12 | 8 | 4 |
| RTT direto explícito filter2 | 12 | 8 | 4 |
| Viewport deslocado | 12 | 12 | 0 |
| Gates públicos/projective/sampling/procedural/RTT | 60 | 40 | 20 |
| LargeBindings/MultiDevice/stress e instancing | 24 | 24 | 0 |
| Sombras8, viewport e alpha/RTT | 36 | 36 | 0 |
| **Total** | **162** | **134** | **28** |

Nenhum SKIP. Fine_uniform passa os seis perfis de mip profundo e RTT direto
com máximo1 (MAE máximo0,75/0,25). Viewport passa em ambos com máximo1.
Os oito FAIL adicionais do native são a divergência nearest AMD já diagnosticada.
Os20 FAIL dos gates públicos são iguais em ambos os modos: projective/sampling
CPU–CoinGL AMD e esfera/defaultUV OpenGL com máximo4 frente ao gate3. Continuam
FAIL, sem elevar tolerância ou reivindicar paridade universal.

Os controles de recursos preservam25.600 draws independentes, reuso entre
dispositivos, stress e instancing. Sombras/transparência não regrediram nos36
processos. Rust:46 testes passaram (41 lib,2 Gouraud,3 sombras). Builds Release
wgpu e BGFX passaram. Uma primeira versão do novo teste Naga/GLSL falhou por
override não resolvido; o teste passou após resolver true/false antes da tradução,
como wgpu faz na compilação. O log inicial foi preservado. QA antecedeu somente
o ajuste final de rótulo/contagem do trace; builds finais e benchmarks usam esse
ajuste, sem diferença nos pipelines de renderização.

## Custo da janela texturizada

84 processos válidos, GPU sequencial, Vulkan,1280×720,45 warmups/150 amostras,
ordem direta e inversa. Cada célula é a mediana de300 amostras medidas dos dois
processos. O tempo é a chamada CPU render/present, sem readback nem drenagem
final da fila; não mede latência de tela ou duração GPU isolada.

| GPU/backend, POT128²,8 unidades, qualidade0,5 | Produção baseline | Native anterior do estudo | Native separado | Fine_uniform |
|---|---:|---:|---:|---:|
| AMD/wgpu | 0,422 ms | 0,461 ms | 0,417 ms | 0,992 ms |
| AMD/BGFX | 0,416 ms | — | 0,424 ms | 1,260 ms |
| NVIDIA/wgpu | 0,586 ms | 0,580 ms | 0,586 ms | 0,584 ms |
| NVIDIA/BGFX | 0,591 ms | — | 0,586 ms | 0,587 ms |

AMD/wgpu reduz≈9,5% frente ao native anterior do laboratório, próximo do
baseline. Qualidade0,8 e NPOT129×127 também retornam perto do baseline. BGFX
AMD/POT conserva≈2% nesta amostra, com variação entre processos: não se afirma
overhead universal zero. Fine_uniform continua mais caro que native na AMD;
a vantagem anterior era frente ao portátil de duas amostras em POT. NVIDIA
nesta janela não revela uma vantagem GPU: CPU/apresentação limita a inferência.
NPOT mantém duas amostras. Todas as cargas/medianas individuais estão no JSON/CSV.

O native anterior wgpu foi congelado antes do rebuild, sem alterar executável
ou bibliotecas. LD_LIBRARY_PATH por filho aponta para original-study/lib;
copiar apenas o executável deixaria seu RUNPATH absoluto carregar o rebuild.
Hashes e resoluções ldd distinguem baseline, controle congelado e novo build.

## Milhão NVIDIA: investigação concluída neste PC

Cena estática de1.000.000 prédios e chão,1.000.001 instâncias,1 draw e nenhuma
unidade de textura. Duas campanhas de18 processos: todas as seis permutações
baseline/native/fine_uniform,45 warmups/150 amostras, telemetria e DPMS On.
A fase anterior usa o runtime congelado; a posterior usa o novo perfil.

| Fase | Baseline | Native | Fine_uniform |
|---|---:|---:|---:|
| Runtime anterior congelado | 9,957 ms | 9,833 ms | 9,571 ms |
| Perfil separado | 9,743 ms | 9,701 ms | 9,544 ms |

São medianas das seis medianas por processo, não uma estimativa de benefício
do filtro. Na fase anterior, o ensaio native4-2 terminou com monitor Off e foi
rejeitado/preservado. Sua repetição, ao final da investigação, deu10,445 ms e
entra nesta tabela; isso quebra a intercalação temporal estrita daquele slot.
O resultado desfavorável está incluído. Na fase posterior todos os18 processos
mantiveram On, com intervalos das medianas baseline9,383–9,993;
native9,506–9,787; fine_uniform9,476–9,775 ms.

Quatro processos separados de diagnóstico (native/fine_uniform/fine_uniform/
native,12 warmups/24 amostras) auditam144 quadros: todos têm3040 bytes de
uniformes e hash FNV64 `8e086e087081fcae`. Timestamps GPU do trecho do encoder
produzem medianas7,206400/7,206912/7,207936/7,206912 ms. A sonda resolve/copia
timestamps e espera sincronicamente, portanto esses processos foram excluídos
da campanha ordinária de render/present.

Não apareceu o padrão anterior de fine_uniform10,740 contra baseline9,070 ms
como custo persistente do seletor. Variação de clocks/temperatura foi registrada,
mas não foi isolada como causa; não se atribui causalidade a ela. A conclusão
é limitada à cena, ao hardware e ao driver: sampling não executa nessa carga,
e suas diferenças entre processos não demonstram melhoria de FPS.

## Decisão, pendências e evidência permanente

Fechar a investigação do custo do laboratório e do milhão como estudo local.
Manter native padrão e fine_uniform como candidato opt-in isolado. Próxima
frente: política comum explícita por recurso/draw, API/capacidades e limites,
sem seletor global/packing experimental, preservando o custo do nativo.
Depois qualificar perspectiva/derivadas/NPOT/HDR, Android/Windows/FreeCAD,
streaming e outros drivers. As divergências CoinGL AMD continuam documentadas.

[Checklist](coin-render-next-fronts-checklist.md),
[agregado](validation/native-sampling-path-study-20261008/results-native.json) e
[manifest](validation/native-sampling-path-study-20261008/manifest.json).
Logs,CSV.gz,telemetria, fontes/patch e hashes ficam versionados neste diretório.
O manifest registra comandos, revisões, identificação dos perfis, scopes,
ensaios rejeitados e hashes. Bibliotecas completas e builds permanecem em
`/mnt/Laranja/Git/externos/coin-portable-sampling-artifacts/20261008-native`.
O milhão é externo e identificado por hash; as cenas texturizadas estão copiadas.

[Runners e instruções](https://github.com/Dikluwe/coin/blob/eff6103da599b8dc6b2ee4c92a2ff23568dc4661/testsuite/reproducers/portable-sampling-study/README.md).
analyze_native.py também lê CSV.gz e permite recalcular o agregado congelado.
O relatório anterior permanece como histórico e recebe link desta continuação.
