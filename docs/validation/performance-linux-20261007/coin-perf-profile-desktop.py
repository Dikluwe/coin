import os,sys,json,hashlib,subprocess,statistics,re
from pathlib import Path
repo=Path('/tmp/coin-render-first-frame');sys.path.insert(0,str(repo/'scripts/coinrender'))
import run_animation_benchmark as runner
out=Path('/tmp/coin-perf-profile-desktop');out.mkdir(exist_ok=True)
scene=Path('/tmp/coin-render-city-40000.iv');rows=[]
for scope in ('offscreen','window'):
 for case in ('static','camera','materials-10','transforms-10','geometry-10','geometry-100'):
  for variant in ('bgfx-vulkan','bgfx-opengl','wgpu-vulkan'):
   key,off,win,_=runner.VARIANTS[variant];build=Path('/tmp/coin-render-first-frame-'+key)
   env=runner.environment(build,variant,'nvidia');env['DISPLAY']=':0';env['__EGL_VENDOR_LIBRARY_FILENAMES']='/usr/share/glvnd/egl_vendor.d/10_nvidia.json';env['COIN_RENDER_TRACE_PHASES']='1';env['COIN_BGFX_TRACE_GL_ADAPTER']='1'
   binary=build/'bin'/('coin_render_gl_benchmark' if scope=='offscreen' else 'coin_render_window_benchmark')
   mode,percent=runner.case_options(case);stem=f'{scope}-{case}-{variant}'
   cmd=[str(binary),'--backend',off if scope=='offscreen' else win,'--scene',str(scene),'--animation',mode,'--animated-percent',str(percent),'--warmup','2','--frames','4','--samples-output',str(out/(stem+'.csv'))]
   cmd+=['--size','512'] if scope=='offscreen' else ['--width','512','--height','512']
   p=subprocess.run(cmd,env=env,capture_output=True,text=True,timeout=180);log=p.stdout+p.stderr;(out/(stem+'.log')).write_text(log)
   records={}
   for line in log.splitlines():
    if line.startswith('COIN_RENDER_PHASE '):
     tag=line.split()[1];vals=dict(re.findall(r'(\w+)=([^ ]+)',line));records.setdefault(tag,[]).append(vals)
   phases={tag:{field:statistics.median(float(row[field]) for row in data[-4:] if field in row) for field in data[-1] if field.endswith('_ms') and all(field in row and re.fullmatch(r'[\d.eE+-]+',row[field]) for row in data[-4:])} for tag,data in records.items()}
   row={'scope':scope,'case':case,'variant':variant,'returncode':p.returncode,'command':cmd,'phases':phases};rows.append(row);(out/'results.json').write_text(json.dumps(rows,indent=2)+'\n')
   print(stem,p.returncode,phases.get('action'),flush=True)
   if p.returncode:raise SystemExit(p.returncode)
