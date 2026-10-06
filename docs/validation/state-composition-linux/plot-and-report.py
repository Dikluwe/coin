#!/usr/bin/env python3
import argparse,json
from pathlib import Path
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.patches import Patch
p=argparse.ArgumentParser();p.add_argument('--evidence',type=Path,default=Path(__file__).resolve().parent);p.add_argument('--output',type=Path,required=True);a=p.parse_args();e=a.evidence
main=json.loads((e/'offscreen/report-summary.json').read_text());stress=json.loads((e/'stress/report-summary.json').read_text());stage=json.loads((e/'stage-metadata.json').read_text())
rows={(x['case'],x['variant']):x for data in (main,stress) for x in data['timing']['comparisons']}
variants=[('bgfx-vulkan','BGFX/Vulkan'),('bgfx-opengl','BGFX/OpenGL'),('wgpu-vulkan','wgpu/Vulkan')]
cases=[('transforms-10','Transformação 10%'),('materials-10','Materiais 10%'),('geometry-10','Geometria 10%')]
def metric(c,v,k='total_median_ms'):return rows[c,v]['metrics'][k]
def pair(c,v,k='total_median_ms',digits=2):
 q=metric(c,v,k);return f"{q['before']:.{digits}f} → {q['after']:.{digits}f}"
def table(cs,key='total_median_ms'):
 s=['| Caso | BGFX/Vulkan (ms) | BGFX/OpenGL (ms) | wgpu/Vulkan (ms) | Coin/OpenGL controle (ms) |','|---|---:|---:|---:|---:|']
 for c,label in cs:s.append('| '+label+' | '+' | '.join(pair(c,v,key) for v,_ in variants)+f" | {metric(c,'coingl',key)['before']:.2f} |")
 return '\n'.join(s)
fig,axes=plt.subplots(1,3,figsize=(15,5.1),sharex=True)
for ax,(case,label) in zip(axes,cases):
 for i,(v,name) in enumerate(variants):
  q=metric(case,v)
  for offset,part,color in [(-.17,'before','#8796a9'),(.17,'after','#247b67')]:
   ax.barh(i+offset,q[part],height=.3,color=color);ax.text(q[part]+1,i+offset,f"{q[part]:.1f}",va='center',fontsize=9)
 control=metric(case,'coingl')['before'];ax.barh(3,control,height=.4,color='#555c65');ax.text(control+1,3,f'{control:.1f}',va='center',fontsize=9)
 ax.set_yticks(range(4));ax.set_yticklabels([x[1] for x in variants]+['Coin/OpenGL']);ax.invert_yaxis();ax.set_xlim(0,135);ax.set_title(label,fontsize=12);ax.set_xlabel('Total por quadro (ms) · menor é melhor');ax.grid(axis='x',alpha=.2);ax.set_axisbelow(True)
 for sp in ['top','right']:ax.spines[sp].set_visible(False)
fig.suptitle('CoinRender · 40.000 objetos · offscreen 1024×1024',fontsize=15,y=.99)
fig.legend(handles=[Patch(color='#8796a9',label='Antes'),Patch(color='#247b67',label='Depois'),Patch(color='#555c65',label='Controle Coin/OpenGL')],loc='upper center',bbox_to_anchor=(.5,.92),ncol=3,frameon=False)
fig.subplots_adjust(top=.73,bottom=.14,left=.09,right=.99,wspace=.55)
fig.savefig(e/'state-composition.png',dpi=150);svg=e/'state-composition.svg';fig.savefig(svg);svg.write_text('\n'.join(x.rstrip() for x in svg.read_text().splitlines())+'\n');plt.close(fig)
abl=json.loads((e/'diagnostic/ablation/derived-profile.json').read_text())
diag=['| Modo | Classificação (ms) | Validação Target (ms) | Pack wgpu (ms) |','|---|---:|---:|---:|']
for mode,label in [('both-on','Ambos ligados'),('composition-off','Composição legada'),('common-state-off','Estado comum legado'),('both-off','Ambos desligados')]:
 q=abl['transforms-10-'+mode]['phase_measured_medians_ms'];diag.append(f"| {label} | {q['composition_detail.classify_ms']:.2f} | {q['target.validation_ms']:.2f} | {q['bridge.pack_ms']:.2f} |")
