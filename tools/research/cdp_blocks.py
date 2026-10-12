"""Per-sub-block statistics for a range of DATA.CDP container entries: size range, nested-container count, leading words.

Usage: python tools/research/cdp_blocks.py FIRST LAST NBLOCKS [--dir out/cdp/DATA]   (folder written by tools/cdp_extract.py)
"""
import struct,collections,sys,os
DIR=sys.argv[sys.argv.index('--dir')+1] if '--dir' in sys.argv else os.path.join('out','cdp','DATA')
def blocks(i):
    b=open(os.path.join(DIR,f"{i:04d}.bin"),'rb').read()
    n=struct.unpack_from('<I',b,0)[0]; offs=list(struct.unpack_from(f'<{n}I',b,4))+[len(b)]
    return b,[(offs[k],offs[k+1]-offs[k]) for k in range(n)]
rng=range(int(sys.argv[1]),int(sys.argv[2])+1); nb=int(sys.argv[3])
stats=[collections.Counter() for _ in range(nb)]; sizes=[[] for _ in range(nb)]; heads=[collections.Counter() for _ in range(nb)]
for i in rng:
    b,bl=blocks(i)
    for k,(o,s) in enumerate(bl):
        sizes[k].append(s); heads[k][b[o:o+4].hex()]+=1
        w=struct.unpack_from('<I',b,o)[0] if s>=4 else None
        if s>=8:
            c=w
            if 0<c<0x1000 and 4+4*c<=s:
                offs=struct.unpack_from(f'<{c}I',b,o+4)
                if all(offs[j]<=offs[j+1] for j in range(c-1)) and offs[0]>=4+4*c and offs[-1]<=s: stats[k]['container']+=1
for k in range(nb):
    s=sizes[k]
    if not s: continue
    print(f"{k:2d}: size min {min(s):9,} max {max(s):9,} zero {s.count(0):3d}  container {stats[k]['container']:3d}/{len(s)}  heads {heads[k].most_common(3)}")
