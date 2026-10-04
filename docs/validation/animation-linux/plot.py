import json
from pathlib import Path
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

root=Path(__file__).resolve().parent
variants=['coingl','bgfx-vulkan','bgfx-opengl','wgpu-vulkan']
labels=['CoinGL','BGFX/Vulkan','BGFX/OpenGL','wgpu/Vulkan']
colors=['#56616f','#276bb1','#c87522','#7b50a0']
cases=[('static','Cena estática'),('camera','Câmera móvel'),('transforms-10','Movimento de 4.000 prédios')]
fig,axes=plt.subplots(3,2,figsize=(12,8),sharex=True,layout='constrained')
for col,scope in enumerate(['offscreen','window']):
    data=json.loads((root/(scope+'-report-summary.json')).read_text())
    lookup={(x['case'],x['variant']):x for x in data}
    for row,(case,title) in enumerate(cases):
        ax=axes[row,col]
        for j,(variant,color) in enumerate(zip(variants,colors)):
            x=lookup[(case,variant)]
            median=x['total_median_ms']; p99=x['total_p99_ms']
            ax.hlines(j,median,p99,color=color,lw=2)
            ax.scatter([median],[j],color=color,s=52,zorder=3)
            ax.scatter([p99],[j],facecolors='white',edgecolors=color,s=35,zorder=3)
            ax.annotate(f'{median:.1f}',(median,j),xytext=((5 if median < 2 else -5),7),textcoords='offset points',ha=('left' if median < 2 else 'right'),fontsize=9,color=color)
        ax.set_yticks(range(4),labels);ax.invert_yaxis();ax.set_xscale('log');ax.set_xlim(1,1600)
        ax.axvline(1000/60,color='#999',ls='--',lw=1);ax.axvline(1000/30,color='#bbb',ls=':',lw=1)
        ax.grid(axis='x',alpha=.18)
        ax.set_title(title if col==0 else title,fontsize=11,loc='left')
        ax.spines[['top','right']].set_visible(False)
        if row==2:ax.set_xlabel('Tempo total por quadro (ms; escala log)')
axes[0,0].set_title('OFFSCREEN — '+cases[0][1],loc='left')
axes[0,1].set_title('JANELA — '+cases[0][1],loc='left')
fig.suptitle('CoinRender: cena estática e animação — RTX 3060 Laptop\nPonto cheio: mediana · Ponto vazio: p99 (mediana de 3 processos)',fontsize=14)
fig.supxlabel('600 quadros por processo após 60 warmup · Linhas: 60 Hz e 30 Hz\nJanela mede CPU/parede; não mede duração GPU ou latência até a tela.\nJanela Vulkan: esperas intermitentes próximas de 1 s nas primeiras rodadas; terceira rodada sem esse padrão.',fontsize=10)
fig.savefig(root/'animation-timings.png',dpi=140)
