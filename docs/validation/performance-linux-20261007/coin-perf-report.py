"""Standalone tables/plot from qualified measurements, no GPU execution."""
from pathlib import Path
import json,csv
repo=Path('/tmp/coin-render-first-frame');out=repo/'docs/validation/performance-linux-20261007'
data=json.loads((out/'coin-perf-analysis.json').read_text());rows=data['comparisons']
with (out/'comparison.csv').open('w',newline='') as stream:
 fields=[k for k in rows[0] if k!='pairs'];writer=csv.DictWriter(stream,fieldnames=fields);writer.writeheader()
 for row in rows:writer.writerow({k:row[k] for k in fields})
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np
fig,axes=plt.subplots(1,2,figsize=(11,4.8),sharey=False)
for ax,scope in zip(axes,('offscreen','window')):
 group=[r for r in rows if r['scope']==scope and r['case']=='geometry-100'];x=np.arange(3)
 ax.bar(x-.18,[r['literal_ms'] for r in group],.34,label='Literal',color='#7c8799')
 ax.bar(x+.18,[r['reserve_ms'] for r in group],.34,label='Reserva',color='#2379b5')
 for i,r in enumerate(group):
  for side,key in ((-.18,'literal_ms'),(.18,'reserve_ms')):
   ax.scatter([i+side]*3,[p[key] for p in r['pairs']],color='#17202c',s=18,zorder=3)
  ax.scatter([i-.18,i+.18],[r['literal_p95_ms'],r['reserve_p95_ms']],marker='x',color='#bb3d39',s=45,zorder=4)
 ax.set_xticks(x,['BGFX Vulkan','BGFX OpenGL','wgpu Vulkan']);ax.set_title(scope);ax.set_ylabel('Tempo total CPU de parede (ms)');ax.grid(axis='y',alpha=.18);ax.set_axisbelow(True)
axes[0].legend(loc='upper right')
fig.suptitle('City 40.000 objetos — geometria 100%, 512², NVIDIA RTX 3060 Laptop')
fig.text(.5,.01,'Barras: mediana de 3 medianas por processo; pontos: cada processo; ×: mediana dos p95.\nJanela: render/present CPU, sem drain por quadro; offscreen: inclui readback/publicação.',ha='center',fontsize=9)
fig.tight_layout(rect=(0,.07,1,.92));fig.savefig(out/'geometry-100.png',dpi=160);plt.close(fig)
table=['| Alvo | API | Literal mediana / p95 | Reserva mediana / p95 | Delta mediana | CoinGL mediana |','|---|---|---:|---:|---:|---:|']
for row in rows:
 if row['case']!='geometry-100':continue
 table.append(f"| {row['scope']} | {row['api']} | {row['literal_ms']:.2f} / {row['literal_p95_ms']:.2f} | {row['reserve_ms']:.2f} / {row['reserve_p95_ms']:.2f} | {row['delta_percent']:+.1f}% | {row['coingl_ms']:.2f} |")
(out/'geometry-table.md').write_text('\n'.join(table)+'\n')
print('Wrote comparison.csv, geometry table and plot')
