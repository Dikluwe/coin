#!/usr/bin/env python3
"""External city checks: real native draws/completions, no invented asset/export API.

Run --url http://localhost:8089/ --profile compatibility; trusted HTTPS also works.
The 40k control is normal; --million enables the expensive million hardware case.
No flags enabling software GPU or certificate bypass are supplied by this runner.
"""
import argparse
import json
import math
import os
import re
import unittest
from urllib.parse import parse_qsl, urlencode, urlsplit, urlunsplit

OPTIONS=None
OBSERVE=r"""
(() => {
 const mode=MODE;
 const d=window.__cityProbe={requests:[],wasm:0,completed:[],pendingNative:0,
   buffers:[],textures:0,writes:[],contexts:[],errors:[],releases:[],nativeLimits:[]};
 d.release=()=>{ for(const resolve of d.releases.splice(0)) resolve(); };
 for(const name of ['instantiate','instantiateStreaming','compile','compileStreaming']) {
   const original=WebAssembly[name];
   if(original) WebAssembly[name]=function(...args){d.wasm++;return original.apply(this,args);};
 }
 const context=HTMLCanvasElement.prototype.getContext;
 HTMLCanvasElement.prototype.getContext=function(type,...args){
   d.contexts.push(type);return context.call(this,type,...args);
 };
 if(!navigator.gpu)return;
 const requestAdapter=navigator.gpu.requestAdapter.bind(navigator.gpu);
 navigator.gpu.requestAdapter=async options=>{
   d.requests.push(options);
   const adapter=await requestAdapter(options);if(!adapter)return adapter;
   d.nativeLimits.push({maxBufferSize:adapter.limits.maxBufferSize,
     maxTextureDimension2D:adapter.limits.maxTextureDimension2D});
   const requestDevice=adapter.requestDevice.bind(adapter);
   adapter.requestDevice=async options=>{
     const device=await requestDevice(options), commandBuffers=new WeakMap();
     device.addEventListener('uncapturederror',e=>d.errors.push(String(e.error?.message)));
     const createBuffer=device.createBuffer.bind(device);
     device.createBuffer=descriptor=>{
       d.buffers.push({size:descriptor.size,usage:descriptor.usage});
       if(mode==='allocation-error')throw new Error('TEST city GPU allocation unavailable');
       return createBuffer(descriptor);
     };
     const createTexture=device.createTexture.bind(device);
     device.createTexture=descriptor=>{d.textures++;return createTexture(descriptor);};
     const createEncoder=device.createCommandEncoder.bind(device);
     device.createCommandEncoder=descriptor=>{
       const encoder=createEncoder(descriptor),draws=[];
       const begin=encoder.beginRenderPass.bind(encoder);
       encoder.beginRenderPass=descriptor=>{
         const pass=begin(descriptor),draw=pass.drawIndexed.bind(pass);
         pass.drawIndexed=(indices,instances=1,...args)=>{
           draws.push({indices,instances});return draw(indices,instances,...args);
         };
         return pass;
       };
       const finish=encoder.finish.bind(encoder);
       encoder.finish=descriptor=>{const buffer=finish(descriptor);commandBuffers.set(buffer,draws);return buffer;};
       return encoder;
     };
     const write=device.queue.writeBuffer.bind(device.queue);
     device.queue.writeBuffer=(buffer,offset,data,dataOffset,size)=>{
       d.writes.push({bytes:size===undefined?data.byteLength:size*(data.BYTES_PER_ELEMENT||1),
         rendering:window.coinRenderWebGPU?.status==='rendering'});
       return write(buffer,offset,data,dataOffset,size);
     };
     let pending=[];
     const submit=device.queue.submit.bind(device.queue);
     device.queue.submit=buffers=>{
       const commands=Array.from(buffers);
       const result=submit(commands);
       for(const buffer of commands)pending.push(...(commandBuffers.get(buffer)||[]));
       return result;
     };
     const done=device.queue.onSubmittedWorkDone.bind(device.queue);
     device.queue.onSubmittedWorkDone=()=>{
       const draws=pending;pending=[];
       return done().then(async()=>{
         if(draws.length){
           d.pendingNative++;
           if(mode==='completion-gate' && d.completed.length===0)
             await new Promise(resolve=>d.releases.push(resolve));
           d.completed.push({draws,instances:draws.reduce((n,x)=>n+x.instances,0),
             triangles:draws.reduce((n,x)=>n+x.instances*x.indices/3,0)});
         }
       });
     };
     return device;
   };
   return adapter;
 };
})();
"""


