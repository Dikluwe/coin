import json
from pathlib import Path
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.patches import Patch
from matplotlib.lines import Line2D
root=Path('/tmp/coin-render-first-frame');stage=root/'docs/validation/material-geometry-linux'
labels={'coingl':'Coin/OpenGL','bgfx-vulkan':'BGFX/Vulkan','bgfx-opengl':'BGFX/OpenGL','wgpu-vulkan':'wgpu/Vulkan'}
names=('bgfx-vulkan','bgfx-opengl','wgpu-vulkan')
data={n:json.loads((stage/n/'report-summary.json').read_text()) for n in ('offscreen','stress','window','controls')}
def groups(n,role):return {(g['case'],g['variant']):g for g in data[n]['campaigns'][role]['groups']}
def metric(n,role,c,v,key='total_ms',stat='median_ms'):return groups(n,role)[c,v]['stats'][key][stat]
def table(n,percent):
 lines=['| Render | Materiais (ms) | Geometria (ms) |','|---|---:|---:|']
 for v in ('coingl',*names):
  cells=[]
  for mode in ('materials','geometry'):
   c=f'{mode}-{percent}';a=metric(n,'before',c,v);b=metric(n,'after',c,v);cells.append(f'{b:.2f} (referência)' if v=='coingl' else f'{a:.2f} → **{b:.2f}**')
  lines.append('| '+labels[v]+' | '+' | '.join(cells)+' |')
 return '\n'.join(lines)
fig,axes=plt.subplots(2,2,figsize=(12,7.5),layout='constrained')
for row,n in enumerate(('offscreen','window')):
 for col,mode in enumerate(('materials','geometry')):
  ax=axes[row,col];c=mode+'-10';old=[metric(n,'before',c,v) for v in names];new=[metric(n,'after',c,v) for v in names]
  ax.barh([y-.17 for y in range(3)],old,height=.3,color='#c1cbd5',label='Antes');ax.barh([y+.17 for y in range(3)],new,height=.3,color='#287aac',label='Depois')
  for y,(a,b) in enumerate(zip(old,new)):
   ax.text(a+7,y-.17,f'{a:.0f}',va='center',fontsize=9);ax.text(b+7,y+.17,f'{b:.0f}',va='center',fontsize=9,color='#164b6b',bbox=dict(facecolor='white',edgecolor='none',pad=.1,alpha=.95))
  ref=metric(n,'after',c,'coingl');ax.axvline(ref,color='#b16b16',ls='--',lw=1.6,label=f'Coin/OpenGL: {ref:.0f} ms')
  ax.set_yticks(range(3),[labels[v] for v in names]);ax.invert_yaxis();ax.set_xlim(0,max(old)*1.14);ax.grid(axis='x',alpha=.18);ax.set_axisbelow(True);ax.spines[['top','right']].set_visible(False)
  ax.set_title(('Offscreen' if n=='offscreen' else 'Janela')+' · '+('materiais' if mode=='materials' else 'geometria'));ax.set_xlabel('Tempo total por quadro (ms)');ax.text(ref+7,.985,f'Coin/OpenGL: {ref:.0f} ms',transform=ax.get_xaxis_transform(),va='top',fontsize=8,color='#8b520b',bbox=dict(facecolor='white',edgecolor='none',pad=.1))
fig.suptitle('CoinRender · 40 mil prédios · 10% animados',fontsize=15)
fig.legend([Patch(color='#c1cbd5'),Patch(color='#287aac'),Line2D([0],[0],color='#b16b16',ls='--')],['Antes','Depois','Coin/OpenGL (referência)'],loc='lower center',bbox_to_anchor=(.5,0),ncols=3,fontsize=9)
fig.get_layout_engine().set(rect=(0,.055,1,.945))
fig.savefig(stage/'material-geometry.png',dpi=160);fig.savefig(stage/'material-geometry.svg');plt.close(fig)
controls=['| Caso | Render | Antes → depois (ms) | Primeiro total antes → depois (ms) |','|---|---|---:|---:|']
for r in data['controls']['timing']['comparisons']:
 if r['variant']=='coingl':continue
 m=r['metrics'];a=m['total_median_ms'];f=m['first_total_ms_from_csv_median'];controls.append(f"| {r['case']} | {labels[r['variant']]} | {a['before']:.2f} → {a['after']:.2f} | {f['before']:.1f} → {f['after']:.1f} |")
