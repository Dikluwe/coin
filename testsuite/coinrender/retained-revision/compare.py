import json, pathlib, math, hashlib, struct, argparse, sys
from validation_common import validate_run
parser=argparse.ArgumentParser(description='Strict retained matrix comparison; nonzero on any failure or omission.')
parser.add_argument('root', type=pathlib.Path)
parser.add_argument('--backends', nargs='+', choices=('recording','bgfx','wgpu'), default=['bgfx','wgpu'])
parser.add_argument('--pipeline3', action='store_true')
args=parser.parse_args()
root=args.root
backends=args.backends
if len(set(backends))!=len(backends): parser.error('duplicate backend')
errors=[error for backend in backends for error in validate_run(root/backend,backend)]
if args.pipeline3:
    if 'bgfx' not in backends: parser.error('--pipeline3 requires bgfx')
    errors.extend(validate_run(root/'bgfx'/'pipeline3','bgfx',False))
if errors:
    failure={'passed':False,'errors':errors}
    (root/'comparison.json').write_text(json.dumps(failure,indent=2)+'\n')
    print(json.dumps(failure,indent=2))
    raise SystemExit(1)
results={'backend_pairs':[], 'analytic_oracles':[], 'negative_controls':[]}
# Pixel-center oracle authored independently of Coin capture/cache/transport.
# The camera is orthographic at (0,0,5), height 3, square 64x64 viewport.
def model(name):
    p=[[-.4,-.4],[.4,-.4],[.4,.4],[-.4,.4]]; indices=[(0,1,2),(0,2,3)]
    moves=[[-.7,0],[.7,0]];colors=[(255,0,0),(0,0,255)]
    t=int(name[-1]) if name[-1].isdigit() else None
    if t==0 or name=='snapshot-new':moves[1]=[.55,.3]
    if t==1 or name=='snapshot-new':colors[1]=(0,255,0)
    if t==2 or name=='snapshot-new':p[2]=[.1,.65]
    if t==3:indices[1]=(0,2,1)
    if name.startswith('faceset_'):
      indices=[(0,1,2)] if t!=3 else [(0,1,2),(0,2,3)]
    return p,indices,moves,colors

def bary(q,a,b,c):
    den=(b[1]-c[1])*(a[0]-c[0])+(c[0]-b[0])*(a[1]-c[1])
    u=((b[1]-c[1])*(q[0]-c[0])+(c[0]-b[0])*(q[1]-c[1]))/den
    v=((c[1]-a[1])*(q[0]-c[0])+(a[0]-c[0])*(q[1]-c[1]))/den
    return (u,v,1-u-v)

def oracle(name,data):
    p,triangles,moves,colors=model(name); checks=errors=0
    for y in range(64):
      for x in range(64):
        height=2.7 if name.endswith("4") else 3
        cameraX=.2 if name.endswith("5") else 0
        q=((x+.5)*height/64-height/2+cameraX,height/2-(y+.5)*height/64)
        candidates=[];near=False
        for move,color in zip(moves,colors):
          local=(q[0]-move[0],q[1]-move[1])
          for ids in triangles:
            w=bary(local,*[p[i] for i in ids])
            if min(w)>0.09:candidates.append(color)
            # Exclude an explicitly defined barycentric edge band from this
            # interior oracle, including points just outside a triangle.
            if min(w)>-0.09 and min(w)<=0.09:near=True
        if near:continue
        expected=candidates[-1] if candidates else (0,0,0)
        actual=tuple(data[(y*64+x)*4:(y*64+x)*4+3]);checks+=1;errors+=actual!=expected
    return checks,errors

# Explicit backend set already validated.
for b in backends:
  for path in sorted((root/b).glob('*.rgba')):
    if not (path.stem.startswith(('indexed_','faceset_')) or path.stem=='snapshot-new'):continue
    data=path.read_bytes();n,e=oracle(path.stem,data)
    results['analytic_oracles'].append({'backend':b,'scene':path.stem,'checks':n,'errors':e,'sha256':hashlib.sha256(data).hexdigest()})
