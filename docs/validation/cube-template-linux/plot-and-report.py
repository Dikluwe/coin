#!/usr/bin/env python3
import argparse, json
from pathlib import Path
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.patches import Patch

p=argparse.ArgumentParser()
p.add_argument('--evidence',type=Path,default=Path(__file__).resolve().parent)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args();e=a.evidence
main=json.loads((e/'offscreen/report-summary.json').read_text())
stress=json.loads((e/'stress/report-summary.json').read_text())
rows={(x['case'],x['variant']):x for data in [main,stress] for x in data['timing']['comparisons']}
variants=[('bgfx-vulkan','BGFX/Vulkan'),('bgfx-opengl','BGFX/OpenGL'),('wgpu-vulkan','wgpu/Vulkan'),('coingl','Coin/OpenGL')]
def metric(case,v,key='total_median_ms'):
    return rows[case,v]['metrics'][key]
def pair(case,v,key='total_median_ms',digits=2):
    q=metric(case,v,key)
    return f"{q['before']:.{digits}f} → {q['after']:.{digits}f}"
def timing_table(case):
    lines=['| Render | Antes → depois (ms) | Variação |','|---|---:|---:|']
    for v,label in variants:
        q=metric(case,v)
        lines.append(f"| {label} | {pair(case,v)} | {q['change_percent']:+.2f}% |")
    return '\n'.join(lines)

fig,axes=plt.subplots(1,2,figsize=(14,5.3))
for ax,key,title in [(axes[0],'total_median_ms','Geometria em movimento · 10% dos objetos'),
                     (axes[1],'first_total_ms_from_csv_median','Primeiro quadro · geometria 10%')]:
    for i,(v,label) in enumerate(variants):
        q=metric('geometry-10',v,key)
        if v=='coingl':
            ax.barh(i,q['before'],height=.4,color='#555c65')
            ax.text(q['before']+3,i,f"{q['before']:.1f}",va='center',fontsize=10)
        else:
            for offset,stage,color in [(-.16,'before','#8b97a8'),(.16,'after','#258269')]:
                ax.barh(i+offset,q[stage],height=.29,color=color)
                ax.text(q[stage]+3,i+offset,f"{q[stage]:.1f}",va='center',fontsize=10)
    ax.set_yticks(range(4))
    ax.set_yticklabels([x[1] for x in variants])
    ax.invert_yaxis()
    ax.set_xlim(0,max(metric('geometry-10',v,key)['before'] for v,_ in variants)*1.18)
    ax.set_title(title,fontsize=12)
    ax.set_xlabel('Tempo CPU total (ms) · menor é melhor')
    ax.grid(axis='x',alpha=.22);ax.set_axisbelow(True)
    for spine in ['top','right']:ax.spines[spine].set_visible(False)
fig.suptitle('CoinRender · 40.000 objetos · offscreen 1024×1024',fontsize=15,y=.99)
fig.legend(handles=[Patch(color='#8b97a8',label='Antes'),Patch(color='#258269',label='Depois'),
                    Patch(color='#555c65',label='Referência Coin/OpenGL')],
           loc='upper center',bbox_to_anchor=(.5,.91),ncol=3,frameon=False)
fig.subplots_adjust(top=.74,bottom=.13,left=.1,right=.98,wspace=.42)
fig.savefig(e/'cube-template.png',dpi=150)
svg_path=e/'cube-template.svg'
fig.savefig(svg_path)
svg_path.write_text('\n'.join(line.rstrip() for line in svg_path.read_text().splitlines())+'\n')
plt.close(fig)
first=['| Render | Primeiro total antes → depois (ms) | RSS antes → depois (MiB) |','|---|---:|---:|']
for v,label in variants:
    first.append(f"| {label} | {pair('geometry-10',v,'first_total_ms_from_csv_median')} | {pair('geometry-10',v,'peak_rss_median_mib',1)} |")
