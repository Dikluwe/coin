#!/usr/bin/env python3
"""Compare retained PBR surfaces with callback capture and optional pinned PBR SDK.

Every renderer runs in its own process. No comparison capture enters the retained
production path. Includes scalar updates and explicit resource invalidation.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT=Path(__file__).resolve().parents[2]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('retained','current','updates','environment','output'):
        parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--reference',type=Path)
    args=parser.parse_args()
    output=args.output.resolve();output.mkdir(parents=True,exist_ok=True)
    base=(ROOT/'testsuite/coinrender/scenes/pbr-surface.iv').read_text()
    fixtures={'textured-normal':base}
    height=' '.join(f'0x{(x+y)*35:02x}' for y in range(4) for x in range(4))
    fixtures['height']=re.sub(r'BumpMap \{[^}]+\}',f'BumpMap {{ image 4 4 1 {height} }}',base)
    neutral=' '.join(['0x8080ff']*16)
    fixtures['neutral-normal']=re.sub(r'BumpMap \{[^}]+\}',f'BumpMap {{ image 4 4 3 {neutral} }}',base)
    fixtures['no-normal']=re.sub(r'BumpMap \{[^}]+\}','',base)
    fixtures['mirrored-model']=base.replace('DEF Move Translation','Scale { scaleFactor -1.3 .7 1.4 }\nDEF Move Translation')
    fixtures['nonuniform']=base.replace('DEF Move Translation','Scale { scaleFactor 1.3 .7 1.4 }\nDEF Move Translation')
    fixtures['mirrored-uv']=base.replace('scaleFactor 1.3 1.2','scaleFactor -1.3 1.2')
    fixtures['degenerate-uv']=base.replace('TextureCoordinate2 { point [ 0 0,1 0,1 1,0 1 ] }','TextureCoordinate2 { point [ 0 0,0 0,0 0,0 0 ] }')
    fixtures['cube']=re.sub(r'TextureCoordinate2 \{[^}]+\}','',base[:base.index('Coordinate3 {')])+ 'Cube {}\n}\n'
    fixtures['generated']=re.sub(r'TextureCoordinate2 \{[^}]+\}','TextureCoordinatePlane { directionS .3 .1 0 directionT 0 .4 .2 }',base)
    sphere=re.sub(r'TextureCoordinate2 \{[^}]+\}','',base[:base.index('Coordinate3 {')])+ 'Sphere { radius 1.3 }\n}\n'
    fixtures['sphere']=sphere
    fixtures['sphere-nonuniform']=sphere.replace('DEF Move Translation','Scale { scaleFactor 1.3 .7 1.4 }\nDEF Move Translation')
    normal_only=re.sub(r'(baseColor|orm|emissive)TextureUnit [0-9]+','',base)
    normal_only=re.sub(r'(DEF BaseMap )?Texture2 \{[^}]+\}','',normal_only)
    normal_only=re.sub(r'TextureCoordinate2 \{[^}]+\}','TextureCoordinatePlane { directionS .3 .1 0 directionT 0 .4 0 }',normal_only)
    fixtures['normal-only-projection']=normal_only
    fixtures['unit7']=base.replace('emissiveTextureUnit 2','emissiveTextureUnit 7').replace('TextureUnit { unit 2 }','TextureUnit { unit 7 }')
    records=[]
    for name,scene in fixtures.items():
        source=output/(name+'.iv');source.write_text(scene)
        for kind,program in [('retained',args.retained),('current',args.current),('reference',args.reference)]:
            if program is None:continue
            prefix=output/(name+'-'+kind)
            process=subprocess.run([str(program.resolve()),str(source),str(args.environment.resolve()),str(prefix)],capture_output=True,text=True,timeout=90)
            (output/(name+'-'+kind+'.log')).write_text(process.stdout+process.stderr)
            assert process.returncode==0, (name,kind,process.stdout,process.stderr)
        for suffix in ('rgba','depth'):
            a=(output/(name+'-retained.'+suffix)).read_bytes()
            b=(output/(name+'-current.'+suffix)).read_bytes()
            assert a==b, (name,suffix,'retained/callback mismatch')
            item=dict(fixture=name,buffer=suffix,bytes=len(a),sha256=hashlib.sha256(a).hexdigest(),callback_equal=True)
            if args.reference:
                c=(output/(name+'-reference.'+suffix)).read_bytes()
                differences=[abs(x-y) for x,y in zip(a,c)] if suffix=='rgba' else []
                item.update(reference_equal=a==c,reference_color_max=max(differences) if differences else None)
                # The original branch resolves tangents/normals on CPU; the
                # retained path keeps object-space buffers and resolves in VS.
                # This one curved, nonuniform case admits one 8-bit channel of
                # rounding; every depth buffer and all other fixtures are exact.
                if name=='sphere-nonuniform' and suffix=='rgba':
                    assert max(differences)<=1 and sum(v!=0 for v in differences)<=1, item
                else:
                    assert a==c, (name,suffix,'pinned reference mismatch',item.get('reference_color_max'))
            records.append(item)
    scene=output/'textured-normal.iv'
    for kind,extra in [('retained',[]),('current',['--current'])]:
        process=subprocess.run([str(args.updates.resolve()),str(scene),str(args.environment.resolve()),str(output/('updates-'+kind)),*extra],capture_output=True,text=True,timeout=90)
        (output/('updates-'+kind+'.log')).write_text(process.stdout+process.stderr)
        assert process.returncode==0,(kind,process.stdout,process.stderr)
    for step in range(10):
        for suffix in ('rgba','depth'):
            a=(output/f'updates-retained-{step}.{suffix}').read_bytes();b=(output/f'updates-current-{step}.{suffix}').read_bytes()
            assert a==b,('updates',step,suffix)
    result=dict(status='PASS',fixtures=len(fixtures),comparisons=records,exact_update_buffers=20,
                reference=str(args.reference) if args.reference else None)
    (output/'results.json').write_text(json.dumps(result,indent=2)+'\n')
    print('PASS:',len(fixtures),'surface fixtures;',len(records),'exact buffers; 20 update buffers')


if __name__=='__main__':main()
