import pathlib,subprocess,sys,os,json,time
r=pathlib.Path(__file__).parent;s=pathlib.Path(sys.argv[1]);env={k.upper():v for k,v in os.environ.items()};env['CARGO_TARGET_DIR']=str(r/'build-wgpu/rust_target');env['CARGO_BUILD_JOBS']='2'
cmd=['cargo','test','--offline','--locked','--release','--manifest-path',str(s/'src/rendering/coinwgpu/rust_bridge/Cargo.toml'),'--lib','--tests','--','--test-threads=1'];t=time.time()
with (r/'rust-serial.log').open('w',encoding='utf-8') as f:code=subprocess.run(cmd,env=env,stdout=f,stderr=subprocess.STDOUT).returncode
(r/'rust-serial.json').write_text(json.dumps({'command':cmd,'exit':code,'seconds':time.time()-t,'jobs':2,'test_threads':1},indent=2));print('Rust serial exit',code,flush=True)
# Keep subsequent SDK diagnostics available even when Rust assertions fail.
