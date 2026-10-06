#!/usr/bin/env python3
"""Regenerate the report and plot from the archived timing/diagnostic data."""
import argparse
import json
from pathlib import Path
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.patches import Patch

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--evidence', type=Path, default=Path(__file__).resolve().parent)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
e = a.evidence
stage = json.loads((e / 'stage-metadata.json').read_text())
main = json.loads((e / 'offscreen/report-summary.json').read_text())
stress = json.loads((e / 'stress/report-summary.json').read_text())
diag = json.loads((e / 'diagnostic/derived-profile.json').read_text())
rows = {(r['case'], r['variant']): r for s in (main, stress) for r in s['timing']['comparisons']}
variants = [('bgfx-vulkan', 'BGFX/Vulkan'), ('bgfx-opengl', 'BGFX/OpenGL'), ('wgpu-vulkan', 'wgpu/Vulkan')]
cases = [('transforms-10', 'Transformação 10%'), ('materials-10', 'Materiais 10%'), ('geometry-10', 'Geometria 10%')]

def metric(case, variant, key='total_median_ms'):
    return rows[case, variant]['metrics'][key]

def table(selected, key='total_median_ms'):
    lines = ['| Caso | BGFX/Vulkan (ms) | BGFX/OpenGL (ms) | wgpu/Vulkan (ms) | Coin/OpenGL controle (ms) |', '|---|---:|---:|---:|---:|']
    for case, label in selected:
        pairs = []
        for variant, _ in variants:
            q = metric(case, variant, key)
            pairs.append(f"{q['before']:.2f} → {q['after']:.2f}")
        lines.append('| ' + label + ' | ' + ' | '.join(pairs) + f" | {metric(case, 'coingl', key)['before']:.2f} |")
    return '\n'.join(lines)

fig, axes = plt.subplots(1, 3, figsize=(15, 5.1), sharex=True)
for ax, (case, label) in zip(axes, cases):
    for i, (variant, _) in enumerate(variants):
        q = metric(case, variant)
        for offset, role, color in [(-.17, 'before', '#8796a9'), (.17, 'after', '#2768a6')]:
            ax.barh(i + offset, q[role], height=.3, color=color)
            ax.text(q[role] + 1, i + offset, f"{q[role]:.1f}", va='center', fontsize=9)
    control = metric(case, 'coingl')['before']
    ax.barh(3, control, height=.4, color='#555c65')
    ax.text(control + 1, 3, f'{control:.1f}', va='center', fontsize=9)
    ax.set_yticks(range(4)); ax.set_yticklabels([v[1] for v in variants] + ['Coin/OpenGL'])
    ax.invert_yaxis(); ax.set_xlim(0, 165); ax.set_title(label, fontsize=12)
    ax.set_xlabel('Total por quadro (ms) · menor é melhor'); ax.grid(axis='x', alpha=.2); ax.set_axisbelow(True)
    for spine in ('top', 'right'):
        ax.spines[spine].set_visible(False)
fig.suptitle('CoinRender · validação Common · 40.000 objetos · offscreen', fontsize=15, y=.99)
fig.legend(handles=[Patch(color='#8796a9', label='Antes'), Patch(color='#2768a6', label='Depois (observado)'), Patch(color='#555c65', label='Controle Coin/OpenGL')], loc='upper center', bbox_to_anchor=(.5, .92), ncol=3, frameon=False)
fig.subplots_adjust(top=.73, bottom=.14, left=.09, right=.99, wspace=.55)
fig.savefig(e / 'validation-programs.png', dpi=150)
svg = e / 'validation-programs.svg'; fig.savefig(svg)
svg.write_text('\n'.join(line.rstrip() for line in svg.read_text().splitlines()) + '\n')
plt.close(fig)

diagnostic_rows = ['| Caso | Estados, literal → memo (ms) | Target, literal → memo (ms) |', '|---|---:|---:|']
for variant, case, label in [('wgpu-vulkan', 'transforms-10', 'wgpu transformação 10%'), ('wgpu-vulkan', 'materials-10', 'wgpu materiais 10%'), ('wgpu-vulkan', 'geometry-10', 'wgpu geometria 10%'), ('bgfx-vulkan', 'transforms-10', 'BGFX/Vulkan transformação 10%')]:
    off = diag['runs'][f'{variant}-{case}-memo-off']['phase_measured_medians_ms']
    on = diag['runs'][f'{variant}-{case}-memo-on']['phase_measured_medians_ms']
    diagnostic_rows.append(f"| {label} | {off['validation_detail.render_states_ms']:.2f} → {on['validation_detail.render_states_ms']:.2f} | {off['target.validation_ms']:.2f} → {on['target.validation_ms']:.2f} |")

