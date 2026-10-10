import pathlib,subprocess,time,json,os,sys
r=pathlib.Path(__file__).resolve().parent;e={k.upper():v for k,v in os.environ.items()};e['MSBUILDDISABLENODEREUSE']='1';print('waiting for isolated BGFX SDK build',flush=True)
while True:
 p=r/'bgfx-device-loss-build-ledger.json'
 if p.exists():
  rows=json.loads(p.read_text())
  if any(v['exit'] for v in rows):raise SystemExit('BGFX SDK build failed')
  if len(rows)==3:break
 time.sleep(3)
commands=[['python',str(r/'build_sdk.py'),'--tag','patched-dependency','--backend','bgfx','--bgfx-prefix',str(r/'bgfx-device-loss-install')],['python',str(r/'build_windows_fronts.py')],['python',str(r/'run_native_removal.py'),'native-removal-patched-final','3'],['python',str(r/'qualify.py'),'--phase','sdk','--name','sdk-final'],['python',str(r/'qualify.py'),'--phase','npot','--backend','bgfx','--name','npot-final'],['python',str(r/'analyze_npot.py'),'--cohort','npot-final'],['python',str(r/'qualify.py'),'--phase','sampling','--backend','bgfx','--cases','api,advanced,deep,direct,viewport,rtt','--name','sampling-final'],['python',str(r/'qualify.py'),'--phase','window','--name','window-final'],['python',str(r/'qualify.py'),'--phase','policy-window','--name','policy-window-final'],['python',str(r/'run_window_campaign.py'),'window-campaign-quiet-final','600','60']]
ledger=[]
for cmd in commands:
 print('EXEC',cmd,flush=True);start=time.time();code=subprocess.run(cmd,cwd=r,env=e).returncode;ledger.append(dict(command=cmd,exit=code,seconds=time.time()-start));(r/'remaining-driver-ledger.json').write_text(json.dumps(ledger,indent=2))
 if code:raise SystemExit(code)
 if 'run_native_removal.py' in cmd[1] and any(not v['pass_gate'] for v in json.loads((r/'native-removal-patched-final/summary.json').read_text())):raise SystemExit('native device recovery failed')
print('ALL FINAL GATES FINISHED',flush=True)
