"""Offline scan of game code for architecture questions: per-category instruction counts (lq/sq, COP2, MMI, COP0,
syscall, jalr, lui of hardware ranges) and memory accesses through lui bases to EE/GS/VU registers and scratchpad.

Usage: python tools/research/hwscan.py [--extracted DIR]
"""
import collections
from scan import W, starts, S, fn, LO, HI, TXT_HI
cats=collections.defaultdict(collections.Counter)  # cat -> func -> count
# track lui register values for simple base tracking
for a in range(LO,HI,4):
    w=W[a];op=w>>26;rs=(w>>21)&31;rt=(w>>16)&31;im=w&0xffff
    if op==0xf:
        if im in(0x1000,0x1001): cats['lui 0x1000/0x1001 (EE timers/IPU/GIF/VIF regs)'][fn(a)]+=1
        elif 0x1000<im<0x1100: cats['lui 0x10xx (DMAC/INTC/other EE regs)'][fn(a)]+=1
        elif im in(0x1100,):   cats['lui 0x1100 (VU0/VU1 mem)'][fn(a)]+=1
        elif 0x1100<im<0x1200: cats['lui 0x11xx (VU mem/VIF)'][fn(a)]+=1
        elif im==0x1200: cats['lui 0x1200 (GS privileged regs)'][fn(a)]+=1
        elif im==0x7000: cats['lui 0x7000 (scratchpad)'][fn(a)]+=1
        elif im in(0x2000,0x3000): cats['lui 0x2000/0x3000 (uncached / uncached-accel)'][fn(a)]+=1
    if op==0x1e: cats['lq'][fn(a)]+=1
    if op==0x1f: cats['sq'][fn(a)]+=1
    if op==0x12: cats['COP2 (VU0 macro)'][fn(a)]+=1
    if op==0x36 : cats['lqc2'][fn(a)]+=1
    if op==0x3e : cats['sqc2'][fn(a)]+=1
    if op==0x1c: cats['MMI'][fn(a)]+=1
    if op==0x10: cats['COP0'][fn(a)]+=1
    if op==0x0d and im==0xfffffff&0xffff: pass
    if op==0 and (w&63)==0x0c: cats['syscall'][fn(a)]+=1
    if op==0 and (w&63)==9: cats['jalr'][fn(a)]+=1
print("approx functions in game code:",len(S))
for c,ctr in sorted(cats.items()):
    print(f"{c}: {sum(ctr.values())} instr in {len(ctr)} funcs; top: "+", ".join(f"{f:08X}x{n}" for f,n in ctr.most_common(8)))

# --- refined: real memory accesses through a lui base ---
LS={0x20:'lb',0x21:'lh',0x23:'lw',0x24:'lbu',0x25:'lhu',0x27:'lwu',0x37:'ld',0x1e:'lq',0x28:'sb',0x29:'sh',0x2b:'sw',0x3f:'sd',0x1f:'sq',0x31:'lwc1',0x39:'swc1',0x36:'lqc2',0x3e:'sqc2'}
REG=collections.defaultdict(collections.Counter); REGF=collections.defaultdict(set)
for a in range(LO,HI,4):
    w=W[a]
    if w>>26!=0xf: continue
    im=w&0xffff; rt=(w>>16)&31
    if not(0x1000<=im<0x1300 or im==0x7000): continue
    for k in range(1,10):
        x=a+4*k; v=W.get(x,0); op=v>>26; rs=(v>>21)&31; rt2=(v>>16)&31; si=(v&0xffff)-0x10000 if v&0x8000 else v&0xffff
        if op in LS and rs==rt:
            ea=((im<<16)+si)&0xffffffff
            region = 'scratchpad' if im==0x7000 else f'{ea:08X}'
            REG[region][LS[op]]+=1; REGF[region].add(fn(a))
        # stop if rt overwritten
        dst = rt2 if op in(9,0xd,0xf,0xc,0xa,0xb,0x23,0x24,0x25,0x20,0x21,0x37,0x1e,0x27) else ((v>>11)&31 if op==0 else None)
        if dst==rt and not(op in LS and op in(0x28,0x29,0x2b,0x3f,0x1f,0x39,0x3e)): break
        if v>>26 in(2,3) or (op==0 and (v&63) in(8,9)): break
NAMES={0x10000000:'T0_COUNT',0x10000010:'T0_MODE',0x10000020:'T0_COMP',0x10000800:'T1_COUNT',0x10000810:'T1_MODE',0x10001000:'T2_COUNT',0x10001800:'T3_COUNT',
0x10002000:'IPU_CMD',0x10002010:'IPU_CTRL',0x10003000:'GIF_CTRL',0x10003020:'GIF_STAT',0x10003C00:'VIF1_STAT',0x10003800:'VIF0_STAT',0x10007000:'VIF0_FIFO',0x10007010:'VIF1_FIFO',0x10006000:'GIF_FIFO',
0x10008000:'D0_CHCR (VIF0)',0x10008010:'D0_MADR',0x10008020:'D0_QWC',0x10008030:'D0_TADR',0x10009000:'D1_CHCR (VIF1)',0x10009010:'D1_MADR',0x10009020:'D1_QWC',0x10009030:'D1_TADR',
0x1000A000:'D2_CHCR (GIF)',0x1000A010:'D2_MADR',0x1000A020:'D2_QWC',0x1000A030:'D2_TADR',0x1000D000:'D8_CHCR (fromSPR)',0x1000D010:'D8_MADR',0x1000D020:'D8_QWC',0x1000D080:'D8_SADR',
0x1000D400:'D9_CHCR (toSPR)',0x1000D410:'D9_MADR',0x1000D420:'D9_QWC',0x1000D480:'D9_SADR',0x1000E000:'D_CTRL',0x1000E010:'D_STAT',0x1000F000:'INTC_STAT',0x1000F010:'INTC_MASK',
0x12000000:'GS PMODE',0x12001000:'GS CSR',0x12001010:'GS IMR',0x11000000:'VU0 micro',0x11004000:'VU0 data',0x11008000:'VU1 micro',0x1100C000:'VU1 data'}
print("\n== real accesses through lui bases (game code) ==")
for r,c in sorted(REG.items(),key=lambda kv:-sum(kv[1].values())):
    ea=int(r,16) if r!='scratchpad' else None
    print(f"{r:10s} {NAMES.get(ea,''):18s} {sum(c.values()):5d} acc in {len(REGF[r]):3d} funcs  {dict(c)}  e.g. "+", ".join(f'{f:08X}' for f in sorted(REGF[r])[:6]))
allhw=set()
for r,fs in REGF.items():
    if r!='scratchpad': allhw|=fs
print("functions touching EE/GS/VU hardware regs directly:",len(allhw))
print("functions touching scratchpad via lui 0x7000:",len(REGF['scratchpad']))
