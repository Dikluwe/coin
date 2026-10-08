import json
from pathlib import Path
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
root=Path(__file__).parent;r=json.loads((root/'results-fine.json').read_text())
fig,axes=plt.subplots(1,2,figsize=(10,4.3),layout='constrained')
modes=['native','center','fine','fine_uniform'];colors=['#476675','#bd8354','#728ba1','#3d8c72']
for ax,gpu in zip(axes,['amd','nvidia']):
 groups=[x for x in r['timings']['gpu']['groups'] if x['gpu']==gpu and x['workload']=='8-0']
 values=[next(x['median_ms'] for x in groups if x['mode']==m) for m in modes]
 ax.bar(modes,values,color=colors)
 for i,v in enumerate(values):ax.text(i,v+.012,f'{v:.3f}',ha='center',fontsize=10)
 ax.set_ylim(0,.87);ax.set_title('AMD Renoir' if gpu=='amd' else 'NVIDIA RTX 3060 Laptop')
 ax.set_ylabel('Tempo GPU por draw (ms)');ax.grid(axis='y',alpha=.2);ax.set_axisbelow(True)
fig.suptitle('Nearest/trilinear: custo de 8 unidades, EGL sem Coin',fontsize=13)
fig.supxlabel('1280×720, textura1024² compartilhada, cache quente; mediana de10 grupos×60 draws',fontsize=9)
fig.savefig(root/'performance.png',dpi=150)
