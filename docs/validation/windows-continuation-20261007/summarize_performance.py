import csv, json, math, pathlib, statistics
ROOT=pathlib.Path(__file__).resolve().parent/'performance'
verify=json.loads((ROOT/'verify/results.json').read_text())
pairs=json.loads((ROOT/'verify/comparisons.json').read_text())
rows=json.loads((ROOT/'measure/results.json').read_text())
assert len(verify)==78 and len(rows)==234 and len(pairs)==36
assert all(row['exit']==0 for row in verify+rows)
replacement_dir='measure-geometry-10-recheck'
replacement=json.loads((ROOT/replacement_dir/'results.json').read_text())
assert len(replacement)==13 and all(r['exit']==0 and r['case']=='geometry-10' and r['round']==1 for r in replacement)
original_manifest=json.loads((ROOT/'measure/manifest.json').read_text())
replacement_manifest=json.loads((ROOT/replacement_dir/'manifest.json').read_text())
assert all(replacement_manifest[k]==v for k,v in original_manifest.items())
excluded=[r for r in rows if r['case']=='geometry-10' and r['round']==1]
assert len(excluded)==13
(ROOT/'excluded-cpu-audit-overlap.json').write_text(json.dumps(excluded,indent=2))
rows=[dict(r,data_directory='measure') for r in rows if not (r['case']=='geometry-10' and r['round']==1)]
rows += [dict(r,data_directory=replacement_dir) for r in replacement]
(ROOT/'selected-measurement-results.json').write_text(json.dumps(rows,indent=2))
def metrics(values):
    values=sorted(values)
    return dict(median_ms=statistics.median(values),p95_ms=values[math.ceil(.95*len(values))-1],
                p99_ms=values[math.ceil(.99*len(values))-1],max_ms=max(values))
groups={}
for row in rows:
    key=(row['case'],row['variant'])
    group=groups.setdefault(key,dict(samples=[],processes=[]))
    with (ROOT/row['data_directory']/(row['stem']+'.csv')).open(newline='') as stream:
        group['samples'] += [s for s in csv.DictReader(stream) if s['warmup'] in ['0','false']]
    group['processes'].append(row)
out=[]
for case in ['static','camera','transforms-10','materials-10','geometry-10','geometry-100']:
    for backend,apis in [('bgfx',['d3d12','vulkan','opengl']),('wgpu',['dx12','vulkan','gl'])]:
        for api in apis:
            choices={}
            for choice in ['literal','reserve']:
                group=groups[(case,backend+'-'+api+'-'+choice)]
                assert len(group['samples'])==180 and len(group['processes'])==3
                choices[choice]={metric:metrics([float(s[metric]) for s in group['samples']])
                    for metric in ['update_ms','render_ms','publication_ms','total_ms']}
                choices[choice]['first_total_ms']=metrics([p['first_frame']['total_with_update_ms'] for p in group['processes']])
                choices[choice]['process_medians']=[p['stats']['total_ms']['median_ms'] for p in sorted(group['processes'],key=lambda p:p['round'])]
            ratios={metric:{stat:(choices['reserve'][metric][stat]/choices['literal'][metric][stat]
                                   if choices['literal'][metric][stat] else None)
                            for stat in ['median_ms','p95_ms','p99_ms']}
                    for metric in ['update_ms','render_ms','publication_ms','total_ms','first_total_ms']}
            out.append(dict(case=case,backend=backend,api=api,choices=choices,reserve_over_literal=ratios))
controls={case:{metric:metrics([float(s[metric]) for s in group['samples']])
               for metric in ['update_ms','render_ms','publication_ms','total_ms']}
          for (case,variant),group in groups.items() if variant=='coingl'}
summary=dict(verification_processes=len(verify),identical_paired_frames=sum(p['identical_frames'] for p in pairs),
             measurement_processes=len(rows),excluded_processes=len(excluded),total_measurement_attempts=247,
             samples_per_variant_case=180,comparisons=out,coingl_controls=controls)
(ROOT/'summary.json').write_text(json.dumps(summary,indent=2))
lines=['# Reserva de updates: piloto A/B Windows offscreen','',
    'Cena city-2500, 256 × 256, publicação RGBA síncrona. Seis casos, seis APIs/backend,',
    'controles literal/reserva e CoinGL. Três processos por variante/caso; cada um usa',
    '10 quadros de aquecimento e 60 amostras. Mediana/p95 abaixo combinam 180 amostras.',
    'Primeiro quadro é de processo novo; o cache do driver não foi limpo.', '',
    'Verificação visual separada: 78 processos, 546 imagens, 252 pares de quadros',
    'literal/reserva idênticos. Medição: 234 processos, sem captura de imagens ou tracing.',
    'Foram excluídos os 13 processos de geometry-10 da primeira rodada, sobrepostos',
    'à auditoria CPU de pixels. O bloco inteiro foi repetido isoladamente ao final,',
    'na mesma ordem intercalada, com manifests idênticos. Os 247 processos tentados',
    'e os 13 registros excluídos foram preservados; a análise usa 234 processos.',
    'Ordem intercalada/rotacionada. Estado/energia GPU e plano de energia amostrados',
    'por rodada. A campanha não alterou display/driver; não houve auditoria contínua',
    'do estado físico dos monitores. Este piloto não qualifica janela sem readback.', '',
    'Razão reserva/literal: abaixo de 1 indica menor tempo; acima de 1 indica regressão.', '',
    '| Caso | Backend/API | Literal mediana/p95 (ms) | Reserva mediana/p95 (ms) | Razão mediana/p95 |',
    '| --- | --- | ---: | ---: | ---: |']
for row in out:
    l=row['choices']['literal']['total_ms']; r=row['choices']['reserve']['total_ms']; q=row['reserve_over_literal']['total_ms']
    lines.append(f"| {row['case']} | {row['backend']}/{row['api']} | {l['median_ms']:.3f}/{l['p95_ms']:.3f} | {r['median_ms']:.3f}/{r['p95_ms']:.3f} | {q['median_ms']:.3f}/{q['p95_ms']:.3f} |")
lines += ['', 'O JSON contém update/render/publicação, p99, primeiro quadro e as medianas',
          'dos três processos. Logs/CSV, manifests de hashes, estado GPU e controles',
          'CoinGL acompanham a evidência. Diferenças nesta amostra não são uma estimativa',
          'universal de ganho nem uma campanha completa de latência.', '']
(ROOT/'summary.md').write_text('\n'.join(lines),encoding='utf-8',newline='\n')
print(json.dumps({k:v for k,v in summary.items() if k not in ['comparisons','coingl_controls']}))
for stat in ['median_ms','p95_ms']:
    ranked=sorted(out,key=lambda r:r['reserve_over_literal']['total_ms'][stat])
    print(stat,'best',[(r['backend'],r['api'],r['case'],round(r['reserve_over_literal']['total_ms'][stat],3)) for r in ranked[:3]])
    print(stat,'worst',[(r['backend'],r['api'],r['case'],round(r['reserve_over_literal']['total_ms'][stat],3)) for r in ranked[-3:]])
