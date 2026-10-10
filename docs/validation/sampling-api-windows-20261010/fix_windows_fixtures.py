from pathlib import Path
root=Path(r'C:\Users\Diklu\.codex\worktrees\sampling-win-20261010\coin')
p=root/'testsuite/CMakeLists.txt';s=p.read_text(encoding='utf-8')
old='\t\t\ttarget_compile_definitions(CoinRenderGluNativeOracle PRIVATE COIN_NOT_DLL)\n'
assert old in s;s=s.replace(old,'');p.write_text(s,encoding='utf-8',newline='\n')
p=root/'include/Inventor/system/gl-fallbacks.h';s=p.read_text(encoding='utf-8')
assert '#ifndef GL_SAMPLES\n' not in s
s=s.replace('#ifndef GL_MAX_TEXTURE_COORDS_ARB','#ifndef GL_SAMPLES\n#define GL_SAMPLES 0x80A9\n#endif /* GL_SAMPLES */\n\n#ifndef GL_MAX_TEXTURE_COORDS_ARB',1)
p.write_text(s,encoding='utf-8',newline='\n')
