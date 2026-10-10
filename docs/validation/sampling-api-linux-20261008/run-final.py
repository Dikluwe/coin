from pathlib import Path
import subprocess,json
repo=Path('/home/dikluwe/.codex/worktrees/coin-portable-sampling/coin');root=Path(__file__).parent;scripts=repo/'testsuite/reproducers/sampling-api';rows=[]
commands=[
 ['python3',str(scripts/'run.py'),'--artifacts',str(root),'--name','window-final','--cases','window,api,selection,publication,ownership'],
 ['python3',str(scripts/'run.py'),'--artifacts',str(root),'--name','stock-portable-final','--modes','portable','--cases','deep,direct,viewport,procedural'],
 ['python3',str(scripts/'benchmark_linux.py'),'--artifacts',str(root),'--name','benchmark-cpu-final','--frames','90','--warmup','30'],
 ['python3',str(scripts/'benchmark_linux.py'),'--artifacts',str(root),'--name','benchmark-gpu','--kind','gpu','--workloads','texture-1,texture-4,texture-8,npot-8','--frames','45','--warmup','15'],
 ['python3',str(scripts/'benchmark_linux.py'),'--artifacts',str(root),'--name','benchmark-baseline','--baseline','--frames','90','--warmup','30'],
 ['python3',str(scripts/'qualify_freecad_linux.py'),'--artifacts',str(root)],
 ['python3',str(scripts/'qualify_freecad_linux.py'),'--artifacts',str(root),'--name','freecad-dpr2','--scale','2','--policies','portable']]
for i,cmd in enumerate(commands):
 with (root/f'final-{i}.log').open('w') as f:code=subprocess.run(cmd,cwd=repo,stdout=f,stderr=subprocess.STDOUT).returncode
 rows.append(dict(command=cmd,exit=code));(root/'final-summary.json').write_text(json.dumps(rows,indent=2));print(i,code,flush=True)
