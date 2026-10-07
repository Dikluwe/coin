import os,json,subprocess,pathlib
r=pathlib.Path('/tmp/coin-windows-integration-linux-20261007');d=json.loads((r/'wgpu-gl-command.json').read_text());p=r/'amd-wgpu-opengl';p.mkdir(exist_ok=True);build='/tmp/coin-render-first-frame-wgpu';env=dict(os.environ,**d['environment']);cmd=[build+'/bin/CoinRenderProductTest'];report=subprocess.run(cmd,env=env,text=True,capture_output=True,check=True);(p/'capabilities.log').write_text(report.stdout+report.stderr)
assert ' renderer=2 ' in report.stdout and 'AMD' in report.stdout and 'llvmpipe' not in report.stdout.lower()
cmd=['ctest','--test-dir',build,'--parallel','1','--output-on-failure','-R','^('+'|'.join(d['tests'])+')$','--output-junit',str(p/'tests.xml')]
with (p/'tests.log').open('w') as f:code=subprocess.run(cmd,env=env,stdout=f,stderr=subprocess.STDOUT,timeout=600).returncode
(r/'wgpu-gl-results.json').write_text(json.dumps(dict(command=cmd,environment=d['environment'],exit=code),indent=2)+'\n');print('amd-wgpu-opengl',code)
