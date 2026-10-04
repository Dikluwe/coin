import os,json,subprocess,re
from pathlib import Path
out=Path('/tmp/coin-render-capture-cpu-results');out.mkdir(exist_ok=True)
rows=[]
for pair in range(1,5):
    for version in (['before','after'] if pair%2 else ['after','before']):
        build='/tmp/coin-render-capture-baseline-bgfx' if version=='before' else '/tmp/coin-render-first-frame-bgfx'
        env=os.environ.copy();env['LD_LIBRARY_PATH']=build+'/lib';env.pop('COIN_RENDER_TRACE_PHASES',None)
        r=subprocess.run(['/tmp/coin-render-capture-cpu-measure','/tmp/coin-render-city-40000.iv'],env=env,capture_output=True,text=True,timeout=120)
        (out/f'{version}-{pair}.log').write_text(r.stdout+r.stderr)
        if r.returncode: raise RuntimeError(r.stderr)
        for line in r.stdout.splitlines():
            fields=dict(x.split('=',1) for x in line.split())
            rows.append(dict(version=version,pair=pair,sample=int(fields['sample']),apply_ms=float(fields['apply_ms']),cpp_allocations=int(fields['cpp_allocations']),requested_bytes=int(fields['requested_bytes']),recording_fnv64=fields['recording_fnv64']))
        (out/'results.json').write_text(json.dumps(rows,indent=2)+'\n')
        print(version,pair,'done',flush=True)
assert len(rows)==64
assert len(set(r['recording_fnv64'] for r in rows))==1
print('All 64 captured recording hashes match',flush=True)
