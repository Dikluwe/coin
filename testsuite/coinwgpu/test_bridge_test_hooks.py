#!/usr/bin/env python3
"""Offline, GPU-free integration checks for the private Rust bridge ABI.

Run from any directory. By default archives are built in /dev/shm. Reuse builds:
  python3 testsuite/coinwgpu/test_bridge_test_hooks.py --source-root . \
    --production-archive /dev/shm/prod/debug/libcoin_wgpu_rust_bridge.a \
    --hooks-archive /dev/shm/hooks/debug/libcoin_wgpu_rust_bridge.a
Use --checks to run an explicit subset; omitted checks are never reported as passes.
C++ test feature definitions can be supplied with repeated --test-cppflag options.
"""
import argparse
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import tempfile
import unittest

HOOKS = {
    'coin_wgpu_inject_fault',
    'coin_wgpu_inject_device_fault',
    'coin_wgpu_inject_fault_after_submits',
    'coin_wgpu_inject_async_fault',
    'coin_wgpu_reset_context',
    'coin_wgpu_set_cache_budget',
}
# Stable representative ABI, independent of the candidate's module organization.
PRODUCTION_API = {
    'coin_wgpu_device_create', 'coin_wgpu_device_destroy',
    'coin_wgpu_submit', 'coin_wgpu_submit_async', 'coin_wgpu_submit_texture',
    'coin_wgpu_surface_create', 'coin_wgpu_surface_destroy',
    'coin_wgpu_surface_submit', 'coin_wgpu_surface_resize',
    'coin_wgpu_readback_query', 'coin_wgpu_readback_poll',
    'coin_wgpu_get_cache_stats', 'coin_wgpu_get_performance_stats',
    'coin_wgpu_trim_cache', 'coin_wgpu_query_runtime_capabilities',
}
OPTIONS = None
WORK = None
ARCHIVES = {}


def run(argv, *, data=None, cwd=None, env=None):
    if env is None:
        env = os.environ.copy()
        if WORK is not None:
            env['TMPDIR'] = str(WORK)
    result = subprocess.run([str(a) for a in argv], input=data, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            cwd=cwd, env=env)
    if result.returncode:
        raise AssertionError('Command failed: ' + shlex.join(map(str, argv)) +
                             '\n' + result.stdout[-12000:] + result.stderr[-12000:])
    return result.stdout


def manifest():
    return OPTIONS.source_root / 'src/rendering/coinwgpu/rust_bridge/Cargo.toml'


def cargo_environment():
    env = os.environ.copy()
    env.update(TMPDIR=str(WORK), CARGO_TARGET_DIR=str(WORK / 'cargo'))
    return env


def archive(enabled):
    if enabled in ARCHIVES:
        return ARCHIVES[enabled]
    supplied = OPTIONS.hooks_archive if enabled else OPTIONS.production_archive
    if supplied:
        if not supplied.is_file():
            raise AssertionError('Archive does not exist: ' + str(supplied))
        ARCHIVES[enabled] = supplied
        return supplied
    # Copy immediately: Cargo reuses a staticlib filename between feature sets.
    command = [OPTIONS.cargo, 'build', '--offline', '--locked', '--lib',
               '--manifest-path', manifest()]
    if enabled:
        command += ['--features', 'test-hooks']
    run(command, env=cargo_environment())
    target = WORK / ('hooks.a' if enabled else 'production.a')
    shutil.copyfile(WORK / 'cargo/debug/libcoin_wgpu_rust_bridge.a', target)
    ARCHIVES[enabled] = target
    return target


def exported(path):
    output = run([OPTIONS.nm, '-g', '--defined-only', path])
    return set(re.findall(r'\b(coin_wgpu_[A-Za-z0-9_]+)\s*$', output, re.MULTILINE))


def preprocess(flags):
    header = OPTIONS.source_root / 'src/rendering/coinwgpu/CoinWgpuFfi.h'
    return run([OPTIONS.cxx, '-E', '-P', '-x', 'c++', *flags, '-'],
               data='#include "' + str(header) + '"\n')


