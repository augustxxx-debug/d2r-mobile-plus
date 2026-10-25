"""Synthetic TVFS regression: adding a panel cannot move existing entries."""
import importlib.util
from pathlib import Path
import struct

spec=importlib.util.spec_from_file_location('casc_overlay',Path(__file__).resolve().parents[2]/'tools/casc_overlay.py')
c=importlib.util.module_from_spec(spec);spec.loader.exec_module(c)
def folder(name,children):
    return bytes([len(name)])+name+b'\x00\xff'+struct.pack('>I',0x80000000|len(children)+4)+children
files,_=c.trie_bytes({b'a.json':0,b'z.json':10})
tree=folder(b'data',folder(b'layouts',files))
header=bytearray(46);size=len(tree);est=b'z';cft=bytes(50);vfs=bytes(20)
header[12:36]=struct.pack('>IIIIII',46,size,47+size+50,20,47+size,50)
header[36:38]=struct.pack('>H',3);header[38:46]=struct.pack('>II',46+size,1)
original=header+tree+est+cft+vfs
record=(bytes(range(16)),100,0,200)
for additions in ({'data/layouts/3panel.json':record},{'data/layouts/3plus/panel.json':record}):
    result=c.add_paths(original,additions)
    off,n=struct.unpack('>II',result[12:20]);paths={p.decode():v for _,p,v,f,_ in c.walk_paths(result,off,off+n) if f is None}
    assert paths['data/layouts/a.json']==0 and paths['data/layouts/z.json']==10
    assert set(paths)=={'data/layouts/a.json','data/layouts/z.json'}|set(additions)
    assert result[-10:]==b'\x01'+struct.pack('>II',0,200)+(50).to_bytes(1,'big')
for conflict in ('data/layouts/another.json','data/layouts/a.json'):
    try: c.add_paths(original,{conflict:record})
    except SystemExit: pass
    else: raise AssertionError('Existing prefix collision must fail closed')

try:
    c.add_paths(original,{'data/layouts/3one.json':record,'data/layouts/3two.json':record})
except SystemExit: pass
else: raise AssertionError('New siblings with the same prefix must fail closed')

print('PASS TVFS direct-file and new-directory insertion preserves existing paths and rejects collisions')