def leaves(node,path=()):
    if isinstance(node,dict):
        for key,value in node.items():
            yield from leaves(value,path+(str(key),))
    elif isinstance(node,list):
        for index,value in enumerate(node):
            yield from leaves(value,path+(str(index),))
    else:
        yield '.'.join(path),node


class CityBrowser(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        from playwright.sync_api import sync_playwright
        cls.driver=sync_playwright().start()
        settings={'headless':not OPTIONS.headed,
                  'args':json.loads(os.environ.get('COIN_WEBGPU_BROWSER_ARGS','[]'))}
        if OPTIONS.channel:settings['channel']=OPTIONS.channel
        cls.browser=getattr(cls.driver,OPTIONS.browser).launch(**settings)

    @classmethod
    def tearDownClass(cls):
        cls.browser.close();cls.driver.stop()

    def setUp(self):
        self.context=self.browser.new_context(viewport={'width':1280,'height':720})
        self.page=self.context.new_page()
        self.reports=[]
        self.page.on('request',lambda request:self.reports.append(request)
                     if urlsplit(request.url).path=='/benchmark-report' else None)

    def tearDown(self):self.context.close()

    def open(self,scenes,mode='normal',terminal=True,report=None):
        parsed=urlsplit(OPTIONS.url)
        query=[(k,v) for k,v in parse_qsl(parsed.query,keep_blank_values=True)
               if k not in ('scene','profile','report')]
        query.append(('profile',OPTIONS.profile));query.extend(('scene',v) for v in scenes)
        if report is not None:query.append(('report',report))
        self.context.add_init_script(OBSERVE.replace('MODE',json.dumps(mode)))
        self.page.goto(urlunsplit((parsed.scheme,parsed.netloc,parsed.path,urlencode(query),'')),
                       wait_until='domcontentloaded',timeout=OPTIONS.timeout*1000)
        if terminal:
            self.page.wait_for_function("['rendering','error'].includes(window.coinRenderWebGPU?.status)",
                                        timeout=OPTIONS.timeout*1000)
        return self.state()

    def state(self):return self.page.evaluate('JSON.parse(JSON.stringify(window.coinRenderWebGPU))')
    def observed(self):return self.page.evaluate('window.__cityProbe')

    def assert_rejected(self,state):
        self.assertEqual(state['status'],'error',state)
        self.assertTrue(state.get('error'),state)
        self.assertEqual(state['stats']['frames'],0,state)
        self.assertIs(state['fallback'],False,state)
        self.assertNotEqual(state['backend'],'webgpu',state)

    def test_invalid_scene_before_gpu_or_wasm(self):
        self.assert_rejected(self.open(('unknown-city',)))
        self.assertEqual(self.observed()['requests'],[])
        self.assertEqual(self.observed()['wasm'],0)

    def test_repeated_ambiguous_scene_before_gpu_or_wasm(self):
        self.assert_rejected(self.open(('cube','million')))
        self.assertEqual(self.observed()['requests'],[])
        self.assertEqual(self.observed()['wasm'],0)

    def test_repeated_identical_scene_before_gpu_or_wasm(self):
        self.assert_rejected(self.open(('million','million')))
        self.assertEqual(self.observed()['requests'],[])
        self.assertEqual(self.observed()['wasm'],0)

    def assert_city_render(self,scene,buildings):
        state=self.open((scene,))
        self.assertEqual(state['status'],'rendering',state)
        self.assertEqual(state['backend'],'webgpu',state)
        self.assertEqual(state['sceneSource'],'coinrender-wasm',state)
        self.assertEqual(state['profile'],OPTIONS.profile,state)
        self.assertEqual(state['scene']['name'],scene,state)
        self.assertIs(state['fallback'],False,state)
        self.assertGreater(state['stats']['frames'],0,state)
        observed=self.observed()
        self.assertTrue(observed['completed'],'No real indexed draws reached native queue completion')
        for frame in observed['completed']:
            self.assertEqual(frame['instances'],buildings+1,frame)
            self.assertEqual(frame['triangles'],12*(buildings+1),frame)
            self.assertTrue(all(draw['indices']==36 for draw in frame['draws']),frame)
        self.assertEqual(len(observed['requests']),1,observed)
        self.assertEqual(observed['requests'][0].get('featureLevel'),OPTIONS.profile)
        self.assertIs(observed['requests'][0].get('forceFallbackAdapter'),False)
        self.assertEqual(observed['errors'],[],observed)
        self.assertFalse(any(c in ('webgl','webgl2','experimental-webgl','2d')
                             for c in observed['contexts']),observed)
        for buffer in observed['buffers']:
            self.assertLessEqual(buffer['size'],observed['nativeLimits'][0]['maxBufferSize'],buffer)
        self.assert_structured_counts(state,buildings,observed['nativeLimits'][0]['maxBufferSize'])
        from test_browser import png_pixels
        screenshot=self.page.locator('#render-canvas').screenshot(animations='allow')
        _,_,channels,pixels=png_pixels(screenshot)
        colors={bytes(pixels[i:i+3]) for i in range(0,len(pixels),channels)}
        self.assertGreater(len(colors),2,'Native draws produced a blank/uniform city canvas')
        initial={'buffers':len(observed['buffers']),'textures':observed['textures']}
        frames=state['stats']['frames']
        self.page.wait_for_function('(target)=>window.coinRenderWebGPU.stats.frames>=target',
                                    arg=frames+OPTIONS.steady_frames,timeout=OPTIONS.timeout*1000)
        after=self.observed()
        self.assertEqual(len(after['buffers']),initial['buffers'],'Unchanged city recreated GPU buffers')
        self.assertEqual(after['textures'],initial['textures'],'Unchanged extent recreated depth textures')
        self.assertFalse(any(w['rendering'] and w['bytes']>4096 for w in after['writes']),
                         'Steady frames re-uploaded geometry/instances instead of only camera uniforms')
        self.assert_metrics(self.state())
        self.assertEqual(self.reports,[],'Reports must be disabled unless report=1 is explicit')
        if scene=='million':
            self.assertEqual(screenshot,self.page.locator('#render-canvas').screenshot(animations='allow'),
                             'Default million benchmark changed its supposedly static camera/scene')

    def assert_structured_counts(self,state,buildings,max_buffer):
        scene=state['scene']
        self.assertEqual(scene['buildings'],buildings,state)
        self.assertEqual(scene['ground'],1,state)
        self.assertEqual(scene['logicalTriangles'],12*(buildings+1),state)
        self.assertEqual(scene['submittedInstances'],buildings+1,state)
        canvas=self.page.locator('#render-canvas').evaluate('(c)=>[c.width,c.height]')
        self.assertEqual([state['stats']['width'],state['stats']['height']],canvas,state)
        self.assertEqual(state['stats']['indices'],36,state)
        self.assertGreater(state['stats']['vertices'],0,state)
        # Adapter limit placement is descriptive in the spec, so discover its path.
        values=list(leaves(state))
        requirements=(('max.*buffer',max_buffer),)
        for pattern,expected in requirements:
            self.assertTrue(any(re.search(pattern,path,re.I) and value==expected
                                for path,value in values),(pattern,expected,state))

    def assert_metrics(self,state):
        values=list(leaves(state))
        for path,value in values:
            if isinstance(value,(int,float)) and not isinstance(value,bool):
                self.assertTrue(math.isfinite(value),(path,value))
        benchmark=state['benchmark']
        self.assertEqual(benchmark['warmupFrames'],10,benchmark)
        self.assertEqual(benchmark['groupSize'],10,benchmark)
        groups=benchmark['groups']
        self.assertTrue(groups,'No actual wall-time steady frame groups reported')
        self.assertLessEqual(len(groups),max(0,(state['stats']['frames']-10)//10),
                             'Groups include initialization/warmup or unfinished frames')
        for group in groups:
            for key in ('fpsWall','renderMs','completionMs','frameMs'):
                self.assertIsInstance(group[key],(int,float),group)
                self.assertTrue(math.isfinite(group[key]),group)
                self.assertGreaterEqual(group[key],0,group)
            self.assertGreater(group['fpsWall'],0,group)
            self.assertGreater(group['frameMs'],0,group)
            self.assertAlmostEqual(group['fpsWall'],1000/group['frameMs'],
                                   delta=max(0.02,group['fpsWall']*0.01),msg=group)
            self.assertLessEqual(group['renderMs']+group['completionMs'],
                                 group['frameMs']+max(1,group['frameMs']*0.02),group)

    def test_control_40000_real_draws_completed_and_steady(self):
        self.assert_city_render('city-40000',40000)

    def test_million_real_draws_completed_and_steady(self):
        if not OPTIONS.million:self.skipTest('Full million hardware run requires --million')
        self.assert_city_render('million',1000000)

    def test_city_waits_for_all_draws_and_native_completion(self):
        self.open(('city-40000',),mode='completion-gate',terminal=False)
        self.page.wait_for_function('window.__cityProbe.pendingNative>0',timeout=OPTIONS.timeout*1000)
        self.page.wait_for_timeout(150)
        state=self.state()
        self.assertEqual(state['status'],'initializing',state)
        self.assertEqual(state['stats']['frames'],0,state)
        self.assertFalse(state['benchmark']['groups'],state)
        self.assertEqual(self.observed()['completed'],[])
        self.page.evaluate('window.__cityProbe.release()')
        self.page.wait_for_function('window.coinRenderWebGPU.status!=="initializing"',
                                    timeout=OPTIONS.timeout*1000)
        self.assertEqual(self.state()['status'],'rendering',self.state())
        frame=self.observed()['completed'][0]
        self.assertEqual(frame['instances'],40001,frame)
        self.assertEqual(frame['triangles'],480012,frame)

    def test_city_allocation_failure_never_reduces_or_claims_success(self):
        self.assert_rejected(self.open(('city-40000',),mode='allocation-error'))
        self.page.wait_for_timeout(300)
        self.assertEqual(self.state()['status'],'error')
        self.assertEqual(self.observed()['completed'],[])

    def test_truncated_city_asset_is_terminal_if_binary_asset_used(self):
        intercepted=[]
        def corrupt(route):
            if re.search(OPTIONS.city_asset_regex,route.request.url):
                response=route.fetch()
                intercepted.append(route.request.url)
                route.fulfill(response=response,body=response.body()[:1])
            else:route.continue_()
        self.context.route('**/*',corrupt)
        state=self.open(('city-40000',))
        if not intercepted:self.skipTest('No matching binary asset used; configure --city-asset-regex')
        self.assert_rejected(state)
        self.assertEqual(self.observed()['completed'],[])

    def test_optin_report_is_real_bounded_and_transport_failure_does_not_stop_rendering(self):
        payloads=[]
        def receive(route):
            request=route.request
            self.assertEqual(request.method,'POST')
            self.assertEqual(urlsplit(request.url).netloc,urlsplit(OPTIONS.url).netloc)
            data=request.post_data_buffer
            self.assertLessEqual(len(data),65536)
            payloads.append(json.loads(data))
            route.fulfill(status=503,content_type='application/json',body='{"error":"TEST transport unavailable"}')
        self.context.route('**/benchmark-report',receive)
        state=self.open(('city-40000',),report='1')
        self.assertEqual(state['status'],'rendering',state)
        for _ in range(OPTIONS.timeout*10):
            if payloads:break
            self.page.wait_for_timeout(100)
        self.assertTrue(payloads,'Explicit report=1 did not POST a completed benchmark group')
        payload=payloads[0]
        for field in ('scene','stats','benchmark','adapter','profile','backend','fallback','status'):
            self.assertIn(field,payload)
        self.assertEqual(payload['scene']['buildings'],40000,payload)
        self.assertEqual(payload['scene']['ground'],1,payload)
        self.assertEqual(payload['scene']['submittedInstances'],40001,payload)
        self.assertEqual(payload['scene']['logicalTriangles'],480012,payload)
        self.assertEqual(payload['status'],'rendering',payload)
        self.assertEqual(payload['backend'],'webgpu',payload)
        self.assertEqual(payload['profile'],OPTIONS.profile,payload)
        self.assertIs(payload['fallback'],False,payload)
        self.assert_metrics(payload)
        self.assertTrue(self.observed()['completed'])
        frames=self.state()['stats']['frames']
        self.page.wait_for_function('(n)=>window.coinRenderWebGPU.stats.frames>n+2',
                                    arg=frames,timeout=OPTIONS.timeout*1000)
        self.assertEqual(self.state()['status'],'rendering',self.state())

    def test_report_zero_does_not_enable_export(self):
        state=self.open(('city-40000',),report='0')
        self.assertEqual(state['status'],'rendering',state)
        self.page.wait_for_function('window.coinRenderWebGPU.stats.frames>=30',
                                    timeout=OPTIONS.timeout*1000)
        self.assertTrue(self.state()['benchmark']['groups'])
        self.assertEqual(self.reports,[],'A present-but-false report query enabled export')


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--url',required=True)
    parser.add_argument('--profile',choices=('core','compatibility'),default='compatibility')
    parser.add_argument('--browser',choices=('chromium','firefox'),default='chromium')
    parser.add_argument('--channel')
    parser.add_argument('--headed',action='store_true')
    parser.add_argument('--million',action='store_true')
    parser.add_argument('--timeout',type=int,default=240)
    parser.add_argument('--steady-frames',type=int,default=40)
    parser.add_argument('--city-asset-regex',default=r'\.(?:bin|city|dat)(?:$|\?)')
    OPTIONS,extra=parser.parse_known_args()
    parsed=urlsplit(OPTIONS.url)
    if parsed.scheme!='https' and not (parsed.scheme=='http' and parsed.hostname in ('localhost','127.0.0.1')):
        parser.error('Use trusted HTTPS or explicit HTTP localhost/127.0.0.1 secure context')
    unittest.main(argv=[__file__]+extra)
