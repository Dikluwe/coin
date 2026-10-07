import sys,os,json,subprocess,statistics,re
from pathlib import Path
repo=Path('/tmp/coin-render-first-frame');sys.path.insert(0,str(repo/'scripts/coinrender'));import run_animation_benchmark as runner
out=Path('/tmp/coin-perf-ab-pilot');out.mkdir(exist_ok=True);rows=[]
for repeat in range(3):
 for case in ('geometry-10','geometry-100'):
  for variant in ('bgfx-vulkan','bgfx-opengl','wgpu-vulkan'):
   for enabled in ((False,True) if repeat%2==0 else (True,False)):
    key,off,win,_=runner.VARIANTS[variant];build=Path('/tmp/coin-render-first-frame-'+key);env=runner.environment(build,variant,'nvidia')
    env.update(DISPLAY=':0',__EGL_VENDOR_LIBRARY_FILENAMES='/usr/share/glvnd/egl_vendor.d/10_nvidia.json',COIN_RENDER_TRACE_PHASES='1',COIN_BGFX_TRACE_GL_ADAPTER='1',COIN_RENDER_DISABLE_OBJECT_UPDATE_RESERVE='0' if enabled else '1')
    stem=f'{case}-{variant}-{enabled}-{repeat}';cmd=[str(build/'bin/coin_render_gl_benchmark'),'--backend',off,'--scene','/tmp/coin-perf-baseline/city.iv','--animation','geometry','--animated-percent',case.split('-')[1],'--size','512','--warmup','5','--frames','15','--samples-output',str(out/(stem+'.csv'))]
    p=subprocess.run(cmd,env=env,capture_output=True,text=True,timeout=180);log=p.stdout+p.stderr;(out/(stem+'.log')).write_text(log)
    phases=[]
    for line in log.splitlines():
     if line.startswith('COIN_RENDER_PHASE object_update_prepare '):phases.append({k:float(v) for k,v in re.findall(r'(\w+)=([\d.eE+-]+)',line)})
    row={'case':case,'variant':variant,'reserve':enabled,'repeat':repeat,'returncode':p.returncode,'stats':runner.csv_stats(out/(stem+'.csv'),15),'phases':{k:statistics.median(r[k] for r in phases[-15:]) for k in phases[-1]} if phases else {}}
    rows.append(row);(out/'results.json').write_text(json.dumps(rows,indent=2)+'\n');print(stem,p.returncode,row['stats']['total_ms']['median_ms'],row['phases'],flush=True)
    if p.returncode:raise SystemExit(p.returncode)
