import pathlib,sys,subprocess,json,shutil
r=pathlib.Path(__file__).parent;s=sys.argv[1]
shutil.copy2(r/'recording-ctest.xml',r/'recording-corrected-without-gl-optin.xml')
cmd=[sys.executable,str(r/'validate.py'),'--source',s,'--modes','recording','--tag','recording-reference']
code=subprocess.run(cmd,cwd=r).returncode
p=r/'recording-correction.json';data=json.loads(p.read_text());data['reference_recheck']={'command':cmd,'exit':code,'required_reference':'COIN_RENDER_REQUIRE_GL_REFERENCE=1','gpu_shadow_requirement':False,'reason':'Exercise four opt-in RTT CoinGL reference cases, not only software Recording.'};p.write_text(json.dumps(data,indent=2))
with (r/'final-analysis.log').open('w') as f:subprocess.run([sys.executable,str(r/'analyze.py')],cwd=r,stdout=f,stderr=subprocess.STDOUT,check=True)
