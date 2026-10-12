"""List code outside every Ghidra function body in the game range (gaps), with the first instruction and whether the
preceding code ends with a return/jump (clean function boundary) or may fall through.

Usage: python tools/research/gapscan.py   (needs out/research/funcs.json)
The 33 copies of the g++ STL char-compare pair are skipped (addresses in docs/port/dead-code.md).
"""
import json,struct,sys
from common import elf, load_json
D=elf().data
ph=struct.unpack_from('<I',D,0x1C)[0]; n=struct.unpack_from('<H',D,0x2C)[0]
segs=[struct.unpack_from('<8I',D,ph+i*32) for i in range(n)]
def rd(a):
    for t,off,va,pa,fs,ms,fl,al in segs:
        if t==1 and va<=a<va+fs: return struct.unpack_from('<I',D,off+a-va)[0]
d=load_json('funcs.json')['game']
R=sorted((int(v['body_start'],16),int(v['body_end'],16),int(k,16)) for k,v in d.items())
E={int(k,16) for k in d}
stl={0x00109F60,0x0010AE40,0x0010C770,0x0011B770,0x0011E7A8,0x00127AD8,0x0012A3B8,0x0012FB38,0x00135358,0x00135B80,0x0013EB38,0x001469E0,0x00149EE8,0x00154488,0x00155AF8,0x0016EBF8,0x00170E98,0x00179808,0x00184758,0x00184D20,0x00186AA8,0x0019F448,0x001A4028,0x001BDA08,0x001D7ED0,0x001E0538,0x001E42B0,0x001E91F0,0x001FED58,0x002105A0,0x002111A8,0x00211AB8,0x001DAA48}
def isret(w): return w==0x03E00008 or (w>>26)==2 or (w&0xFC1FFFFF)==0x00000008
c=[]
for (lo,hi,k),(lo2,hi2,k2) in zip(R,R[1:]):
    s,e=hi+1,lo2-1
    if not (0x001000D8<=lo<0x002616E8) or e-s+1<=4: continue
    if any(s<=x<=e for x in E): continue
    a=s
    while a<=e and rd(a)==0: a+=4
    if a>e or a in stl: continue
    b=a-4
    while rd(b)==0: b-=4
    c.append((hex(a),e-a+1,'clean' if (isret(rd(b-4)) or isret(rd(b))) else 'fallthrough?', hex(rd(a))))
print(len(c)); [print(x) for x in c]
