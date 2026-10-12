"""Static GS-usage scan of game code: A+D register ids written at qword+8 (constant-propagated), by number of functions.

Usage: python tools/research/gs_scan.py [--extracted DIR]
"""
import collections
from scan import W, starts, S, fn, LO, HI, TXT_HI
GSREG={0x00:'PRIM',0x01:'RGBAQ',0x02:'ST',0x03:'UV',0x04:'XYZF2',0x05:'XYZ2',0x06:'TEX0_1',0x07:'TEX0_2',0x08:'CLAMP_1',0x09:'CLAMP_2',0x0A:'FOG',
0x0C:'XYZF3',0x0D:'XYZ3',0x14:'TEX1_1',0x15:'TEX1_2',0x16:'TEX2_1',0x17:'TEX2_2',0x18:'XYOFFSET_1',0x19:'XYOFFSET_2',0x1A:'PRMODECONT',0x1B:'PRMODE',
0x1C:'TEXCLUT',0x22:'SCANMSK',0x34:'MIPTBP1_1',0x35:'MIPTBP1_2',0x36:'MIPTBP2_1',0x37:'MIPTBP2_2',0x3B:'TEXA',0x3D:'FOGCOL',0x3F:'TEXFLUSH',
0x40:'SCISSOR_1',0x41:'SCISSOR_2',0x42:'ALPHA_1',0x43:'ALPHA_2',0x44:'DIMX',0x45:'DTHE',0x46:'COLCLAMP',0x47:'TEST_1',0x48:'TEST_2',0x49:'PABE',
0x4A:'FBA_1',0x4B:'FBA_2',0x4C:'FRAME_1',0x4D:'FRAME_2',0x4E:'ZBUF_1',0x4F:'ZBUF_2',0x50:'BITBLTBUF',0x51:'TRXPOS',0x52:'TRXREG',0x53:'TRXDIR',
0x54:'HWREG',0x60:'SIGNAL',0x61:'FINISH',0x62:'LABEL'}
regs=collections.defaultdict(set); vals=collections.defaultdict(collections.Counter)
C={}
SDOPS=(0x3f,)
for a in range(LO,HI,4):
    w=W[a];op=w>>26;rs=(w>>21)&31;rt=(w>>16)&31;rd=(w>>11)&31;im=w&0xffff;si=im-0x10000 if im&0x8000 else im;f=w&63
    if a in starts: C={}; last={}
    if op==9 and rs==0: C[rt]=si&0xffffffffffffffff; continue
    if op==0xd and rs==0: C[rt]=im; continue
    if op==0xf: C[rt]=(si<<16)&0xffffffffffffffff; continue
    if op==0xd and rs in C: C[rt]=C[rs]|im; continue
    if op==9 and rs in C: C[rt]=(C[rs]+si)&0xffffffffffffffff; continue
    if op==0 and f in(0x3c,0x38) and rt in C: C[rd]=(C[rt]<<(((w>>6)&31)+(32 if f==0x3c else 0)))&0xffffffffffffffff; continue
    if op==0 and f in(0x25,0x2d) and rs in C and rt in C: C[rd]=(C[rs]|C[rt]) if f==0x25 else (C[rs]+C[rt])&0xffffffffffffffff; continue
    if op in SDOPS:
        if rt in C and (si & 15)==8 and C[rt] in GSREG:
            regs[GSREG[C[rt]]].add(fn(a))
        continue
    if op==0 and f in(8,9) or op in(2,3): C={k:v for k,v in C.items() if 16<=k<=23}; continue
    if op==0: C.pop(rd,None)
    elif op in(9,0xa,0xb,0xc,0xd,0xe,0xf,0x19,0x20,0x21,0x23,0x24,0x25,0x27,0x37,0x1e): C.pop(rt,None)
    elif op==0x1c: C.pop(rd,None)
print("A+D register ids stored at qword+8 (constant), by #functions:")
for r,fs in sorted(regs.items(),key=lambda kv:-len(kv[1])): print(f"  {r:12s} {len(fs):4d}  e.g. {', '.join(f'{x:08X}' for x in sorted(fs)[:5])}")
