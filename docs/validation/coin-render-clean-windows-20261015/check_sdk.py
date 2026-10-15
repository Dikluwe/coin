import pathlib,subprocess,json,hashlib,re,os,sys
r=pathlib.Path(__file__).parent;s=pathlib.Path(sys.argv[1]);env={k.upper():v for k,v in os.environ.items()};env['MSBUILDDISABLENODEREUSE']='1'
if not (r/'rust-serial.json').exists():subprocess.run([sys.executable,str(r/'rust_tests.py'),str(s)],check=True)
cm=(s/'src/rendering/coinrender/CMakeLists.txt').read_text(encoding='utf-8')
headers=sorted(set(re.findall(r'\$\{PROJECT_SOURCE_DIR\}/include/(Inventor/[^\s]+\.h)',cm)));assert len(headers)==17,len(headers)
d=r/'headers-src';d.mkdir(exist_ok=True)
(d/'main.cpp').write_text('\n'.join('#include <'+h+'>' for h in headers)+'\nint main(){CoinRenderOptions o;return o.textureSamplingPolicy==COIN_RENDER_SAMPLING_NATIVE?0:1;}\n')
(d/'CMakeLists.txt').write_text('''cmake_minimum_required(VERSION 3.20)
project(public_headers LANGUAGES CXX)
find_package(Coin REQUIRED)
find_package(CoinRender REQUIRED)
add_executable(public_headers main.cpp)
set_property(TARGET public_headers PROPERTY CXX_STANDARD 11)
set_property(TARGET public_headers PROPERTY CXX_STANDARD_REQUIRED ON)
target_link_libraries(public_headers PRIVATE CoinRender::CoinRender Coin::Coin)
''')
results=[]
for mode in ['off','recording','bgfx','wgpu']:
 prefix=r/('install-'+mode);row={'mode':mode,'headers':[]};ok=True
 if not prefix.exists():row['missing_install']=True;results.append(row);continue
 for h in headers:
  p=prefix/'include'/h;expected=mode!='off';exists=p.is_file();item={'path':h,'installed':exists,'expected':expected}
  if exists:item['sha256']=hashlib.sha256(p.read_bytes()).hexdigest();item['source_equal']=p.read_bytes()==(s/'include'/h).read_bytes();ok &= item['source_equal']
  ok &= exists==expected;row['headers'].append(item)
 if mode!='off':
  b=r/('headers-'+mode);e=dict(env);e['PATH']=str(prefix/'bin')+os.pathsep+e['PATH'];steps=[]
  for label,cmd in [('configure',['cmake','-S',str(d),'-B',str(b),'-G','Visual Studio 17 2022','-A','x64','-DCMAKE_PREFIX_PATH='+str(prefix)]),('build',['cmake','--build',str(b),'--config','Release','--parallel','2']),('run',[str(b/'Release/public_headers.exe')])]:
   with (r/('headers-'+mode+'-'+label+'.log')).open('w') as f:code=subprocess.run(cmd,env=e,stdout=f,stderr=subprocess.STDOUT).returncode
   steps.append({'step':label,'command':cmd,'exit':code});ok &= code==0
   if code:break
  row['probe_steps']=steps
 row['pass']=bool(ok);results.append(row)
(r/'header-sdk-results.json').write_text(json.dumps(results,indent=2));print([(x['mode'],x.get('pass')) for x in results])
