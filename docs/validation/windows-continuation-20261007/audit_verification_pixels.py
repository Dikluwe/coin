import collections, hashlib, json, pathlib
ROOT=pathlib.Path(__file__).resolve().parent/'performance/verify'
rows=json.loads((ROOT/'results.json').read_text())
audit=[]
for row in rows:
    frames=[]
    for name, digest in row['images'].items():
        data=(ROOT/name).read_bytes()
        header,width_height,maximum,pixels=data.split(b'\n',3)
        assert header==b'P6' and width_height==b'256 256' and maximum==b'255'
        assert len(pixels)==256*256*3 and hashlib.sha256(data).hexdigest()==digest
        colors=collections.Counter(zip(pixels[::3],pixels[1::3],pixels[2::3]))
        background=tuple(pixels[:3])
        nonbackground=256*256-colors[background]
        assert nonbackground>1000, (name,'empty or almost empty scene')
        frames.append(dict(file=name,background=background,nonbackground_pixels=nonbackground,distinct_colors=len(colors)))
    unique=len(set(row['images'].values()))
    if row['case']!='static': assert unique>1, (row['stem'],'animation did not change pixels')
    audit.append(dict(case=row['case'],variant=row['variant'],unique_frames=unique,frames=frames))
(ROOT/'pixel-audit.json').write_text(json.dumps(audit,indent=2))
print('Audited',len(audit),'processes; all frames nonempty and all dynamic cases changed pixels')
for case in sorted(set(r['case'] for r in audit)):
    selected=[r for r in audit if r['case']==case]
    print(case,'unique frames',sorted(set(r['unique_frames'] for r in selected)))
