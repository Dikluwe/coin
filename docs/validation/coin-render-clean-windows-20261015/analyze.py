import pathlib,json,re,xml.etree.ElementTree as ET,hashlib,subprocess
r=pathlib.Path(__file__).parent;ledger=json.loads((r/'ledger.json').read_text());report={'source':(r/'source-sha.txt').read_text().strip(),'suites':{},'sdk':[]}
for p in sorted(r.glob('*-ledger.json')):ledger+=json.loads(p.read_text())
if (r/'final-source-sha.txt').exists():report['full_matrix_source']=(r/'final-source-sha.txt').read_text().strip()
if (r/'recording-final-source-sha.txt').exists():report['recording_recheck_source']=(r/'recording-final-source-sha.txt').read_text().strip()
for mode in ['off','recording','bgfx','wgpu']:
 p=r/(mode+'-ctest.xml')
 if p.exists():
  x=ET.parse(p).getroot();cases=x.findall('.//testcase');report['suites'][mode]={'tests':len(cases),'failures':[c.get('name') for c in cases if c.find('failure') is not None],'skips':[c.get('name') for c in cases if c.find('skipped') is not None]}
 else:report['suites'][mode]={'not_executed':True}
for row in ledger:
 if '-sdk-' not in row['label']:continue
 log=(r/row['log']).read_text(encoding='utf-8',errors='replace');mode=re.search(r'(bgfx|wgpu)-sdk-',row['label']).group(1);entry={'label':row['label'],'exit':row['exit']}
 if row['label'].endswith('c11'):entry['pass']=row['exit']==0
 else:
  api,policy=re.search(r'-sdk-(d3d12|vulkan|opengl)-(native|portable)$',row['label']).groups();ordinal={'d3d12':4,'vulkan':1,'opengl':2}[api]
  entry['receipt']=re.findall(r'installed_sdk .*',log);entry['modules']=dict(re.findall(r'sdk_loaded_module name=(\S+) path=([^\r\n]+)',log))
  entry['pass']=row['exit']==0 and bool(re.search(r'installed_sdk renderer='+str(ordinal)+r' vendor_id=0x10de\b.*pixels_ok=1',log)) and 'reference_ready=1' in log and ('sdk_sampling policy='+('0' if policy=='native' else '1')) in log and 'result=1' in log
  for name in ['Coin4.dll','CoinRender4.dll']:
   expected=r/('install-'+mode)/'bin'/name;actual=entry['modules'].get(name,'');entry['pass'] &= actual.casefold()==str(expected).casefold()
 report['sdk'].append(entry)
report['commands_failed']=[x['label'] for x in ledger if x['exit']!=0]
if (r/'camera-checks.json').exists():
 report['camera']=[]
 for row in json.loads((r/'camera-checks.json').read_text()):
  api=row['label'].rsplit('-',1)[1];ordinal={'d3d12':'4','vulkan':'1','opengl':'2'}[api]
  log=(r/row.get('log',row['label']+'.log')).read_text(encoding='utf-8',errors='replace')
  receipts=re.findall(r'camera_gpu adapter=.*',log)
  physical=row['exit']==0 and len(receipts)==2 and all(re.search(r' renderer='+ordinal+r' vendor=0x10de\b',line) for line in receipts)
  report['camera'].append({'label':row['label'],'exit':row['exit'],'receipts':receipts,'physical_api_pass':bool(physical)})
(r/'analysis.json').write_text(json.dumps(report,indent=2));print(json.dumps(report,indent=2))
