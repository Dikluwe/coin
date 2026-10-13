from pathlib import Path
import argparse,json,os,re,statistics,subprocess,hashlib,math
p=argparse.ArgumentParser();p.add_argument('--viewer',default='/tmp/coin-pbr-sdk/bin/coin_render_viewer');p.add_argument('--environment',default='/tmp/coin-pbr-m3-env/studio.cibl');p.add_argument('--output',default='/tmp/coin-pbr-m5-benchmark');p.add_argument('--cad-source',default='/home/dikluwe/Área de trabalho/Estudo coin/estudos/So/SoWgpu-FreeCAD-Exemplos/cenas/Assembly.iv');a=p.parse_args()
out=Path(a.output);out.mkdir(parents=True,exist_ok=True)
env=os.environ.copy();env.update(DISPLAY=':0',VK_DRIVER_FILES='/usr/share/vulkan/icd.d/radeon_icd.json',COIN_BGFX_RENDERER='vulkan',COIN_RENDER_TRACE_PHASES='1',COIN_WGPU_GPU_TIMESTAMPS='1')
rows=[]
def stats(values):
 if not values:return None
 s=sorted(values);return dict(samples=len(s),median=statistics.median(s),p95=s[math.ceil(.95*len(s))-1],min=s[0],max=s[-1])
for name,fixture,extra,pbr in [('assembly-legacy','assembly',[],False),('assembly-pbr','assembly',[],True),('housing-tools-pbr','housing',['--cad-tools'],True),('freecad-pbr','assembly',['--scene',a.cad_source,'--convert-import'],True)]:
 cmd=[a.viewer,'--fixture',fixture,'--view','iso-perspective','--width','1280','--height','960','--frames','144',*extra]
 if pbr:cmd+=['--environment',a.environment,'--direct-intensity','0']
 result=subprocess.run(['/usr/bin/time','-v',*cmd],env=env,capture_output=True,text=True,timeout=180)
 log=result.stdout+result.stderr;(out/(name+'.log')).write_text(log)
 phase={k:[] for k in ('bgfx','target','action')}
 for line in result.stderr.splitlines():
  match=re.match(r'COIN_RENDER_PHASE (bgfx|target|action) ',line)
  if not match:continue
  value={}
  for key,val in re.findall(r'(\w+)=([^\s]+)',line):
   try:value[key]=float(val)
   except ValueError:value[key]=val
  phase[match[1]].append(value)
 devices=re.findall(r'bgfx_device renderer=(\w+) vendor_id=(0x[0-9a-f]+) device_id=(0x[0-9a-f]+)',log)
 assert devices and all(d==('Vulkan','0x1002','0x1638') for d in devices),devices
 n=len(phase['action']);assert result.returncode==0 and n==144 and all(len(v)==n for v in phase.values()),(name,result.returncode,{k:len(v) for k,v in phase.items()})
 samples=[]
 for i in range(n):
  b,t,act=(phase[k][i] for k in ('bgfx','target','action'))
  samples.append(dict(cpu_reported_phases_ms=sum(act.get(k,0) for k in ('traversal_ms','frame_plan_ms'))+sum(t.get(k,0) for k in ('validation_ms','prepare_ms'))+sum(b.get(k,0) for k in ('lower_ms','upload_ms','encode_ms')),action_return_ms=sum(act.get(k,0) for k in ('traversal_ms','frame_plan_ms','backend_ms')),**{k:b.get(k) for k in ('gpu_frame_ms','gpu_opaque_ms','gpu_composite_ms','gpu_query_drain_ms','submit_frame_ms','gpu_wait_ms','geometry_buffer_reused','material_patch','resource_cache_hit')},plan_cache_hit=act['plan_cache_hit']))
 warm=samples[16:];summary={k:stats([r[k] for r in warm if isinstance(r[k],(int,float)) and r[k]>=0]) for k in samples[0] if k.endswith('_ms')}
 rss=re.search(r'Maximum resident set size \(kbytes\): (\d+)',log)
 uploads=re.findall(r'pbr_environment id=(\w+) bytes=(\d+) uploads=(\d+) levels=(\d+)',log)
 resource={k:v for k,v in phase['bgfx'][-1].items() if 'memory' in k or k.startswith('gpu_') and not k.endswith('_ms') or k.endswith('_capacity')}
 rows.append(dict(case=name,command=cmd,passed=result.returncode==0,cold=samples[0],warm=summary,warm_geometry_reuse=all(r['geometry_buffer_reused']==1 for r in warm),warm_plan_cache_hits=sum(r['plan_cache_hit']==1 for r in warm),uploads=uploads,process_peak_rss_kib=int(rss.group(1)) if rss else None,resource_stats=resource,samples=samples))
 print(name,json.dumps(summary),flush=True)
summary=dict(build='Release',resolution=[1280,960],warmup_frames=16,warm_frames=128,renderer='Vulkan',vendor_id=4098,device_id=5688,readback_per_frame=False,instrumentation='existing native Coin/BGFX phase trace plus GPU timestamps; query drain separately reported; not a presentation FPS benchmark',environment_sha256=hashlib.sha256(Path(a.environment).read_bytes()).hexdigest(),rows=rows)
(out/'results.json').write_text(json.dumps(summary,indent=2)+'\n')
