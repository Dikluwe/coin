import pathlib, subprocess, tarfile, io, json, difflib, os, time
r=pathlib.Path(r'H:\Git\coin\build\sampling-win-20261010'); old=pathlib.Path(r'H:\Git\coin\build\bgfx-windows-source'); src=r/'bgfx-device-loss-source'; src.mkdir(exist_ok=True)
provenance=[]
for part in ['', 'bgfx', 'bx', 'bimg']:
 repo=old/part; sha=subprocess.check_output(['git','-C',str(repo),'rev-parse','HEAD'],text=True).strip(); target=src/part; target.mkdir(exist_ok=True)
 data=subprocess.check_output(['git','-C',str(repo),'archive',sha]); tarfile.open(fileobj=io.BytesIO(data)).extractall(target,filter='data'); provenance.append({'part':part,'revision':sha})
p=src/'bgfx/src/renderer_d3d12.cpp'; before=p.read_text(); marker='\t\t\tDX_RELEASE(m_zeroInitBuffer, 0);\n\n\t\t\tpreReset();'
assert before.count(marker)==1
replacement='''\t\t\tDX_RELEASE(m_zeroInitBuffer, 0);

#if BX_PLATFORM_WINDOWS
			// Device removal destroys the renderer before queued framebuffer teardown.
			// Release secondary native swapchains so the removed device can die.
			for (uint32_t ii = 0; ii < BX_COUNTOF(m_frameBuffers); ++ii)
			{
				if (ii != kMainFrameBufferIdx && NULL != m_frameBuffers[ii].m_swapChain)
				{
					m_frameBuffers[ii].destroy();
				}
			}
#endif // BX_PLATFORM_WINDOWS

			preReset();'''
after=before.replace(marker,replacement); p.write_text(after,newline='\n'); (r/'bgfx-device-loss.patch').write_text(''.join(difflib.unified_diff(before.splitlines(True),after.splitlines(True),fromfile='a/bgfx/src/renderer_d3d12.cpp',tofile='b/bgfx/src/renderer_d3d12.cpp'))); (r/'bgfx-device-loss-provenance.json').write_text(json.dumps(provenance,indent=2))
e={k.upper():v for k,v in os.environ.items()};e['MSBUILDDISABLENODEREUSE']='1'; rows=[]
commands=[['cmake','-S',str(src),'-B',str(r/'bgfx-device-loss-build'),'-G','Visual Studio 17 2022','-A','x64','-DBGFX_BUILD_EXAMPLES=OFF','-DBGFX_BUILD_TESTS=OFF','-DBGFX_BUILD_TOOLS=ON','-DBGFX_INSTALL=ON','-DBGFX_LIBRARY_TYPE=STATIC','-DBGFX_CONFIG_MAX_DRAW_CALLS=131072','-DCMAKE_INSTALL_PREFIX='+str(r/'bgfx-device-loss-install')],['cmake','--build',str(r/'bgfx-device-loss-build'),'--config','Release','--parallel','2'],['cmake','--install',str(r/'bgfx-device-loss-build'),'--config','Release']]
for name,cmd in zip(['configure','build','install'],commands):
 start=time.time(); print(name,'started',flush=True)
 with (r/('bgfx-device-loss-'+name+'.log')).open('w') as f: code=subprocess.run(cmd,env=e,stdout=f,stderr=subprocess.STDOUT).returncode
 rows.append({'command':cmd,'exit':code,'seconds':time.time()-start});(r/'bgfx-device-loss-build-ledger.json').write_text(json.dumps(rows,indent=2));print(name,code,flush=True)
 if code:raise SystemExit(code)
