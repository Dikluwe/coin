#!/usr/bin/env python3
"""Plot measured Coin-free EGL RGB dumps, without recomputing native samples."""
import argparse
import os
from pathlib import Path
import numpy as np
parser=argparse.ArgumentParser()
parser.add_argument('directory',type=Path)
parser.add_argument('output',type=Path)
args=parser.parse_args()
os.environ['MPLCONFIGDIR']=str(args.output.parent/'.matplotlib-cache')
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
p=args.directory
fig,axes=plt.subplots(1,2,figsize=(12,4.5),constrained_layout=True)
x=(np.arange(64)+.5-32)/4096
for gpu,label,color in [('amd','AMD nativo','#d55e00'),('nvidia','NVIDIA nativo','#0072b2')]:
    b=np.frombuffer((p/f'{gpu}-egl-q.5-o0-boundary-scan.rgb').read_bytes(),dtype=np.uint8).reshape(64,64,3)
    axes[0].step(x,b[32,:,0],where='mid',label=label,color=color,linewidth=2.2)
b=np.frombuffer((p/'amd-egl-q.5-o0-boundary-fetch.rgb').read_bytes(),dtype=np.uint8).reshape(64,64,3)
axes[0].step(x,b[32,:,0],where='mid',label='texelFetch AMD/NVIDIA',color='#333333',linestyle=':',linewidth=2)
axes[0].axvline(-1/512,color='#d55e00',linestyle='--',alpha=.35)
axes[0].set(xlabel='Deslocamento em texels do mip 3',ylabel='Canal vermelho',title='Nearest com mip fixo e coordenadas exatas',ylim=(20,180),yticks=[40,160])
axes[0].legend(fontsize=9);axes[0].grid(alpha=.2)
for gpu,label,color in [('amd','AMD: textureQueryLod','#d55e00'),('nvidia','NVIDIA: textureQueryLod','#0072b2')]:
    q=np.frombuffer((p/f'{gpu}-egl-q.5-o0-query-lod.rgb').read_bytes(),dtype=np.uint8).reshape(64,64,3).astype(float)
    ys=np.arange(27,37)
    axes[1].plot(ys,(q[ys,33,0]*256+q[ys,33,1])/4096,'o-',label=label,color=color)
axes[1].axhline(2.70668163367804,color='#333333',linestyle=':',label='Fórmula CPU: maior norma das derivadas')
axes[1].set(xlabel='Linha da imagem, coluna x=33',ylabel='LOD',title='LOD implícito consultado no driver')
axes[1].legend(fontsize=8);axes[1].grid(alpha=.2)
fig.suptitle('Sampling isolado em EGL/OpenGL sem linkar Coin',fontsize=14)
fig.savefig(args.output,dpi=170)
