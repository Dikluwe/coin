import importlib.util, json, subprocess, statistics
from pathlib import Path
spec=importlib.util.spec_from_file_location("runner", "/tmp/coin-render-first-frame/scripts/coinrender/run_animation_benchmark.py")
r=importlib.util.module_from_spec(spec); spec.loader.exec_module(r)
build=Path("/tmp/coin-render-first-frame-wgpu"); out=Path("/tmp/coin-render-wgpu-motion-ablation"); out.mkdir(exist_ok=True)
rows=[]; manifest={"binary_hashes":{str(p):r.sha256(p) for p in (build/"bin/coin_render_gl_benchmark",build/"lib/libCoinRender.so",build/"lib/libCoin.so.80")},"commands":[]}
configs=[("full",{}),("cpp-only",{"COIN_WGPU_DISABLE_FRAME_BUFFER_REUSE":"1"}),("arena-only",{"COIN_WGPU_DISABLE_FRAME_BUFFER_REUSE":"1","COIN_WGPU_DISABLE_INCREMENTAL_OPAQUE_BATCH":"1"})]
for round_index in range(3):
 for case in ["transforms-10","transforms-100"]:
  for name, switches in (configs if round_index%2==0 else list(reversed(configs))):
   stem=f"{case}-{name}-{round_index+1}"; env=r.environment(build,"wgpu-vulkan","nvidia"); env.update(switches)
   cmd=[str(build/"bin/coin_render_gl_benchmark"),"--backend","wgpu","--scene","/tmp/coin-render-city-40000.iv","--animation","transforms","--animated-percent",case.split("-")[-1],"--transparency","object","--warmup","5","--frames","20","--size","1024","--samples-output",str(out/(stem+".csv"))]
   manifest["commands"].append({"stem":stem,"command":cmd,"switches":switches,"environment":{k:env[k] for k in ["LD_LIBRARY_PATH","VK_ICD_FILENAMES","__NV_PRIME_RENDER_OFFLOAD","__GLX_VENDOR_LIBRARY_NAME","WGPU_BACKEND"]}})
   r.write_json(out/"manifest.json",manifest); print("START",stem,flush=True)
   result=subprocess.run(["/usr/bin/time","-f","benchmark_peak_rss_kib=%M",*cmd],env=env,capture_output=True,text=True,timeout=1800)
   (out/(stem+".log")).write_text(result.stdout+result.stderr)
   if result.returncode: raise RuntimeError(stem+result.stderr[-2000:])
   stats=r.csv_stats(out/(stem+".csv"),20); row={"case":case,"config":name,"round":round_index+1,"stats":stats}; rows.append(row); r.write_json(out/"results.json",rows)
   print("DONE",stem,stats["total_ms"]["median_ms"],flush=True)
for case in ["transforms-10","transforms-100"]:
 for name,_ in configs:
  print("SUMMARY",case,name,statistics.median(x["stats"]["total_ms"]["median_ms"] for x in rows if x["case"]==case and x["config"]==name),flush=True)