first=['| Caso | Render | Primeiro total antes → depois (ms) | RSS antes → depois (MiB) |','|---|---|---:|---:|']
for n in ('offscreen','stress'):
 for r in data[n]['timing']['comparisons']:
  if r['variant']=='coingl':continue
  m=r['metrics'];f=m['first_total_ms_from_csv_median'];rss=m['peak_rss_median_mib'];first.append(f"| {r['case']} | {labels[r['variant']]} | {f['before']:.1f} → {f['after']:.1f} | {rss['before']:.1f} → {rss['after']:.1f} |")
text=f"""# Materiais e geometria no CoinRender — Linux, 2026-10-05

Branch: `codex/coin-render-transform-performance`.
Implementação de overlays e lowering: `6d076c4707`.
Índice de materiais e notificações: `ff289a50b9`.
Antes: conteúdo compilado `b02aa54783`. Coin/OpenGL clássico
(`SoGLRenderAction`, Coin3D): controle congelado `4d63bb9930`.
Continuação da [etapa de instâncias e transformação](coin-render-wgpu-instancing-linux.md).

## Alterações

- **Common:** atualiza materiais opacos escalares na tabela e posições locais de
  `SoCube` positivo, junto das translações pendentes. Preserva índices, UVs,
  normais, matrizes e centros de ordenação. Atualiza o ID de origem do draw
  quando o Cube muda. A transação restaura todos os campos e a revisão em caso
  de falha; os objetos continuam pendentes para retry.
- **Prova do Common:** exige tipos exatos, objeto isolado `SoSeparator` com
  material, transform e Cube, material OVERALL, ausência de override efetivo,
  conexões/ignored e callbacks adicionais. Confere todos os consumidores de
  slots/ranges e todas as ocorrências sintáticas do material, inclusive sob
  overrides parciais. Referências guardadas para restauração não contam como
  ocorrência renderizada. Aliases ambíguos fazem captura completa.
- **Captura de materiais:** índice temporário no builder, hash dos bytes e
  `memcmp` em colisões, mantendo o menor slot e a ordem da busca anterior.
  Ativa a partir de 32 materiais; sincroniza acréscimos por polygon/stroke,
  cópia, transferência, reset e troca de builders. Abrange `captureMaterial`
  de triângulos, linhas, pontos, formas nativas e geometria indexada. A função
  independente `Core::material` e os loops de assembly mantêm a busca anterior.
- **BGFX e wgpu:** fatoram posições quando cada eixo contém extremos opostos
  e apenas valores `−h`, zero com sinal ou `+h`, com reconstrução float exata.
  Canonicalizam em `−1/0/+1` e levam a escala positiva para a matriz de posição
  da instância. **As normais e a matriz normal authored são preservadas**;
  essa escala de transporte não redefine a transformação authored.
  A prova depende de valores/atributos/topologia, sem reconhecer nós ou cenas.
- **Lowering:** comparação exata com a primeira/última malha evita FNV de
  ranges repetidos. BGFX admite até 65.536 spans, mantendo o teto de 256 malhas;
  wgpu mantém 128 grupos consecutivos. A/B/A conserva sua ordem. Geometria
  plana, zero, subnormal, com pontos interiores ou sem reconstrução exata
  continua pelo caminho de igualdade/fallback existente.
- **Notificações:** campos estáveis são verificados uma vez por fonte dirty
  antes da mutação, evitando repetir a inspeção em cada width/height/depth.

A ABI privada continua 43. Rust e shaders não mudaram nesta etapa.
O material overlay depende da propriedade dos slots: fontes diferentes com
valores inicialmente iguais podem compartilhar slot e exigir captura completa
antes de readmissão. A atualização não funde novamente slots que se tornam
iguais. A comparação de semântica usa atributos expandidos por draw.

### Limites

O Common mantém até 65.536 objetos/estados/draws/materiais, dimensões Cube
entre 0,0001 e 32.768, 24 vértices/36 índices no range qualificado e metadata
suplementar estimada até 16 MiB. Posições pendentes + undo têm teto de 32 MiB
(1.048.576 entradas por vetor); a validação usa até 4 MiB temporários de slots.
O índice de materiais limita metadata a 65.536 slots, estimativa abaixo de
9 MiB, e retorna à busca linear no excesso ou falha de alocação opcional.
Não limita a tabela de materiais. Lowerers limitam metadata estimada a 8 MiB.
Os budgets anteriores de geometria/instâncias/materiais continuam. São limites
de payload/metadata; o RSS inclui planos, drivers e recursos coexistentes.

Optouts: `COIN_RENDER_DISABLE_MATERIAL_OVERLAY=1`,
`COIN_RENDER_DISABLE_CUBE_OVERLAY=1`,
`COIN_RENDER_DISABLE_MATERIAL_INTERNING=1` e
`COIN_WGPU_DISABLE_DIAGONAL_MESH_LOWERING=1`.
O optout de translação continua desativando o perfil comum completo.

## Protocolo e resultados

Cidade com 40.000 prédios, seed 136, 480.012 triângulos, PHONG, duas luzes
direcionais e 1024². Cena SHA256:
`bb9ccc612cd38ffb749ee599d9350a80974649eab5bf477d2f344538a42b2576`.
Ryzen 5800H, NVIDIA RTX 3060 Laptop, driver 610.57.04, Linux
6.17.0-42, GCC 13.3, Release; governor powersave. O ambiente seleciona NVIDIA
explicitamente. Os números são tempos CPU de parede, incluindo update, render
e publicação; não são tempo GPU isolado nem garantia de FPS em outras cenas.

O runner alterna antes/depois e rotaciona ordem por rodada. O Coin/OpenGL
congelado é executado **uma vez por caso/rodada**, com CSV/log compartilhados
nas duas campanhas. O controle não é uma segunda execução depois da mudança.
Agregação: mediana das três medianas por processo. p95/p99 e máximos completos
estão nos JSON/CSV. Parâmetros iguais dentro de cada par não estabelecem
igualdade de estado térmico, driver ou compositor.

### Offscreen, 10% dos objetos

Três rodadas × 30 quadros medidos, após 5 warmup.

{table('offscreen',10)}

Redução: materiais BGFX/Vulkan 88,9%, BGFX/OpenGL 87,5%, wgpu 69,0%;
geometria 84,1%, 83,1% e 73,9%, respectivamente. Wgpu/geometria continua
10,6% acima do Coin/OpenGL nessa medição.

### Janela real, 10% dos objetos

Três rodadas × 15 quadros medidos, após 3 warmup. A política de apresentação
foi preservada; o drain final continua registrado separadamente pelo bench.

{table('window',10)}

Wgpu/geometria ficou 3,9% acima do controle em janela.

![Tempos antes/depois e referência Coin/OpenGL](validation/material-geometry-linux/material-geometry.png)

### Carga com 100% dos objetos

Três rodadas × 7 quadros medidos, após 3 warmup; teste curto de carga.
Não se usa esse conjunto para concluir estabilidade de cauda p99.

{table('stress',100)}

### Primeiro quadro e memória

Primeiro total obtido da primeira linha do CSV, incluindo warmup; RSS é o
pico do processo, não VRAM.

{chr(10).join(first)}

A ablação instrumentada na mesma revisão, wgpu/materials-100, deu primeira
travessia CPU de **377,14 ms com índice** e **2555,19 ms sem índice**. São
duas execuções de diagnóstico, não uma mediana qualificada.

### Preservação de transformação, câmera e estático

Três rodadas curtas × 7 quadros, após 3 warmup.

{chr(10).join(controls)}

Há custo observado: transformação BGFX aumentou 1,91–2,68 ms; wgpu 0,42 ms.
Estático/câmera variaram até 0,32 ms na mediana. O primeiro quadro desses
controles aumentou **22,6–52,3 ms**. Essas regressões pequenas acompanham
as novas provas e fatoração; a campanha não isola cada parcela desse custo.
Os tempos de transformação continuam abaixo do controle Coin/OpenGL.

## Correção e disponibilidade

- Sete gates CPU finais passaram: Action, ReuseCore, TransformCore, FfiFrame,
  BgfxCore, DepthContract e DrawStyle. Incluem limites, aliases, overrides
  parciais, campos conectados/ignored, callback tardio, mixed updates, rollback,
  retry, câmera, signed zero, colisão do índice e suffix polygon/stroke.
- Dez gates GPU finais passaram: packer wgpu real em dois dispositivos,
  referência de câmera com Coin/OpenGL, câmera em janela, RTT direto e
  instâncias/depth/RTT em BGFX Vulkan/OpenGL. O oracle expandido independe do
  normalizador, usa PHONG direcional+pontual, normais oblíquas e depth; exige
  detectar a contraprova com normal calculada do transporte errado.
- **196 comparações RGB antes/depois** em frames lógicos 0,100,…,600, com
  digests da cena iguais e movimento confirmado. Materiais/geometria 10%,
  transformação e estático ficaram idênticos. Camera BGFX/Vulkan diferiu em
  até 2 pixels (máximo canal 30); materiais100 BGFX em até 2 pixels de 1 nível.
  Geometria100 e demais combinações ficaram idênticas. As diferenças existentes
  contra Coin/OpenGL também foram quantificadas nos arquivos de verificação.
- **wgpu/OpenGL: N/A.** A versão antiga já falhava na criação do dispositivo.
  O probe da dependência wgpu24 mostrou contexto desktop GL3.3 no NVIDIA e
  falha do shader interno de validação por BUFFER_STORAGE/COMPUTE_SHADER/
  DYNAMIC_ARRAY_SIZE, terminando em `Parent device is lost`. Os logs/probe
  estão preservados em `availability`. A medição cobre BGFX/Vulkan,
  BGFX/OpenGL e wgpu/Vulkan. O lowering wgpu é comum às suas APIs, mas
  OpenGL não pôde ser validado em runtime nesta máquina.

## Evidência e reprodução

[Pacote de validação](validation/material-geometry-linux/) contém comandos,
ambiente relevante, SHA256 dos binários, fonte declarada por variante, logs,
CSV, máximos, contagens de frames acima de budgets, RSS e hashes das imagens.
PPMs originais permanecem nos diretórios locais `/tmp`; o pacote versionado
guarda métricas/hash e não copia esses binários grandes. `exploratory-*` e
`diagnostic` registram a etapa intermediária `6d076c4707`; campanhas finais
identificam o conteúdo compilado `ff289a50b9`.

Foram 189 processos de timing alternados (42 offscreen + 42 stress + 42
janela + 63 controles), além de verificação e gates. Os CSVs do Coin/OpenGL
compartilhados não contam como dois processos.

Para repetir, use `run-all-matched.py` com os builds antes/depois e CoinGL
separado, `--runner <repo>/scripts/coinrender/run_animation_benchmark.py` e
`--row-helper <pacote>/matched-rows.py`. Os manifestos guardam os argumentos
completos. `summarize.py` recomputa estatísticas dos CSVs; para validar o pacote
sem PPMs, passe `--recorded-rgb <campanha>/rgb-before-after.json` e
`--validate-only`. Esse modo declara que reutiliza métricas RGB arquivadas.
O helper acomoda o arredondamento a seis algarismos significativos do log
do primeiro quadro; o CSV de precisão completa continua autoritativo.

O custo restante de geometria wgpu está sobretudo na validação/composição
e pack CPU do plano expandido (no piloto geometry10, cerca de 32,6 ms no
target e 64,7 ms no pack), com 864.048 vértices de origem. O transporte GPU
qualificado usa 24 vértices, 36 índices e 40.001 instâncias. Um próximo passo
concreto é reduzir a duplicação do plano de origem e a revalidação de partes
comprovadamente preservadas.
"""
(root/'docs/coin-render-material-geometry-linux.md').write_text(text)
print('Report and figures generated')
