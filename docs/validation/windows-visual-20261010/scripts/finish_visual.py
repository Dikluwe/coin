import pathlib,time,subprocess,sys,json
r=pathlib.Path(__file__).parent
# The first cohort's XML is written only after all GPU processes have exited.
deadline=time.monotonic()+1800
while not (r/'visual-million/results.xml').exists():
 if time.monotonic()>deadline:sys.exit('million cohort has not completed')
 time.sleep(2)
steps=[['run_visual.py','visual-static-control','2','1','city-40000','bgfx,wgpu','static'],['analyze_visual.py','visual-million'],['analyze_visual.py','visual-static-control'],['compare_visual.py','visual-million'],['compare_visual.py','visual-static-control']]
ledger=[]
for step in steps:
 cmd=[sys.executable]+step;print('step',step,flush=True)
 code=subprocess.run(cmd,cwd=r).returncode;ledger.append({'command':cmd,'exit':code})
 (r/'visual-finish-ledger.json').write_text(json.dumps(ledger,indent=2))
 if code:sys.exit(code)
