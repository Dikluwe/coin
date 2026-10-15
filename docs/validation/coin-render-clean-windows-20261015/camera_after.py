import pathlib,time,subprocess,sys
r=pathlib.Path(__file__).parent
while not (r/'recording-correction.json').exists():time.sleep(2)
subprocess.run([sys.executable,str(r/'camera_checks.py')],cwd=r,check=True)