controls=['| Caso | BGFX/Vulkan (ms) | BGFX/OpenGL (ms) | wgpu/Vulkan (ms) | Coin/OpenGL (ms) |','|---|---:|---:|---:|---:|']
for case in ['materials-10','transforms-10','static','camera']:
    controls.append('| '+case+' | '+' | '.join(pair(case,v) for v,_ in variants[:3])+f" | {metric(case,'coingl')['before']:.2f} |")

results='''### Geometria em movimento: 10% dos objetos

'''+timing_table('geometry-10')+'''

O plano de origem caiu de **864.048 para 96.216 vértices**, de **36.002 para 4.009 intervalos**, e de **86,40 para 9,62 MB decimais de vértices** (−88,86%). O transporte GPU permanece em **24 vértices, 36 índices e 40.001 instâncias**. Não há redução do conteúdo renderizado.

Mesmo com quatro mil dimensões distintas, o Cube estável reaparece entre clones e permanece no LRU. O trace final mostra 32 templates, 39 ranges retidos e 3.970 evictions de templates. Os 4.009 ranges do plano capturado continuam válidos, independentemente da retenção no cache.

![Geometria em movimento e primeiro quadro](validation/cube-template-linux/cube-template.png)

### Primeiro quadro e memória de processo: geometria 10%

'''+ '\n'.join(first)+'''

O primeiro total é a primeira linha CSV, incluindo warmup, atualização, render e publicação. RSS é pico do processo, sem medir memória GPU. O primeiro total do wgpu aproxima-se do Coin/OpenGL neste caso; BGFX ainda fica acima.

### Controles de preservação

'''+ '\n'.join(controls)+'''

Materiais e transformação apresentaram aumentos de até **1,42 ms** e **1,22 ms**, respectivamente. A cena estática e câmera variaram entre −0,23 e +0,16 ms. O primeiro quadro estático BGFX/Vulkan aumentou de **424,78 para 447,93 ms** (+23,15 ms); o BGFX/OpenGL variou +0,98 ms, e wgpu reduziu 10,24 ms. O ganho de geometria parcial não representa melhora uniforme em todos os casos. A ablação demonstra redução local da qualificação, mas não atribui sozinha cada variação do primeiro total.

### Todos os objetos alterando geometria

'''+timing_table('geometry-100')+'''

Com 100% de geometria distinta não há Cube estável repetido a preservar. Observamos **+6,65 ms em BGFX/Vulkan**, **+1,84 ms em BGFX/OpenGL** e **+2,04 ms em wgpu/Vulkan**. Essa amostra curta serve para detectar a ausência de ganho e possíveis regressões, sem caracterizar caudas p99. O custo do cache é de captura/qualificação; os resultados não isolam a causa dessas variações em regime.

### Ablação e custo que permanece

Oito execuções instrumentadas wgpu/Vulkan alternaram cache de templates e memoização. São diagnósticos isolados, não medianas de campanhas:

- geometry-10, ambos ligados: validação target **24,92 ms**, contra **31,50 ms** com ambos desligados; medianas das sete amostras após o primeiro quadro (quatro warmups e três medidos). Considerando somente os três medidos, são **24,91 e 31,10 ms**. São 4.009 contra 36.002 ranges. A reutilização continua resource_rebuild; o ganho não é mera transferência de tempo para uma recaptura.
- Payload da qualificação geometry-10: **13,65 ms** com ambos ligados, **19,26 ms** sem memoização e **23,83 ms** com ambos desligados.
- Estático: payload da qualificação **11,25 ms** com memoização contra **17,59 ms** sem ela. Os checks de material passam de uma verificação por ocorrência para oito snapshots, mantendo todos os checks de ownership.

A qualificação aparece agora como object_qualification (scene_profile/frame_profile/ownership/payload). Ela ocorria depois da linha action e não fazia parte da soma antiga traversal/frame_plan/backend. Ainda ficam cerca de **65,6 MB de snapshots de render state** e ~25 ms de validação target no diagnóstico; reduzir geometria não elimina esse custo.
'''
protocol='''Código final: 9737b2e1e60eba6b44988d684c2838271d99dc05; controle CoinRender congelado: ff289a50b971fbc012e791194acf0a5ec24cfb3b; controle Coin/OpenGL: 4d63bb993022ee8d40802558b0871a4803002b8d. Bibliotecas e executáveis foram identificados por SHA-256. A master e o checkout principal foram preservados; trabalho na branch codex/coin-render-transform-performance.

Cena coin-render-city-40000.iv, SHA-256 bb9ccc612cd38ffb749ee599d9350a80974649eab5bf477d2f344538a42b2576: 40.000 objetos, 480.012 triângulos, PHONG, duas luzes direcionais, 1024×1024. Ryzen 5800H e NVIDIA RTX 3060 Laptop; GCC 13.3, Release/Ninja, governor powersave. ABI privado C++/Rust permanece 43; Rust, shaders e backends não receberam alterações nesta etapa.

Campanha principal: cinco casos, três rodadas, cinco warmups e 15 quadros medidos por processo. Stress geometry100: três rodadas, três warmups e sete quadros medidos. Ordem alternada entre antes/depois e variantes; uma execução Coin/OpenGL por caso/rodada é compartilhada nos dois conjuntos (CSV/log idênticos). Seu 0% na tabela não é uma segunda medição. Total: **126 processos válidos, 1.722 quadros medidos e 588 de warmup**. Valores são medianas de três medianas por processo; CPU wall total = update + render + publication, sem medir duração GPU ou latência de exibição.

A campanha em janela foi interrompida e excluída: DPMS registrou **Monitor Off** e Cinnamon ScreenSaver GetActive=true. Múltiplas variantes apresentaram outliers de apresentação próximos de 1.000 ms. Os dados incompletos e o diagnóstico foram preservados em window-excluded/; não são utilizados como comparação de desempenho. A sessão não foi desbloqueada nem teve seus ajustes alterados. wgpu/OpenGL fica fora desta campanha pela limitação de criação do device documentada no relatório anterior.
'''
validation='''- **Nove gates CPU** passaram: Action, PlanAssemblyCore, FrameReuseCore, TransformCore, WgpuFfiFrame, BgfxCore, DepthContract, DrawStyle e ClipPlane.
- **Dez gates GPU** passaram: três wgpu/Vulkan (referência de câmera obrigatória, multi-device e RTT), surface camera com display obrigatório, e seis BGFX/Vulkan/OpenGL (instancing, profundidade e RTT).
- Oráculo por callbacks completos compara payload expandido, normais/UVs/material/estado/índices. Testes cobrem LRU, limites, signed zero, binding, reset, optout, invalidação, copy/append/transfer/swap, índices combinados e ownership A/B/C intercalado.
- Cubes distintos inicialmente iguais: alias é recusado para edição direta; mudança só de C recaptura, preserva A, readmite C e permite atualização posterior com rollback/retry.
- **168 comparações RGB foram idênticas por bytes**, nos seis casos × quatro variantes × sete quadros lógicos (0..600, passo100), com digests de estado iguais e movimento confirmado. Incluem 42 comparações Coin/OpenGL entre suas duas execuções de verificação. Essas imagens demonstram preservação antes/depois de cada variante; os testes GPU específicos mantêm o oráculo de profundidade/PHONG.

[Pacote de evidência](validation/cube-template-linux/) conserva CSVs, logs, comandos, protocolo, controles de binários, digests/SHAs das imagens, métricas, diagnóstico, gates e scripts de reprodução. PPMs permanecem em /tmp, sem cópia para Git. Os resumos recalculam estatísticas dos CSVs; a validação posterior pode reutilizar as métricas RGB arquivadas sem alegar nova comparação de pixels.
'''
draft=(e/'report-source.txt').read_text()
report=draft.replace('RESULTADOS_A_INSERIR',results).replace('PROTOCOLO_A_INSERIR',protocol).replace('VALIDACAO_A_INSERIR',validation)
a.output.write_text(report)
print('Generated',a.output,e/'cube-template.png')
