import pathlib,json,subprocess,sys,time,shutil
r=pathlib.Path(__file__).parent;s=sys.argv[1];tag=sys.argv[2] if len(sys.argv)>2 else 'fixed'
while not (r/'template-fix-bgfx-library.json').exists():time.sleep(2)
assert json.loads((r/'template-fix-bgfx-library.json').read_text())['exit']==0
for n in ['off-ctest.xml']:
 if (r/n).exists() and not (r/'initial-source'/n).exists():shutil.copy2(r/n,r/'initial-source'/n)
for args in [['validate.py','--source',s,'--tag',tag],['rust_tests.py',s],['check_sdk.py',s],['analyze.py']]:
 print('step',args,flush=True);code=subprocess.run([sys.executable]+args,cwd=r).returncode
 if code:sys.exit(code)
