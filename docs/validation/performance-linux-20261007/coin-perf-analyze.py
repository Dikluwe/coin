"""Recompute performance rows from buffered CSV, without executing benchmarks."""
import csv,hashlib,json,math,statistics
from pathlib import Path
import argparse


def stats(values):
    x=sorted(values)
    return {'median_ms':statistics.median(x),'p95_ms':x[math.ceil(.95*len(x))-1],
            'p99_ms':x[math.ceil(.99*len(x))-1],'max_ms':x[-1],
            'over_16_67':sum(v>1000/60 for v in x),'over_33_33':sum(v>1000/30 for v in x)}


def analyze(root):
    data=[];comparisons=[]
    assert 'NVIDIA' in (root/'coingl-physical-proof.log').read_text()
    assert 'Monitor is On' in (root/'dpms-forced-on.log').read_text()
    audit=json.loads((root/'dpms-audit.json').read_text())
    assert len(audit)>=259 and all('DPMS is Disabled' in row['state'] and 'Monitor is Off' not in row['state'] for row in audit)
    for scope in ('offscreen','window'):
        directory=root/scope
        rows=json.loads((directory/'results.json').read_text())
        manifest=json.loads((directory/'manifest.json').read_text());params=manifest['parameters']
        assert params['mode']=='measure' and params['frames']==120 and params['warmup']==30 and params['rounds']==3
        assert params['size']==512 and params['gpu']=='nvidia' and params['scope']==scope
        assert set(params['cases'].split(','))=={'static','camera','materials-10','transforms-10','geometry-10','geometry-100'}
        assert len(rows)==126,(scope,len(rows))
        seen=set()
        for r in rows:
            key=(r['case'],r['variant'],r['round']);assert key not in seen;seen.add(key)
            with (directory/r['samples']).open(newline='') as stream:all_rows=list(csv.DictReader(stream))
            warm=[v for v in all_rows if v['warmup']=='1'];sample=[v for v in all_rows if v['warmup']=='0']
            assert len(warm)==30 and len(sample)==120
            assert [int(v['frame_index']) for v in sample]==list(range(120))
            assert [int(v['logical_frame']) for v in sample]==list(range(120))
            for metric in ('update_ms','render_ms','publication_ms','total_ms'):
                field='render_present_ms' if scope=='window' and metric=='render_ms' else metric
                values=[float(v.get(field,0)) for v in sample]
                assert all(math.isfinite(v) and v>=0 for v in values)
                actual=stats(values)
                for k,v in actual.items():assert abs(v-r['stats'][metric][k])<1e-8,(key,metric,k)
            cmd=next(x for x in manifest['commands'] if x['stem']==f"{r['case']}-{r['variant']}-{r['round']}")
            env=cmd['environment'];assert env['VK_DRIVER_FILES'].endswith('nvidia_icd.json')
            if r['variant']!='coingl':
                assert env['COIN_RENDER_DISABLE_OBJECT_UPDATE_RESERVE']==('1' if r['variant'].endswith('-literal') else '0')
                assert 'NVIDIA' in (directory/r['log']).read_text()
            elif scope=='window':assert 'NVIDIA' in (directory/r['log']).read_text()
            for intrusive in ('COIN_RENDER_TRACE_PHASES','COIN_WGPU_GPU_TIMESTAMPS','COIN_BGFX_TRACE_GL_ADAPTER'):
                assert intrusive not in env
            data.append({'scope':scope,**r})
        for case in sorted({r['case'] for r in rows}):
            gl=[r for r in rows if r['case']==case and r['variant']=='coingl']
            for api in ('bgfx-vulkan','bgfx-opengl','wgpu-vulkan'):
                a=[r for r in rows if r['case']==case and r['variant']==api+'-literal']
                b=[r for r in rows if r['case']==case and r['variant']==api+'-reserve']
                assert len(a)==len(b)==len(gl)==3
                value=lambda group,metric,key:statistics.median(r['stats'][metric][key] for r in group)
                x,y=value(a,'total_ms','median_ms'),value(b,'total_ms','median_ms')
                pairs=[{'round':i,'literal_ms':next(r for r in a if r['round']==i)['stats']['total_ms']['median_ms'],
                        'reserve_ms':next(r for r in b if r['round']==i)['stats']['total_ms']['median_ms']} for i in (1,2,3)]
                comparisons.append({'scope':scope,'case':case,'api':api,'coingl_ms':value(gl,'total_ms','median_ms'),
                    'literal_ms':x,'reserve_ms':y,'delta_percent':100*(y/x-1),
                    'literal_p95_ms':value(a,'total_ms','p95_ms'),'reserve_p95_ms':value(b,'total_ms','p95_ms'),
                    'literal_first_ms':statistics.median(r['first_total_ms'] for r in a),
                    'reserve_first_ms':statistics.median(r['first_total_ms'] for r in b),
                    'literal_rss_kib':statistics.median(r['peak_rss_kib'] for r in a),
                    'reserve_rss_kib':statistics.median(r['peak_rss_kib'] for r in b),'pairs':pairs})
    return {'processes':len(data),'measured_frames':len(data)*120,'warmup_frames':len(data)*30,
            'statistic':'median of process medians; p95 nearest-rank within each process then median across 3 processes',
            'comparisons':comparisons}


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('root',type=Path);parser.add_argument('--output',type=Path);args=parser.parse_args()
    result=analyze(args.root)
    if args.output:args.output.write_text(json.dumps(result,indent=2)+'\n')
    print(result['processes'],result['measured_frames'])
