"""Serial Windows offscreen reserve A/B, with separate image verification."""
import argparse, hashlib, importlib.util, json, os, pathlib, re, subprocess, time

ROOT = pathlib.Path(__file__).resolve().parent
SOURCE = pathlib.Path(r'C:\Users\Diklu\.codex\worktrees\coin-render-windows-20261007\coin')
parser = argparse.ArgumentParser()
parser.add_argument('--mode', choices=['verify', 'measure'], required=True)
parser.add_argument('--only-case',choices=['static','camera','transforms-10','materials-10','geometry-10','geometry-100'])
parser.add_argument('--only-round',type=int,choices=[1,2,3])
parser.add_argument('--output-directory')
args = parser.parse_args()
OUT = ROOT / 'performance' / (args.output_directory or args.mode)
OUT.mkdir(parents=True, exist_ok=True)
SCENE = ROOT / 'performance' / 'city-2500.iv'
if not SCENE.exists():
    SCENE.write_bytes(pathlib.Path(r'H:\Git\coin\build\large-scenes\city-2500.iv').read_bytes())
spec = importlib.util.spec_from_file_location('animation_stats', SOURCE / 'scripts/coinrender/run_animation_benchmark.py')
stats = importlib.util.module_from_spec(spec)
spec.loader.exec_module(stats)
variants = [('coingl', 'bgfx', 'gl', None)] + [
    (backend+'-'+api+'-'+choice, backend, backend, api)
    for backend, apis in [('bgfx', ['d3d12','vulkan','opengl']), ('wgpu', ['dx12','vulkan','gl'])]
    for api in apis for choice in ['literal', 'reserve']]
cases = ['static', 'camera', 'transforms-10', 'materials-10', 'geometry-10', 'geometry-100']
base = {k.upper():v for k,v in os.environ.items() if not k.upper().startswith(('COIN_', 'WGPU_'))}
base['MSBUILDDISABLENODEREUSE'] = '1'
def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()
manifest = {'source_sha': (ROOT/'source-sha.txt').read_text().strip(), 'scope':'offscreen',
    'size':256, 'scene_sha256':sha(SCENE), 'rounds':1 if args.mode=='verify' else 3,
    'frames':7 if args.mode=='verify' else 60, 'warmup':0 if args.mode=='verify' else 10,
    'binary_hashes':{backend:{name:sha(ROOT/backend/'bin'/name) for name in
        ['Coin4.dll','CoinRender4.dll','coin_render_gl_benchmark.exe']} for backend in ['bgfx','wgpu']}}
manifest_path = OUT/'manifest.json'
if args.only_case or args.only_round:
    manifest.update(selection_case=args.only_case,selection_round=args.only_round)
if manifest_path.exists():
    assert json.loads(manifest_path.read_text()) == manifest, 'Cannot mix different campaigns/binaries'
