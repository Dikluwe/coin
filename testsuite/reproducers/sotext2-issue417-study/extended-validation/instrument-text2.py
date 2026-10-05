from pathlib import Path
import shlex, subprocess, sys
build=Path('/tmp/coin-issue417-branch-build')
mode = sys.argv[1] if len(sys.argv) > 1 else 'ubsan'
sanitizers = 'address,undefined,float-cast-overflow' if mode == 'asan' else 'undefined,float-cast-overflow'
out=Path('/tmp/coin-issue417-study') / mode;out.mkdir(exist_ok=True)
flags=(build/'src/shapenodes/CMakeFiles/shapenodes.dir/flags.make').read_text().splitlines()
args=[]
for key in ('CXX_DEFINES','CXX_INCLUDES','CXX_FLAGS'):
 line=next(x for x in flags if x.startswith(key+' ='))
 args+=shlex.split(line.split('=',1)[1])
source='/home/dikluwe/.codex/worktrees/issue417-sotext2/coin/src/shapenodes/SoText2.cpp'
subprocess.run(['/usr/bin/c++',*args,'-O1','-g','-UNDEBUG','-fsanitize='+sanitizers,'-fno-sanitize-recover=all','-c',source,'-o',str(out/'SoText2.cpp.o')],check=True)
rsp=(build/'src/CMakeFiles/Coin.dir/objects1.rsp').read_text()
rsp=rsp.replace('shapenodes/CMakeFiles/shapenodes.dir/SoText2.cpp.o',str(out/'SoText2.cpp.o'))
(out/'objects.rsp').write_text(rsp)
args=shlex.split((build/'src/CMakeFiles/Coin.dir/link.txt').read_text())
args[args.index('-o')+1]=str(out/'libCoin.so.80.0.10')
args[args.index('@CMakeFiles/Coin.dir/objects1.rsp')]='@'+str(out/'objects.rsp')
args.insert(1,'-fsanitize='+sanitizers)
subprocess.run(args,cwd=build/'src',check=True)
for name in ['libCoin.so.80','libCoin.so']:
 p=out/name
 if p.is_symlink():p.unlink()
 p.symlink_to('libCoin.so.80.0.10')
print('Instrumented SoText2 only:', sanitizers, 'assertions enabled.')
