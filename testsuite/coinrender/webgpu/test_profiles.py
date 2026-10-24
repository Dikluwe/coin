#!/usr/bin/env python3
"""Black-box WebGPU profile selection tests against a trusted HTTPS demo.

python test_profiles.py --url https://host:8443/ --channel chrome -v
The option/rejection cases need no successful GPU rendering. The separate real
compatibility render test explicitly skips with the actual error when unsupported.
"""
import argparse
import json
import os
import unittest
from urllib.parse import parse_qsl, urlencode, urlsplit, urlunsplit

OPTIONS = None
OBSERVE = r"""
(() => {
  const mode = MODE;
  const record = window.__profileProbe = {requests: [], wasm: [], devices: 0,
    submits: 0, completed: 0, injected: false, contexts: [],
    adapterLimits: [], deviceRequests: []};
  const getContext=HTMLCanvasElement.prototype.getContext;
  HTMLCanvasElement.prototype.getContext=function(type,...args) {
    record.contexts.push(type); return getContext.call(this,type,...args);
  };
  for (const name of ['compile','compileStreaming','instantiate','instantiateStreaming']) {
    const original=WebAssembly[name];
    if (original) WebAssembly[name]=function(...args) {
      record.wasm.push(name); return Reflect.apply(original,WebAssembly,args);
    };
  }
  let gpu=navigator.gpu;
  const original=gpu?.requestAdapter?.bind(gpu);
  if (!gpu && mode !== 'real') {
    gpu={}; Object.defineProperty(navigator,'gpu',{value:gpu, configurable:true});
  }
  if (!gpu) return;
  gpu.requestAdapter=async options => {
    record.requests.push(JSON.parse(JSON.stringify(options || {})));
    if (mode === 'null') return null;
    if (mode.startsWith('reject-')) {
      record.injected=true;
      const info={vendor:'Independent test vendor',device:'Independent test device',
        architecture:'Independent test architecture',description:'Independent test GPU',
        isFallbackAdapter:false};
      let legacy=false;
      if (mode === 'reject-legacy') legacy=true;
      if (mode === 'reject-modern') info.isFallbackAdapter=true;
      if (mode === 'reject-software') info.description='SwiftShader Device (Subzero)';
      return {info,isFallbackAdapter:legacy,requestAdapterInfo:async()=>info,
        requestDevice:async()=> {
          record.devices++;
          throw new Error('Rejected adapter must not reach device creation');
        }};
    }
    const adapter=await original(options);
    if (!adapter) return adapter;
    const limits={};
    for (let prototype=adapter.limits; prototype; prototype=Object.getPrototypeOf(prototype)) {
      for (const name of Object.getOwnPropertyNames(prototype)) {
        if (name === 'constructor') continue;
        const value=adapter.limits[name];
        if (typeof value === 'number') limits[name]=value;
      }
    }
    const adapterId=record.adapterLimits.length;
    record.adapterLimits.push(limits);
    const requestDevice=adapter.requestDevice.bind(adapter);
    adapter.requestDevice=async descriptor => {
      record.deviceRequests.push({adapterId,
        descriptor:JSON.parse(JSON.stringify(descriptor || {}))});
      const device=await requestDevice(descriptor); record.devices++;
      const submit=device.queue.submit.bind(device.queue);
      const done=device.queue.onSubmittedWorkDone.bind(device.queue);
      device.queue.submit=commands=> {
        submit(commands); record.submits++;
        done().then(()=>record.completed++);
      };
      return device;
    };
    return adapter;
  };
})();
"""