class BridgeContract(unittest.TestCase):
    def test_cargo_feature_contract(self):
        package_data = json.loads(run([
            OPTIONS.cargo, 'metadata', '--offline', '--locked', '--no-deps',
            '--format-version', '1', '--manifest-path', manifest()],
            env=cargo_environment()))
        packages = [p for p in package_data['packages']
                    if p['name'] == 'coin_wgpu_rust_bridge']
        self.assertEqual(len(packages), 1)
        features = packages[0]['features']
        self.assertIn('test-hooks', features)
        self.assertEqual(features.get('default'), [],
                         'production Cargo defaults must explicitly be empty')

    def test_production_header_hides_controls(self):
        production = preprocess([])
        for name in HOOKS:
            self.assertNotRegex(production, r'\b' + name + r'\b')
        for name in PRODUCTION_API:
            self.assertRegex(production, r'\b' + name + r'\b')

    def test_test_header_declares_controls(self):
        test_header = preprocess(OPTIONS.test_cppflag)
        for name in HOOKS | PRODUCTION_API:
            self.assertRegex(test_header, r'\b' + name + r'\b')

    def test_archive_symbol_partition(self):
        production = exported(archive(False))
        enabled = exported(archive(True))
        self.assertTrue(PRODUCTION_API <= production,
                        'missing production ABI: ' + repr(PRODUCTION_API - production))
        self.assertFalse(HOOKS & production,
                         'failure controls leaked: ' + repr(HOOKS & production))
        self.assertTrue(HOOKS <= enabled,
                        'test controls missing: ' + repr(HOOKS - enabled))
        # Catch newly added injection aliases, not just the original injection names.
        self.assertFalse({s for s in production if s.startswith('coin_wgpu_inject_')})
        self.assertEqual(enabled - HOOKS, production,
                         'enabling hooks must preserve the rest of the exported ABI')
        if OPTIONS.baseline_archive:
            baseline = exported(OPTIONS.baseline_archive)
            self.assertEqual(production, baseline - HOOKS,
                             'module extraction changed production FFI symbols')

    def test_cpp_production_links_without_controls(self):
        header = OPTIONS.source_root / 'src/rendering/coinwgpu/CoinWgpuFfi.h'
        source = '#include "' + str(header) + '"\n'
        source += 'static_assert(sizeof(CoinWgpuDeviceId) == 8, "device ABI");\n'
        source += 'static_assert(COIN_WGPU_DEVICE_LOST == 5, "status ABI");\n'
        # Volatile references force real relocation/link checks without a GPU call.
        for i, name in enumerate(sorted(PRODUCTION_API)):
            source += f'auto volatile api_{i} = &{name};\n'
        source += 'int main() { return 0; }\n'
        self.link_and_run('production_client', source, archive(False), [])

    def test_cpp_controls_link_and_accept_boundary_inputs(self):
        header = OPTIONS.source_root / 'src/rendering/coinwgpu/CoinWgpuFfi.h'
        source = '#include "' + str(header) + '"\n'
        source += '''int main() {
  coin_wgpu_inject_device_fault(UINT64_MAX, COIN_WGPU_DEVICE_LOST);
  coin_wgpu_inject_device_fault(0, 0);
  coin_wgpu_inject_fault_after_submits(COIN_WGPU_OUT_OF_MEMORY, UINT32_MAX);
  coin_wgpu_inject_fault_after_submits(0, 0);
  coin_wgpu_inject_async_fault(0);
  coin_wgpu_inject_fault(0);
  coin_wgpu_set_cache_budget(UINT64_MAX, UINT64_MAX);
  coin_wgpu_reset_context();
  return 0;
}\n'''
        self.link_and_run('hooks_client', source, archive(True), OPTIONS.test_cppflag)

    def link_and_run(self, name, source, library, flags):
        executable = WORK / name
        run([OPTIONS.cxx, '-std=c++11', '-x', 'c++', *flags, '-', '-x', 'none',
             library, '-o', executable, *OPTIONS.native_lib], data=source)
        # /dev/shm can be mounted noexec. The ELF loader can execute these small
        # probes without relocating build output outside the disposable RAM area.
        if OPTIONS.elf_loader:
            run([OPTIONS.elf_loader, executable])
        else:
            run([executable])

    def test_unit_configuration_compiles_without_feature(self):
        run([OPTIONS.cargo, 'test', '--offline', '--locked', '--lib', '--no-run',
             '--manifest-path', manifest()], env=cargo_environment())

    def test_cmake_selects_feature_only_for_test_builds(self):
        for enabled in (False, True):
            build = WORK / ('cmake-tests' if enabled else 'cmake-production')
            run([OPTIONS.cmake, '-S', OPTIONS.source_root, '-B', build, '-G', 'Ninja',
                 '-DCOIN_BUILD_RENDER=ON', '-DCOIN_RENDER_BACKEND=RUST_BRIDGE',
                 '-DCOIN_BUILD_TESTS=' + ('ON' if enabled else 'OFF'),
                 '-DCMAKE_BUILD_TYPE=Debug', '-DCOIN_BUILD_DOCUMENTATION=OFF',
                 '-DCARGO_EXECUTABLE=' + shutil.which(OPTIONS.cargo),
                 *OPTIONS.cmake_arg], env=cargo_environment())
            commands = run([OPTIONS.ninja, '-C', build, '-t', 'commands',
                            'coin_wgpu_rust_bridge_build'])
            cargo_commands = [line for line in commands.splitlines()
                              if 'Cargo.toml' in line and ' build ' in line]
            self.assertTrue(cargo_commands, 'no generated Cargo build command found')
            for command in cargo_commands:
                self.assertEqual('test-hooks' in command, enabled, command)
                self.assertNotIn('--all-features', command)


