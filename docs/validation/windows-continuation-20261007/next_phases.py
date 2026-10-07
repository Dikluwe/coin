"""Wait for the serial API matrix, then run native and installed SDK probes."""
import json, pathlib, subprocess, sys, time
ROOT=pathlib.Path(__file__).resolve().parent
while True:
    path=ROOT/'cross-results.json'
    records=json.loads(path.read_text()) if path.exists() else []
    if len(records)==4:
        if any(row['exit'] or row.get('failed') or row.get('skipped') for row in records):
            raise SystemExit('Cross matrix needs review before native probes')
        break
    time.sleep(2)
for script, args in [('qualify_current.py',['--phase','smoke']),('sdk_current.py',['--phase','probe'])]:
    code=subprocess.call([sys.executable,str(ROOT/script),*args])
    if code: raise SystemExit(code)
print('Native and SDK phases finished; review their result records',flush=True)
