import argparse,json,os,pathlib,re,subprocess,time
p=argparse.ArgumentParser();p.add_argument('backend',choices=['bgfx','wgpu']);p.add_argument('--binary');p.add_argument('--output-root',default='/tmp/coin-linux-visual-results/final');a=p.parse_args()
base=pathlib.Path(a.output_root)/a.backend;base.mkdir(parents=True,exist_ok=True)
exe=pathlib.Path(a.binary) if a.binary else pathlib.Path('/tmp/coin-linux-visual-installed-'+a.backend+'-build/coin_linux_visual_campaign')
cases=[('visual-initial','city-40000',False,140),('visual-initial','terrain-million',False,None),('visual-initial','solids',False,None),('visual-million','city-million',False,None),('visual-static-control','city-40000',True,155),('visual-static-matched','city-40000',True,140)]
results=[]
for cohort,scene,static,height in cases:
 for api in ('vulkan','opengl'):
  for policy in ('native','portable'):
   name=f'{cohort}-{scene}-{a.backend}-{api}-{policy}'
   env=os.environ.copy();env.pop('COIN_VISUAL_STATIC_CAMERA',None);env.pop('COIN_VISUAL_CITY_HEIGHT_155',None)
   if static:env['COIN_VISUAL_STATIC_CAMERA']='1'
   if height==155:env['COIN_VISUAL_CITY_HEIGHT_155']='1'
   if a.backend=='bgfx':env['COIN_BGFX_RENDERER']=api
   if a.backend=='bgfx' and api=='opengl':env['COIN_BGFX_TRACE_GL_ADAPTER']='1'
   if a.backend=='wgpu':env['WGPU_BACKEND']='gl' if api=='opengl' else 'vulkan'
   prefix=base/name; cmd=[str(exe),api,policy,scene,'2','1',str(prefix)]
   begin=time.monotonic()
   try:r=subprocess.run(cmd,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=300);code=r.returncode;out=r.stdout
   except subprocess.TimeoutExpired as e:code=124;out=(e.stdout or b'').decode(errors='replace') if isinstance(e.stdout,bytes) else (e.stdout or '')
   duration=round(time.monotonic()-begin,2);(base/(name+'.log')).write_text(out)
   oracle=re.findall(r'surface_oracle stage=(\w+) rgb_max=(\d+) exact=(\d+)',out)
   receipt=re.search(r'receipt renderer=([^\n]+)',out)
   gladapter=re.search(r'bgfx_gl_adapter vendor=([^\n]+)',out)
   qualified=(api!='opengl' or a.backend!='bgfx' or (gladapter and ('renderer=AMD Radeon' in gladapter.group(1) or 'renderer=NVIDIA' in gladapter.group(1))))
   passed=code==0 and len(oracle)==2 and all(x[2]=='1' for x in oracle) and qualified
   item={'name':name,'backend':a.backend,'api':api,'policy':policy,'scene':scene,'cohort':cohort,'exit':code,'seconds':duration,'oracle':oracle,'qualified_gpu':bool(qualified),'receipt':receipt.group(1) if receipt else None,'gl_adapter':gladapter.group(1) if gladapter else None,'pass':passed}
   results.append(item);(base/'results.json').write_text(json.dumps(results,indent=2)+'\n')
   print(f'{name}: {"PASS" if passed else "FAIL"} exit={code} oracle={oracle} seconds={duration}',flush=True)
print(f'COMPLETE {sum(x["pass"] for x in results)}/{len(results)} PASS',flush=True)
