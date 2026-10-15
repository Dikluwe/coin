import ctypes,pathlib,os,time,subprocess,json,sys
r=pathlib.Path(__file__).parent;s=sys.argv[1]
k=ctypes.WinDLL('kernel32',use_last_error=True);k.OpenProcess.restype=ctypes.c_void_p;k.OpenProcess.argtypes=[ctypes.c_ulong,ctypes.c_int,ctypes.c_ulong];k.WaitForSingleObject.argtypes=[ctypes.c_void_p,ctypes.c_ulong];k.CloseHandle.argtypes=[ctypes.c_void_p]
pid=int(sys.argv[2]);h=k.OpenProcess(0x00100000,False,pid)
if not h:raise RuntimeError('Cannot wait on initial runner',ctypes.get_last_error())
print('waiting for initial matrix pid',pid,flush=True)
while k.WaitForSingleObject(h,2000)==258:pass
k.CloseHandle(h)
steps=[['validate.py','--source',s,'--modes','recording','--tag','recording-retry'],['rust_tests.py',s],['check_sdk.py',s],['analyze.py']]
for args in steps:
 print('step',args,flush=True)
 code=subprocess.run([sys.executable]+args,cwd=r).returncode
 if code:sys.exit(code)
