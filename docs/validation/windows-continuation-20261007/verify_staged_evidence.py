import hashlib, json, pathlib, subprocess
SOURCE=pathlib.Path(r'C:\Users\Diklu\.codex\worktrees\coin-render-windows-20261007\coin')
PREFIX='docs/validation/windows-continuation-20261007/'
manifest=json.loads((SOURCE/PREFIX/'evidence-sha256.json').read_text())
index=subprocess.check_output(['git','ls-files','--stage','--',PREFIX],cwd=SOURCE,text=True)
entries={}
for line in index.splitlines():
    metadata,path=line.split('\t',1)
    mode,oid,stage=metadata.split()
    assert stage=='0' and mode=='100644',line
    entries[path]=oid
paths=[PREFIX+name.replace('\\','/') for name in manifest]
assert set(entries)==set(paths+[PREFIX+'evidence-sha256.json']), 'Missing or unexpected staged artifact'
oids=[entries[path] for path in paths]
result=subprocess.run(['git','cat-file','--batch'],cwd=SOURCE,
    input=('\n'.join(oids)+'\n').encode(),stdout=subprocess.PIPE,stderr=subprocess.PIPE,check=True)
offset=0
for name,oid in zip(manifest,oids):
    end=result.stdout.index(b'\n',offset)
    actual,kind,size=result.stdout[offset:end].decode().split()
    assert actual==oid and kind=='blob'
    offset=end+1
    data=result.stdout[offset:offset+int(size)]
    assert hashlib.sha256(data).hexdigest()==manifest[name], name+' staged bytes differ'
    offset+=int(size)
    assert result.stdout[offset:offset+1]==b'\n'
    offset+=1
assert offset==len(result.stdout)
print('Verified',len(manifest),'staged artifact blobs against SHA-256')
