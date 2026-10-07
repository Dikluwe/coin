import pathlib,shutil,json,hashlib,subprocess,re
source=pathlib.Path('/tmp/coin-render-first-frame'); raw=pathlib.Path('/tmp/coin-hardware-20261007');dest=source/'docs/validation/hardware-surfaces-linux-20261007'
dest.mkdir(parents=True,exist_ok=True)
for item in raw.iterdir():
 if item.name in ('android','source'):continue
 if item.is_dir():shutil.copytree(item,dest/item.name,dirs_exist_ok=True)
 else:shutil.copy2(item,dest/item.name)
for backend in ('bgfx','wgpu'):
 shutil.copy2(pathlib.Path('/tmp/coin-render-first-frame-'+backend)/'Testing/Temporary/LastTest.log',dest/('cpu-'+backend+'-details.log'))
old=source/'docs/inventories/coin-render-p20-physical-matrix.json'
if not (dest/'prior-p20-matrix.json').exists(): shutil.copy2(old,dest/'prior-p20-matrix.json')
commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=source,text=True).strip()
cells=[];comparisons=[];devices={}
for gpu in ('amd','nvidia'):
 d=json.loads((raw/(gpu+'-p20')/'results.json').read_text());devices[gpu]={k:d[k] for k in ('expected_ids','gl_renderer','icd','vulkan_driver')}
 for c in d['comparisons']:comparisons.append(dict(device=gpu,**c))
 for row in d['runs']:
  c=next((x for x in d['comparisons'] if x.get('backends')==['coin-gl',row['backend']] and x['scene']==row['scene'] and x['target']==row['target']),None)
  cells.append(dict(device=gpu,target=row['target'],scene=row['scene'],backend=row['backend'],execution='pass',qualification='verified nondegenerate RGB oracle' if row['backend']=='coin-gl' else 'RGB-qualified' if c and c['passed'] else 'RGB-divergence-delimited-study',rgb_reference_metrics=c.get('metrics') if c else None,rgba_fnv64=row['rgba_fnv64'],log='../validation/hardware-surfaces-linux-20261007/'+gpu+'-p20/'+row['log']))
for target in ('window','offscreen'):
 for scene in ('opaque-interleaved','transparent-overlap'):
  for backend in ('coin-gl','bgfx-opengl','bgfx-vulkan','wgpu-vulkan'):
   cells.append(dict(device='intel',target=target,scene=scene,backend=backend,execution='not-run',qualification='physical-device-unavailable',reason='No Intel graphics PCI device on this AMD/NVIDIA host'))
old.write_text(json.dumps(dict(profile='P20 Linux physical X11, 128x128, object, 1 warmup + 3 frames; top-down RGB max<=2 MAE<=1.0; separate Vulkan RGBA exactness',source_base_commit=commit,source_manifest='../validation/hardware-surfaces-linux-20261007/manifest.json',devices=devices,cells=cells,comparisons=comparisons,summary=dict(planned=48,executed=32,execution_failures=0,unavailable=16,rgb_comparisons=24,rgb_passed=22,rgb_divergences=2,vulkan_rgba_pairs_exact=8)),indent=2)+'\n')
changed=subprocess.check_output(['git','diff','--name-only'],cwd=source,text=True).splitlines()
changed+=subprocess.check_output(['git','ls-files','--others','--exclude-standard'],cwd=source,text=True).splitlines()
files=sorted({p for p in changed if p.endswith(('.cpp','.h','.py')) or p.endswith('CMakeLists.txt') or p=='.github/scripts/qualify-coin-render-shadows-linux.sh'})
files=[p for p in files if not p.startswith('docs/validation/') and '__pycache__' not in p]
source_hashes={}
for name in files:
 p=source/name; to=dest/'source'/name;to.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,to);source_hashes[name]=hashlib.sha256(p.read_bytes()).hexdigest()
