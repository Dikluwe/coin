import pathlib,shutil,json,hashlib,subprocess,sys
r=pathlib.Path(__file__).parent
repo=pathlib.Path(r'C:\Users\Diklu\.codex\worktrees\sampling-win-20261010\coin')
out=repo/'docs/validation/windows-visual-20261010';out.mkdir(exist_ok=True)
for cohort in sys.argv[1:]:
 dst=out/cohort;dst.mkdir(exist_ok=True)
 for p in (r/cohort).iterdir():
  if p.is_file() and p.suffix!='.ppm':shutil.copy2(p,dst/p.name)
for name in ['visual-src','visual-initial-source','visual-million-source']:
 shutil.copytree(r/name,out/name,dirs_exist_ok=True)
scripts=out/'scripts';scripts.mkdir(exist_ok=True)
for name in ['prepare_visual.py','extend_visual.py','build_visual.py','run_visual.py','analyze_visual.py','compare_visual.py','finish_visual.py','match_visual_control.py','report_visual.py','package_visual.py']:
 shutil.copy2(r/name,scripts/name)
for p in r.glob('visual-*-normalized.log'):shutil.copy2(p,out/p.name)
for name in ['visual-build-ledger.json','visual-finish-ledger.json','bgfx-device-loss-installed-hashes.json','query-resize-driver-ledger.json','hardware.json']:
 if (r/name).exists():shutil.copy2(r/name,out/name)
rev=subprocess.check_output(['git','rev-parse','HEAD'],cwd=repo,text=True).strip()
hashes=[]
for backend in ['bgfx','wgpu']:
 for p in [(r/('install-'+backend)/'bin'/f) for f in ['Coin4.dll','CoinRender4.dll']]+[r/('visual-'+backend)/'Release/coin_visual_campaign.exe']:
  hashes.append({'path':str(p),'size':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()})
(out/'provenance.json').write_text(json.dumps({'coin_source_revision':rev,'gpu':'NVIDIA GeForce GTX 1060 6GB','driver':'581.08 / 32.0.15.8108','extent':[1280,960],'tolerance':0,'builds':hashes,'sdk_reference':'../windows-remaining-20261010','initial_fixture_revision':'visual-initial-source','extended_fixture_revision':'visual-src'},indent=2))
(out/'.gitattributes').write_text('* -text\n')
files=[{'path':p.relative_to(out).as_posix(),'size':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()} for p in sorted(out.rglob('*')) if p.is_file() and p.name!='artifact-manifest.json']
(out/'artifact-manifest.json').write_text(json.dumps(files,indent=2))
print(json.dumps({'files':len(files),'bytes':sum(p['size'] for p in files)}))
