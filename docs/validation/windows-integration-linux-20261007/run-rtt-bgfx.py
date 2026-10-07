import json,pathlib,subprocess,os
root=pathlib.Path('/tmp/coin-windows-integration-linux-20261007'); records=[]
for row in json.loads((root/'results.json').read_text()):
 if '-bgfx-' not in row['key'] or row['key'].endswith('-camera'):continue
 api=row['key'].split('-')[-1];key=row['key']+'-rtt';folder=root/key;folder.mkdir(exist_ok=True)
 names=[f'CoinRenderRttProfile{route}_{api}' for route in ('Staged','Direct','Mips','MipsPbuffer')]+[f'CoinRenderSceneTexture{budget}_{api}_{route}' for budget in ('','Budget') for route in ('staged','direct')]
 build='/tmp/coin-render-first-frame-bgfx';inventory=json.loads(subprocess.check_output(['ctest','--test-dir',build,'--show-only=json-v1'],text=True)); available={t['name'] for t in inventory['tests']};assert set(names)<=available
 env=dict(os.environ,**row['environment']);cmd=['ctest','--test-dir',build,'--parallel','1','--output-on-failure','-R','^('+'|'.join(names)+')$','--output-junit',str(folder/'tests.xml')]
 with (folder/'tests.log').open('w') as f:code=subprocess.run(cmd,env=env,stdout=f,stderr=subprocess.STDOUT,timeout=600).returncode
 records.append(dict(key=key,command=cmd,environment=row['environment'],exit=code));print(key,code,flush=True)
 (root/'rtt-results.json').write_text(json.dumps(records,indent=2)+'\n')