report='''# CoinRender: composição e estado comum wgpu — Linux, 2026-10-05

Continuação de [cache de Cubes e qualificação CPU](coin-render-cube-template-linux.md).
Referência: Coin3D/Coin/OpenGL clássico, `SoGLRenderAction`.

## O que mudou

### Common: classificação de ranges compartilhados

A classificação repetia índices, materiais e profundidade dos mesmos vértices em cada ocorrência. Agora um memo **local à chamada**, com até **1.024 intervalos exatos `(firstIndex, indexCount)`**, guarda alpha dos materiais dos vértices e máximos absolutos XYZ. Cada ocorrência continua verificando estado, textura, política, centro, câmera e profundidade de saída.

O scan de Z é omitido somente quando há centro de ordenação, estilo de superfície 1 e model-view afim com divisor homogêneo exatamente 1. Um bound absoluto em double menor que `FLT_MAX/8` prova que produtos, somas parciais e midpoint seriam finitos; o centro determina a profundidade final. Câmeras perspectiva e ortográficas são atendidas quando a model-view satisfaz essa prova.

Miss inválido não altera alpha nem diagnóstico: executa o loop original na mesma ordem. Transformações projetivas, strokes, ausência de centro, bound insuficiente, NaN, saturação ou falha de alocação opcional usam o caminho original. O cache ainda serve seus hits depois de saturar; novos misses não criam summaries. Não há cache por nó, revision ou ponteiro entre chamadas.

Optout: `COIN_RENDER_DISABLE_COMPOSITION_RANGE_MEMOIZATION=1`.

### wgpu: um estado comum e uma prova de matrizes

`tryOpaqueInstancing` monta uma única `CoinWgpuRenderState` comum. Todos os estados, inclusive não referenciados por draws, continuam qualificados. Igualdade dos campos fonte que o packer transporta preserva dados de textura, fog e offset desabilitados, signed zero, slots e codificação de maximum depth. Por ocorrência continuam os cálculos authored de model-view/normal e as provas de determinant, finite e residual.

O caminho evita montar a struct de 2.280 bytes e calcular projeção/MVP descartados para cada ocorrência. A projeção comum é preparada uma vez. `rememberOpaqueCamera` consome a prova de matrizes concluída no mesmo `prepare`, eliminando a segunda inversão/model-view. Essa prova é privada, consumida antes de retornos e revogada em reset/falha; nenhuma mutação com a mesma revision ganha licença para usar valores anteriores.

Optout: `COIN_WGPU_DISABLE_INSTANCE_COMMON_STATE=1` conserva o caminho literal de montagem por estado e a segunda prova de câmera. O runner limpa ambos os optouts e os dois aliases de tracing. ABI C++/Rust permanece **43**; Rust, shaders e código específico BGFX não foram alterados.

## Resultados

40.000 objetos, PHONG, duas luzes direcionais, 1024×1024 offscreen. Tempo total de parede no processo = update + render + publication, incluindo espera GPU/readback. Cada valor é a mediana das medianas de três processos.

'''+table(cases)+'''

![Animação e comparação Coin/OpenGL](validation/state-composition-linux/state-composition.png)

O wgpu reduziu **23,53% em transformação**, **20,65% em materiais** e **19,30% em geometria parcial**. O Common também beneficiou BGFX: transformação caiu cerca de 10%, materiais cerca de 8%, e geometria parcial entre 6% e 8%. Não houve redução de objetos ou conteúdo renderizado. O transporte mantém 24 vértices, 36 índices e 40.001 instâncias nesses perfis; o plano de origem mantém a geometria da etapa anterior.

### Primeiro quadro

A primeira linha CSV é o primeiro quadro registrado durante warmup, com atualização do objeto; não representa todo o tempo de inicialização do aplicativo.

'''+table([('static','Estático')]+cases,'first_total_ms_from_csv_median')+'''

O primeiro quadro estático wgpu caiu de **306,82 para 290,99 ms**. BGFX/OpenGL caiu 5,55 ms; BGFX/Vulkan aumentou **8,74 ms**. A campanha demonstra ganho de animação, sem melhora uniforme do primeiro quadro; não isolamos a causa desse aumento BGFX/Vulkan.

### Controles e stress

'''+table([('static','Estático'),('camera','Câmera'),('geometry-100','Geometria 100%')])+'''

Os caminhos estático e câmera não executam os novos scans a cada quadro. Observamos +0,09 ms no estático BGFX/Vulkan e +0,44 ms na câmera; no wgpu estático −0,13 ms e câmera +0,36 ms. São variações pequenas nesta amostra, sem atribuição isolada de causa.

Com 100% da geometria distinta, BGFX variou +0,19% a +0,23% (até +0,52 ms); wgpu caiu **4,88%**, ou **11,55 ms**. O cache de ranges traz pouca oportunidade de compartilhamento nesse caso, mas a preparação do estado comum wgpu ainda se aplica. O stress tem sete quadros medidos por processo; não caracteriza caudas p99.

## Ablação: onde ficou o ganho

Doze processos instrumentados alternaram os dois optouts em transformação, materiais e geometria 10%. A tabela usa transformação 10%: mediana dos **três quadros medidos**, excluindo todos os cinco warmups. É diagnóstico N=1 por configuração, separado da campanha alternada.

'''+ '\n'.join(diag)+'''

O trace de transformação mostra 9 ranges retidos, 39.992 hits e 1.440.036 profundidades de índices evitadas por classificação. O trace wgpu confirma **1 estado packed contra 40.001**, e **40.001 provas authored reutilizadas pela câmera**. Esses contadores descrevem trabalho CPU evitado; a única struct comum já era o estado transportado à GPU na versão anterior. Não alegam redução de 91,2 MB de upload GPU.

Restam aproximadamente **19 ms de validação Target** e **23 ms de pack wgpu** nesse diagnóstico. A classificação ainda custa cerca de 7 ms; o scan de estados e a preparação de matrizes continuam relevantes.

## Protocolo e limites

Código final: SOURCE_FINAL. Controle CoinRender congelado: SOURCE_BEFORE. Coin/OpenGL congelado: SOURCE_GL. Bibliotecas/executáveis identificados por SHA-256; o controle Coin/OpenGL usa uma execução por caso/rodada compartilhada nos dois conjuntos. Seu valor repetido é controle, não uma nova medição nem um ganho zero reavaliado.

Cena `/tmp/coin-render-city-40000.iv`, SHA-256 `bb9ccc612cd38ffb749ee599d9350a80974649eab5bf477d2f344538a42b2576`: 40.000 objetos e 480.012 triângulos, seed 136. Ryzen 5800H, NVIDIA RTX 3060 Laptop, driver 610.57.04, GCC 13.3, Release/Ninja, governor powersave. As observações de clock/temperatura em machine.json foram registradas depois da campanha; não são uma série temporal das amostras.

Campanha principal: cinco casos × três rodadas × cinco warmups + 15 medidos. Stress geometry100: três rodadas × três warmups + sete medidos. Papéis/variantes alternados: **126 processos válidos, 1.722 quadros medidos e 588 warmups**. Não medimos duração GPU nem latência real de exibição.

A tela estava ativa no diagnóstico inicial, mas antes da campanha em janela o checkpoint registrou **Monitor Off** e **Cinnamon ScreenSaver=true**. Nenhum processo de benchmark em janela foi iniciado; esse escopo ficou fora dos resultados. O estado foi consultado sem alterar a sessão. wgpu/OpenGL continua fora pela limitação de criação do device documentada nas etapas anteriores.

## Validação

- **10 gates CPU**: os nove gates de Action/Core/FFI/profundidade/style/clipping e `CoinRenderCompositionTest --range-memo`.
- **13 gates GPU**: três wgpu/Vulkan de câmera/multi-device/RTT, surface camera com display obrigatório, seis BGFX/Vulkan/OpenGL de instancing/depth/RTT e três execuções completas de composição (wgpu/Vulkan, BGFX/Vulkan e BGFX/OpenGL). Nenhum desses gates foi pulado.
- O gate de clipping passou inicialmente com sua subchecagem opcional Coin/OpenGL omitida. Uma execução complementar com GLX pixmap direto passou, exigindo explicitamente `Coin/GL reference passed` e ausência de `[SKIP]`; comando e log foram preservados.
- Oráculo de composição compara todos os campos e bytes float, resultado, ordem parcial e diagnósticos. Cobre ranges sobrepostos, alpha heterogêneo, texturas, centros/câmera, todos os modos, cap, overflow, strokes, NaN, erros concorrentes e mutações com a mesma revision. Fallback OOM foi inspecionado; não houve injeção de OOM.
- Oráculo FFI compara buffers completos default/optout, 59 mutações tardias em estado não referenciado, sentinelas equivalentes, câmera→objetos 10/100%→câmera, RTT, resize, erro de nove lights e retry com a mesma revision.
- **168 comparações RGB idênticas por bytes**: seis casos × quatro variantes × sete quadros lógicos 0..600, passo100. Digests de estado iguais e movimento confirmado. São comparações antes/depois de cada variante; incluem 42 comparações Coin/OpenGL entre duas execuções independentes de verificação.

[Pacote de evidência e reprodução](validation/state-composition-linux/README.md) conserva CSVs, logs, comandos, hashes, scripts, métricas, gates, ablação e motivo da exclusão de janela. Os PPMs permanecem fora do Git.

## Próximos custos identificados

1. **Validação dos estados:** no perfil, cerca de 7,8 ms percorrem render states; oito programas de textura por estado representam 5,12 milhões de verificações de floats mesmo com texturas desabilitadas. Um memo local por bytes completos de programas já validados pode evitar repetição, mantendo rejeição de programas desabilitados malformados. Ganho ainda não medido.
2. **Matrizes por ocorrência:** o novo pack ainda calcula/prova MV/normal de todos os estados. Investigar reutilização por valores próprios exatos de model/view, com limite de memória e publicação transacional, preservando oráculo de bytes, estados não referenciados, erros tardios, câmera, RTT e retry. Não usar revision ou identidade do nó como prova.
3. **Primeiro quadro Common:** `rememberFrameRoot` prepara a base de câmera e `qualifyTranslationCapture` a prepara novamente na mesma captura. Uma prova local pode eliminar o segundo preparo. Unir as travessias exige resultados independentes para admissão de câmera e objetos, preservando conexões, ignored fields e contagem sintática por ocorrência.
4. **Cópias de composição:** Target copia a ordem capturada e o lowering monta outro schedule. O perfil instanced opaco pode estudar um empréstimo restrito à submissão, mantendo o schedule geral para transparência/RTT/sombras. Ainda não há medição isolada dessas cópias.
5. **Overlay de geometria:** a validação ordena todos os slots de posições para detectar duplicatas. Intervalos consecutivos podem reduzir esse trabalho, mantendo colisões parciais, limites e rollback; o perfil não separou ainda o custo do sort.
6. **Materiais:** a cena tem nove slots (720 bytes na tabela GPU de materiais wgpu). Otimizar bulk copy ou separar o hit exato da tabela de materiais no Rust merece uma cena com tabela maior; nesta cena esse custo é pequeno.

Esses itens são candidatos não implementados nesta etapa. Trabalho em `codex/coin-render-transform-performance`; master e checkout principal preservados.
'''
report=report.replace('SOURCE_FINAL',stage['source_content_revision']).replace('SOURCE_BEFORE',stage['baseline_source_content_revision']).replace('SOURCE_GL',stage['coingl_source_content_revision'])
a.output.write_text(report)
