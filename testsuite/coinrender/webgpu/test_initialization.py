#!/usr/bin/env python3
"""External startup diagnostics tests; HTTPS must already be trusted.

python test_initialization.py --url https://host:8443/ --channel chrome -v
Only application timeout callbacks are advanced, after a real asynchronous
boundary has been held. Native GPU promises and network responses stay real.
"""
import argparse
import json
import os
import unittest
from urllib.parse import urlsplit

OPTIONS = None
INSTRUMENT = r"""
(() => {
  const mode = MODE;
  const d = window.__startupDiagnostic = {phases: [], hits: [], submits: 0,
    pipelines: 0, configurations: 0, nativeCompletions: 0, releases: []};
  let state;
  Object.defineProperty(window, 'coinRenderWebGPU', {
    configurable: true,
    get: () => state,
    set: value => {
      state = new Proxy(value, {set(target, key, value) {
        target[key] = value;
        if (key === 'phase') d.phases.push(value);
        return true;
      }});
      if (value.phase) d.phases.push(value.phase);
    }
  });
  const realTimeout = window.setTimeout.bind(window);
  const realClear = window.clearTimeout.bind(window);
  const timers = new Map();
  window.setTimeout = (callback, delay, ...args) => {
    if (typeof callback !== 'function') return realTimeout(callback, delay, ...args);
    let id;
    const wrapped = () => { timers.delete(id); callback(...args); };
    id = realTimeout(wrapped, delay);
    if (typeof callback === 'function' && Number(delay) >= 1000)
      timers.set(id, {delay:Number(delay), callback:wrapped});
    return id;
  };
  window.clearTimeout = id => { timers.delete(id); realClear(id); };
  d.pendingTimeouts = () => [...timers.values()].map(timer => timer.delay);
  d.expireBoundedWaits = () => {
    const due = [...timers.entries()].filter(([,timer]) => timer.delay <= 30000);
    for (const [id, timer] of due) {
      realClear(id); timers.delete(id); timer.callback();
    }
    return due.length;
  };
  d.release = () => { for (const resolve of d.releases.splice(0)) resolve(); };
  function checkpoint(name) {
    d.hits.push({name, phase:state?.phase,
      ui:document.getElementById('status')?.textContent || ''});
  }
  async function gate(name, nativePromise) {
    const result = await nativePromise;
    checkpoint(name);
    if (mode === name) await new Promise(resolve => d.releases.push(resolve));
    return result;
  }
  const originalContext = HTMLCanvasElement.prototype.getContext;
  HTMLCanvasElement.prototype.getContext = function(type, ...args) {
    const context = originalContext.call(this, type, ...args);
    if (type === 'webgpu' && context) {
      const configure = context.configure.bind(context);
      context.configure = (...args) => {
        checkpoint('configuration'); d.configurations++; return configure(...args);
      };
    }
    return context;
  };
  if (!navigator.gpu) return;
  const requestAdapter = navigator.gpu.requestAdapter.bind(navigator.gpu);
  navigator.gpu.requestAdapter = async options => {
    const adapter = await gate('adapter', requestAdapter(options));
    if (!adapter) return adapter;
    const requestDevice = adapter.requestDevice.bind(adapter);
    adapter.requestDevice = async descriptor => {
      const device = await gate('device', requestDevice(descriptor));
      const submit = device.queue.submit.bind(device.queue);
      device.queue.submit = commands => {
        checkpoint('submission'); d.submits++; return submit(commands);
      };
      const completed = device.queue.onSubmittedWorkDone.bind(device.queue);
      device.queue.onSubmittedWorkDone = () => gate('completion', completed().then(() => {
        d.nativeCompletions++;
      }));
      const pipeline = device.createRenderPipeline.bind(device);
      device.createRenderPipeline = descriptor => {
        checkpoint('pipeline'); d.pipelines++; return pipeline(descriptor);
      };
      if (device.createRenderPipelineAsync) {
        const pipelineAsync = device.createRenderPipelineAsync.bind(device);
        device.createRenderPipelineAsync = descriptor => {
          checkpoint('pipeline'); d.pipelines++; return pipelineAsync(descriptor);
        };
      }
      return device;
    };
    return adapter;
  };
})();
"""

