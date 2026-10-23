#!/usr/bin/env python3
"""Run against built, HTTPS-served artifacts: python test_browser.py --url URL.

Requires Python playwright and a Chromium capable of actual WebGPU. Set
COIN_WEBGPU_BROWSER_ARGS to a JSON array of machine-specific Chromium flags.
No software GPU flags are added by this runner. --channel can select Chrome.
--artifacts-dir additionally checks the distributable for leaked secrets.
"""
import argparse
import json
import os
from pathlib import Path
import struct
import unittest
import urllib.error
import urllib.parse
import urllib.request
import ssl
import zlib

OPTIONS = None
PROBE = Path(__file__).with_name('browser_probe.js').read_text()

# Instrument native GPU objects without replacing the rendering implementation.
GPU_OBSERVER = r"""
(() => {
  const record = window.__gpuObserved = {submits: 0, completed: 0, errors: [],
    adapters: [], devices: 0, pipelineFailures: 0, contexts: [], fallbackAdapter: false,
    adapterInfo: [], injectionApplied: false, completionRequests: 0,
    nativeCompletions: 0, completionReleased: false};
  const mode = MODE;
  const getContext = HTMLCanvasElement.prototype.getContext;
  HTMLCanvasElement.prototype.getContext = function (type, ...args) {
    record.contexts.push(type);
    return getContext.call(this, type, ...args);
  };
  if (mode === 'insecure') {
    Object.defineProperty(window, 'isSecureContext', {get: () => false});
    return;
  }
  if (mode === 'missing') {
    Object.defineProperty(navigator, 'gpu', {get: () => undefined});
    return;
  }
  if (!navigator.gpu) return;
  const requestAdapter = navigator.gpu.requestAdapter.bind(navigator.gpu);
  navigator.gpu.requestAdapter = async options => {
    record.adapters.push(options || {});
    if (mode === 'adapter-null') return null;
    if (mode === 'adapter-reject') throw new Error('TEST adapter denied');
    const adapter = await requestAdapter(options);
    if (!adapter) return adapter;
    const hardware = {vendor: 'TEST hardware vendor', architecture: 'TEST hardware',
      device: 'TEST hardware device', description: 'TEST hardware GPU', isFallbackAdapter: false};
    let injectedInfo;
    let injectedLegacy;
    if (mode === 'fallback-legacy') {
      injectedInfo = hardware;
      injectedLegacy = true;
    } else if (mode === 'fallback-info') {
      injectedInfo = {...hardware, isFallbackAdapter: true};
      injectedLegacy = false;
    } else if (mode.startsWith('software-')) {
      injectedInfo = {...hardware, description: mode.slice('software-'.length)};
      injectedLegacy = false;
    } else if (mode === 'adapter-unverifiable') {
      injectedInfo = {vendor: '', architecture: '', device: '', description: ''};
      injectedLegacy = undefined;
    }
    if (injectedInfo) {
      Object.defineProperty(adapter, 'info', {get: () => injectedInfo});
      Object.defineProperty(adapter, 'isFallbackAdapter', {get: () => injectedLegacy});
      // Cover callers using the earlier asynchronous information API as well.
      adapter.requestAdapterInfo = async () => injectedInfo;
      record.injectionApplied = true;
    }
    const info = adapter.info || (adapter.requestAdapterInfo ? await adapter.requestAdapterInfo() : {});
    record.adapterInfo.push({vendor: info.vendor, architecture: info.architecture,
      device: info.device, description: info.description,
      isFallbackAdapter: info.isFallbackAdapter});
    record.fallbackAdapter ||= adapter.isFallbackAdapter === true || info.isFallbackAdapter === true;
    const requestDevice = adapter.requestDevice.bind(adapter);
    adapter.requestDevice = async descriptor => {
      if (mode === 'device-reject') throw new Error('TEST device denied');
      const device = await requestDevice(descriptor);
      record.devices++;
      device.addEventListener('uncapturederror', event => {
        record.errors.push(String(event.error.message));
      });
      if (mode === 'pipeline') {
        device.createRenderPipeline = () => {
          record.pipelineFailures++;
          throw new Error('TEST pipeline compilation failed');
        };
        device.createRenderPipelineAsync = async () => {
          record.pipelineFailures++;
          throw new Error('TEST pipeline compilation failed');
        };
      }
      const submit = device.queue.submit.bind(device.queue);
      const done = device.queue.onSubmittedWorkDone.bind(device.queue);
      if (mode === 'completion-gate') {
        const releases = [];
        window.__gpuReleaseCompletion = () => {
          record.completionReleased = true;
          for (const release of releases.splice(0)) release();
        };
        device.queue.onSubmittedWorkDone = () => {
          record.completionRequests++;
          // Native completion remains mandatory even after the test opens the gate.
          const native = done();
          const gate = record.completionReleased ? Promise.resolve() :
            new Promise(resolve => releases.push(resolve));
          return native.then(() => {
            record.nativeCompletions++;
            return gate;
          });
        };
      }
      device.queue.submit = commands => {
        if (mode === 'submit-failure') throw new Error('TEST GPU submission failed');
        submit(commands);
        record.submits++;
        done().then(() => record.completed++, error => record.errors.push(String(error)));
        if (mode === 'loss') setTimeout(() => device.destroy(), 0);
      };
      return device;
    };
    return adapter;
  };
})();
"""