manifest_path.write_text(json.dumps(manifest, indent=2))
results_path = OUT/'results.json'
results = json.loads(results_path.read_text()) if results_path.exists() else []
for round_index in range(manifest['rounds']):
    if args.only_round and round_index+1 != args.only_round: continue
    with (OUT/f'power-plan-round-{round_index+1}.txt').open('w',encoding='utf-8') as log:
        subprocess.run(['powercfg','/getactivescheme'],stdout=log,stderr=subprocess.STDOUT,check=True)
    with (OUT/f'gpu-state-round-{round_index+1}.csv').open('w',encoding='utf-8') as log:
        probe=subprocess.run(['nvidia-smi','--query-gpu=name,driver_version,memory.total,pstate,power.draw,clocks.current.graphics',
            '--format=csv'],stdout=log,stderr=subprocess.STDOUT)
        if probe.returncode: raise SystemExit('GPU state probe failed')
    order = cases[round_index:] + cases[:round_index]
    if round_index % 2: order.reverse()
    for case_index, case in enumerate(order):
        if args.only_case and case != args.only_case: continue
        offset = (round_index + case_index) % len(variants)
        variant_order = variants[offset:] + variants[:offset]
        if round_index % 2: variant_order.reverse()
        for name, backend, renderer, api in variant_order:
            stem = f'{case}-{name}-{round_index+1}'
            if any(row['stem']==stem and row['exit']==0 for row in results): continue
            animation, percent = stats.case_options(case)
            env = dict(base)
            if api: env['COIN_BGFX_RENDERER' if backend=='bgfx' else 'WGPU_BACKEND'] = api
            if name.endswith('-literal'): env['COIN_RENDER_DISABLE_OBJECT_UPDATE_RESERVE']='1'
            elif name.endswith('-reserve'): env['COIN_RENDER_DISABLE_OBJECT_UPDATE_RESERVE']='0'
            csv = OUT/(stem+'.csv')
            command = [str(ROOT/backend/'bin/coin_render_gl_benchmark.exe'), '--backend', renderer,
                '--scene',str(SCENE),'--animation',animation,'--animated-percent',str(percent),
                '--transparency','object','--size','256','--warmup',str(manifest['warmup']),
                '--frames',str(manifest['frames']),'--samples-output',str(csv)]
            if args.mode=='verify':
                command += ['--animation-step','100','--capture-frames','0,1,2,3,4,5,6',
                            '--capture-prefix',str(OUT/stem)]
            start = time.time()
            with (OUT/(stem+'.log')).open('w', encoding='utf-8') as log:
                proc = subprocess.run(command, env=env, cwd=OUT, stdout=log, stderr=subprocess.STDOUT, timeout=180)
            row = dict(stem=stem,case=case,variant=name,round=round_index+1,command=command,
                exit=proc.returncode,seconds=round(time.time()-start,3),
                environment={k:v for k,v in env.items() if k.startswith(('COIN_','WGPU_'))})
            if not proc.returncode:
                row['stats']=stats.csv_stats(csv, manifest['frames'])
                log_text=(OUT/(stem+'.log')).read_text(errors='replace')
                first=re.search(r'^\S+_first_detail (.+)$',log_text,re.M)
                assert first, (stem,'missing first-frame detail')
                row['first_frame']={key:float(value) for key,value in re.findall(r'(\w+)=([\d.eE+-]+)',first.group(1))}
            if args.mode=='verify':
                row['images']={p.name:sha(p) for p in sorted(OUT.glob(stem+'*.ppm'))}
                assert len(row['images'])==7, (stem, row['images'])
            results.append(row)
            results_path.write_text(json.dumps(results, indent=2))
            print(time.strftime('%H:%M:%S'), stem, 'exit', proc.returncode, flush=True)
            if proc.returncode:
                print((OUT/(stem+'.log')).read_text(errors='replace')[-3000:], flush=True)
                raise SystemExit(proc.returncode)
if args.mode=='verify':
    comparisons=[]
    for case in cases:
        for backend, apis in [('bgfx',['d3d12','vulkan','opengl']),('wgpu',['dx12','vulkan','gl'])]:
            for api in apis:
                pairs=[next(row for row in results if row['case']==case and
                    row['variant']==backend+'-'+api+'-'+choice) for choice in ['literal','reserve']]
                hashes=[list(row['images'].values()) for row in pairs]
                assert hashes[0]==hashes[1], (case, backend, api, 'A/B images differ')
                comparisons.append(dict(case=case,backend=backend,api=api,identical_frames=len(hashes[0])))
    (OUT/'comparisons.json').write_text(json.dumps(comparisons, indent=2))
with (OUT/'gpu-state-finished.csv').open('w',encoding='utf-8') as log:
    subprocess.run(['nvidia-smi','--query-gpu=name,driver_version,memory.total,pstate,power.draw,clocks.current.graphics',
        '--format=csv'],stdout=log,stderr=subprocess.STDOUT,check=True)
print('Completed',len(results),'processes',flush=True)
