#!/usr/bin/env python3
import json,sys
from pathlib import Path
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
r=json.loads(Path(sys.argv[1]).read_text())['gpu']['benchmark-gpu-optimized'];fig,axs=plt.subplots(2,2,figsize=(10,7))
for row,q in enumerate([0,1]):
 for col,g in enumerate(['amd','nvidia']):
  ax=axs[row,col]
  for m,label in [('0','Nativo'),('3','Centro (linear: nativo)'),('4','Fetch otimizado')]:
   ax.plot([1,4,8],[r[f'{g}-{u}-{q}'][m] for u in [1,4,8]],marker='o',label=label)
  ax.set_title(('AMD Renoir' if g=='amd' else 'NVIDIA RTX 3060 Laptop')+' — '+('nearest+mips' if q==0 else 'linear+mips'));ax.set_xticks([1,4,8]);ax.set_xlabel('Unidades de textura');ax.set_ylabel('Tempo GPU por draw (ms)');ax.grid(alpha=.25);ax.set_ylim(bottom=0)
axs[0,0].legend(fontsize=9);fig.suptitle('Sampling: custo GPU isolado em EGL/OpenGL, 1280 × 720');fig.tight_layout();fig.savefig(sys.argv[2],dpi=160)
