"""Reachability of game functions from main over the raw call graph.

Edges: jal/j sites (owner = nearest function entry below), lui+addiu/ori builds of function addresses in code.
Roots: entry point, main, every data word pointing at a function except .eh_frame FDE pc_begin fields.
Usage: python tools/research/reach.py   (needs out/research/funcs.json)
Output: out/research/reach.json (reachable set, data roots, roots, call edges); prints functions whose coverage status disagrees (unreachable but not dead, or reachable but dead).
"""
import sys,struct,json,bisect,csv
from common import elf, work, load_json, COVERAGE
D=elf().data
d=load_json('funcs.json')['game']
R=sorted((int(k,16),int(v['body_end'],16),int(k,16)) for k,v in d.items())  # entry-based ownership
starts=[r[0] for r in R]; F={r[2]:r for r in R}
def owner(a):
    i=bisect.bisect_right(starts,a)-1
    return R[i][2] if i>=0 else None   # body-less code (switch cases) -> nearest preceding function
ph=struct.unpack_from('<I',D,0x1C)[0]; n=struct.unpack_from('<H',D,0x2C)[0]
segs=[struct.unpack_from('<8I',D,ph+i*32) for i in range(n)]
def rd(a):
    for t,off,va,pa,fs,ms,fl,al in segs:
        if t==1 and va<=a<va+fs: return struct.unpack_from('<I',D,off+a-va)[0]
    return None
edges={}; roots=set([0x00100008,0x001001F0]); fde=0; dataroots={}
for t,off,va,pa,fs,ms,fl,al in segs:
    if t!=1: continue
    hi={}  # reg -> (value, addr)
    for a in range(va,va+fs,4):
        w=struct.unpack_from('<I',D,off+a-va)[0]
        incode=0x00100000<=a<0x00297C00
        if incode:
            op=w>>26
            if op in (2,3):
                tg=(w&0x3ffffff)<<2
                if tg in F: edges.setdefault(owner(a),set()).add(tg)
            if op==0xF: hi[(w>>16)&31]=((w&0xffff)<<16,a)
            elif op in (9,0xD):  # addiu / ori
                rs=(w>>21)&31; rt=(w>>16)&31
                if rs in hi and a-hi[rs][1]<=24:
                    lo=w&0xffff
                    v=hi[rs][0]+((lo-0x10000) if (op==9 and lo&0x8000) else lo)
                    v&=0xffffffff
                    if v in F and v!=owner(a): edges.setdefault(owner(a),set()).add(v)
        else:
            if w in F:
                rng=rd(a+4); ln=rd(a-8) or 0; cie=rd(a-4) or 0
                if rng is not None and 0<rng<0x20000 and ln%4==0 and 8<=ln<0x400 and 0<cie<0x100000 and rd(a-4-cie+4)==0: fde+=1; continue
                roots.add(w); dataroots.setdefault(w,[]).append(a)
seen=set(); st=list(roots)
while st:
    x=st.pop()
    if x in seen or x is None: continue
    seen.add(x); st+=list(edges.get(x,()))
cov={int(r['address'],16):r for r in csv.DictReader(open(COVERAGE,encoding='utf-8'))}
game=[a for a in F if 0x001000D8<=a<0x002616E8]
unreach=[a for a in sorted(game) if a not in seen]
json.dump({'reachable':sorted(seen),'dataroots':{f'{k:08X}':v for k,v in dataroots.items()},
           'roots':sorted(r for r in roots if r is not None),
           'edges':{f'{k:08X}':sorted(v) for k,v in edges.items() if k is not None}},open(work('reach.json'),'w'))
print('fde refs skipped',fde,'data roots',len(dataroots),'reachable game',sum(1 for a in game if a in seen),'of',len(game))
bad=[a for a in unreach if cov.get(a,{}).get('status')!='dead']
print('unreachable but not dead:',len(bad))
for a in bad: print(f"  {a:08X} {cov[a]['subsystem']}/{cov[a]['status']} {cov[a]['note'][:80]}")
live=[a for a in game if a in seen and cov.get(a,{}).get('status')=='dead']
print('reachable but marked dead:',len(live))
for a in live: print(f"  {a:08X} {cov[a]['note'][:90]}")
