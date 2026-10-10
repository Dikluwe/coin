import pathlib,difflib,subprocess,json,time,os,shutil
r=pathlib.Path(__file__).resolve().parent;src=r/'bgfx-device-loss-source/bgfx/src';orig=pathlib.Path(r'H:\Git\coin\build\bgfx-windows-source\bgfx\src')
for name in ['bgfx-device-loss.patch','bgfx-device-loss-installed-hashes.json']:shutil.copyfile(r/name,r/(name.replace('.', '-before-history.',1)))
h=src/'renderer_d3d12.h';s=h.read_text();marker='struct TimerQueryD3D12\n\t{';assert marker in s;s=s.replace('\t\t\t: m_control(kMinTimerQueries)','\t\t\t: m_traceResults(false)\n\t\t\t, m_control(kMinTimerQueries)',1);s=s.replace('\t\tuint64_t m_frequency;','\t\tbool m_traceResults;\n\t\tuint64_t m_frequency;',1);h.write_text(s,newline='\n')
p=src/'renderer_d3d12.cpp';s=p.read_text();s=s.replace('#include "bgfx_p.h"','#include "bgfx_p.h"\n#include <cstdio>\n#include <cstdlib>',1);marker='void TimerQueryD3D12::init()\n\t{';assert marker in s;s=s.replace(marker,marker+'\n\t\tm_traceResults = NULL != std::getenv("COIN_BGFX_TRACE_D3D12_QUERY_RESULTS");',1);marker='\t\t\tresult.m_end    = m_queryResult[offset+1];';assert s.count(marker)==1;s=s.replace(marker,marker+'''
			if (m_traceResults)
			{
				// Preserve every completed query before the public latest-result slot is reused.
				std::fprintf(stderr, "COIN_BGFX_D3D12_QUERY kind=%s view=%u frame=%u begin=%llu end=%llu frequency=%llu\\n",
					query.m_resultIdx == BGFX_CONFIG_MAX_VIEWS ? "frame" : "view",
					query.m_resultIdx, query.m_frameNum,
					(unsigned long long)result.m_begin, (unsigned long long)result.m_end,
					(unsigned long long)m_frequency);
			}''',1);p.write_text(s,newline='\n')
patch=''
for name in ['renderer_d3d12.cpp','renderer_d3d12.h']:patch+=''.join(difflib.unified_diff((orig/name).read_text().splitlines(True),(src/name).read_text().splitlines(True),fromfile='a/bgfx/src/'+name,tofile='b/bgfx/src/'+name))
(r/'bgfx-device-loss.patch').write_text(patch)
e={k.upper():v for k,v in os.environ.items()};e['MSBUILDDISABLENODEREUSE']='1';rows=[]
for name,cmd in [('build',['cmake','--build',str(r/'bgfx-device-loss-build'),'--config','Release','--parallel','2']),('install',['cmake','--install',str(r/'bgfx-device-loss-build'),'--config','Release'])]:
 start=time.time();print(name,'started',flush=True)
 with (r/('bgfx-device-loss-history-'+name+'.log')).open('w') as f:code=subprocess.run(cmd,env=e,stdout=f,stderr=subprocess.STDOUT).returncode
 rows.append(dict(command=cmd,exit=code,seconds=time.time()-start));(r/'bgfx-device-loss-history-ledger.json').write_text(json.dumps(rows,indent=2));print(name,code,flush=True)
 if code:raise SystemExit(code)
