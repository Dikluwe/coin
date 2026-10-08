#!/usr/bin/env python3
"""Independent authored-quad oracle; native controls never use captured Coin IR."""
import argparse
import json
import math
from pathlib import Path

parser=argparse.ArgumentParser()
parser.add_argument('directory',type=Path)
args=parser.parse_args()
root=args.directory
points=[(x,y,c) for y in range(27,37) for x in range(27,37) for c in range(3)]

def metrics(a,b):
    values=[abs(a[(y*64+x)*3+c]-b[(y*64+x)*3+c]) for x,y,c in points]
    return dict(samples=len(values),mae=sum(values)/len(values),maximum=max(values))

def uv(x,y,offset):
    s=.03+.94*(x-24)/16
    t=.02+.96*(40-y)/16
    q=1+.3*s
    return ((s+offset)/q,(t+offset)/q)

def lod(x,y,offset):
    left,top=(x&~1)+.5,(y&~1)+.5
    a,b=uv(left,y+.5,offset),uv(left+1,y+.5,offset)
    c,d=uv(x+.5,top,offset),uv(x+.5,top+1,offset)
    gx=[(b[i]-a[i])*128 for i in range(2)]
    gy=[(d[i]-c[i])*128 for i in range(2)]
    aa,cc=gx[0]**2+gy[0]**2,gx[1]**2+gy[1]**2
    bb=gx[0]*gx[1]+gy[0]*gy[1]
    major=math.sqrt((aa+cc+math.hypot(aa-cc,2*bb))/2)
    return math.log2(max(math.hypot(*gx),math.hypot(*gy))),math.log2(major)

