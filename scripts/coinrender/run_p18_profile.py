#!/usr/bin/env python3
"""Profile P18 phases/resources on the physical P17 fixtures; keep raw traces."""
import argparse
import json
import math
import os
from pathlib import Path
import statistics
import subprocess
import sys

import run_p17_campaign as p17


def records(output, label):
    return [p17.fields(line[len('COIN_RENDER_PHASE '+label+' '):])
            for line in output.splitlines()
            if line.startswith('COIN_RENDER_PHASE '+label+' ')]


def metric(rows, key):
    values = [float(row[key]) for row in rows if row.get(key) not in (None, 'unavailable', 'not_requested')]
    if not all(math.isfinite(value) and value >= 0 for value in values):
        raise ValueError('invalid telemetry '+key)
    return round(statistics.median(values), 6) if values else None


def peak(rows, key):
    values = [int(row[key]) for row in rows if row.get(key) not in (None, 'unavailable', '-1')]
    if not all(value >= 0 for value in values):
        raise ValueError('invalid resource '+key)
    return max(values) if values else None


def summarize(output, backend, target, warmup, frames):
    n = warmup+frames
    action = records(output, 'action')
    if len(action) != n:
        raise ValueError(f'action traces {len(action)} != {n}')
    actions = action[warmup:]
    result = {'action_backend_median_ms': metric(actions,'backend_ms'),
              'action_traversal_median_ms': metric(actions,'traversal_ms'),
              'action_plan_median_ms': metric(actions,'frame_plan_ms')}
    if backend.startswith('bgfx-'):
        trace = records(output,'bgfx')
        if len(trace) != n: raise ValueError(f'BGFX traces {len(trace)} != {n}')
        rows=trace[warmup:]
        if any(row.get('gpu_resource_stats')!='1' for row in rows):
            raise ValueError('BGFX resource stats unavailable')
        gpu=[row for row in rows if row.get('gpu_frame_ms') not in ('unavailable','not_requested',None)]
        if len(gpu)<frames//2:
            raise ValueError('BGFX GPU timestamps mostly unavailable')
        result.update(trace_samples=len(rows),gpu_samples=len(gpu),
            gpu_frame_median_ms=metric(rows,'gpu_frame_ms'),
            gpu_opaque_median_ms=metric(rows,'gpu_opaque_ms'),
            gpu_transparent_median_ms=metric(rows,'gpu_transparent_ms'),
            gpu_composite_median_ms=metric(rows,'gpu_composite_ms'),
            gpu_blit_readback_median_ms=metric(rows,'gpu_blit_readback_ms'),
            bgfx_gpu_wait_median_ms=metric(rows,'gpu_wait_ms'),
            read_wait_median_ms=metric(rows,'read_wait_ms'),
            lower_median_ms=metric(rows,'lower_ms'),
            upload_median_ms=metric(rows,'upload_ms'),
            encode_median_ms=metric(rows,'encode_ms'),
            gpu_draw_submits_peak=peak(rows,'gpu_draw_submits'),
            logical_pipeline_changes_peak=peak(rows,'logical_pipeline_changes'),
            logical_material_changes_peak=peak(rows,'logical_material_changes'),
            logical_texture_changes_peak=peak(rows,'logical_texture_changes'),
            logical_lighting_changes_peak=peak(rows,'logical_lighting_changes'),
            gpu_memory_used_peak_bytes=peak(rows,'gpu_memory_used_bytes'),
            texture_memory_used_peak_bytes=peak(rows,'texture_memory_used_bytes'),
            render_target_memory_used_peak_bytes=peak(rows,'render_target_memory_used_bytes'),
            vertex_buffers_peak=peak(rows,'gpu_vertex_buffers'),
            index_buffers_peak=peak(rows,'gpu_index_buffers'),
            textures_peak=peak(rows,'gpu_textures'),
            framebuffers_peak=peak(rows,'gpu_framebuffers'),
            staging_gpu_peak_bytes=peak(rows,'readback_gpu_staging_bytes'),
            staging_cpu_peak_bytes=peak(rows,'readback_cpu_staging_bytes'))
        if target=='window' and (result['staging_gpu_peak_bytes']!=0 or result['staging_cpu_peak_bytes']!=0):
            raise ValueError('window unexpectedly allocated readback staging')
    else:
        resource=records(output,'rust_resources')
        if len(resource)!=n: raise ValueError(f'wgpu resource traces {len(resource)} != {n}')
        rows=resource[warmup:]
        if any(row.get('target')!=target or row.get('gpu_memory_used_bytes')!='unavailable'
               or row.get('framebuffer_count')!='unavailable' for row in rows):
            raise ValueError('wgpu resource domain/status changed')
        result.update(trace_samples=len(rows),gpu_samples=0,
            gpu_memory_used_peak_bytes=None,framebuffers_peak=None,
            geometry_active_peak_bytes=peak(rows,'geometry_active_bytes'),
            geometry_retired_peak_bytes=peak(rows,'geometry_retired_bytes'),
            texture_payload_peak_bytes=peak(rows,'texture_payload_bytes'),
            rtt_color_nominal_peak_bytes=peak(rows,'rtt_color_nominal_bytes'),
            attachment_nominal_peak_bytes=peak(rows,'attachment_nominal_bytes'),
            staging_color_peak_bytes=peak(rows,'staging_frame_color_bytes'),
            staging_depth_peak_bytes=peak(rows,'staging_frame_depth_bytes'),
            staging_pool_free_peak_bytes=peak(rows,'staging_pool_free_bytes'),
            vertex_buffers_peak=peak(rows,'vertex_buffers'),
            index_buffers_peak=peak(rows,'index_buffers'),
            textures_peak=peak(rows,'texture_cache_count'))
        if target=='window':
            cpu=records(output,'rust_surface_cpu')
            if len(cpu)!=n: raise ValueError('wgpu window CPU phase count mismatch')
            cpu=cpu[warmup:]
            for row in cpu:
                total=float(row['total_ms'])
                parts=sum(float(row[key]) for key in ('validation_ms','acquire_ms','encode_ms','submit_present_ms'))
                if abs(total-parts)>0.01 or row.get('gpu_timing')!='unavailable':
                    raise ValueError('wgpu window CPU spans do not reconcile')
            result.update(rust_cpu_total_median_ms=metric(cpu,'total_ms'),
                          rust_cpu_encode_median_ms=metric(cpu,'encode_ms'),
                          rust_cpu_wait_median_ms=None,
                          gpu_frame_median_ms=None,gpu_opaque_median_ms=None,
                          gpu_transparent_median_ms=None,gpu_composite_median_ms=None,
                          gpu_blit_readback_median_ms=None)
            if result['staging_color_peak_bytes']!=0 or result['staging_depth_peak_bytes']!=0:
                raise ValueError('window unexpectedly allocated readback staging')
        else:
            cpu=records(output,'rust_cpu_detail');gpu=records(output,'rust_gpu')
            if len(cpu)!=n or len(gpu)!=n:
                raise ValueError('wgpu offscreen phase count mismatch')
            cpu=cpu[warmup:];gpu=gpu[warmup:]
            phase_keys=('validation_ms','gpu_probe_setup_ms','attachments_ms','draw_encode_ms',
                        'scene_snapshot_ms','staging_prepare_ms','submit_ms','map_request_ms',
                        'gpu_wait_ms','post_wait_checks_ms','gpu_probe_read_ms','map_receive_ms',
                        'color_copy_ms','depth_copy_ms','output_commit_ms','recycle_ms')
            for row in cpu:
                if abs(float(row['total_ms'])-sum(float(row[key]) for key in phase_keys))>0.02:
                    raise ValueError('wgpu offscreen CPU spans do not reconcile')
            good=[row for row in gpu if row.get('status')=='ok']
            if len(good)<frames//2:
                raise ValueError('wgpu GPU timestamps mostly unavailable')
            result.update(gpu_samples=len(good),rust_cpu_total_median_ms=metric(cpu,'total_ms'),
                          rust_cpu_encode_median_ms=metric(cpu,'draw_encode_ms'),
                          rust_cpu_wait_median_ms=metric(cpu,'gpu_wait_ms'),
                          gpu_frame_median_ms=None,
                          gpu_render_median_ms=metric(good,'render_ms'),
                          gpu_copy_median_ms=metric(good,'copy_ms'))
    return result


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-dir',type=Path,required=True)
    parser.add_argument('--bgfx-build',type=Path,required=True)
    parser.add_argument('--wgpu-build',type=Path,required=True)
    parser.add_argument('--scenes-dir',type=Path,required=True)
    parser.add_argument('--warmup',type=int,default=10)
    parser.add_argument('--frames',type=int,default=40)
    args=parser.parse_args()
    if not os.getenv('COIN_TEST_ISOLATED_X11'):
        parser.error('P18 requires an isolated X11 session')
    if args.frames<10 or args.warmup<0:parser.error('invalid sample count')
    glx=p17.capture(['glxinfo','-B'])
    renderer=next((line for line in glx.splitlines() if line.startswith('OpenGL renderer string: ')),'')
    if 'Accelerated: yes' not in glx or 'radeonsi' not in renderer.lower():
        raise RuntimeError('expected accelerated Radeon GLX')
    if os.getenv('VK_DRIVER_FILES')!='/usr/share/vulkan/icd.d/radeon_icd.json':
        raise RuntimeError('pin physical Radeon Vulkan ICD')
    if (os.getenv('__GLX_VENDOR_LIBRARY_NAME')!='mesa' or
            os.getenv('__EGL_VENDOR_LIBRARY_FILENAMES')!=
            '/usr/share/glvnd/egl_vendor.d/50_mesa.json'):
        raise RuntimeError('pin Mesa GLX/EGL vendors')
    args.output_dir.mkdir(parents=True,exist_ok=True)
    (args.output_dir/'glxinfo-B.log').write_text(glx+'\n')
    scenes={name:args.scenes_dir/(name+'.iv') for name in ('opaque-interleaved','transparent-overlap')}
    import hashlib
    manifest=json.loads((args.scenes_dir/'scenes.json').read_text())
    for name,path in scenes.items():
        if hashlib.sha256(path.read_bytes()).hexdigest()!=manifest[name]['sha256']:
            raise RuntimeError('scene hash changed: '+str(path))
    builds={'bgfx':args.bgfx_build.resolve(),'wgpu':args.wgpu_build.resolve()}
    for name,path in builds.items():
        if 'CMAKE_BUILD_TYPE:STRING=Release' not in (path/'CMakeCache.txt').read_text():
            raise RuntimeError(name+' build is not Release')
    cases=[]
    for size in (512,1024):
        for target in ('window','offscreen'):
            for scene in scenes:
                for update in (('static','dynamic') if size==512 else ('static',)):
                    for backend in ('bgfx-opengl','bgfx-vulkan','wgpu-vulkan'):
                        modes=(('object','weighted_oit','sorted_layers') if backend!='wgpu-vulkan' else ('object','sorted_layers')) if scene=='transparent-overlap' and size==512 else ('object',)
                        for mode in modes:cases.append((size,target,scene,update,backend,mode))
    results={'metadata':{'date_utc':p17.capture(['date','-u','+%Y-%m-%dT%H:%M:%SZ']),
                         'git_base':p17.capture(['git','rev-parse','HEAD']),
                         'glxinfo_B':glx,'vulkan_icd':os.getenv('VK_DRIVER_FILES'),
                         'warmup':args.warmup,'frames':args.frames,
                         'scenes':manifest,'builds':{k:str(v) for k,v in builds.items()},
                         'probe_is_intrusive':True},'runs':[],'failures':[]}
    result_path=args.output_dir/'results.json'
    for index,(size,target,scene,update,backend,mode) in enumerate(cases,1):
        key=f'{target}-{size}-{scene}-{update}-{backend}-{mode}'
        build=builds['wgpu' if backend=='wgpu-vulkan' else 'bgfx']
        executable=build/'bin'/('coin_render_window_benchmark' if target=='window' else 'coin_render_gl_benchmark')
        cmd=[str(executable),'--backend',backend if target=='window' else 'wgpu' if backend=='wgpu-vulkan' else 'bgfx',
             '--transparency',mode,'--scene',str(scenes[scene].resolve()),'--warmup',str(args.warmup),'--frames',str(args.frames)]
        cmd+=(['--width',str(size),'--height',str(size)] if target=='window' else ['--size',str(size)])
        if update=='dynamic':cmd.append('--dynamic')
        env=dict(os.environ,LD_LIBRARY_PATH=str(build/'lib'),COIN_RENDER_TRACE_PHASES='1',
                 COIN_WGPU_GPU_TIMESTAMPS='1',COIN_GLX_PIXMAP_DIRECT_RENDERING='1')
        if backend.startswith('bgfx-'):env['COIN_BGFX_RENDERER']=backend.split('-',1)[1]
        else:env.pop('COIN_BGFX_RENDERER',None)
        try:
            process=subprocess.run(cmd,env=env,text=True,stdout=subprocess.PIPE,
                                   stderr=subprocess.STDOUT,timeout=120)
            output=process.stdout
            (args.output_dir/(key+'.log')).write_text(output)
            if process.returncode:raise RuntimeError(f'exit {process.returncode}: {output[-500:]}')
            base=p17.parse_result(output,target,backend,mode,scenes[scene].resolve(),
                                  'static' if update=='static' else 'transform-each-frame' if target=='window' else 'camera-each-frame',
                                  size,args.warmup,args.frames)
            profile=summarize(output,backend,target,args.warmup,args.frames)
            results['runs'].append(dict(key=key,size=size,target=target,scene=scene,update=update,
                                       backend=backend,mode=mode,log=key+'.log',**base,**profile))
            status='PASS'
        except Exception as exc:
            results['failures'].append({'key':key,'error':str(exc)})
            status='FAIL '+str(exc)[:100]
        result_path.write_text(json.dumps(results,indent=2)+'\n')
        print(f'[{index}/{len(cases)}] {key}: {status}',flush=True)
    print('P18 runs',len(results['runs']),'failures',len(results['failures']),flush=True)
    return 1 if results['failures'] or len(results['runs'])!=len(cases) else 0


if __name__=='__main__':sys.exit(main())
