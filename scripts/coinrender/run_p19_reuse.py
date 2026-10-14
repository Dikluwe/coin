#!/usr/bin/env python3
"""Run a paired P19 reuse/readback campaign on the pinned physical adapter."""
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import statistics
import subprocess

import run_p17_campaign as p17


def phase(log, label):
    prefix = 'COIN_RENDER_PHASE ' + label + ' '
    return [p17.fields(line[len(prefix):]) for line in log.splitlines()
            if line.startswith(prefix)]


def median(rows, key):
    values = [float(row[key]) for row in rows if key in row and row[key] not in
              ('unavailable', 'not_requested')]
    return round(statistics.median(values), 6) if values else None


def peak(rows, key):
    values = [int(row[key]) for row in rows if key in row and row[key] not in
              ('unavailable', '-1')]
    return max(values) if values else None


def specs():
    # Key, target, backend, scene mutation, feature switches, extra arguments.
    return [
        ('bgfx_static_1', 'offscreen', 'bgfx-vulkan', 'static', {}, []),
        ('bgfx_static_2', 'offscreen', 'bgfx-vulkan', 'static', {}, ['--async-depth', '2']),
        ('bgfx_static_3', 'offscreen', 'bgfx-vulkan', 'static', {}, ['--async-depth', '3']),
        ('bgfx_camera_patch', 'offscreen', 'bgfx-vulkan', 'camera', {}, []),
        ('bgfx_camera_full', 'offscreen', 'bgfx-vulkan', 'camera', {'COIN_BGFX_DISABLE_CAMERA_PATCH': '1'}, []),
        ('bgfx_material_patch', 'offscreen', 'bgfx-vulkan', 'material', {}, []),
        ('bgfx_grouped', 'offscreen', 'bgfx-vulkan', 'static', {}, []),
        ('bgfx_ungrouped', 'offscreen', 'bgfx-vulkan', 'static', {'COIN_BGFX_DISABLE_DRAW_GROUPING': '1'}, []),
        ('wgpu_static', 'offscreen', 'wgpu-vulkan', 'static', {}, []),
        ('wgpu_attachments', 'offscreen', 'wgpu-vulkan', 'static', {'COIN_WGPU_ATTACHMENT_CACHE': '1'}, []),
        ('wgpu_camera_full', 'offscreen', 'wgpu-vulkan', 'camera', {}, []),
        ('wgpu_camera_bindings', 'offscreen', 'wgpu-vulkan', 'camera', {'COIN_WGPU_CAMERA_BINDINGS': '1'}, []),
        ('wgpu_material', 'offscreen', 'wgpu-vulkan', 'material', {}, []),
        ('wgpu_async_2', 'offscreen', 'wgpu-vulkan', 'static', {}, ['--async-depth', '2']),
        ('bgfx_window', 'window', 'bgfx-vulkan', 'static', {}, []),
        ('bgfx_window_material', 'window', 'bgfx-vulkan', 'material', {}, []),
        ('wgpu_window', 'window', 'wgpu-vulkan', 'static', {}, []),
        ('wgpu_window_material', 'window', 'wgpu-vulkan', 'material', {}, []),
    ]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-dir', required=True, type=Path)
    parser.add_argument('--bgfx-build', required=True, type=Path)
    parser.add_argument('--wgpu-build', required=True, type=Path)
    parser.add_argument('--scene', required=True, type=Path)
    parser.add_argument('--size', type=int, default=512)
    parser.add_argument('--warmup', type=int, default=10)
    parser.add_argument('--frames', type=int, default=40)
    parser.add_argument('--repetitions', type=int, default=2)
    args = parser.parse_args()
    if not os.getenv('COIN_TEST_ISOLATED_X11'):
        parser.error('run in testsuite/qt-quarter/run_isolated.py --server xwayland')
    if os.getenv('VK_DRIVER_FILES') != '/usr/share/vulkan/icd.d/radeon_icd.json':
        parser.error('pin the Radeon ICD with VK_DRIVER_FILES')
    glx = subprocess.run(['glxinfo', '-B'], text=True, capture_output=True, check=True).stdout
    if 'Accelerated: yes' not in glx or 'radeonsi' not in glx.lower():
        parser.error('expected accelerated Radeon GLX')
    args.output_dir.mkdir(parents=True, exist_ok=True)
    source = args.scene.resolve().read_text()
    if not source.startswith('#Inventor V2.1 ascii\nSeparator {\n'):
        raise ValueError('expected the P17 opaque fixture')
    groupable = args.output_dir / 'opaque-groupable.iv'
    groupable.write_text(source.replace('Separator {\n',
        'Separator {\nDepthBuffer { test TRUE write TRUE function LESS range 0 1 }\n', 1))
    builds = {'bgfx': args.bgfx_build.resolve(), 'wgpu': args.wgpu_build.resolve()}
    for name, path in builds.items():
        if 'CMAKE_BUILD_TYPE:STRING=Release' not in (path / 'CMakeCache.txt').read_text():
            parser.error(name + ' build must be Release')
    def digest(path):
        return hashlib.sha256(path.read_bytes()).hexdigest()
    manifest = {
        'git_head': subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip(),
        'glxinfo_B': glx,
        'vk_driver_files': os.environ['VK_DRIVER_FILES'],
        'scene_sha256': digest(args.scene), 'groupable_scene_sha256': digest(groupable),
        'size': args.size, 'warmup': args.warmup, 'frames': args.frames,
        'repetitions': args.repetitions,
        'builds': {name: {'path': str(path),
            'offscreen_sha256': digest(path / 'bin/coin_render_gl_benchmark'),
            'window_sha256': digest(path / 'bin/coin_render_window_benchmark')}
            for name, path in builds.items()}}
    (args.output_dir / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    records = []
    for key, target, backend, update, switches, extra in specs():
        build = args.bgfx_build if backend.startswith('bgfx-') else args.wgpu_build
        library = str((build / 'lib').resolve())
        executable = build / 'bin' / ('coin_render_window_benchmark' if target == 'window'
                                       else 'coin_render_gl_benchmark')
        command = [str(executable.resolve())]
        if target == 'window':
            command += ['--backend', backend, '--width', str(args.size), '--height', str(args.size)]
        else:
            command += ['--backend', 'bgfx' if backend.startswith('bgfx-') else 'wgpu',
                        '--size', str(args.size)]
        scene = groupable if key in ('bgfx_grouped', 'bgfx_ungrouped') else args.scene.resolve()
        command += ['--scene', str(scene), '--warmup', str(args.warmup),
                    '--frames', str(args.frames)]
        if update != 'static':
            command += ['--dynamic' if update == 'camera' else '--material-dynamic']
        command += extra
        for rep in range(args.repetitions + 1):
            traced = rep == 0
            env = os.environ.copy()
            env.update(switches)
            env['LD_LIBRARY_PATH'] = library + ':' + env.get('LD_LIBRARY_PATH', '')
            env['COIN_RENDER_TRACE_PHASES'] = '1' if traced else '0'
            env['COIN_BGFX_RENDERER'] = 'vulkan'
            result = subprocess.run(command, env=env, text=True, capture_output=True,
                                    timeout=180, check=False)
            stem = key + ('_trace' if traced else f'_rep{rep}')
            (args.output_dir / (stem + '.out')).write_text(result.stdout)
            (args.output_dir / (stem + '.err')).write_text(result.stderr)
            if result.returncode:
                raise RuntimeError(f'{stem} exited {result.returncode}: {result.stderr[-1000:]}')
            row = {'case': key, 'target': target, 'backend': backend, 'update': update,
                   'rep': 'trace' if traced else str(rep), 'size': args.size,
                   'warmup': args.warmup, 'frames': args.frames}
            if target == 'window':
                reports = [p17.fields(line[len('window_benchmark '):])
                           for line in result.stdout.splitlines()
                           if line.startswith('window_benchmark ')]
                if len(reports) != 1 or reports[0].get('readback') != 'none':
                    raise RuntimeError(f'{stem}: missing no-readback report')
                row.update(cpu_median_ms=float(reports[0]['cpu_frame_median_ms']),
                           throughput_fps=float(reports[0]['throughput_fps']),
                           adapter=reports[0]['adapter'],
                           vendor_id=reports[0]['vendor_id'], device_id=reports[0]['device_id'])
            else:
                hashes = [line.split('=', 1)[1] for line in result.stdout.splitlines()
                          if line.startswith('rgba_fnv64=')]
                if len(hashes) != 1:
                    raise RuntimeError(f'{stem}: missing RGBA checksum')
                row['rgba_fnv64'] = hashes[0]
                row['adapter'] = next(line for line in result.stdout.splitlines()
                                      if line.startswith('adapter='))
                throughput = [p17.fields(line) for line in result.stdout.splitlines()
                              if '_throughput ' in line]
                row['throughput_fps'] = float(throughput[0]['fps'])
                timings = [p17.fields(line) for line in result.stdout.splitlines()
                           if ' median_ms=' in line]
                row['cpu_median_ms'] = float(timings[0]['median_ms'])
                if key == 'wgpu_async_2':
                    row['async_latency_median_ms'] = float(timings[1]['median_ms'])
            if traced:
                label = ('bgfx' if backend.startswith('bgfx-') else
                         'rust_surface_cpu' if target == 'window' else 'rust')
                phases = phase(result.stderr, label)
                if len(phases) != args.warmup + args.frames and key != 'wgpu_async_2':
                    raise RuntimeError(f'{stem}: trace count {len(phases)}')
                phases = phases[args.warmup:]
                if backend.startswith('bgfx-'):
                    for field in ('camera_patch', 'material_patch', 'opaque_reordered',
                                  'readback_pipeline_depth', 'readback_latency_frames',
                                  'read_wait_ms', 'readback_pipeline_bytes',
                                  'readback_gpu_staging_bytes', 'readback_cpu_staging_bytes',
                                  'readback_published_bytes', 'logical_material_changes',
                                  'resource_cache_hit', 'geometry_buffer_reused',
                                  'vertex_buffer_capacity', 'index_buffer_capacity'):
                        row[field] = median(phases, field) if field.endswith('_ms') else peak(phases, field)
                    row['readback_latency_min_frames'] = min(
                        int(item['readback_latency_frames']) for item in phases)
                    if target == 'window' and any(row[field] != 0 for field in
                        ('readback_gpu_staging_bytes', 'readback_cpu_staging_bytes',
                         'readback_pipeline_bytes', 'readback_published_bytes')):
                        raise RuntimeError(f'{stem}: window allocated readback staging')
                else:
                    row['attachments_reused'] = peak(phases, 'attachments_reused')
                    row['camera_bindings_created'] = peak(phases, 'camera_bindings_created')
                    row['camera_bindings_reused'] = peak(phases, 'camera_bindings_reused')
                    resources = phase(result.stderr, 'rust_resources')[args.warmup:]
                    row['attachment_nominal_bytes'] = peak(resources, 'attachment_nominal_bytes')
                    row['geometry_active_bytes'] = peak(resources, 'geometry_active_bytes')
                    row['vertex_buffers'] = peak(resources, 'vertex_buffers')
                    row['index_buffers'] = peak(resources, 'index_buffers')
                    row['staging_frame_color_bytes'] = peak(resources, 'staging_frame_color_bytes')
                    row['staging_frame_depth_bytes'] = peak(resources, 'staging_frame_depth_bytes')
                    row['staging_pool_free_bytes'] = peak(resources, 'staging_pool_free_bytes')
                    if target == 'window' and any(row[field] != 0 for field in
                        ('staging_frame_color_bytes', 'staging_frame_depth_bytes',
                         'staging_pool_free_bytes')):
                        raise RuntimeError(f'{stem}: window allocated readback staging')
            records.append(row)
            print(stem, round(row['throughput_fps'], 2), row.get('rgba_fnv64', ''), flush=True)
    traces = {row['case']: row for row in records if row['rep'] == 'trace'}
    if traces['bgfx_camera_patch']['camera_patch'] != 1 or \
       traces['bgfx_camera_full']['camera_patch'] != 0 or \
       traces['bgfx_material_patch']['material_patch'] != 1 or \
       traces['wgpu_attachments']['attachments_reused'] != 1 or \
       traces['wgpu_camera_bindings']['camera_bindings_reused'] == 0:
        raise RuntimeError('expected reuse path was not exercised')
    if traces['bgfx_grouped']['opaque_reordered'] != 1 or \
       traces['bgfx_ungrouped']['opaque_reordered'] != 0 or \
       traces['bgfx_grouped']['logical_material_changes'] >= traces['bgfx_ungrouped']['logical_material_changes']:
        raise RuntimeError('opaque grouping did not reduce material transitions')
    hashes = {row['case']: row['rgba_fnv64'] for row in records
              if row['target'] == 'offscreen' and row['rep'] == 'trace'}
    for group in (('bgfx_static_1', 'bgfx_static_2', 'bgfx_static_3', 'bgfx_grouped',
                   'bgfx_ungrouped'), ('wgpu_static', 'wgpu_attachments', 'wgpu_async_2')):
        if len({hashes[key] for key in group}) != 1:
            raise RuntimeError(f'RGBA mismatch in {group}')
    if hashes['bgfx_static_1'] != hashes['wgpu_static'] or \
       hashes['bgfx_material_patch'] != hashes['wgpu_material']:
        raise RuntimeError('BGFX and wgpu RGBA mismatch')
    if hashes['bgfx_camera_patch'] != hashes['bgfx_camera_full'] or \
       hashes['wgpu_camera_full'] != hashes['wgpu_camera_bindings']:
        raise RuntimeError('camera A/B RGBA mismatch')
    (args.output_dir / 'results.json').write_text(json.dumps(records, indent=2) + '\n')
    fields = list(dict.fromkeys(key for row in records for key in row))
    with (args.output_dir / 'results.csv').open('w', newline='') as stream:
        writer = csv.DictWriter(stream, fieldnames=fields, lineterminator='\n')
        writer.writeheader()
        writer.writerows(records)

if __name__ == '__main__':
    main()