def texel(x,y,level):
    if level>=4:return 100
    n=128>>level
    return 160 if (((x%n)//(8>>level)+(y%n)//(8>>level))&1) else 40

def sample(u,v,level,linear):
    n=128>>level
    if not linear:return texel(math.floor(u*n),math.floor(v*n),level)
    xx,yy=u*n-.5,v*n-.5
    x,y=math.floor(xx),math.floor(yy)
    fx,fy=xx-x,yy-y
    top=texel(x,y,level)*(1-fx)+texel(x+1,y,level)*fx
    bottom=texel(x,y+1,level)*(1-fx)+texel(x+1,y+1,level)*fx
    return top*(1-fy)+bottom*fy

def ideal(quality,offset):
    result=[0]*(64*64*3)
    for y in range(27,37):
        for x in range(27,37):
            u,v=uv(x+.5,y+.5,offset)
            ll=max(0,min(7,lod(x,y,offset)[0]));lo=math.floor(ll)
            value=sample(u,v,lo,quality>=.8)*(1-(ll-lo))+sample(u,v,min(7,lo+1),quality>=.8)*(ll-lo)
            # Authored alpha .8 over clear .1; independent of GL readbacks.
            red=math.floor(.8*value+.2*.1*255+.5)
            result[(y*64+x)*3:(y*64+x)*3+3]=[red]*3
    return result

rows=[];samples=[];checks=[]
for quality in ['.5','.8']:
    for offset in ['0','-.001','.001']:
        controls={}
        for gpu in ['amd','nvidia']:
            def read(api,route):
                p=root/f'{gpu}-{api}-q{quality}-o{offset}-{route}.rgb'
                data=p.read_bytes()
                assert len(data)==64*64*3,p
                return data
            coin=read('coin','coin');fixed=read('egl','fixed');implicit=read('egl','implicit')
            fetch=read('egl','fetch');explicit=read('egl','explicit-lod');rounded=read('egl','fetch-round256')
            q=float(quality);o=float(offset)
            pairs={'coin_vs_raw_fixed_same_context':metrics(coin,read('coin','fixed')),
                   'coin_vs_raw_shader_same_context':metrics(coin,read('coin','implicit')),
                   'coin_vs_pure_egl_fixed':metrics(coin,fixed),
                   'coin_vs_pure_egl_shader':metrics(coin,implicit),
                   'fetch_vs_independent_ideal':metrics(fetch,ideal(q,o)),
                   'native_vs_fetch':metrics(coin,fetch),
                   'explicit_lod_vs_fetch':metrics(explicit,fetch),
                   'explicit_lod_vs_round256':metrics(explicit,rounded)}
            for name in ['coin_vs_raw_fixed_same_context','coin_vs_raw_shader_same_context','coin_vs_pure_egl_fixed','coin_vs_pure_egl_shader','fetch_vs_independent_ideal']:
                m=pairs[name]
                checks.append(dict(gpu=gpu,quality=q,offset=o,name=name,passed=m['mae']<=1.5 and m['maximum']<=4,metrics=m))
            rows.append(dict(gpu=gpu,quality=q,offset=o,comparisons=pairs))
            controls[gpu]={'native':coin,'fetch':fetch}
            if quality=='.5' and offset=='0':
                query=read('egl','query-lod')
                for x,y in [(33,27),(33,36),(35,31)]:
                    i=(y*64+x)*3;queried=(query[i]*256+query[i+1])/4096
                    norm,major=lod(x,y,0)
                    samples.append(dict(gpu=gpu,pixel=[x,y],native_query_lod=queried,
                        shader_max_norm_lod=norm,major_singular_lod=major,
                        native_red=coin[i],explicit_lod_red=explicit[i],fetch_red=fetch[i],
                        round256_red=rounded[i],ideal_red=ideal(q,0)[i]))
        checks.append(dict(name='cross_gpu_explicit_fetch',quality=float(quality),offset=float(offset),
            metrics=metrics(controls['amd']['fetch'],controls['nvidia']['fetch']),
            passed=metrics(controls['amd']['fetch'],controls['nvidia']['fetch'])['maximum']<=4))
        rows.append(dict(comparison='cross_gpu',quality=float(quality),offset=float(offset),
            native=metrics(controls['amd']['native'],controls['nvidia']['native']),
            fetch=metrics(controls['amd']['fetch'],controls['nvidia']['fetch'])))
boundaries=[]
for gpu in ['amd','nvidia']:
    def boundary(api,route):
        return (root/f'{gpu}-{api}-q.5-o0-{route}.rgb').read_bytes()
    native=boundary('egl','boundary-scan')
    fetched=boundary('egl','boundary-fetch')
    model=[];integer=[]
    for y in range(64):
        for x in range(64):
            at=8+(x+.5-32)/4096
            index=math.floor(at)
            integer += [160 if index==7 else 40]*3
            rounded=math.floor(at*256+.5)/256 if gpu=='amd' else at
            model += [160 if math.floor(rounded)==7 else 40]*3
    checks += [dict(gpu=gpu,name='boundary_fetch_exact_integer_oracle',passed=list(fetched)==integer),
               dict(gpu=gpu,name='boundary_native_matches_measured_precision_model',passed=list(native)==model),
               dict(gpu=gpu,name='boundary_coin_matches_coin_free_egl',passed=native==boundary('coin','boundary-scan'))]
    reds=[native[(32*64+x)*3] for x in range(64)]
    transitions=[x for x in range(1,64) if reds[x]!=reds[x-1]]
    boundaries.append(dict(gpu=gpu,native_transition_columns=transitions,
        shader_fetch_transition_column=32,step_in_mip3_texels=1/4096,
        experimental_model='round256 then floor' if gpu=='amd' else 'floor',
        rgb8_full_image_oracle=True))
result={'rows':rows,'boundary_scans':boundaries,'native_lod_samples':samples,'checks':checks,'passed':sum(c['passed'] for c in checks),'total':len(checks)}
(root/'analysis.json').write_text(json.dumps(result,indent=2)+'\n')
print('controls',result['passed'],'/',result['total'])
for r in rows:
    if r.get('comparison')=='cross_gpu' or (r.get('quality')==.5 and r.get('offset')==0):print(r)
print('LOD',samples)
print('Boundaries',boundaries)
raise SystemExit(0 if all(c['passed'] for c in checks) else 1)
