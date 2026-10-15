import pathlib,sys,subprocess,json
r=pathlib.Path(__file__).parent;s=sys.argv[1]
cmd=[sys.executable,str(r/'validate.py'),'--source',s,'--modes','recording','--tag','recording-reference-retry']
subprocess.run(cmd,cwd=r,check=True)
rows=json.loads((r/'recording-reference-retry-ledger.json').read_text())
p=r/'recording-correction.json';data=json.loads(p.read_text());data['reference_recheck_first_attempt']=data['reference_recheck'];data['reference_recheck_first_attempt']['actual_build_exit']=1;data['reference_recheck_first_attempt']['log']='recording-reference-recording-build.log';data['reference_recheck']={'command':cmd,'commands':rows,'required_reference':'COIN_RENDER_REQUIRE_GL_REFERENCE=1','gpu_shadow_requirement':False,'reason':'Exercise four opt-in RTT CoinGL reference cases.'};p.write_text(json.dumps(data,indent=2))
assert len(rows)==4 and all(x['exit']==0 for x in rows),rows
with (r/'final-analysis.log').open('w') as f:subprocess.run([sys.executable,str(r/'analyze.py')],cwd=r,stdout=f,stderr=subprocess.STDOUT,check=True)