for b in backends:
  for path in sorted((root/b).glob('*.depth')):
    data=path.read_bytes();values=struct.unpack('<'+str(len(data)//4)+'f',data)
    bad=sum(not math.isfinite(v) or v<0 or v>1 for v in values)
    foreground=[v for v in values if v<.999]
    results.setdefault('depth_oracles',[]).append({'backend':b,'scene':path.stem,'samples':len(values),'invalid':bad,'max_foreground_error':max((abs(v-4/9) for v in foreground),default=0),'max_quantization_bound':1/(2**24-1)})
for i,a in enumerate(backends):
  for b in backends[i+1:]:
    for p in sorted((root/a).glob('*.rgba')):
      if not (p.stem.startswith(('indexed_','faceset_')) or p.stem=='snapshot-new'):continue
      q=root/b/p.name
      if not q.exists():continue
      x,y=p.read_bytes(),q.read_bytes()
      diffs=[j for j,(u,v) in enumerate(zip(x,y)) if u!=v]
      results['backend_pairs'].append({'a':a,'b':b,'scene':p.stem,'bytes_a':len(x),'bytes_b':len(y),'differing_channels':len(diffs),'different_lengths':len(x)!=len(y),'first_offset':diffs[0] if diffs else None})
    for p in sorted((root/a).glob('*.depth')):
      q=root/b/p.name
      if not q.exists():continue
      x=struct.unpack('<'+str(len(p.read_bytes())//4)+'f',p.read_bytes());y=struct.unpack('<'+str(len(q.read_bytes())//4)+'f',q.read_bytes())
      results.setdefault('depth_pairs',[]).append({'a':a,'b':b,'scene':p.stem,'different_samples':sum(u!=v for u,v in zip(x,y)),'max_absolute_error':max((abs(u-v) for u,v in zip(x,y)),default=0),'different_lengths':len(x)!=len(y)})
    for p in sorted((root/a).glob('*.semantic')):
      q=root/b/p.name
      if not q.exists():continue
      x,y=p.read_text().splitlines(),q.read_text().splitlines()
      if x!=y:
        results['backend_pairs'].append({'a':a,'b':b,'semantic':p.stem,'mismatches':[{'line':j+1,'a':u,'b':v} for j,(u,v) in enumerate(zip(x,y)) if u!=v][:5],'lines_a':len(x),'lines_b':len(y)})
# Negative controls must reject source edits replaced with stale geometry/colors,
# a corrupted image, and a corrupted semantic vertex. Never use one backend as truth.
for b in backends:
  p=root/b/'indexed_published1.rgba'
  if p.exists():
    data=bytearray(p.read_bytes());n,e=oracle('indexed_published0',data)
    results['negative_controls'].append({'backend':b,'kind':'wrong-scene-identity','detected':e>0,'errors':e})
    # Replace every visible pixel with an impossible color to guarantee an
    # interior mismatch independently of raster edge conventions.
    for j in range(0,len(data),4):
      if any(data[j:j+3]):data[j:j+3]=b'\xff\xff\x00'
    n,e=oracle('indexed_published1',data)
    results['negative_controls'].append({'backend':b,'kind':'corrupt-image','detected':e>0,'errors':e})
  p=root/b/'indexed_published_2_incremental.semantic'
  q=root/b/'indexed_published_2_baseline.semantic'
  if p.exists() and q.exists():
    x,y=p.read_text().splitlines(),q.read_text().splitlines()
    vertices=lambda z:[l for l in z if l.startswith('V ')]
    vx,vy=vertices(x),vertices(y)
    mismatch=next((j for j,(u,v) in enumerate(zip(vx,vy)) if u!=v),None)
    import re
    log=(root/b/'audit.log').read_text()
    def receipt(label):
      m=re.search(r'^OBS '+re.escape(label)+r' (.*)$',log,re.M)
      return {k:int(v) for k,v in re.findall(r'(root|content|resources|generation|frameRevision)=(\d+)',m.group(1))} if m else {}
    results['negative_controls'].append({'backend':b,'kind':'stale-geometry-dto','detected':mismatch is not None,'first_vertex':mismatch,'expected':vx[mismatch] if mismatch is not None else None,'stale':vy[mismatch] if mismatch is not None else None,'expected_revision':receipt('indexed/published/2/incremental'),'stale_revision':receipt('indexed/published/2/baseline'),'resource':'shared authored coordinate array; two draw occurrences'})
errors=[]
if len(results['analytic_oracles'])!=21*len(backends): errors.append('incomplete color oracle set')
if len(results.get('depth_oracles',[]))!=21*len(backends): errors.append('incomplete depth oracle set')
if any(item['errors'] for item in results['analytic_oracles']): errors.append('analytic color mismatch')
if any(item['invalid'] or item['max_foreground_error']>item['max_quantization_bound'] for item in results.get('depth_oracles',[])):
    errors.append('analytic depth mismatch')
if any(item.get('differing_channels',0) or item.get('different_lengths',False) or 'semantic' in item for item in results['backend_pairs']):
    errors.append('executor color/semantic mismatch')
if any(item['different_samples'] or item['different_lengths'] for item in results.get('depth_pairs',[])):
    errors.append('executor depth mismatch')
expected_pairs=21*len(backends)*(len(backends)-1)//2
if len(results['backend_pairs'])!=expected_pairs or len(results.get('depth_pairs',[]))!=expected_pairs:
    errors.append('incomplete pair comparison set')
if len(results['negative_controls'])!=3*len(backends) or not all(item['detected'] for item in results['negative_controls']):
    errors.append('missing or undetected negative control')
if args.pipeline3:
    exact=[]
    for suffix in ('rgba','depth','semantic'):
        for path in sorted((root/'bgfx').glob('*.'+suffix)):
            candidate=root/'bgfx'/'pipeline3'/path.name
            if candidate.read_bytes()!=path.read_bytes(): errors.append('pipeline3 mismatch: '+path.name)
            else: exact.append(path.name)
    results['pipeline3']={'exact_files':exact,'passed':not any('pipeline3' in error for error in errors)}
results['errors']=errors
results['passed']=not errors
(root/'comparison.json').write_text(json.dumps(results,indent=2)+'\n')
summary={'passed':not errors,'backends':backends,'oracles':len(results['analytic_oracles']),
 'color_checks':sum(item['checks'] for item in results['analytic_oracles']),
 'oracle_errors':sum(item['errors'] for item in results['analytic_oracles']),
 'depth_samples':sum(item['samples'] for item in results['depth_oracles']),
 'negative_controls_detected':sum(item['detected'] for item in results['negative_controls']),
 'errors':errors}
(root/'comparison-summary.json').write_text(json.dumps(summary,indent=2)+'\n')
print(json.dumps(summary,indent=2))
sys.exit(1 if errors else 0)
