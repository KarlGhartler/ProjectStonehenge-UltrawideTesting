"""Audit Ghidra function boundaries against the .eh_frame FDEs in the ELF (pc_begin, pc_range).

Usage: python tools/research/fdeaudit.py   (needs out/research/funcs.json)
Prints FDE functions missing in Ghidra and boundary mismatches; writes out/research/fdeaudit.json.
"""
import json,struct,sys,bisect
from common import elf, work, load_json
D=elf().data
ph=struct.unpack_from('<I',D,0x1C)[0]; n=struct.unpack_from('<H',D,0x2C)[0]
segs=[struct.unpack_from('<8I',D,ph+i*32) for i in range(n)]
d=load_json('funcs.json')
allf={}
for part in d.values():
    if isinstance(part,dict):
        for k,v in part.items():
            if isinstance(v,dict) and 'body_start' in v: allf[int(k,16)]=(int(v['body_start'],16),int(v['body_end'],16))
R=sorted((lo,hi,k) for k,(lo,hi) in allf.items()); starts=[r[0] for r in R]
def cont(a):
    i=bisect.bisect_right(starts,a)-1
    return R[i] if i>=0 and R[i][0]<=a<=R[i][1] else None
fdes=[]
for t,off,va,pa,fs,ms,fl,al in segs:
    if t!=1: continue
    for a in range(va+8,va+fs-4,4):
        if 0x00100000<=a<0x00297C00: continue
        pc=struct.unpack_from('<I',D,off+a-va)[0]
        if not (0x00100000<=pc<0x00297C00): continue
        ln=struct.unpack_from('<I',D,off+a-8-va)[0]; cie=struct.unpack_from('<I',D,off+a-4-va)[0]; rng=struct.unpack_from('<I',D,off+a+4-va)[0]
        if ln%4==0 and 8<=ln<0x400 and 0<cie<0x100000 and 0<rng<0x20000 and rng%4==0:
            # CIE check: the cie field holds the distance back to a CIE whose id word is 0
            ciea=a-4-cie
            try: cid=struct.unpack_from('<I',D,off+ciea+4-va)[0]
            except Exception: continue
            if cid==0: fdes.append((pc,rng,a))
fdes=sorted(set(fdes))
print('FDEs',len(fdes),'covering game range',sum(1 for p,r,a in fdes if 0x001000D8<=p<0x002616E8))
missing=[];mis=[];ok=0
for pc,rng,a in fdes:
    c=cont(pc)
    if c is None: missing.append((pc,rng,a)); continue
    if c[2]!=pc: mis.append((pc,rng,'inside',c[2]))
    elif c[1]-c[0]+1!=rng: mis.append((pc,rng,'size',c[1]-c[0]+1))
    else: ok+=1
print('match',ok,'missing',len(missing),'mismatch',len(mis))
json.dump({'fdes':fdes,'missing':missing,'mismatch':mis},open(work('fdeaudit.json'),'w'))
for x in missing: print(' MISSING %08X size %#x (fde %08X)'%x)
for x in mis: print(' MISMATCH %08X fde size %#x %s %s'%(x[0],x[1],x[2],hex(x[3])))