(dest/'source/tracked.patch').write_bytes(subprocess.check_output(['git','diff','--','.github','examples','scripts','src','testsuite'],cwd=source))
manifest=dict(source_base_commit=commit,source_sha256=source_hashes,native_binary_sha256={},native_library_byte_identity_after_common_gl_cleanup={},qualified_shadow_cells={},controls={},divergences=['AMD offscreen transparency Vulkan versus CoinGL: max17,4 pixels over3; gate not relaxed','AMD camera Vulkan versus CoinGL frame5: same mismatch on full traversal; gate not relaxed'],excluded={'amd-wgpu-vulkan':'interrupted by test link repair; replaced by complete amd-wgpu-vulkan-final','*-wayland-scale1/2':'launcher GDK_BACKEND leak; smoke did not execute','*-wayland-scale2-final':'passed with actual scale1; not a scale2 qualification'},limits=['No Intel graphics, new Windows/macOS or Android device execution','Android native libraries/app link remains blocked by real desktop GL components','Wayland headless integer scale smoke; fractional scale/physical output/other visual profiles pending','Native eight-map CoinGL reference unavailable at8 coordinate units','Initial test executables evolved during diagnostics; Coin/CoinRender shared libraries are byte-identical before and after final native compilation'])
for backend in ('bgfx','wgpu'):
 build=pathlib.Path('/tmp/coin-render-first-frame-'+backend)
 for name in ('CoinRenderShadowReferenceTest','CoinRenderShadowEightMapTest','CoinRenderCameraReuseReferenceTest','CoinBumpGLXTest','coin_render_window_benchmark','coin_render_gl_benchmark','CoinTests')+ (('coin_render_wayland_smoke',) if backend=='wgpu' else ()):
  p=build/'bin'/name;manifest['native_binary_sha256'][backend+'/bin/'+name]=hashlib.sha256(p.read_bytes()).hexdigest()
 for name in ('libCoin.so','libCoinRender.so'):
  p=(build/'lib'/name).resolve();manifest['native_binary_sha256'][backend+'/lib/'+name]=hashlib.sha256(p.read_bytes()).hexdigest()
before=json.loads((raw/'native-libraries-before-header-cleanup.json').read_text())
manifest['native_library_byte_identity_after_common_gl_cleanup']={p:hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()==sha for p,sha in before.items()}
for key,folder,expected in [('amd-bgfx-vulkan','amd-bgfx-vulkan',51),('amd-bgfx-opengl','amd-bgfx-opengl-final',52),('amd-wgpu-vulkan','amd-wgpu-vulkan-final',48),('nvidia-bgfx-vulkan','nvidia-bgfx-vulkan',52),('nvidia-bgfx-opengl','nvidia-bgfx-opengl-final',52),('nvidia-wgpu-vulkan','nvidia-wgpu-vulkan',48)]:
 log=(raw/folder/'tests.txt').read_text(); count=int(re.search(r'0 tests failed out of (\d+)',log)[1]);assert count==expected and 'Skipped' not in log and 'Not Run' not in log
 manifest['qualified_shadow_cells'][key]={'ctest_pass':count,'ctest_failed':0,'ctest_skipped':0,'additional_eight_map_pass':1 if key=='amd-bgfx-vulkan' else 0,'evidence':folder}
manifest['qualified_shadow_checks_total']=sum(d['ctest_pass']+d['additional_eight_map_pass'] for d in manifest['qualified_shadow_cells'].values())
wayland=json.loads((raw/'wayland-qualified-results.json').read_text())
for row in wayland:
 assert row['exit']==0
 log=(raw/row['key']/'run.log').read_text();scale=int(row['key'].split('scale')[1].split('-')[0]);assert set(re.findall(r' scale=(\d+)',log))=={str(scale)};assert 'wayland_CoinGL_rgb_max=0' in log;assert 'wayland_surface_recreate=3 success=1' in log
manifest['wayland_qualified_cells']=[d['key'] for d in wayland]
for file in ('wayland-scale-negative-result.json','wrong-egl-negative-result.json'):
 d=json.loads((raw/file).read_text());assert d['exit']==d['expected_exit'];manifest['controls'][file]={'exit':d['exit'],'expected_exit':d['expected_exit']}
manifest['artifact_sha256']={str(p.relative_to(dest)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(dest.rglob('*')) if p.is_file() and p.name!='manifest.json'}
(dest/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
print('archived',len(manifest['artifact_sha256']),'files; shadow checks',manifest['qualified_shadow_checks_total'])
