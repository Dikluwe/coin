#!/usr/bin/env python3
"""Audit B06 reproduction, CoinRender Linux workaround and negative controls."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import tarfile


def require(value, message):
    if not value:
        raise ValueError(message)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('evidence', type=Path)
    root = parser.parse_args().evidence
    manifest = json.loads((root/'manifest.json').read_text())
    for name,digest in manifest['evidence_sha256'].items():
        require(hashlib.sha256((root/name).read_bytes()).hexdigest()==digest,'changed evidence '+name)
    with tarfile.open(root/manifest['source_snapshot'],'r:gz') as archive:
        for name,digest in manifest['source_sha256'].items():
            member=archive.extractfile(name)
            require(member is not None and hashlib.sha256(member.read()).hexdigest()==digest,'changed archived source '+name)
    before=json.loads((root/'baseline-results.json').read_text())
    unpinned=json.loads((root/'unpinned-results.json').read_text())
    after=json.loads((root/'after-results.json').read_text())
    require(len(before)==12 and {(x['gpu'],x['threads'],x['iteration']) for x in before}=={(g,t,i) for g in ['amd','nvidia'] for t in [1,8] for i in range(3)},'baseline incomplete')
    require(len(unpinned)==6 and {(x['threads'],x['iteration']) for x in unpinned}=={(t,i) for t in [1,8] for i in range(3)},'unpinned baseline incomplete')
    require(len(after)==18 and {(x['gpu'],x['threads'],x['iteration']) for x in after}=={(g,t,i) for g in ['amd','nvidia','amd-gl'] for t in [1,8] for i in range(3)},'final matrix incomplete')
    for row in before+unpinned+after:
        log=(root/row['log']).read_text()
        require(row['returncode']==0 and 'test result: ok. 43 passed; 0 failed; 0 ignored;' in log and 'Skipping ' not in log and 'panicked' not in log,'failed/skipped library process')
    for row in after:
        log=(root/row['log']).read_text(); backend='Gl' if row['gpu']=='amd-gl' else 'Vulkan'
        vendor,device=(4318,9568) if row['gpu']=='nvidia' else (4098,0) if row['gpu']=='amd-gl' else (4098,5688)
        require(f'vendor: {vendor}, device: {device},' in log and f'backend: {backend}' in log,'depth API/GPU mismatch')
        shadow=re.search(r'Coin captured shadow caster adapter: (.+?) \((Gl|Vulkan)\), driver',log)
        require(shadow is not None and shadow[2]==backend and ('NVIDIA GeForce RTX 3060' in shadow[1] if vendor==4318 else 'AMD Radeon Graphics' in shadow[1]),'shadow API/GPU mismatch')
    old_gl=(root/'shadow-requested-gl-before.log').read_text()
    require('AMD Radeon Graphics (RADV RENOIR) (Vulkan)' in old_gl and 'test result: ok. 1 passed; 0 failed;' in old_gl,'pre-fix API mismatch absent')
    require('wgpu::Instance::default()' in (root/'source/shadow_tests-before.rs').read_text(),'pre-fix source absent')
    rows=json.loads((root/'lifecycle-results.json').read_text())
    profiles=['amd-vulkan','nvidia-vulkan','amd-opengl','mixed-vulkan','mixed-all']
    expected={(p,'direct-lifecycle') for p in profiles}|{(p,k) for p in profiles[:3] for k in ['public','public-stress']}
    require(len(rows)==11 and {(x['profile'],x['kind']) for x in rows}==expected,'lifecycle matrix incomplete')
    for row in rows:
        log=(root/row['log']).read_text()
        require(row['returncode']==0 and row['api_verified'] and row['physical_gpu_verified'],'unverified lifecycle process')
        wanted={(4098,5688)} if row['profile']=='amd-vulkan' else {(4318,9568)} if row['profile']=='nvidia-vulkan' else {(4098,0)} if row['profile']=='amd-opengl' else {(4098,5688),(4318,9568)}
        if row['kind']=='direct-lifecycle':
            receipts=re.findall(r'lifecycle_adapter worker=(\d+) round=(\d+) info=AdapterInfo \{ name: "([^"]+)", vendor: (\d+), device: (\d+),.*?backend: (\w+) \}',log)
            require(len(receipts)==32 and 'test result: ok. 2 passed; 0 failed; 0 ignored;' in log,'incomplete direct probe')
            require(all(f'lifecycle_complete explicit_unmap={mode} workers=2 rounds=8 devices=16' in log for mode in ['true','false']),'missing drop variant')
            require({(int(x[3]),int(x[4])) for x in receipts}==wanted,'direct GPU identities changed')
            if row['profile']!='mixed-all':
                require(all(x[5]==('Gl' if row['profile']=='amd-opengl' else 'Vulkan') for x in receipts),'direct API mismatch')
        else:
            require('CoinWgpuMultiDeviceTest passed' in log and 'FAIL:' not in log,'public API failed')
            adapter=re.search(r'^adapter=(.+) renderer=(\d+) vendor=(\d+) device=(\d+)',log,re.M)
            require(adapter is not None and (int(adapter[3]),int(adapter[4])) in wanted and int(adapter[2])==(2 if row['profile']=='amd-opengl' else 1),'public API/GPU mismatch')
    for name,threads,iterations in [('loader-serial.log',1,50),('loader-parallel.log',2,100)]:
        data=json.loads((root/name).read_text())
        require(data['threads']==threads and data['iterations_per_thread']==iterations and data['finished_threads']==threads and not data['failures'],'loader probe failed')
    overlap=json.loads((root/'overlap-results.json').read_text())
    require(len(overlap)==5 and {x['profile'] for x in overlap}==set(profiles),'independent overlap coverage incomplete')
    failures=[x for x in overlap if x['returncode']!=0]
    require(len(failures)==1 and failures[0]['profile']=='nvidia-vulkan' and failures[0]['returncode']==-11,'historical independent-instance crash missing')
    for row in overlap:
        log=(root/row['log']).read_text()
        if row['profile']!='nvidia-vulkan':
            require(row['passed'] and log.count('lifecycle_render_verified ')==16,'independent control failed')
    crash=(root/'nvidia-overlap-gdb.log').read_text()
    historical=(root/'historical/rust-parallel-diagnostic.txt').read_text()
    for stack in [crash,historical]:
        require('0x0000000000000000' in stack and 'vkEnumerateInstanceExtensionProperties' in stack and 'ash::prelude::read_into_uninitialized_vector' in stack,'historical stack mismatch')
    require('wgpu_core::instance::Instance::new' in crash and 'received signal SIGSEGV' in crash,'GDB crash not captured')
    shared_results=json.loads((root/'shared-results.json').read_text())
    shared=[row for row in shared_results if row['kind']=='shared']
    require(len(shared)==9,'shared-instance controls incomplete')
    wanted_shared={('amd-vulkan',0),('mixed-vulkan',0)}|{('nvidia-vulkan',i) for i in range(5)}
    for row in shared:
        log=(root/row['log']).read_text()
        if (row['profile'],row['rep']) in wanted_shared:
            require(row['passed'] and row['returncode']==0 and log.count('lifecycle_render_verified ')==16 and 'test result: ok. 1 passed; 0 failed; 0 ignored;' in log,'Vulkan shared control failed')
        elif row['profile']=='amd-opengl':
            require(row['returncode']==-6 and 'BadAccess' in log,'GL shared-context control missing')
        else:
            require(row['profile']=='mixed-all' and row['returncode']==-11,'mixed API failure missing')
    require({(x['profile'],x['rep']) for x in shared}==wanted_shared|{('amd-opengl',0),('mixed-all',0)},'shared profiles mismatch')
    library=json.loads((root/'fixture-final-results.json').read_text())
    require(len(library)==18 and {(x['profile'],x['threads'],x['rep']) for x in library}=={(p,t,i) for p in profiles[:3] for t in [1,8] for i in range(3)},'final library matrix incomplete')
    for row in library:
        log=(root/row['log']).read_text()
        require(row['passed'] and row['returncode']==0 and 'test result: ok. 43 passed; 0 failed; 0 ignored;' in log,'final library process failed')
        expected_api='Gl' if row['profile']=='amd-opengl' else 'Vulkan'
        require(f'backend: {expected_api}' in log and f'({expected_api}), driver' in log,'final library API mismatch')
    public=json.loads((root/'public-final-results.json').read_text())
    require(len(public)==6 and {(x['profile'],x['kind']) for x in public}=={(p,k) for p in profiles[:3] for k in ['normal','stress']},'public matrix incomplete')
    for row in public:
        log=(root/row['log']).read_text()
        require(row['passed'] and row['returncode']==0 and 'CoinWgpuMultiDeviceTest passed' in log,'public final process failed')
    with tarfile.open(root/manifest['source_snapshot'],'r:gz') as archive:
        source=archive.extractfile('testsuite/coinwgpu/CoinWgpuMultiDeviceTest.cpp').read().decode()
        runtime=archive.extractfile('src/rendering/coinwgpu/rust_bridge/src/lib.rs').read().decode()
        require('concurrentSubmissions() && concurrentDeviceLifetimes()' in source and 'std::thread red(deviceWorker' in source,'public lifecycle control omitted')
        require('instance: runtime.instance.clone()' in runtime and 'make_extra_device(runtime, id, id)' in runtime,'production instance sharing absent')
        require('if backends == wgpu::Backends::GL { create() }' in runtime,'GL fixture qualification absent')
    result={'baseline_library_processes_passed':18,'final_library_processes_passed':18,'library_tests_per_process':43,'independent_wgpu_nvidia_vulkan_sigsegv_reproduced':True,'gdb_stack_matches_historical':True,'vulkan_shared_instance_controls_passed':7,'direct_gl_shared_instance_bad_access_preserved':True,'public_final_processes_passed':6,'public_physical_api_cells':3,'loader_enumeration_calls':250,'shadow_backend_selection_fixed':True,'B06_coinrender_linux_contract_closed':True,'direct_wgpu_independent_instance_crash_open':True,'source_and_evidence_hashes_verified':True}
    (root/'verification.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))


if __name__=='__main__':
    main()
