import json,pathlib,subprocess,os
root=pathlib.Path('/tmp/coin-hardware-20261007'); records=[]
for gpu in ('amd','nvidia'):
 record=next(x for x in json.loads((root/'results.json').read_text()) if x.get('cell')==gpu+'-bgfx-opengl' and 'environment' in x)
 env=dict(os.environ,**record['environment']); key=gpu+'-bgfx-opengl-final';out=root/key;out.mkdir(exist_ok=True)
 cmd=['bash','.github/scripts/qualify-coin-render-shadows-linux.sh','/tmp/coin-render-first-frame-bgfx','bgfx',str(out)]
 with (out/'runner.log').open('w') as f: code=subprocess.run(cmd,env=env,stdout=f,stderr=subprocess.STDOUT,timeout=600).returncode
 record=dict(key=key,command=cmd,environment=record['environment'],exit=code);records.append(record);print(key,code,flush=True)
 (root/'final-opengl-results.json').write_text(json.dumps(records,indent=2)+'\n')
record=records[-1];env=dict(os.environ,**record['environment']);env['__EGL_VENDOR_LIBRARY_FILENAMES']='/usr/share/glvnd/egl_vendor.d/50_mesa.json'
out=root/'nvidia-wrong-egl-negative';out.mkdir(exist_ok=True)
cmd=['bash','.github/scripts/qualify-coin-render-shadows-linux.sh','/tmp/coin-render-first-frame-bgfx','bgfx',str(out)]
with (out/'runner.log').open('w') as f: code=subprocess.run(cmd,env=env,stdout=f,stderr=subprocess.STDOUT,timeout=120).returncode
(root/'wrong-egl-negative-result.json').write_text(json.dumps(dict(command=cmd,environment={**record['environment'],'__EGL_VENDOR_LIBRARY_FILENAMES':env['__EGL_VENDOR_LIBRARY_FILENAMES']},exit=code,expected_exit=1),indent=2)+'\n');print('wrong-egl-negative',code,flush=True)
