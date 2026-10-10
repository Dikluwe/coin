import pathlib, subprocess, json, re, os, time, hashlib, shutil, xml.etree.ElementTree as E
r=pathlib.Path(__file__).resolve().parent;out=r/'suite-recovery';out.mkdir(exist_ok=True)
original=r/'suite/wgpu-d3d12-ctest.log'
text=original.read_text(encoding='utf-8',errors='replace')
completed={int(m[1]):(m[3],m[4]) for m in re.finditer(r'^\s*(\d+)/(\d+)\s+Test\s+#\d+:.*?(Passed|\*\*\*Skipped|\*\*\*Failed|\*\*\*Timeout)\s+([\d.]+)\s+sec\s*$',text,re.M)}
assert completed and set(completed)==set(range(1,max(completed)+1))
assert all(s in ['Passed','***Skipped'] for s,t in completed.values()),completed
start=max(completed)+1
inventory=json.loads(subprocess.check_output(['ctest','--test-dir',str(r/'build-wgpu'),'-C','Release','--show-only=json-v1']))['tests']
assert start<=len(inventory)
partial=r/'build-wgpu/Testing/Temporary/LastTest.log.tmp'
if partial.exists():shutil.copyfile(partial,out/'original-last-test-partial.log')
env={k.upper():v for k,v in os.environ.items() if not k.upper().startswith(('COIN_','WGPU_'))}
rows=json.loads((r/'suite/summary.json').read_text());prior=next(x for x in rows if x['label']=='wgpu-d3d12-ctest');assert prior['exit']==124
env.update(prior['environment'])
cmd=['ctest','--test-dir',str(r/'build-wgpu'),'-C','Release','--output-on-failure','-I',f'{start},{len(inventory)}','--output-junit',str(out/'wgpu-tail.xml')]
begin=time.time();print('resuming index',start,'of',len(inventory),flush=True)
with (out/'wgpu-tail.log').open('w',encoding='utf-8') as f:
 code=subprocess.run(cmd,env=env,cwd=out,stdout=f,stderr=subprocess.STDOUT,timeout=7200).returncode
shutil.copyfile(r/'build-wgpu/Testing/Temporary/LastTest.log',out/'wgpu-tail-last-test.log')
summary=dict(command=cmd,environment=prior['environment'],exit=code,seconds=time.time()-begin,original_runner_exit=124,original_runner_timeout_seconds=1800,remaining_runner_budget_seconds=7200,per_test_limits_unchanged=True,start_index=start,total=len(inventory),original_log_sha256=hashlib.sha256(original.read_bytes()).hexdigest(),recovery_log_sha256=hashlib.sha256((out/'wgpu-tail.log').read_bytes()).hexdigest())
(out/'summary.json').write_text(json.dumps(summary,indent=2),encoding='utf-8')
effective=E.Element('testsuite',name='wgpu-effective',tests=str(len(inventory)))
for index,(status,seconds) in completed.items():
 c=E.SubElement(effective,'testcase',name=inventory[index-1]['name'],time=seconds)
 if status=='***Skipped':E.SubElement(c,'skipped',message='Original CTest skip retained')
 E.SubElement(c,'system-out').text='Original suite/wgpu-d3d12-ctest.log; full per-test output: suite-recovery/original-last-test-partial.log'
for c in E.parse(out/'wgpu-tail.xml').findall('.//testcase'):effective.append(c)
assert len(effective)==len(inventory) and len({c.attrib['name'] for c in effective})==len(inventory)
fails=sum(c.find('failure') is not None for c in effective);skips=sum(c.find('skipped') is not None for c in effective)
effective.set('failures',str(fails));effective.set('skipped',str(skips))
E.ElementTree(effective).write(r/'suite-effective-wgpu.xml',encoding='utf-8',xml_declaration=True)
print('effective',len(effective),'failures',fails,'skips',skips,'exit',code,flush=True)
raise SystemExit(code or bool(fails))
