#!/usr/bin/env python3
"""Serial exact IBL comparison; scene capture and upload reuse are asserted by fixtures."""
import argparse
import csv
import hashlib
import json
import math
from pathlib import Path
import struct
import subprocess


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--current',type=Path,required=True)
    p.add_argument('--retained',type=Path,required=True)
    p.add_argument('--reference',type=Path)
    p.add_argument('--environment',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();root=a.output.resolve();root.mkdir(parents=True,exist_ok=True)
    names=[]
    for name,exe in [('current',a.current),('retained',a.retained),('reference',a.reference)]:
        if exe is None:continue
        dest=root/name;dest.mkdir(exist_ok=True)
        with (root/(name+'.log')).open('w') as log:
            subprocess.run([str(exe.resolve()),str(dest),str(a.environment.resolve())],
                           stdout=log,stderr=subprocess.STDOUT,timeout=180,check=True)
        with (dest/'ibl.tsv').open() as f:rows=list(csv.DictReader(f,delimiter='\t'))
        if len(rows)!=12 or any(row['width']!='1200' or row['height']!='840' for row in rows):
            raise RuntimeError(f'Invalid IBL metadata: {name}')
        if name!='reference' and any(row['environment_uploads']!='1' for row in rows):
            raise RuntimeError(f'IBL reupload: {name}')
        if name=='retained' and any(row[k]!='1' for row in rows for k in ['captures','plans','transport_builds']):
            raise RuntimeError('IBL duplicated capture/plan/transport')
        names.append(name)
    expected={f'{scene}-{state}.{kind}' for scene in ['materials','cad']
              for state in ['base','rotated','dim','off','exposure'] for kind in ['rgba','depth']}
    for name in names:
        actual={f.name for f in (root/name).iterdir() if f.suffix in ['.rgba','.depth']}
        if actual!=expected:raise RuntimeError(f'Incomplete IBL files: {name}')
    hashes={}
    for name in sorted(expected):
        payloads=[(root/control/name).read_bytes() for control in names]
        if any(len(payload)!=1200*840*4 for payload in payloads):raise RuntimeError(f'Invalid IBL buffer length: {name}')
        if any(payload!=payloads[0] for payload in payloads[1:]):
            if name.endswith('.rgba'):
                for control,payload in zip(names[1:],payloads[1:]):
                    differences=[abs(x-y) for x,y in zip(payloads[0],payload)]
                    print(control,name,'different bytes',sum(v!=0 for v in differences),'max',max(differences),flush=True)
            raise RuntimeError(f'Exact IBL comparison failed: {name}')
        if name.endswith('.depth'):
            depth=struct.unpack('<'+'f'*(1200*840),payloads[0])
            if not all(math.isfinite(v) and 0<=v<=1 for v in depth) or not any(v<1 for v in depth):
                raise RuntimeError(f'Invalid IBL depth: {name}')
        hashes[name]=hashlib.sha256(payloads[0]).hexdigest()
    report={'status':'PASS','paths':names,'environment_sha256':hashlib.sha256(a.environment.read_bytes()).hexdigest(),
            'scenes':['materials','cad'],'states':['base','rotated','dim','off','exposure'],
            'dimensions':[1200,840],'exact_buffers':len(expected),'sha256':hashes,
            'retained_reuse':{'captures':1,'plans':1,'transport_builds':1,'environment_uploads':1},
            'assertions':['Frozen source survives deletion','Scalar edits preserve depth','Intensity zero invents no lighting',
                          'Invalid parameters preserve configuration/publication','Restoration is byte-identical']}
    (root/'comparison.json').write_text(json.dumps(report,indent=2)+'\n')
    print(f'IBL PASS: 2 scenes, 5 states, {len(expected)} exact buffers, {len(names)} paths',flush=True)

if __name__=='__main__':main()
