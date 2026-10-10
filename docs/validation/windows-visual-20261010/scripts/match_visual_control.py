import pathlib,time,json,subprocess,sys,shutil
r=pathlib.Path(__file__).parent
while True:
 p=r/'visual-finish-ledger.json'
 if p.exists() and len(json.loads(p.read_text()))==5:break
 time.sleep(2)
shutil.copytree(r/'visual-src',r/'visual-million-source',dirs_exist_ok=True)
p=r/'visual-src/main.cpp';s=p.read_text();old='(work=="city-40000"||work=="city-million")?155:';assert old in s
p.write_text(s.replace(old,'work=="city-40000"?140:work=="city-million"?155:',1))
for args in [['build_visual.py'],['run_visual.py','visual-static-matched','2','1','city-40000','bgfx,wgpu','static'],['analyze_visual.py','visual-static-matched'],['compare_visual.py','visual-static-matched']]:
 print('step',args,flush=True)
 code=subprocess.run([sys.executable]+args,cwd=r).returncode
 if code:sys.exit(code)
