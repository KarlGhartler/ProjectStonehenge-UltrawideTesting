"""Minimal offline R5900 disassembler for quick reads (integer, branch, load/store, COP1 single
precision and lwc1/swc1/lqc2/sqc2; other COP2, COP0 and MMI shown raw).

Usage: python tools/research/dis.py ADDR COUNT [--extracted DIR]
For VU0 macro, MMI and COP1 details use Ghidra; never disassemble .vutext/.vudata/.DVP.overlay.
"""
import sys
from common import elf
R=["zero","at","v0","v1","a0","a1","a2","a3","t0","t1","t2","t3","t4","t5","t6","t7","s0","s1","s2","s3","s4","s5","s6","s7","t8","t9","k0","k1","gp","sp","fp","ra"]
I={9:"addiu",0xb:"sltiu",0xa:"slti",0xc:"andi",0xd:"ori",0xe:"xori",0x23:"lw",0x2b:"sw",0x21:"lh",0x25:"lhu",0x29:"sh",0x20:"lb",0x24:"lbu",0x28:"sb",0x37:"ld",0x3f:"sd",0x1e:"lq",0x1f:"sq",0x31:"lwc1",0x39:"swc1",0x27:"lwu",0x19:"daddiu"}
def dec(a,w):
    op=w>>26;rs=R[(w>>21)&31];rt=R[(w>>16)&31];rd=R[(w>>11)&31];im=w&0xffff;si=im-0x10000 if im&0x8000 else im;sh=(w>>6)&31
    if w==0:return "nop"
    if op==3:return f"jal {(w&0x3ffffff)<<2:08X}"
    if op==2:return f"j {(w&0x3ffffff)<<2:08X}"
    if op==0:
        f=w&63
        n={8:"jr",9:"jalr",0x21:"addu",0x2d:"daddu",0x25:"or",0x24:"and",0x2a:"slt",0x2b:"sltu",0x23:"subu",0x26:"xor",0x27:"nor",0x0c:"syscall",0x18:"mult",0x1a:"div",0x10:"mfhi",0x12:"mflo",0x0d:"break",0x0b:"movn",0x0a:"movz",0x04:"sllv",0x06:"srlv",0x07:"srav",0x14:"dsllv",0x16:"dsrlv",0x2f:"dsubu"}.get(f)
        if f in(0,2,3,0x38,0x3a,0x3b,0x3c,0x3e,0x3f):return f"{ {0:'sll',2:'srl',3:'sra',0x38:'dsll',0x3a:'dsrl',0x3b:'dsra',0x3c:'dsll32',0x3e:'dsrl32',0x3f:'dsra32'}[f]} {rd},{rt},{sh}"
        if f==8:return f"jr {rs}"
        if f==9:return f"jalr {rd},{rs}"
        if f==0xc:return "syscall"
        if n:return f"{n} {rd},{rs},{rt}"
        return f"special.{f:02x} {w:08X}"
    if op in(4,5,0x14,0x15):return f"{ {4:'beq',5:'bne',0x14:'beql',0x15:'bnel'}[op]} {rs},{rt},{a+4+si*4:08X}"
    if op in(6,7,0x16,0x17):return f"{ {6:'blez',7:'bgtz',0x16:'blezl',0x17:'bgtzl'}[op]} {rs},{a+4+si*4:08X}"
    if op==1:return f"regimm.{(w>>16)&31:x} {rs},{a+4+si*4:08X}"
    if op==0xf:return f"lui {rt},{im:#x}"
    if op==0x10:return f"cop0 {w:08X}"
    if op==0x11:return cop1(a,w)
    if op==0x12:return f"cop2 {w:08X}"
    if op==0x1c:return f"mmi {w:08X}"
    if op in(0x31,0x39):return f"{'lwc1' if op==0x31 else 'swc1'} f{(w>>16)&31},{si:#x}({rs})"
    if op in(0x36,0x3e):return f"{'lqc2' if op==0x36 else 'sqc2'} vf{(w>>16)&31},{si:#x}({rs})"
    if op in(9,0xb,0xa,0xc,0xd,0xe,0x19):return f"{I[op]} {rt},{rs},{si:#x}"
    if op in I:return f"{I[op]} {rt},{si:#x}({rs})"
    return f"op{op:02x} {w:08X}"
def cop1(a,w):
    fmt=(w>>21)&31;ft=(w>>16)&31;fs=(w>>11)&31;fd=(w>>6)&31;f=w&63;im=w&0xffff;si=im-0x10000 if im&0x8000 else im
    if fmt==0:return f"mfc1 {R[ft]},f{fs}"
    if fmt==4:return f"mtc1 {R[ft]},f{fs}"
    if fmt==2:return f"cfc1 {R[ft]},{fs}"
    if fmt==6:return f"ctc1 {R[ft]},{fs}"
    if fmt==8:return f"{['bc1f','bc1t','bc1fl','bc1tl'][ft&3]} {a+4+si*4:08X}"
    if fmt==0x14 and f==0x20:return f"cvt.s.w f{fd},f{fs}"
    if fmt==0x10:
        n={0:"add.s",1:"sub.s",2:"mul.s",3:"div.s",4:"sqrt.s",5:"abs.s",6:"mov.s",7:"neg.s",0x16:"rsqrt.s",0x18:"adda.s",0x19:"suba.s",0x1a:"mula.s",0x1c:"madd.s",0x1d:"msub.s",0x1e:"madda.s",0x1f:"msuba.s",0x24:"cvt.w.s",0x28:"max.s",0x29:"min.s",0x30:"c.f.s",0x32:"c.eq.s",0x34:"c.lt.s",0x36:"c.le.s"}.get(f)
        if n is None:return f"cop1 {w:08X}"
        if f in(4,5,6,7,0x24):return f"{n} f{fd},f{fs}"
        if f in(0x18,0x19,0x1a,0x1e,0x1f) or f>=0x30:return f"{n} f{fs},f{ft}"
        return f"{n} f{fd},f{fs},f{ft}"
    return f"cop1 {w:08X}"
if __name__=="__main__":
    a=int(sys.argv[1],16);n=int(sys.argv[2],0)
    for i in range(n):
        x=a+4*i;print(f"{x:08X}: {dec(x,elf().u32(x))}")