class WebGPUProfiles(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        from playwright.sync_api import sync_playwright
        cls.driver=sync_playwright().start()
        launch={'headless':not OPTIONS.headed,
                'args':json.loads(os.environ.get('COIN_WEBGPU_BROWSER_ARGS','[]'))}
        if OPTIONS.channel:
            launch['channel']=OPTIONS.channel
        cls.browser=getattr(cls.driver,OPTIONS.browser).launch(**launch)

    @classmethod
    def tearDownClass(cls):
        cls.browser.close()
        cls.driver.stop()

    def setUp(self):
        self.context=self.browser.new_context()
        self.page=self.context.new_page()

    def tearDown(self):
        self.context.close()

    def open(self, profiles=(), mode='null'):
        base=urlsplit(OPTIONS.url)
        query=[(key,value) for key,value in parse_qsl(base.query,keep_blank_values=True)
               if key!='profile']
        query.extend(('profile',value) for value in profiles)
        url=urlunsplit((base.scheme,base.netloc,base.path,urlencode(query),base.fragment))
        self.context.add_init_script(OBSERVE.replace('MODE',json.dumps(mode)))
        self.page.goto(url,wait_until='domcontentloaded')
        self.page.wait_for_function("['rendering','error'].includes(window.coinRenderWebGPU?.status)",
                                    timeout=35000)
        return self.page.evaluate('JSON.parse(JSON.stringify(window.coinRenderWebGPU))')

    def observed(self):
        return self.page.evaluate('window.__profileProbe')

    def assert_request(self, profile):
        record=self.observed()
        self.assertEqual(len(record['requests']),1,record)
        request=record['requests'][0]
        self.assertEqual(request.get('featureLevel'),profile,record)
        self.assertIs(request.get('forceFallbackAdapter'),False,record)
        state=self.page.evaluate('window.coinRenderWebGPU')
        self.assertEqual(state['profile'],profile,state)
        self.assertIs(state['fallback'],False,state)
        return record

    def assert_terminal_error(self, state):
        self.assertEqual(state['status'],'error',state)
        self.assertTrue(state.get('error'),state)
        self.assertEqual(state['stats']['frames'],0,state)
        self.assertIs(state['fallback'],False,state)
        self.assertNotEqual(state['backend'],'webgpu',state)
        self.page.wait_for_timeout(300)
        self.assertEqual(self.page.evaluate('window.coinRenderWebGPU.status'),'error')
        self.assertFalse(any(name in ('webgl','webgl2','experimental-webgl','2d')
                             for name in self.observed()['contexts']),self.observed())

    def test_default_core_request_unavailable_has_no_automatic_switch(self):
        state=self.open()
        self.assert_terminal_error(state)
        self.assert_request('core')

    def test_explicit_core_request_unavailable_has_no_automatic_switch(self):
        state=self.open(('core',))
        self.assert_terminal_error(state)
        self.assert_request('core')

    def test_compatibility_request_unavailable_has_no_retry(self):
        state=self.open(('compatibility',))
        self.assert_terminal_error(state)
        self.assert_request('compatibility')

    def invalid_profile(self, values):
        state=self.open(values)
        self.assert_terminal_error(state)
        self.assertIn('profil',state['error'].casefold(),state)
        record=self.observed()
        self.assertEqual(record['requests'],[],record)
        self.assertEqual(record['wasm'],[],record)
        self.assertEqual(record['devices'],0,record)

    def test_unknown_profile_rejected_before_initialization(self):
        self.invalid_profile(('webgl',))

    def test_empty_profile_rejected_before_initialization(self):
        self.invalid_profile(('',))

    def test_case_mismatched_profile_rejected_before_initialization(self):
        self.invalid_profile(('Compatibility',))

    def test_conflicting_repeated_profiles_rejected_before_initialization(self):
        self.invalid_profile(('core','compatibility'))

    def test_identical_repeated_profiles_rejected_before_initialization(self):
        self.invalid_profile(('compatibility','compatibility'))

    def rejected_compat_adapter(self, mode):
        state=self.open(('compatibility',),mode=mode)
        self.assert_terminal_error(state)
        record=self.assert_request('compatibility')
        self.assertTrue(record['injected'],record)
        self.assertEqual(record['devices'],0,record)

    def test_compatibility_rejects_legacy_fallback(self):
        self.rejected_compat_adapter('reject-legacy')

    def test_compatibility_rejects_modern_fallback(self):
        self.rejected_compat_adapter('reject-modern')

    def test_compatibility_rejects_known_software(self):
        self.rejected_compat_adapter('reject-software')

    def test_real_compatibility_render_if_available(self):
        state=self.open(('compatibility',),mode='real')
        self.assert_request('compatibility')
        if state['status']=='error':
            self.skipTest('Actual compatibility rendering unavailable: '+state['error'])
        self.assertEqual(state['status'],'rendering',state)
        self.assertEqual(state['backend'],'webgpu',state)
        self.assertEqual(state['sceneSource'],'coinrender-wasm',state)
        self.assertIs(state['fallback'],False,state)
        self.assertGreater(state['stats']['vertices'],0,state)
        self.assertGreaterEqual(state['stats']['indices'],36,state)
        self.assertGreater(state['stats']['frames'],0,state)
        self.page.wait_for_function('(n)=>window.coinRenderWebGPU.stats.frames>n',
                                    arg=state['stats']['frames'],timeout=15000)
        observed=self.observed()
        self.assertGreater(observed['submits'],0,observed)
        self.assertGreater(observed['completed'],0,observed)
        self.assertFalse(any(name in ('webgl','webgl2','experimental-webgl','2d')
                             for name in observed['contexts']),observed)

    def test_render_only_compatibility_device_limits_and_resize(self):
        state=self.open(('compatibility',),mode='real')
        observed=self.assert_request('compatibility')
        if not observed['deviceRequests']:
            self.assertEqual(state['status'],'error',state)
            self.skipTest('No real device descriptor reached: '+state['error'])
        self.assertEqual(len(observed['deviceRequests']),1,observed)
        request=observed['deviceRequests'][0]
        actual=observed['adapterLimits'][request['adapterId']]
        requested=request['descriptor'].get('requiredLimits',{})
        unused=(
            'maxComputeWorkgroupStorageSize','maxComputeInvocationsPerWorkgroup',
            'maxComputeWorkgroupSizeX','maxComputeWorkgroupSizeY','maxComputeWorkgroupSizeZ',
            'maxComputeWorkgroupsPerDimension','maxStorageBuffersPerShaderStage',
            'maxStorageTexturesPerShaderStage','maxStorageBufferBindingSize',
            'maxDynamicStorageBuffersPerPipelineLayout',
        )
        for name in unused:
            with self.subTest(limit=name):
                self.assertEqual(requested.get(name,0),0,
                                 f'Render-only demo requested unused {name}: {request}')
        for name in ('maxColorAttachments','maxColorAttachmentBytesPerSample'):
            if name in requested:
                self.assertIn(name,actual,observed)
                self.assertLessEqual(requested[name],actual[name],observed)
                self.assertGreaterEqual(requested[name],1 if name=='maxColorAttachments' else 4,
                                        'Descriptor cannot support even one RGBA/BGRA color target')
        self.assertIn('maxTextureDimension2D',actual,observed)
        self.assertEqual(requested.get('maxTextureDimension2D'),actual['maxTextureDimension2D'],
                         'Canvas texture resolution must come from the actual adapter')
        if state['status']=='error':
            self.skipTest('Real descriptor inspected, rendering unavailable: '+state['error'])
        self.assertEqual(state['status'],'rendering',state)
        self.assertEqual(state['backend'],'webgpu',state)
        self.assertEqual(state['sceneSource'],'coinrender-wasm',state)
        self.assertIs(state['fallback'],False,state)
        for width,height in ((640,480),(1920,1080)):
            with self.subTest(viewport=(width,height)):
                frames=self.page.evaluate('window.coinRenderWebGPU.stats.frames')
                self.page.set_viewport_size({'width':width,'height':height})
                self.page.wait_for_function('(n)=>window.coinRenderWebGPU.stats.frames>n+1',
                                            arg=frames,timeout=15000)
                canvas=self.page.locator('#render-canvas').evaluate('(c)=>[c.width,c.height]')
                self.assertTrue(all(0 < extent <= actual['maxTextureDimension2D']
                                    for extent in canvas),(canvas,actual))
                state=self.page.evaluate('window.coinRenderWebGPU')
                self.assertEqual(state['status'],'rendering',state)
                self.assertEqual(state['backend'],'webgpu',state)
                self.assertIs(state['fallback'],False,state)
        observed=self.observed()
        self.assertEqual(len(observed['requests']),1,observed)
        self.assertEqual(len(observed['deviceRequests']),1,observed)
        self.assertGreater(observed['submits'],0,observed)
        self.assertGreater(observed['completed'],0,observed)
        self.assertFalse(any(name in ('webgl','webgl2','experimental-webgl','2d')
                             for name in observed['contexts']),observed)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--url',required=True)
    parser.add_argument('--browser',choices=('chromium','firefox'),default='chromium')
    parser.add_argument('--channel')
    parser.add_argument('--headed',action='store_true')
    OPTIONS,extra=parser.parse_known_args()
    if urlsplit(OPTIONS.url).scheme!='https':
        parser.error('--url must use trusted HTTPS')
    unittest.main(argv=[__file__]+extra)
