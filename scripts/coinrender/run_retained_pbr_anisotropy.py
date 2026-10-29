#!/usr/bin/env python3
"""Independent retained/callback/pinned-SDK anisotropy image qualification."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
ROOT=Path(__file__).resolve().parents[2]
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('retained','current','updates','environment','products','output'):
        parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--reference',type=Path)
    args=parser.parse_args();output=args.output.resolve();output.mkdir(parents=True,exist_ok=True)
    base=(ROOT/'testsuite/coinrender/scenes/pbr-surface.iv').read_text().replace('metallic .35 roughness .6','metallic .8 roughness .35 anisotropy .8 anisotropyRotation .3')
    untextured=re.sub(r'(baseColor|orm|emissive)TextureUnit [0-9]+','',base)
    untextured=re.sub(r'(DEF BaseMap )?Texture2 \{[^}]+\}','',untextured)
    untextured=re.sub(r'BumpMap \{[^}]+\}','',untextured)
    fixtures={
        'normal-and-maps':base,
        'no-images':untextured,
        'ibl-only':re.sub(r'DirectionalLight \{[^}]+\}','',untextured),
        'direct-only':untextured,
        'diagnostic-bent':base,
        'ibl64':base,
        'rotated90':base.replace('anisotropyRotation .3','anisotropyRotation 1.87079632679'),
        'mirrored-model':base.replace('DEF Move Translation','Scale { scaleFactor -1.3 .7 1.4 }\nDEF Move Translation'),
        'mirrored-uv':base.replace('scaleFactor 1.3 1.2','scaleFactor -1.3 1.2'),
        'degenerate-uv':untextured.replace('TextureCoordinate2 { point [ 0 0,1 0,1 1,0 1 ] }','TextureCoordinate2 { point [ 0 0,0 0,0 0,0 0 ] }'),
        'bump-transform':base.replace('BumpMap {','BumpMapTransform { rotation .7 scaleFactor .7 1.2 }\nBumpMap {'),
        'unit7-projection':untextured.replace('anisotropy .8','tangentCoordinateUnit 7 anisotropy .8').replace('DEF Move Translation','TextureUnit { unit 7 } TextureCoordinatePlane { directionS .4 .1 0 directionT -.2 .3 0 }\nDEF Move Translation'),
    }
    sphere=re.sub(r'TextureCoordinate2 \{[^}]+\}','',untextured[:untextured.index('Coordinate3 {')])+'Sphere { radius 1.3 }\n}\n'
    fixtures['sphere']=sphere
    fixtures['sphere-nonuniform']=sphere.replace('DEF Move Translation','Scale { scaleFactor 1.3 .7 1.4 }\nDEF Move Translation')
    records=[]
    for name,scene in fixtures.items():
        source=output/(name+'.iv');source.write_text(scene)
        for kind,program in [('retained',args.retained),('current',args.current),('reference',args.reference)]:
            if program is None:continue
            env='-' if name=='direct-only' else str(args.environment.resolve())
            # '-' is interpreted by the common public consumer as no environment.
            samples='0' if name=='diagnostic-bent' else '64' if name=='ibl64' else '16'
            command=[str(program.resolve()),str(source),env,str(output/(name+'-'+kind)),samples,str(args.products.resolve())]
            result=subprocess.run(command,capture_output=True,text=True,timeout=120)
            (output/(name+'-'+kind+'.log')).write_text(result.stdout+result.stderr)
            assert result.returncode==0,(name,kind,result.stdout,result.stderr)
        for suffix in ('rgba','depth'):
            a=(output/(name+'-retained.'+suffix)).read_bytes();b=(output/(name+'-current.'+suffix)).read_bytes()
            assert a==b,(name,suffix,'callback mismatch')
            row=dict(fixture=name,buffer=suffix,callback_equal=True,sha256=hashlib.sha256(a).hexdigest())
            if args.reference:
                c=(output/(name+'-reference.'+suffix)).read_bytes()
                row.update(reference_equal=a==c)
                if suffix=='rgba':
                    row['reference_max']=max(abs(x-y) for x,y in zip(a,c));row['reference_changed_channels']=sum(x!=y for x,y in zip(a,c))
                assert a==c,(name,suffix,'pinned mismatch',row)
            records.append(row)
    for kind,extra in [('retained',[]),('current',['--current'])]:
        result=subprocess.run([str(args.updates.resolve()),str(output/'normal-and-maps.iv'),str(args.environment.resolve()),str(output/('updates-'+kind)),*extra],capture_output=True,text=True,timeout=120)
        (output/('updates-'+kind+'.log')).write_text(result.stdout+result.stderr)
        assert result.returncode==0,(kind,result.stdout,result.stderr)
    for step in range(9):
        for suffix in ('rgba','depth'):
            assert (output/f'updates-retained-{step}.{suffix}').read_bytes()==(output/f'updates-current-{step}.{suffix}').read_bytes(),(step,suffix)
    result=dict(status='PASS',fixtures=len(fixtures),comparisons=records,exact_update_buffers=18)
    (output/'results.json').write_text(json.dumps(result,indent=2)+'\n')
    print('PASS:',len(fixtures),'anisotropy fixtures;',len(records),'buffers; 18 update buffers')
if __name__=='__main__':main()
