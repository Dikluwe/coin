#!/usr/bin/env python3
"""Independent binary-city protocol and public benchmark arithmetic checks.

Use --url trusted HTTPS or HTTP localhost and optionally --million for the large
served-asset comparison. Pure exports are imported with an invalid scene selected,
so these tests do not require successful GPU initialization. No implementation read.
"""
import argparse
import base64
import math
import os
import json
import random
import struct
import unittest
from urllib.parse import urlsplit,urlunsplit,urljoin
from urllib.request import urlopen

OPTIONS=None
HEADER=struct.Struct('<8s6I6f')
RECORD=struct.Struct('<6fI')
PALETTE=((.72,.78,.85),(.36,.52,.67),(.82,.65,.43),(.53,.65,.59),
         (.70,.45,.34),(.53,.48,.65),(.83,.82,.73),(.32,.43,.52),(.16,.21,.23))


def records(grid):
    """Oracle transcribed from historical Inventor authorship, not binary writer."""
    span=grid*2.2+(grid//10)*2.5
    yield (0,-.25,0,span+6,.5,span+6,8)
    rng=random.Random(136)
    for index in range(grid*grid):
        x,z=index%grid,index//grid
        px=x*2.2+(x//10)*2.5-(span-2.2)/2
        pz=z*2.2+(z//10)*2.5-(span-2.2)/2
        height=rng.uniform(1.2,7.0)*(2 if index%31==0 else 1)
        width,depth=rng.uniform(1.1,1.7),rng.uniform(1.1,1.7)
        authored=tuple(float(f'{v:.3f}') for v in (px,height/2,pz,width,height,depth))
        yield (*authored,index%8)


def reference_asset(grid=200):
    city=list(records(grid))
    lower=[min(r[axis]-r[axis+3]/2 for r in city) for axis in range(3)]
    upper=[max(r[axis]+r[axis+3]/2 for r in city) for axis in range(3)]
    data=bytearray(HEADER.pack(b'CRCTY001',grid,136,grid*grid,grid*grid+1,28,9,*lower,*upper))
    for color in PALETTE:data.extend(struct.pack('<3f',*color))
    for record in city:data.extend(RECORD.pack(*record))
    return bytes(data)


class PublicCityFunctions(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        from playwright.sync_api import sync_playwright
        cls.driver=sync_playwright().start()
        settings={'headless':not OPTIONS.headed,
                  'args':json.loads(os.environ.get('COIN_WEBGPU_BROWSER_ARGS','[]'))}
        if OPTIONS.channel:settings['channel']=OPTIONS.channel
        cls.browser=getattr(cls.driver,OPTIONS.browser).launch(**settings)
        cls.context=cls.browser.new_context()
        cls.page=cls.context.new_page()
        parsed=urlsplit(OPTIONS.url)
        cls.page.goto(urlunsplit((parsed.scheme,parsed.netloc,parsed.path,
                                 'scene=invalid-pure-function-test','')),wait_until='domcontentloaded')
        cls.page.evaluate("async()=>{window.__cityModule=await import(new URL('./app.js',location.href));}")
        cls.page.evaluate("data=>{window.__referenceAsset=Uint8Array.from(atob(data),c=>c.charCodeAt(0)).buffer;}",
                          base64.b64encode(reference_asset()).decode())

    @classmethod
    def tearDownClass(cls):
        cls.context.close();cls.browser.close();cls.driver.stop()

    def test_valid_reference_asset_is_accepted(self):
        self.assertTrue(self.page.evaluate("""()=>{
          const value=__cityModule.parseCityAsset(__referenceAsset.slice(0),40000);
          return value !== null && typeof value==='object';
        }"""))

    def test_asset_header_record_and_length_corruptions_are_rejected(self):
        cases=('magic','grid','seed','buildings','count','stride','paletteCount',
               'boundsNaN','boundsInfinity','paletteNaN','recordNaN','recordInfinity',
               'colorOutOfRange','truncateHeader','truncateRecord','trailingByte','expectedCount')
        for case in cases:
            with self.subTest(case=case):
                result=self.page.evaluate("""name=>{
                  let buffer=__referenceAsset.slice(0), view=new DataView(buffer), expected=40000;
                  const u=(offset,value)=>view.setUint32(offset,value,true);
                  const f=(offset,value)=>view.setFloat32(offset,value,true);
                  if(name==='magic')u(0,0);
                  if(name==='grid')u(8,1000);
                  if(name==='seed')u(12,137);
                  if(name==='buildings')u(16,39999);
                  if(name==='count')u(20,40000);
                  if(name==='stride')u(24,24);
                  if(name==='paletteCount')u(28,8);
                  if(name==='boundsNaN')f(32,NaN);
                  if(name==='boundsInfinity')f(32,Infinity);
                  if(name==='paletteNaN')f(56,NaN);
                  if(name==='recordNaN')f(164,NaN);
                  if(name==='recordInfinity')f(164,Infinity);
                  if(name==='colorOutOfRange')u(164+24,9);
                  if(name==='truncateHeader')buffer=buffer.slice(0,55);
                  if(name==='truncateRecord')buffer=buffer.slice(0,-1);
                  if(name==='trailingByte'){
                    const expanded=new Uint8Array(buffer.byteLength+1);
                    expanded.set(new Uint8Array(buffer));buffer=expanded.buffer;
                  }
                  if(name==='expectedCount')expected=1000000;
                  try {__cityModule.parseCityAsset(buffer,expected);return {rejected:false};}
                  catch(error){return {rejected:true,message:String(error)};}
                }""",case)
                self.assertTrue(result['rejected'],(case,result))
                self.assertTrue(result['message'])

    def test_benchmark_group_arithmetic_and_zero_phase_quantization(self):
        result=self.page.evaluate("""()=>{
          const samples=Array.from({length:10},(_,i)=>({renderMs:i+1,completionMs:(i%3)*.5}));
          return {normal:__cityModule.benchmarkGroup(samples,250),
            slower:__cityModule.benchmarkGroup(samples,500),
            zero:__cityModule.benchmarkGroup(Array.from({length:10},()=>({renderMs:0,completionMs:0})),100),
            outlier:__cityModule.benchmarkGroup(Array.from({length:10},(_,i)=>({renderMs:i===0?20:0,completionMs:0})),100)};
        }""")
        normal=result['normal']
        for key,value in (('fpsWall',40),('frameMs',25),('renderMs',5.5),('completionMs',.45)):
            self.assertAlmostEqual(normal[key],value,delta=1e-8)
        self.assertAlmostEqual(result['slower']['fpsWall'],normal['fpsWall']/2,delta=1e-8)
        self.assertEqual(result['slower']['frameMs'],50)
        self.assertEqual(result['slower']['renderMs'],normal['renderMs'])
        self.assertEqual(result['zero']['fpsWall'],100)
        self.assertEqual(result['zero']['renderMs'],0)
        self.assertEqual(result['zero']['completionMs'],0)
        self.assertEqual(result['outlier']['renderMs'],2)

    def test_benchmark_group_rejects_invalid_samples_and_elapsed(self):
        cases=('nine','eleven','empty','nullSamples','elapsedZero','elapsedNegative',
               'elapsedNaN','elapsedInfinity','negativeRender','negativeCompletion',
               'nanRender','infiniteCompletion','missingPhase','nullSample','meanExceedsInterval')
        for case in cases:
            with self.subTest(case=case):
                rejected=self.page.evaluate("""name=>{
                  let samples=Array.from({length:10},()=>({renderMs:1,completionMs:2})),elapsed=100;
                  if(name==='nine')samples.pop();
                  if(name==='eleven')samples.push({renderMs:1,completionMs:2});
                  if(name==='empty')samples=[];
                  if(name==='nullSamples')samples=null;
                  if(name==='elapsedZero')elapsed=0;
                  if(name==='elapsedNegative')elapsed=-1;
                  if(name==='elapsedNaN')elapsed=NaN;
                  if(name==='elapsedInfinity')elapsed=Infinity;
                  if(name==='negativeRender')samples[0].renderMs=-1;
                  if(name==='negativeCompletion')samples[0].completionMs=-1;
                  if(name==='nanRender')samples[0].renderMs=NaN;
                  if(name==='infiniteCompletion')samples[0].completionMs=Infinity;
                  if(name==='missingPhase')delete samples[0].renderMs;
                  if(name==='nullSample')samples[0]=null;
                  if(name==='meanExceedsInterval')samples=samples.map(()=>({renderMs:9,completionMs:2}));
                  try {__cityModule.benchmarkGroup(samples,elapsed);return false;}catch(_){return true;}
                }""",case)
                self.assertTrue(rejected,case)


class ServedCityAssets(unittest.TestCase):
    def compare(self,grid):
        name='city-million.bin' if grid==1000 else 'city-40000.bin'
        with urlopen(urljoin(OPTIONS.url,name),timeout=60) as response:data=response.read()
        self.assertGreaterEqual(len(data),HEADER.size)
        header=HEADER.unpack_from(data)
        self.assertEqual(header[:7],(b'CRCTY001',grid,136,grid*grid,grid*grid+1,28,9))
        self.assertEqual(len(data),HEADER.size+9*12+(grid*grid+1)*RECORD.size)
        self.assertTrue(all(math.isfinite(x) for x in header[7:]))
        self.assertEqual(data[56:164],b''.join(struct.pack('<3f',*color) for color in PALETTE))
        lower=[math.inf]*3;upper=[-math.inf]*3
        for index,expected in enumerate(records(grid)):
            offset=164+index*28
            self.assertEqual(data[offset:offset+28],RECORD.pack(*expected),
                             f'Authored record {index} differs from historical seed136/.3f fields')
            for axis in range(3):
                lower[axis]=min(lower[axis],expected[axis]-expected[axis+3]/2)
                upper[axis]=max(upper[axis],expected[axis]+expected[axis+3]/2)
        for actual,expected in zip(header[7:],(*lower,*upper)):
            self.assertAlmostEqual(actual,expected,delta=max(.002,abs(expected)*1e-6))

    def test_control_asset_matches_historical_authored_records(self):self.compare(200)

    def test_million_asset_matches_historical_authored_records(self):
        if not OPTIONS.million:self.skipTest('Full million asset comparison requires --million')
        self.compare(1000)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--url',required=True)
    parser.add_argument('--browser',choices=('chromium','firefox'),default='chromium')
    parser.add_argument('--channel')
    parser.add_argument('--headed',action='store_true')
    parser.add_argument('--million',action='store_true')
    OPTIONS,extra=parser.parse_known_args()
    parsed=urlsplit(OPTIONS.url)
    if parsed.scheme!='https' and not(parsed.scheme=='http' and parsed.hostname in ('localhost','127.0.0.1')):
        parser.error('Use trusted HTTPS or explicit localhost HTTP')
    unittest.main(argv=[__file__]+extra)
