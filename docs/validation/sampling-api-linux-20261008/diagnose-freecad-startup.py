import os,json,subprocess,shutil
from pathlib import Path
r=Path('/mnt/Laranja/Git/externos/coin-portable-sampling-artifacts/20261008-sampling-api-linux');repo=Path('/home/dikluwe/.codex/worktrees/coin-portable-sampling/coin');out=r/'freecad-startup-diagnostic';out.mkdir(exist_ok=True)
prior=next(x for x in json.loads((r/'freecad-qualified-final/summary.json').read_text()) if x['profile']=='wgpu-nvidia-vulkan' and x['policy']=='portable');env=dict(os.environ);env.update(prior['environment']);host=r/'freecad-host/build';exe=host/'bin/FreeCAD'
fixture=out/'fixture';fixture.mkdir(exist_ok=True)
for name in ['freecad_sampling_policy.FCMacro','freecad_screen_content.FCMacro','freecad_overlays.FCMacro']:shutil.copyfile(repo/'testsuite/qt-quarter'/name,fixture/name)
p=fixture/'freecad_sampling_policy.FCMacro';s=p.read_text();s+='''
original_baseline_ready = baseline_ready

def baseline_ready():
    # Diagnostic only: isolate root changes caused by picking/font first use.
    before = capture('startup-before-pick')
    viewport = viewer_widget().viewport()
    ratio = viewer_widget().devicePixelRatioF()
    picked = view.getObjectInfo((int(viewport.width()*ratio/2),int(viewport.height()*ratio/2)))
    after_pick = capture('startup-after-pick')
    stages[0][1].whichChild = coin.SO_SWITCH_ALL
    capture('startup-first-glyph')
    stages[0][1].whichChild = coin.SO_SWITCH_NONE
    after_glyph = capture('startup-after-glyph')
    rect = QtCore.QRect(0,0,before.width()-1,before.height()-1)
    report['startup_diagnostic'] = {'picking_delta':changes(before,after_pick,rect),
        'glyph_delta':changes(after_pick,after_glyph,rect),'document_pick':bool(picked)}
    original_baseline_ready()
''';p.write_text(s)
profile=out/'private-profile';profile.mkdir(exist_ok=True);old=r/'freecad-qualified-final/wgpu-nvidia-vulkan-portable/vulkan-object-opaque-1x-freecad-sampling-policy/private-profile'
for name in ['user.cfg','system.cfg']:
 if (old/name).exists():shutil.copyfile(old/name,profile/name)
env.update(FREECAD_COIN_WGPU='1',COIN_TEST_GL_REFERENCE='0',QT_QPA_PLATFORM='xcb',COIN_RENDER_TRACE_PHASES='1',COIN_BGFX_RENDERER='vulkan',COIN_BGFX_TRANSPARENCY='object',COIN_RENDER_TRANSPARENCY='object',COIN_TEST_ALPHA='opaque',QT_SCALE_FACTOR='1',COIN_TEST_ARTIFACTS=str(out),COIN_TEST_MACRO_DIR=str(fixture))
cmd=[str(exe),'--user-cfg',str(profile/'user.cfg'),'--system-cfg',str(profile/'system.cfg'),str(p)]
(out/'command.json').write_text(json.dumps(dict(command=cmd,environment={k:v for k,v in env.items() if k in prior['environment'] or k.startswith('COIN_TEST') or k in ['FREECAD_COIN_WGPU','QT_SCALE_FACTOR','QT_QPA_PLATFORM','COIN_RENDER_TRACE_PHASES','COIN_RENDER_TRANSPARENCY','COIN_BGFX_TRANSPARENCY','COIN_BGFX_RENDERER']},note='Diagnostic fixture adds picking/glyph startup probes; not part of qualification totals.'),indent=2))
with (out/'process.log').open('w') as f:code=subprocess.run(cmd,env=env,stdout=f,stderr=subprocess.STDOUT,timeout=150).returncode
print('exit',code)
if (out/'result.json').exists():
 t=json.loads((out/'result.json').read_text());print(t['status'],t.get('reason'),t.get('startup_diagnostic'))