TOKENS = {
    'coin': ('coin', 'captur'),
    'renderer': ('renderer', 'renderizador', 'rust'),
    'adapter': ('adapter', 'adaptador'),
    'device': ('device', 'dispositivo'),
    'pipeline': ('pipeline', 'configur'),
    'configuration': ('pipeline', 'configur'),
    'submission': ('frame', 'quadro', 'submiss', 'render'),
    'completion': ('completion', 'conclu', 'confirma', 'fila', 'frame', 'quadro'),
    'module': ('módulo', 'modulo', 'module', 'script', 'import', 'carreg'),
}


class InitializationDiagnostics(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        from playwright.sync_api import sync_playwright
        cls.driver = sync_playwright().start()
        browser_type = getattr(cls.driver, OPTIONS.browser)
        settings = {'headless':not OPTIONS.headed,
                    'args':json.loads(os.environ.get('COIN_WEBGPU_BROWSER_ARGS', '[]'))}
        if OPTIONS.channel:
            settings['channel'] = OPTIONS.channel
        cls.browser = browser_type.launch(**settings)

    @classmethod
    def tearDownClass(cls):
        cls.browser.close()
        cls.driver.stop()

    def setUp(self):
        self.context = self.browser.new_context()
        self.page = self.context.new_page()
        self.routes = []

    def tearDown(self):
        self.context.close()

    def open(self, mode='normal', block=None, abort=False):
        self.context.add_init_script(INSTRUMENT.replace('MODE', json.dumps(mode)))
        if block:
            def intercept(route):
                path = urlsplit(route.request.url).path
                match = ((block == 'coin' and path.endswith('coin_capture.wasm')) or
                         (block == 'renderer' and path.endswith('.wasm') and
                          not path.endswith('coin_capture.wasm')) or
                         (block in ('app.js','coin_capture.js','renderer.js') and
                          path.endswith('/' + block)))
                if match:
                    if abort:
                        route.abort('failed')
                    else:
                        self.routes.append(route)
                else:
                    route.continue_()
            self.context.route('**/*', intercept)
        self.page.goto(OPTIONS.url, wait_until='commit')
        self.page.wait_for_function('window.coinRenderWebGPU !== undefined')

    def state(self):
        return self.page.evaluate('JSON.parse(JSON.stringify(window.coinRenderWebGPU))')

    def assert_names_stage(self, text, stage):
        self.assertIsInstance(text, str)
        self.assertTrue(any(token in text.casefold() for token in TOKENS[stage]),
                        f'{stage} is not identified: {text!r}')

    def assert_waiting_stage(self, stage):
        state = self.state()
        self.assertEqual(state['status'], 'initializing', state)
        self.assertEqual(state['stats']['frames'], 0, state)
        self.assert_names_stage(state.get('phase'), stage)
        self.assert_names_stage(self.page.locator('#status').inner_text(), stage)
        self.assertIs(state['fallback'], False)
        return state

    def expire_and_check(self, stage):
        self.assert_waiting_stage(stage)
        delays = self.page.evaluate('window.__startupDiagnostic.pendingTimeouts()')
        self.assertTrue(any(1000 <= delay <= 30000 for delay in delays),
                        f'No bounded startup timeout: {delays}')
        self.assertGreater(self.page.evaluate('window.__startupDiagnostic.expireBoundedWaits()'), 0)
        self.page.wait_for_function('window.coinRenderWebGPU.status === "error"', timeout=3000)
        value = self.state()
        self.assert_names_stage(value['error'], stage)
        self.assertRegex(value['error'].casefold(), r'timeout|tempo|30\s*s|demor|limite')
        self.assertIs(value['fallback'], False)
        self.assertEqual(value['stats']['frames'], 0)
        self.assertNotEqual(value['backend'], 'webgpu')
        return value

    def assert_late_result_cannot_restart(self, original):
        before = self.page.evaluate('({submits:__startupDiagnostic.submits, '
                                    'pipelines:__startupDiagnostic.pipelines, '
                                    'configurations:__startupDiagnostic.configurations})')
        for route in self.routes:
            route.continue_()
        self.routes.clear()
        self.page.evaluate('window.__startupDiagnostic.release()')
        self.page.wait_for_timeout(1500)
        value = self.state()
        self.assertEqual(value['status'], 'error', value)
        self.assertEqual(value['error'], original['error'], value)
        self.assertEqual(value['stats']['frames'], 0, value)
        self.assertIs(value['fallback'], False)
        after = self.page.evaluate('({submits:__startupDiagnostic.submits, '
                                   'pipelines:__startupDiagnostic.pipelines, '
                                   'configurations:__startupDiagnostic.configurations})')
        self.assertEqual(after, before, 'Late result resumed GPU initialization/rendering')

    def module_timeout(self, stage):
        self.open(block=stage)
        # Route callbacks are dispatched while Playwright pumps browser events.
        for _ in range(200):
            if self.routes:
                break
            self.page.wait_for_timeout(50)
        self.assertTrue(self.routes, f'No {stage} request was intercepted')
        original = self.expire_and_check(stage)
        self.assert_late_result_cannot_restart(original)

    def test_coin_wasm_timeout_and_late_result(self):
        self.module_timeout('coin')

    def test_renderer_wasm_timeout_and_late_result(self):
        self.module_timeout('renderer')

    def gpu_timeout(self, stage):
        self.open(mode=stage)
        self.page.wait_for_function('(name) => window.__startupDiagnostic.hits.some(h => h.name===name)',
                                    arg=stage, timeout=20000)
        original = self.expire_and_check(stage)
        self.assert_late_result_cannot_restart(original)

    def test_adapter_timeout_and_late_result(self):
        self.gpu_timeout('adapter')

    def test_device_timeout_and_late_result(self):
        self.gpu_timeout('device')

    def test_first_completion_timeout_and_late_result(self):
        self.gpu_timeout('completion')
        self.assertGreater(self.page.evaluate('__startupDiagnostic.nativeCompletions'), 0)

    def test_static_module_watchdog_and_late_import(self):
        for module in ('app.js', 'coin_capture.js', 'renderer.js'):
            with self.subTest(module=module):
                # A fresh context is required for each independent ES module graph.
                if module != 'app.js':
                    self.context.close()
                    self.context = self.browser.new_context()
                    self.page = self.context.new_page()
                    self.routes = []
                self.open(block=module)
                for _ in range(200):
                    if self.routes:
                        break
                    self.page.wait_for_timeout(50)
                self.assertTrue(self.routes, module)
                original = self.expire_and_check('module')
                self.assert_late_result_cannot_restart(original)

    def test_failed_static_import_is_explicit_error(self):
        self.open(block='app.js', abort=True)
        self.page.wait_for_function('window.coinRenderWebGPU.status === "error"', timeout=5000)
        value = self.state()
        self.assertTrue(value.get('phase'), value)
        self.assert_names_stage(value['error'], 'module')
        self.assertEqual(value['stats']['frames'], 0)
        self.assertIs(value['fallback'], False)

    def test_success_publishes_pipeline_and_frame_progress(self):
        self.open()
        self.page.wait_for_function('window.coinRenderWebGPU.status !== "initializing"', timeout=35000)
        value = self.state()
        self.assertEqual(value['status'], 'rendering', value)
        self.assertEqual(value['backend'], 'webgpu', value)
        self.assertIs(value['fallback'], False)
        observed = self.page.evaluate('({phases:__startupDiagnostic.phases,hits:__startupDiagnostic.hits,'
                                      'submits:__startupDiagnostic.submits,'
                                      'completed:__startupDiagnostic.nativeCompletions})')
        for stage in ('coin', 'renderer', 'adapter', 'device', 'pipeline', 'submission', 'completion'):
            self.assertTrue(any(any(token in str(phase).casefold() for token in TOKENS[stage])
                                for phase in observed['phases']), (stage, observed))
        for hit in observed['hits']:
            self.assert_names_stage(hit['phase'], hit['name'])
            self.assert_names_stage(hit['ui'], hit['name'])
        self.assertGreater(observed['submits'], 0)
        self.assertGreater(observed['completed'], 0)
        self.assertGreater(value['stats']['frames'], 0)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--url', required=True)
    parser.add_argument('--browser', choices=('chromium','firefox'), default='chromium')
    parser.add_argument('--channel')
    parser.add_argument('--headed', action='store_true')
    OPTIONS, remaining = parser.parse_known_args()
    if urlsplit(OPTIONS.url).scheme != 'https':
        parser.error('--url must use trusted HTTPS')
    unittest.main(argv=[__file__] + remaining)
