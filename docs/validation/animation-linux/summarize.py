import csv, json, math, statistics
from pathlib import Path

ROOT=Path(__file__).resolve().parent

def summarize(scope):
    source=ROOT/scope
    manifest=json.loads((source/'manifest.json').read_text())
    runs=json.loads((source/'results.json').read_text())
    params=manifest['parameters']
    assert len(runs)==36
    assert params['rounds']==3 and params['frames']==600 and params['warmup']==60
    groups={}
    for r in runs:
        with (source/r['samples']).open() as f:
            rows=list(csv.DictReader(f))
        assert len(rows)==660
        assert [int(x['logical_frame']) for x in rows]==list(range(-60,600))
        measured=[x for x in rows if not int(x['warmup'])]
        assert [int(x['frame_index']) for x in measured]==list(range(600))
        for x in rows:
            for k in ('update_ms','total_ms','render_ms' if scope=='offscreen' else 'render_present_ms'):
                assert math.isfinite(float(x[k])) and float(x[k])>=0
        totals=[float(x['total_ms']) for x in measured]
        assert abs(statistics.median(totals)-r['stats']['total_ms']['median_ms'])<1e-9
        g=groups.setdefault((r['case'],r['variant']),{'runs':[],'totals':[]})
        g['runs'].append(r);g['totals'].extend(totals)
    out=[]
    for (case,variant),g in sorted(groups.items()):
        assert sorted(r['round'] for r in g['runs'])==[1,2,3]
        a={'scope':scope,'case':case,'variant':variant,'processes':3,'measured_frames':len(g['totals']),
           'total_median_ms':statistics.median(r['stats']['total_ms']['median_ms'] for r in g['runs']),
           'total_p95_ms':statistics.median(r['stats']['total_ms']['p95_ms'] for r in g['runs']),
           'total_p99_ms':statistics.median(r['stats']['total_ms']['p99_ms'] for r in g['runs']),
           'total_max_global_ms':max(g['totals']),
           'frames_over_60hz':sum(t>1000/60 for t in g['totals']),
           'frames_over_30hz':sum(t>1000/30 for t in g['totals']),
           'update_median_ms':statistics.median(r['stats']['update_ms']['median_ms'] for r in g['runs']),
           'render_median_ms':statistics.median(r['stats']['render_ms']['median_ms'] for r in g['runs']),
           'first_total_median_ms':statistics.median(r['first_total_ms'] for r in g['runs']),
           'result_since_main_median_ms':statistics.median(r['result_since_main_ms'] for r in g['runs']),
           'peak_rss_median_mib':statistics.median(r['peak_rss_kib'] for r in g['runs'])/1024}
        if scope=='offscreen':a['publication_median_ms']=statistics.median(r['stats']['publication_ms']['median_ms'] for r in g['runs'])
        else:
            scopes={r['throughput_scope'] for r in g['runs']};assert len(scopes)==1
            a['throughput_scope']=scopes.pop()
        out.append(a)
    assert len(out)==12
    (ROOT/(scope+'-report-summary.json')).write_text(json.dumps(out,indent=2)+'\n')
    return out

if __name__=='__main__':
    for scope in ('offscreen','window'):
        print(scope)
        for r in summarize(scope):
            print(r['case'],r['variant'],*[round(r[k],3) for k in ('total_median_ms','total_p95_ms','total_p99_ms')],r['frames_over_60hz'],r['frames_over_30hz'])
