#!/usr/bin/env python3
"""Interleaved literal/reserved object-update A/B with a frozen CoinGL control.

Uses the animation protocol unchanged: measurements exclude image capture and
tracing; RGB verification is a separate mode. Each variant runs in a new process.
The additional environment keys are included in the parent runner's manifest.
"""
import run_animation_benchmark as animation


DEFAULT_VARIANTS = 'coingl,bgfx-vulkan-literal,bgfx-vulkan-reserve,bgfx-opengl-literal,bgfx-opengl-reserve,wgpu-vulkan-literal,wgpu-vulkan-reserve'
DEFAULT_CASES = 'static,camera,materials-10,transforms-10,geometry-10,geometry-100'
_original_environment = animation.environment


def environment(build, variant, gpu):
    env = _original_environment(build, variant, gpu)
    if variant.endswith('-literal'):
        env['COIN_RENDER_DISABLE_OBJECT_UPDATE_RESERVE'] = '1'
    elif variant.endswith('-reserve'):
        env['COIN_RENDER_DISABLE_OBJECT_UPDATE_RESERVE'] = '0'
    # Force the same physical NVIDIA EGL/GLX and Vulkan device for all paths.
    # The caller selects a session that supports these native contexts.
    if gpu == 'nvidia':
        env['__EGL_VENDOR_LIBRARY_FILENAMES'] = '/usr/share/glvnd/egl_vendor.d/10_nvidia.json'
        env['VK_DRIVER_FILES'] = '/usr/share/vulkan/icd.d/nvidia_icd.json'
    return env


def configure_variants():
    for base in ('bgfx-vulkan', 'bgfx-opengl', 'wgpu-vulkan'):
        for choice in ('literal', 'reserve'):
            animation.VARIANTS[base + '-' + choice] = animation.VARIANTS[base]


def main():
    import sys
    configure_variants()
    animation.environment = environment
    if '--variants' not in sys.argv:
        sys.argv.extend(['--variants', DEFAULT_VARIANTS])
    if '--cases' not in sys.argv:
        sys.argv.extend(['--cases', DEFAULT_CASES])
    animation.main()


if __name__ == '__main__':
    main()