def main():
    global OPTIONS, WORK
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root', type=Path,
                        default=(Path(__file__).resolve().parents[2]
                                 if len(Path(__file__).resolve().parents) > 2 else Path.cwd()))
    parser.add_argument('--production-archive', type=Path)
    parser.add_argument('--hooks-archive', type=Path)
    parser.add_argument('--baseline-archive', type=Path)
    parser.add_argument('--test-cppflag', action='append', default=None)
    parser.add_argument('--native-lib', action='append', default=None)
    parser.add_argument('--elf-loader', type=Path)
    for tool in ('cargo', 'nm', 'cxx', 'cmake', 'ninja'):
        parser.add_argument('--' + tool, default='c++' if tool == 'cxx' else tool)
    parser.add_argument('--cmake-arg', action='append', default=[])
    parser.add_argument('--checks', nargs='+',
                        choices=['metadata', 'headers', 'symbols', 'link', 'unit', 'cmake'],
                        default=['metadata', 'headers', 'symbols', 'link', 'unit', 'cmake'])
    OPTIONS = parser.parse_args()
    OPTIONS.source_root = OPTIONS.source_root.resolve()
    OPTIONS.test_cppflag = OPTIONS.test_cppflag or ['-DCOIN_WGPU_TEST_HOOKS=1']
    OPTIONS.native_lib = OPTIONS.native_lib or ['-ldl', '-lpthread', '-lm']
    names = {
        'metadata': ['test_cargo_feature_contract'],
        'headers': ['test_production_header_hides_controls', 'test_test_header_declares_controls'],
        'symbols': ['test_archive_symbol_partition'],
        'link': ['test_cpp_production_links_without_controls',
                 'test_cpp_controls_link_and_accept_boundary_inputs'],
        'unit': ['test_unit_configuration_compiles_without_feature'],
        'cmake': ['test_cmake_selects_feature_only_for_test_builds'],
    }
    # All disposable binaries, Cargo outputs and generated CMake files stay in RAM.
    with tempfile.TemporaryDirectory(prefix='coin-bridge-contract-', dir='/dev/shm') as tmp:
        WORK = Path(tmp)
        suite = unittest.TestSuite(BridgeContract(name) for group in OPTIONS.checks
                                   for name in names[group])
        result = unittest.TextTestRunner(verbosity=2).run(suite)
    return 0 if result.wasSuccessful() and not result.skipped else 1


if __name__ == '__main__':
    raise SystemExit(main())