def png_pixels(png):
    """Decode screenshot PNG scanlines using only stdlib (RGB/RGBA, 8 bit)."""
    assert png[:8] == b'\x89PNG\r\n\x1a\n'
    at, compressed = 8, bytearray()
    while at < len(png):
        size = struct.unpack('>I', png[at:at + 4])[0]
        kind, data = png[at + 4:at + 8], png[at + 8:at + 8 + size]
        if kind == b'IHDR':
            width, height, depth, color, _, _, interlace = struct.unpack('>IIBBBBB', data)
            assert depth == 8 and color in (2, 6) and interlace == 0
        if kind == b'IDAT':
            compressed.extend(data)
        at += size + 12
    channels = 3 if color == 2 else 4
    stride = width * channels
    raw = zlib.decompress(compressed)
    previous = bytearray(stride)
    pixels, at = bytearray(), 0
    for _ in range(height):
        filtering = raw[at]
        row = bytearray(raw[at + 1:at + 1 + stride])
        at += stride + 1
        for x in range(stride):
            left = row[x - channels] if x >= channels else 0
            up = previous[x]
            upper_left = previous[x - channels] if x >= channels else 0
            if filtering == 1:
                predictor = left
            elif filtering == 2:
                predictor = up
            elif filtering == 3:
                predictor = (left + up) // 2
            elif filtering == 4:
                p = left + up - upper_left
                distances = (abs(p - left), abs(p - up), abs(p - upper_left))
                predictor = (left, up, upper_left)[distances.index(min(distances))]
            else:
                assert filtering == 0
                predictor = 0
            row[x] = (row[x] + predictor) & 255
        pixels.extend(row)
        previous = row
    return width, height, channels, pixels


