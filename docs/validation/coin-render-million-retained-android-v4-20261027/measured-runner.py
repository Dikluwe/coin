from pathlib import Path
import subprocess,time,json,re,hashlib,struct,statistics,shutil
adb='/home/dikluwe/Android/Sdk/platform-tools/adb';serial='emulator-5580';pkg='org.coin3d.coinrender.retainedmillion'
out=Path('/tmp/coin-retained-million-v4');out.mkdir(exist_ok=True)
commands=[]
def run(label,args,check=True,timeout=30,binary=False):
 p=subprocess.run([adb,'-s',serial]+list(args),capture_output=True,text=not binary,timeout=timeout)
 commands.append({'label':label,'args':list(args),'exit':p.returncode,'stdout':p.stdout if not binary else '<binary>','stderr':p.stderr if not binary else '<binary>'})
 (out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
 if check and p.returncode:raise RuntimeError(label+': '+str(p.stderr)+str(p.stdout))
 return p.stdout
shutil.copyfile('/tmp/coin-retained-million-visible/coin-render-million-branch.apk',out/'coin-render-million-ab.apk')
shutil.copyfile('/tmp/coin-retained-million-v3-final/coin-render-million-ab.apk',out/'previous.apk')
run('portrait',['shell','wm','user-rotation','lock','0'])
results=[]
for trial,mode in enumerate(['previous','retained','current','current','retained','previous'],1):
 label=f'{trial}-{mode}';case=out/label;case.mkdir(exist_ok=True)
 actual='current' if mode=='current' else 'retained'
 apk='previous.apk' if mode=='previous' else 'coin-render-million-ab.apk'
 run(label+'-install',['install','--no-incremental','-r',str(out/apk)],timeout=90)
 run(label+'-stop',['shell','am','force-stop',pkg]);start=time.monotonic()
 run(label+'-launch',['shell','am','start','-f','0x10008000','-n',pkg+'/android.app.NativeActivity','--ei','coinrender_renderer','2','--ei','coinrender_samples','1','--ez','coinrender_city','true','--ez','coinrender_million_city','true','--ez','coinrender_continuous','true','--ez','coinrender_compare','true','--ez','coinrender_retained','true' if mode!='current' else 'false'])
 pid='';peak=0;last=''
 for poll in range(150):
  pid=run(label+'-pid',['shell','pidof',pkg],check=False).strip()
  if not pid:
   if time.monotonic()-start<15:time.sleep(1);continue
   raise RuntimeError(label+' exited')
  rss=run(label+'-rss',['shell','ps','-p',pid,'-o','RSS='],check=False).strip()
  if rss.isdigit():peak=max(peak,int(rss))
  log=run(label+'-log',['logcat','-d','--pid='+pid,'-s','CoinRenderP23:I'])
  (case/'application.log').write_text(log)
  if 'comparison_failed' in log or 'comparison_capture_failed' in log:raise RuntimeError(log)
  milestones='\n'.join(l for l in log.splitlines() if 'comparison_first' in l or 'retained_stats' in l or 'comparison_perf' in l)
  if milestones!=last:print(label,milestones,flush=True);last=milestones
  if 'comparison_perf path='+actual in log:break
  time.sleep(2)
 else:raise RuntimeError(label+' timed out')
 assert 'render_path='+actual+' comparison=1' in log and 'buildings=1000000 ground=1 triangles=12000012' in log
 first=re.search(r'comparison_first path=\w+ render_ms=([\d.]+) (size=.*)',log)
 assert first and first[2].startswith('size=1080x2400 ')
 if mode!='current':
  vertices=216
  assert re.search(r'retained_stats captures=1 plans=1 transport_builds=1 .*commands=1000001 resources=2 vertices='+str(vertices)+' cameras=1 materials=9',log)
  if mode=='retained':
   assert 'command_storage_bytes=28010524' in log
   assert 'compaction_passes=0' in log
   assert 'schedule_bytes=0' in log
 samples=[]
 for i in range(5):
  text=run(label+'-mem-'+str(i),['shell','dumpsys','meminfo',pkg]);(case/f'meminfo-{i}.txt').write_text(text)
  m=re.search(r'TOTAL PSS:\s*(\d+)\s+TOTAL RSS:\s*(\d+)\s+TOTAL SWAP(?: PSS| \(KB\))?:\s*(\d+)',text);assert m
  heap=list(map(int,re.search(r'^\s*Native Heap\s+(.+)$',text,re.M)[1].split()))
  samples.append({'reported_pss_kib':int(m[1]),'rss_kib':int(m[2]),'swap_kib':int(m[3]),'native_alloc_kib':heap[6]})
  time.sleep(1)
 for suffix in ['rgba','depth','times']:
  data=run(label+'-file-'+suffix,['exec-out','run-as',pkg,'cat','files/million-'+actual+'.'+suffix],binary=True)
  (case/('frame.'+suffix)).write_bytes(data)
 screenshot=run(label+'-screenshot',['exec-out','screencap','-p'],binary=True)
 (case/'display.png').write_bytes(screenshot)
 timings=list(struct.unpack('<120d',(case/'frame.times').read_bytes()))
 assert len((case/'frame.rgba').read_bytes())==1080*2400*4
 result={'trial':trial,'mode':mode,'pid':int(pid),'camera_and_viewport':first[2],'first_render_ms':float(first[1]),'steady_median_ms':statistics.median(timings),'steady_p90_ms':sorted(timings)[107],'sampled_peak_rss_kib':peak,'samples':samples,'median_memory':{k:statistics.median(x[k] for x in samples) for k in samples[0]},'directory':str(case),'seconds_to_measurement_end':time.monotonic()-start}
 results.append(result);(out/'results.json').write_text(json.dumps(results,indent=2)+'\n')
 print('CASE COMPLETE',json.dumps(result),flush=True)
 run(label+'-finish',['shell','am','force-stop',pkg])
print('ABBA COMPLETE',flush=True)
