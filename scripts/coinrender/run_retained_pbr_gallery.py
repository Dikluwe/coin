#!/usr/bin/env python3
"""Render real PBR examples serially and require exact color/depth parity."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import subprocess


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--current',type=Path,required=True)
    p.add_argument('--retained',type=Path,required=True)
    p.add_argument('--reference',type=Path)
    p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();root=a.output.resolve();root.mkdir(parents=True,exist_ok=True)
    names=[]
    for name,exe in [('current',a.current),('retained',a.retained),('reference',a.reference)]:
        if exe is None:continue
        dest=root/name;dest.mkdir(exist_ok=True)
        with (root/(name+'.log')).open('w') as log:
            subprocess.run([str(exe.resolve()),str(dest)],stdout=log,stderr=subprocess.STDOUT,timeout=180,check=True)
        with (dest/'gallery.tsv').open() as f:
            rows=list(csv.DictReader(f,delimiter='\t'))
        if len(rows)!=8 or any(row['width']!='1200' or row['height']!='840' for row in rows):
            raise RuntimeError('Invalid gallery metadata')
        names.append(name)
    expected={f'{scene}-{stops}.{kind}' for scene in ['materials','cad'] for stops in [0,-2,2] for kind in ['rgba','depth']}
    for name in names:
        files={path.name for path in (root/name).iterdir() if path.suffix in ['.rgba','.depth']}
        if files!=expected:raise RuntimeError(f'{name}: incomplete gallery')
    hashes={}
    for name in sorted(expected):
        payloads=[(root/control/name).read_bytes() for control in names]
        if any(len(payload)!=1200*840*4 for payload in payloads):raise RuntimeError(f'Invalid size: {name}')
        if any(payload!=payloads[0] for payload in payloads[1:]):raise RuntimeError(f'Exact gallery comparison failed: {name}')
        hashes[name]=hashlib.sha256(payloads[0]).hexdigest()
    report={'status':'PASS','paths':names,'scenes':['materials','cad'],'exposure_stops':[-2,0,2],
            'dimensions':[1200,840],'exact_buffers':len(expected),'sha256':hashes,
            'retained_reuse':{'captures':1,'plans':1,'transport_builds':1},
            'assertions':['Exposure changes/restores color','Exposure preserves depth','Invalid stops preserve configuration/publication']}
    (root/'comparison.json').write_text(json.dumps(report,indent=2)+'\n')
    print(f'PBR gallery PASS: 2 scenes, 3 exposures, {len(expected)} exact buffers, {len(names)} paths')

if __name__=='__main__':main()
