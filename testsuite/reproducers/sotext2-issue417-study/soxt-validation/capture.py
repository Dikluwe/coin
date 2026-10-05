import json, os, re, subprocess, sys, time
from pathlib import Path
from PIL import Image
base=Path('/tmp/coin-issue417-study')
version=sys.argv[1] if len(sys.argv)>1 else '1.4.2'
soxtbuild=base/('soxt-1.4.0-build' if version=='1.4.0' else 'soxt-build')
binary=base/('original-soxt-1.4.0-example' if version=='1.4.0' else 'original-soxt-example')
output=base/('soxt-1.4.0-validation' if version=='1.4.0' else 'soxt-validation')
for variant in ['master','fixed']:
 out=output/variant;out.mkdir(parents=True,exist_ok=True)
 coin=base/'master-build/lib' if variant=='master' else Path('/tmp/coin-issue417-branch-build/lib')
 env=dict(os.environ,LD_LIBRARY_PATH=str(coin)+':'+str(soxtbuild/'lib'),LIBGL_ALWAYS_SOFTWARE='1')
 with (out/'launch.log').open('w') as log:
  app=subprocess.Popen([str(binary)],env=env,stdout=log,stderr=log)
  try:
   window=None
   for _ in range(100):
    if app.poll() is not None:raise RuntimeError('SoXt exited early: '+str(app.returncode))
    tree=subprocess.check_output(['xwininfo','-root','-tree'],env=env,text=True)
    match=re.search(r'(0x[0-9a-f]+) "(?:2D Text|original-soxt-example)[^"]*"',tree)
    if match:window=match.group(1);break
    time.sleep(.1)
   if not window:raise RuntimeError('SoXt window not found: '+tree)
   time.sleep(2)
   (out/'window-tree.txt').write_text(subprocess.check_output(['xwininfo','-root','-tree'],env=env,text=True))
   (out/'window-info.txt').write_text(subprocess.check_output(['xwininfo','-id',window],env=env,text=True))
   maps=Path('/proc',str(app.pid),'maps').read_text()
   libraries=sorted({line.split()[-1] for line in maps.splitlines() if ('libCoin.so' in line or 'libSoXt.so' in line or 'libXm.so' in line) and '/' in line})
   (out/'libraries.json').write_text(json.dumps(libraries,indent=2))
   subprocess.run(['xwd','-silent','-id',window,'-out',str(out/'window.xwd')],env=env,check=True)
   subprocess.run(['convert',str(out/'window.xwd'),str(out/'window.png')],env=env,check=True)
   image=Image.open(out/'window.png').convert('RGB')
   count=sum(max(pixel)>0 for pixel in image.getdata())
   assert len([lib for lib in libraries if 'libCoin.so' in lib]) == 1
   assert any(lib.startswith(str(coin)+'/') and 'libCoin.so' in lib for lib in libraries)
   assert any(lib.startswith(str(soxtbuild/'lib')+'/') and 'libSoXt.so' in lib for lib in libraries)
   assert (count > 0) == (variant == 'fixed')
   result={'soxt_version':version,'variant':variant,'window':window,'dimensions':image.size,'nonblack_pixels':count,'libraries':libraries}
   (out/'result.json').write_text(json.dumps(result,indent=2));print(json.dumps(result),flush=True)
  finally:
   app.terminate()
   try:app.wait(timeout=5)
   except subprocess.TimeoutExpired:app.kill();app.wait()