class BrowserContract(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        from playwright.sync_api import sync_playwright
        cls.driver = sync_playwright().start()
        cls.browser = cls.driver.chromium.launch(
            headless=not OPTIONS.headed, channel=OPTIONS.channel,
            args=json.loads(os.environ.get('COIN_WEBGPU_BROWSER_ARGS', '[]')))

    @classmethod
    def tearDownClass(cls):
        cls.browser.close()
        cls.driver.stop()

    def setUp(self):
        self.context = self.browser.new_context(ignore_https_errors=True,
                                                viewport={'width': 1000, 'height': 750})
        self.page = self.context.new_page()

    def tearDown(self):
        self.context.close()

    def open(self, mode='normal'):
        self.context.add_init_script(GPU_OBSERVER.replace('MODE', json.dumps(mode)))
        self.context.add_init_script(PROBE)
        self.page.goto(OPTIONS.url, wait_until='domcontentloaded')

    def terminal(self):
        return self.page.evaluate('() => coinRenderWebGPUProbe()')

    def assert_success(self, value):
        self.assertEqual(value['status'], 'rendering', value)
        self.assertEqual(value['backend'], 'webgpu', value)
        self.assertIs(value['fallback'], False, value)
        self.assertEqual(value['sceneSource'], 'coinrender-wasm', value)
        self.assertFalse(value.get('error'), value)
        self.assertIsInstance(value.get('adapter'), dict, value)
        self.assertTrue(value['adapter'], 'Success must expose actual adapter information')
        public_info = json.dumps(value['adapter']).lower()
        for software in ('swiftshader', 'llvmpipe', 'lavapipe'):
            self.assertNotIn(software, public_info, value)
        stats = value['stats']
        for field in ('vertices', 'indices', 'frames'):
            self.assertIsInstance(stats[field], (int, float), value)
            self.assertGreater(stats[field], 0, value)
        self.assertGreaterEqual(stats['vertices'], 8)
        self.assertGreaterEqual(stats['indices'], 36)
        self.assertEqual(stats['indices'] % 3, 0)

    def test_real_render_motion_and_resize(self):
        self.open()
        first = self.terminal()
        self.assert_success(first)
        canvas = self.page.locator('#render-canvas')
        self.assertEqual(canvas.count(), 1)
        before = canvas.screenshot(animations='allow')
        width, height, channels, pixels = png_pixels(before)
        self.assertGreater(width, 1)
        self.assertGreater(height, 1)
        colors = {bytes(pixels[i:i + 3]) for i in range(0, len(pixels), channels)}
        self.assertGreater(len(colors), 2, 'Canvas is blank or a uniform clear')
        self.page.wait_for_function('(n) => window.coinRenderWebGPU.stats.frames > n + 2',
                                    arg=first['stats']['frames'], timeout=15000)
        self.page.wait_for_timeout(500)
        after = canvas.screenshot(animations='allow')
        self.assertNotEqual(before, after, 'Frames advance but visible cube does not move')
        old_size = canvas.evaluate('(c) => [c.width,c.height]')
        self.page.set_viewport_size({'width': 630, 'height': 490})
        self.page.wait_for_function('old => { const c=document.getElementById("render-canvas"); '
                                    'return c.width>0 && c.height>0 && '
                                    '(c.width!==old[0] || c.height!==old[1]); }', arg=old_size)
        self.assert_success(self.page.evaluate('window.coinRenderWebGPU'))
        observed = self.page.evaluate('window.__gpuObserved')
        self.assertGreater(observed['submits'], 0, observed)
        self.assertGreater(observed['completed'], 0, observed)
        self.assertEqual(observed['errors'], [], observed)
        self.assertFalse(observed['fallbackAdapter'], observed)
        identities = [str(info.get(field) or '').strip()
                      for info in observed['adapterInfo']
                      for field in ('vendor', 'architecture', 'device', 'description')]
        identities = [identity for identity in identities if identity]
        self.assertTrue(identities, 'Actual adapter identity could not be verified')
        actual_info = json.dumps(observed['adapterInfo']).lower()
        for software in ('swiftshader', 'llvmpipe', 'lavapipe'):
            self.assertNotIn(software, actual_info, observed)
        public_info = json.dumps(first['adapter']).lower()
        self.assertTrue(any(identity.lower() in public_info for identity in identities),
                        'Published adapter has no identity matching the actual GPU adapter')
        self.assertIn('webgpu', observed['contexts'], observed)
        self.assertFalse(any(name in ('webgl', 'webgl2', 'experimental-webgl', '2d')
                             for name in observed['contexts']), observed)
        self.assertTrue(observed['adapters'], 'Native WebGPU adapter never requested')
        self.assertFalse(any(o.get('forceFallbackAdapter') for o in observed['adapters']))

    def failure(self, mode):
        self.open(mode)
        if mode == 'loss':
            self.page.wait_for_function('window.coinRenderWebGPU?.status === "error"',
                                        timeout=15000)
            value = self.page.evaluate('window.coinRenderWebGPU')
        else:
            value = self.terminal()
        self.assertEqual(value['status'], 'error', (mode, value))
        self.assertIs(value['fallback'], False, value)
        self.assertIsInstance(value['error'], str, value)
        self.assertTrue(value['error'].strip(), value)
        observed = self.page.evaluate('window.__gpuObserved')
        self.assertFalse(any(name in ('webgl', 'webgl2', 'experimental-webgl', '2d')
                             for name in observed['contexts']), observed)
        self.page.wait_for_timeout(300)
        self.assertEqual(self.page.evaluate('window.coinRenderWebGPU.status'), 'error')
        if mode in ('insecure', 'missing', 'adapter-null', 'adapter-reject', 'device-reject'):
            self.assertEqual(self.page.evaluate('window.__gpuObserved.submits'), 0)

    def test_insecure_context(self):
        self.failure('insecure')

    def test_missing_webgpu(self):
        self.failure('missing')

    def test_adapter_unavailable(self):
        self.failure('adapter-null')

    def test_adapter_rejected(self):
        self.failure('adapter-reject')

    def test_device_rejected(self):
        self.failure('device-reject')

    def test_pipeline_failure(self):
        self.failure('pipeline')

    def test_device_loss(self):
        self.failure('loss')

    def test_submission_failure(self):
        self.failure('submit-failure')

    def test_rendering_waits_for_gpu_completion(self):
        self.open('completion-gate')
        self.page.wait_for_function('() => { const o=window.__gpuObserved; '
                                    'return o.submits>0 && o.completionRequests>0 && '
                                    'o.nativeCompletions>0; }', timeout=15000)
        # Give promise continuations, RAF handlers and premature success paths time to run.
        self.page.wait_for_timeout(300)
        value = self.page.evaluate('window.coinRenderWebGPU')
        self.assertEqual(value['status'], 'initializing', value)
        self.assertEqual(value['stats']['frames'], 0, value)
        observed = self.page.evaluate('window.__gpuObserved')
        self.assertFalse(observed['completionReleased'], observed)
        self.assertGreater(observed['completed'], 0, observed)
        self.page.evaluate('window.__gpuReleaseCompletion()')
        self.assert_success(self.terminal())

    def reject_adapter(self, mode):
        self.failure(mode)
        observed = self.page.evaluate('window.__gpuObserved')
        self.assertTrue(observed['injectionApplied'], 'Adapter rejection injection was not exercised')
        self.assertEqual(observed['submits'], 0, 'Rejected adapter submitted rendering work')

    def test_legacy_fallback_adapter(self):
        self.reject_adapter('fallback-legacy')

    def test_info_fallback_adapter(self):
        self.reject_adapter('fallback-info')

    def test_swiftshader_adapter(self):
        self.reject_adapter('software-SwiftShader Device (Subzero)')

    def test_llvmpipe_adapter(self):
        self.reject_adapter('software-llvmpipe (LLVM 19.1.0, 256 bits)')

    def test_lavapipe_adapter(self):
        self.reject_adapter('software-Mesa lavapipe')

    def test_unverifiable_adapter(self):
        self.reject_adapter('adapter-unverifiable')


class ArtifactBoundary(unittest.TestCase):
    def test_no_private_material_in_artifacts(self):
        if not OPTIONS.artifacts_dir:
            self.skipTest('Pass --artifacts-dir to check files exposed by the server')
        root = Path(OPTIONS.artifacts_dir)
        self.assertTrue(root.is_dir())
        wasm = []
        for path in root.rglob('*'):
            self.assertFalse(path.is_symlink(), f'Distribution symlink: {path}')
            if not path.is_file():
                continue
            self.assertNotIn(path.suffix.lower(), ('.key', '.pem', '.p12', '.pfx', '.log'), str(path))
            self.assertNotIn('.git', path.parts, str(path))
            content = path.read_bytes()
            self.assertNotIn(b'PRIVATE KEY-----', content, str(path))
            if path.suffix == '.wasm':
                self.assertEqual(content[:8], b'\0asm\1\0\0\0', str(path))
                wasm.append(path)
        self.assertGreaterEqual(len(wasm), 1, 'No WASM shipped')

    def test_static_server_does_not_serve_repository_or_keys(self):
        base = OPTIONS.server_url or OPTIONS.url
        parsed = urllib.parse.urlsplit(base)
        origin = urllib.parse.urlunsplit((parsed.scheme, parsed.netloc, '/', '', ''))
        paths = ('.git/config', 'CMakeLists.txt', '.env', 'server.key', 'key.pem',
                 '../.git/config', '%2e%2e/.git/config', '%2e%2e%2f.git%2fconfig',
                 '..%2f..%2fetc%2fpasswd', '%2e%2e/%2e%2e/etc/passwd')
        context = ssl._create_unverified_context()
        for path in paths:
            with self.subTest(path=path):
                request = urllib.request.Request(origin + path)
                try:
                    with urllib.request.urlopen(request, context=context, timeout=10) as response:
                        data = response.read()
                        status = response.status
                except urllib.error.HTTPError as error:
                    status, data = error.code, error.read()
                self.assertIn(status, (400, 403, 404), (path, status, data[:100]))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--url', required=True)
    parser.add_argument('--server-url')
    parser.add_argument('--artifacts-dir')
    parser.add_argument('--headed', action='store_true')
    parser.add_argument('--channel')
    OPTIONS, remaining = parser.parse_known_args()
    unittest.main(argv=[__file__] + remaining)
