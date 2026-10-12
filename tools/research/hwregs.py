"""Constant-propagating scan of game code: loads/stores whose base register holds a known constant
(lui/addiu/ori/daddu), grouped by EE register, GS privileged register, VU memory, scratchpad and uncached RAM.

Usage: python tools/research/hwregs.py [--extracted DIR]
"""
import collections
from scan import W, starts, S, fn, LO, HI, TXT_HI
from hwreg_names import NAMES
LS={0x20:'lb',0x21:'lh',0x23:'lw',0x24:'lbu',0x25:'lhu',0x27:'lwu',0x37:'ld',0x1e:'lq',0x28:'sb',0x29:'sh',0x2b:'sw',0x3f:'sd',0x1f:'sq',0x31:'lwc1',0x39:'swc1',0x36:'lqc2',0x3e:'sqc2'}
def region(ea):
    if 0x10000000<=ea<0x10010000: return 'EE regs'
    if 0x11000000<=ea<0x11010000: return 'VU mem'
    if 0x12000000<=ea<0x12002000: return 'GS priv regs'
    if 0x70000000<=ea<0x70004000: return 'scratchpad'
    if 0x20000000<=ea<0x22000000 or 0x30000000<=ea<0x32000000: return 'uncached RAM'
    return None
def run(lo,hi):
    acc=collections.defaultdict(collections.Counter); fns=collections.defaultdict(set); per=collections.defaultdict(set)
    C={}
    for a in range(lo,hi,4):
        w=W[a];op=w>>26;rs=(w>>21)&31;rt=(w>>16)&31;rd=(w>>11)&31;im=w&0xffff;si=im-0x10000 if im&0x8000 else im
        if a in starts: C={}
        if op==0xf: C[rt]=im<<16; continue
        if op==9 and rs in C: C[rt]=(C[rs]+si)&0xffffffff; continue
        if op==0xd and rs in C: C[rt]=C[rs]|im; continue
        if op in LS:
            if rs in C:
                ea=(C[rs]+si)&0xffffffff; r=region(ea)
                if r:
                    nm=NAMES.get(ea,'') if r in('EE regs','GS priv regs') else ''
                    key=f"{ea:08X} {nm}" if r in('EE regs','GS priv regs','VU mem') else r
                    acc[key][LS[op]]+=1; fns[key].add(fn(a)); per[r].add(fn(a))
            if op in(0x20,0x21,0x23,0x24,0x25,0x27,0x37,0x1e,0x31,0x36) and op not in(0x31,0x36): C.pop(rt,None)
            continue
        if op==0 and (w&63) in(0x2d,0x25,0x21) and rt==0 and rs in C: C[rd]=C[rs]; continue
        if op==0 and (w&63) in(8,9) or op in(2,3): C={k:v for k,v in C.items() if 16<=k<=23 or k==28}; continue
        # generic write -> invalidate
        if op==0: C.pop(rd,None)
        elif op in(9,0xa,0xb,0xc,0xd,0xe,0x19,0x1a,0x1b): C.pop(rt,None)
        elif op==0x1c: C.pop(rd,None)
    return acc,fns,per
if __name__=="__main__":
    acc,fns,per=run(LO,HI)
    print("== game code 0x001000D8-0x002616E7 ==")
    for r,f in per.items(): print(f"{r}: {len(f)} functions")
    for k,c in sorted(acc.items(),key=lambda kv:(-len(fns[kv[0]]))):
        print(f"  {k:32s} {sum(c.values()):4d} acc {len(fns[k]):3d} funcs  {dict(c)}  e.g. "+", ".join(f'{x:08X}' for x in sorted(fns[k])[:8]))