report = '''# CoinRender: validação Common e próximos custos — Linux, 2026-10-05

Continuação de [composição e estado comum wgpu](coin-render-state-composition-linux.md). Referência de render: Coin3D/Coin/OpenGL clássico, `SoGLRenderAction`.

## Organização atual

As [quatro fases de isolamento](coin-render-isolation-roadmap.md) estão concluídas. Esta etapa mantém as fronteiras existentes:

| Parte | Responsabilidade |
|---|---|
| `CoinRenderAction` e Builder | Entrada comum, travessia da cena, leitura de Coin, captura dos estados e callbacks. |
| Core comum | Montagem do plano, validação dos snapshots, composição, transparência, matemática e decisões de reuse. |
| `CoinRenderTarget` | Admissão, dimensões, gerações, execução e publicação transacional de pixels/tickets. |
| Factory, Backend e Runtime | Factory escolhe o executor no build; os contratos ligam Target aos conectores e serviços específicos. |
| BGFX / wgpu | BGFX mantém lowering, recursos e shaders; wgpu mantém pack/FFI, Rust, recursos e shaders. |

`CoinBgfxAction` continua como tipo de compatibilidade, encaminhando à entrada comum. A seleção de backend permanece no build. A biblioteca e os snapshots ainda usam tipos Coin; o isolamento atual organiza responsabilidades dentro do módulo.

```mermaid
flowchart LR
    Scene[Cena Coin] --> Capture[Action e Builder]
    Capture --> Core[Core e FramePlan]
    Core --> Target[Target]
    Factory[Factory do build] --> Contracts[Backend e Runtime]
    Target --> Contracts
    Contracts --> BGFX[Infra BGFX]
    Contracts --> WGPU[Infra wgpu]
```

## Primeiro custo implementado: programas de textura

`CoinRenderFramePlan::isValid` validava os 16 floats de cada um dos oito programas de textura de todos os estados. Isso inclui unidades desabilitadas e estados que nenhum draw referencia.

Agora a chamada possui um memo de **512 bytes de programas próprios**, mais flags e contadores, na pilha. Guarda o último programa válido de cada unidade. Um hit exige igualdade dos **64 bytes completos** do programa; um miss chama o validador original. Só sucesso entra no cache. Não há alocação, revisão, ponteiro ou identidade de nó como chave, nem licença entre chamadas.

Os checks de textura habilitada, imagem, sampler, matriz, modelo e todos os checks anteriores/posteriores continuam na ordem original. A comparação inclui signed zero e todos os campos de programas inativos. A/B/A numa unidade exige nova validação de A; esse é o limite deliberado deste memo simples.

Optout: `COIN_RENDER_DISABLE_COMBINE_VALIDATION_MEMO=1`. O runner limpa essa variável antes dos benchmarks normais. ABI C++/Rust continua **43**; código específico BGFX/wgpu, Rust e shaders não foram alterados.

## Ablação: custo local

Oito processos instrumentados usaram o mesmo binário com memo ligado/desligado. Cada um teve cinco warmups e três quadros medidos. As medianas abaixo excluem os warmups por meio da coluna CSV, com correspondência de contagem entre trace e CSV. É diagnóstico N=1 por configuração; os intervalos são aninhados e não devem ser somados.

DIAGNOSTIC_TABLE

Em cada quadro desses perfis: **320.008 consultas**, **320.000 hits**, **8 chamadas ao validador original**, contra 320.008 chamadas com optout. O loop de finitude passa de 5.120.128 para 128 verificações nesse validador; as comparações de bytes ainda ocorrem por consulta.

A ablação registrou economia de aproximadamente **0,9 a 1,8 ms na validação dos estados**. Ela apoia a redução desse custo local. A campanha completa abaixo registra também regressões; ainda não há ganho uniforme do quadro total.

## Tempos totais observados

40.000 objetos, 480.012 triângulos, PHONG com duas luzes direcionais, 1024×1024 offscreen. Tempo de parede no processo = update + render + publication, incluindo espera GPU/readback. Cada valor é a mediana das medianas de três processos; antes é o código da etapa anterior, medido novamente nesta campanha.

MAIN_TABLE

![Tempos observados antes/depois](validation/validation-programs-linux/validation-programs.png)

wgpu registrou −2,94% em transformação, **+3,67% em materiais** e −0,76% em geometria parcial. BGFX/Vulkan registrou +0,32%, −6,91% e −4,02%; BGFX/OpenGL −9,78%, −14,91% e −4,91%, respectivamente. Esses percentuais descrevem a amostra completa, sem atribuição isolada de todas as diferenças à memoização. Os custos restantes de captura, matrizes e transporte continuam presentes.

### Controles e stress

CONTROL_TABLE

A câmera wgpu registrou **+6,35%** (+0,66 ms). Geometria 100% registrou +0,68% BGFX/Vulkan, **+4,63% BGFX/OpenGL** e **+6,05% wgpu**. Esses resultados ficam registrados para acompanhamento; a causa das variações não foi isolada. Não houve redução de objetos ou conteúdo para obter os resultados. O stress tem sete quadros medidos por processo e não caracteriza caudas p99.

### Primeiro quadro estático

FIRST_TABLE

É o primeiro quadro CSV, registrado durante warmup, incluindo atualização; não é toda a inicialização do aplicativo. wgpu caiu 7,36 ms; BGFX/OpenGL e BGFX/Vulkan aumentaram 4,63 e 5,65 ms, respectivamente.

## Protocolo e validação

Código final: SOURCE_FINAL. Controle CoinRender congelado: SOURCE_BEFORE. Coin/OpenGL congelado: SOURCE_GL. O congelamento verificou os oito hashes da etapa anterior antes de reconstruir as bibliotecas atuais. Os 20 hashes de binários/bibliotecas dos três controles continuaram iguais depois da campanha.

Cena `/tmp/coin-render-city-40000.iv`, SHA-256 `bb9ccc612cd38ffb749ee599d9350a80974649eab5bf477d2f344538a42b2576`, seed 136. Ryzen 5800H, NVIDIA RTX 3060 Laptop, driver 610.57.04, GCC 13.3, Linux 6.17.0-42, Release/Ninja e governor powersave. `machine.json` é uma observação posterior à campanha; não é história de clocks/temperatura das amostras. `load-observation.json` registra uma consulta durante a campanha; seu ps usa uma visão restrita de processos e não demonstra que o host estivesse ocioso.

- Principal: cinco casos × três rodadas × cinco warmups + 15 medidos.
- Stress geometry100: três rodadas × três warmups + sete medidos.
- Papéis/variantes alternados: **126 processos de tempo, 1.722 quadros medidos e 588 warmups**. Coin/OpenGL usa um processo por caso/rodada compartilhado nos conjuntos antes/depois; seu valor repetido é um controle compartilhado.
- **9 execuções Core/CPU e 14 GPU** passaram, sem skips: FrameCore nas duas builds, Action/Core/FFI/composição; texturas/multitexturas/composição nos três conectores/API; câmera, RTT e clipping. A referência Coin/OpenGL foi exigida nos testes de multitextura e clipping.
- Oráculo Core compara aceitação e diagnóstico completo literal/memo: active/inactive, cada uma das 16 posições × oito unidades × NaN/±Inf, signed zero, estados sem draws, A/B/A, textura habilitada, limites, precedência de erros, mutação/reparo sem nova revisão e diagnóstico opcional.
- **168 comparações RGB idênticas por bytes**, seis casos × quatro variantes × sete quadros lógicos 0..600, passo100, com digests iguais e movimento confirmado. Antes reaproveita a verificação da etapa anterior, compilada de SOURCE_BEFORE, cujos hashes coincidem com o controle congelado. Depois executou 24 processos novos. São comparações antes/depois por variante, incluindo 42 Coin/OpenGL; não são uma alegação de igualdade entre backends.

Esta campanha é offscreen. Não medimos janela, duração GPU ou latência real de exibição. Os PPMs permanecem fora do Git. [Evidência e reprodução](validation/validation-programs-linux/README.md) conserva comandos, CSVs, logs, hashes de binários, controles, fases, contadores e scripts.

## Próximos custos, em ordem

1. **Matrizes por ocorrência wgpu:** a etapa anterior mediu aproximadamente 23 ms de pack, sem separar só a matemática. A cena tem modelos distintos dentro de cada quadro; a oportunidade é temporal. Proposta: cache por índice com cópias próprias de model/MV/normal (192 bytes por entrada) e view comum copiada, igualdade dos bytes atuais, orçamento independente de 8 MiB e publicação só após sucesso. Para 40.001 estados, dados das entradas somam 7,68 MB, além da view e flags. Potencial teórico: cerca de 90% de hits em transformação 10% e quase todos em materiais/geometria. Ganho ainda não medido. Exige ambiente de arredondamento/denormais compatível, invalidação em erro/fallback/RTT/camera patch e preservação das provas de campos comuns, bounds, estados não referenciados e retry. Uma alternativa menor é fundir operações para evitar um determinante e uma transposta por estado, preservando a inversa/residual literais.
2. **Captura do primeiro quadro:** reaproveitar a base de câmera preparada na mesma captura; preservar admissão independente de câmera/objetos e as travessias necessárias.
3. **Cópias de composição:** estudar empréstimo limitado à submissão no perfil instanced opaco, mantendo schedules gerais para transparência, RTT e sombras.
4. **Overlay de geometria:** reconhecer intervalos consecutivos e evitar sort completo, mantendo colisões, limites e rollback.
5. **Materiais:** estudar cópias/tabela no Rust com uma cena que tenha mais slots; esta cena tem nove slots, 720 bytes na tabela GPU wgpu.

Esses próximos itens ainda não foram implementados. Trabalho em `codex/coin-render-transform-performance`; master e checkout principal preservados.
'''
report = report.replace('DIAGNOSTIC_TABLE', '\n'.join(diagnostic_rows)).replace('MAIN_TABLE', table(cases)).replace('CONTROL_TABLE', table([('static', 'Estático'), ('camera', 'Câmera'), ('geometry-100', 'Geometria 100%')])).replace('FIRST_TABLE', table([('static', 'Estático')], 'first_total_ms_from_csv_median'))
report = report.replace('SOURCE_FINAL', stage['source_content_revision']).replace('SOURCE_BEFORE', stage['baseline_source_content_revision']).replace('SOURCE_GL', stage['coingl_source_content_revision'])
a.output.write_text(report)
