"""Disassemble VU microcode from SLUS_201.52 (research only; .claude/rules/vu-microcode.md).

The `.vutext` blocks are VIF packets: a DMA tag, VIF codes (FLUSHE, MPG load address and size), then
the microcode as 64-bit instruction pairs (lower word first in memory, upper word second).

Usage:
  python tools/research/vu_disasm.py ADDR [--raw QWORDS] [--base N]
    ADDR     address of a `.vutext` block (its DMA tag), or of raw microcode with --raw
    --raw Q  disassemble Q qwords (2*Q pairs) at ADDR without parsing VIF codes
    --base N micro-memory address (in pairs) of the first pair with --raw (default 0)
Output: one line per pair: micro address (pair index, hex), upper, lower; branch targets are
absolute pair addresses. Options: --extracted DIR (default: CLAUDE.local.md).
"""
import struct
import sys

from common import elf

DEST = lambda d: "".join(c for c, b in zip("xyzw", (8, 4, 2, 1)) if d & b)
BC = "xyzw"
FIELD = "xyzw"


def sx(v, bits):
    return v - (1 << bits) if v & (1 << (bits - 1)) else v


def upper(w):
    flags = "".join(f for f, b in (("I", 31), ("E", 30), ("M", 29), ("D", 28), ("T", 27)) if w >> b & 1)
    dest = DEST(w >> 21 & 15)
    ft, fs, fd = w >> 16 & 31, w >> 11 & 31, w >> 6 & 31
    op = w & 63
    tab = {0: "add", 1: "sub", 2: "madd", 3: "msub", 4: "max", 5: "mini", 6: "mul"}
    if op < 0x1C:
        name = tab[op >> 2] + BC[op & 3]
        s = f"{name}.{dest} vf{fd:02d}, vf{fs:02d}, vf{ft:02d}{BC[op & 3]}"
    elif op < 0x30:
        names = {0x1C: ("mulq", "q"), 0x1D: ("maxi", "i"), 0x1E: ("muli", "i"), 0x1F: ("minii", "i"),
                 0x20: ("addq", "q"), 0x21: ("maddq", "q"), 0x22: ("addi", "i"), 0x23: ("maddi", "i"),
                 0x24: ("subq", "q"), 0x25: ("msubq", "q"), 0x26: ("subi", "i"), 0x27: ("msubi", "i"),
                 0x28: ("add", ""), 0x29: ("madd", ""), 0x2A: ("mul", ""), 0x2B: ("max", ""),
                 0x2C: ("sub", ""), 0x2D: ("msub", ""), 0x2E: ("opmsub", ""), 0x2F: ("mini", "")}
        n, src = names.get(op, (f"?u{op:02x}", ""))
        s = f"{n}.{dest} vf{fd:02d}, vf{fs:02d}, " + (src if src else f"vf{ft:02d}")
    elif op >= 0x3C:
        sop = (w >> 6 & 31) << 2 | (w & 3)
        if sop < 0x10:
            n = ("adda", "suba", "madda", "msuba")[sop >> 2] + BC[sop & 3]
            s = f"{n}.{dest} ACC, vf{fs:02d}, vf{ft:02d}{BC[sop & 3]}"
        elif sop < 0x18:
            n = ("itof0", "itof4", "itof12", "itof15", "ftoi0", "ftoi4", "ftoi12", "ftoi15")[sop - 0x10]
            s = f"{n}.{dest} vf{ft:02d}, vf{fs:02d}"
        elif sop < 0x1C:
            s = f"mula{BC[sop & 3]}.{dest} ACC, vf{fs:02d}, vf{ft:02d}{BC[sop & 3]}"
        else:
            names = {0x1C: "mulaq", 0x1D: "abs", 0x1E: "mulai", 0x1F: "clip", 0x20: "addaq", 0x21: "maddaq",
                     0x22: "addai", 0x23: "maddai", 0x24: "subaq", 0x25: "msubaq", 0x26: "subai",
                     0x27: "msubai", 0x28: "adda", 0x29: "madda", 0x2A: "mula", 0x2C: "suba", 0x2D: "msuba",
                     0x2E: "opmula", 0x2F: "nop"}
            n = names.get(sop, f"?us{sop:02x}")
            if n == "nop":
                s = "nop"
            elif n == "abs":
                s = f"abs.{dest} vf{ft:02d}, vf{fs:02d}"
            elif n == "clip":
                s = f"clipw.xyz vf{fs:02d}, vf{ft:02d}w"
            elif n.endswith("q") or n.endswith("i"):
                s = f"{n}.{dest} ACC, vf{fs:02d}, {n[-1]}"
            else:
                s = f"{n}.{dest} ACC, vf{fs:02d}, vf{ft:02d}"
    else:
        s = f"?u{op:02x} {w:08x}"
    return (s + (f"  [{flags}]" if flags else "")), flags


def lower(w, pc):
    op7 = w >> 25
    dest = DEST(w >> 21 & 15)
    it, is_, id_ = w >> 16 & 31, w >> 11 & 31, w >> 6 & 31
    ft, fs = it, is_
    imm11 = sx(w & 0x7FF, 11)
    if op7 == 0x40:
        f = w & 63
        if f == 0x30:
            return f"iadd vi{id_:02d}, vi{is_:02d}, vi{it:02d}"
        if f == 0x31:
            return f"isub vi{id_:02d}, vi{is_:02d}, vi{it:02d}"
        if f == 0x32:
            return f"iaddi vi{it:02d}, vi{is_:02d}, {sx(id_, 5)}"
        if f == 0x34:
            return f"iand vi{id_:02d}, vi{is_:02d}, vi{it:02d}"
        if f == 0x35:
            return f"ior vi{id_:02d}, vi{is_:02d}, vi{it:02d}"
        if f >= 0x3C:
            sop = (w >> 6 & 31) << 2 | (w & 3)
            fsf, ftf = FIELD[w >> 21 & 3], FIELD[w >> 23 & 3]
            m = {0x30: f"move.{dest} vf{ft:02d}, vf{fs:02d}", 0x31: f"mr32.{dest} vf{ft:02d}, vf{fs:02d}",
                 0x34: f"lqi.{dest} vf{ft:02d}, (vi{is_:02d}++)", 0x35: f"sqi.{dest} vf{fs:02d}, (vi{it:02d}++)",
                 0x36: f"lqd.{dest} vf{ft:02d}, (--vi{is_:02d})", 0x37: f"sqd.{dest} vf{fs:02d}, (--vi{it:02d})",
                 0x38: f"div Q, vf{fs:02d}{fsf}, vf{ft:02d}{ftf}", 0x39: f"sqrt Q, vf{ft:02d}{ftf}",
                 0x3A: f"rsqrt Q, vf{fs:02d}{fsf}, vf{ft:02d}{ftf}", 0x3B: "waitq",
                 0x3C: f"mtir vi{it:02d}, vf{fs:02d}{fsf}", 0x3D: f"mfir.{dest} vf{ft:02d}, vi{is_:02d}",
                 0x3E: f"ilwr.{dest} vi{it:02d}, (vi{is_:02d})", 0x3F: f"iswr.{dest} vi{it:02d}, (vi{is_:02d})",
                 0x40: f"rnext.{dest} vf{ft:02d}, R", 0x41: f"rget.{dest} vf{ft:02d}, R",
                 0x42: f"rinit R, vf{fs:02d}{fsf}", 0x43: f"rxor R, vf{fs:02d}{fsf}",
                 0x64: f"mfp.{dest} vf{ft:02d}, P", 0x68: f"xtop vi{it:02d}", 0x69: f"xitop vi{it:02d}",
                 0x6C: f"xgkick vi{is_:02d}", 0x70: f"esadd P, vf{fs:02d}", 0x71: f"ersadd P, vf{fs:02d}",
                 0x72: f"eleng P, vf{fs:02d}", 0x73: f"erleng P, vf{fs:02d}", 0x74: f"eatanxy P, vf{fs:02d}",
                 0x75: f"eatanxz P, vf{fs:02d}", 0x76: f"esum P, vf{fs:02d}", 0x78: f"esqrt P, vf{fs:02d}{fsf}",
                 0x79: f"ersqrt P, vf{fs:02d}{fsf}", 0x7A: f"ercpr P, vf{fs:02d}{fsf}", 0x7B: "waitp",
                 0x7C: f"esin P, vf{fs:02d}{fsf}", 0x7D: f"eatan P, vf{fs:02d}{fsf}", 0x7E: f"eexp P, vf{fs:02d}{fsf}"}
            return m.get(sop, f"?ls{sop:02x} {w:08x}")
        return f"?l40.{f:02x} {w:08x}"
    imm15 = (w & 0x7FF) | (w >> 21 & 15) << 11
    imm12 = (w & 0x7FF) | (w >> 21 & 1) << 11
    tgt = pc + 1 + imm11
    m = {0x00: f"lq.{dest} vf{ft:02d}, {imm11}(vi{is_:02d})", 0x01: f"sq.{dest} vf{fs:02d}, {imm11}(vi{it:02d})",
         0x04: f"ilw.{dest} vi{it:02d}, {imm11}(vi{is_:02d})", 0x05: f"isw.{dest} vi{it:02d}, {imm11}(vi{is_:02d})",
         0x08: f"iaddiu vi{it:02d}, vi{is_:02d}, 0x{imm15:x}", 0x09: f"isubiu vi{it:02d}, vi{is_:02d}, 0x{imm15:x}",
         0x10: f"fceq vi01, 0x{w & 0xFFFFFF:06x}", 0x11: f"fcset 0x{w & 0xFFFFFF:06x}",
         0x12: f"fcand vi01, 0x{w & 0xFFFFFF:06x}", 0x13: f"fcor vi01, 0x{w & 0xFFFFFF:06x}",
         0x14: f"fseq vi{it:02d}, 0x{imm12:x}", 0x15: f"fsset 0x{imm12:x}", 0x16: f"fsand vi{it:02d}, 0x{imm12:x}",
         0x17: f"fsor vi{it:02d}, 0x{imm12:x}", 0x18: f"fmeq vi{it:02d}, vi{is_:02d}",
         0x1A: f"fmand vi{it:02d}, vi{is_:02d}", 0x1B: f"fmor vi{it:02d}, vi{is_:02d}", 0x1C: f"fcget vi{it:02d}",
         0x20: f"b 0x{tgt:03x}", 0x21: f"bal vi{it:02d}, 0x{tgt:03x}", 0x24: f"jr vi{is_:02d}",
         0x25: f"jalr vi{it:02d}, vi{is_:02d}", 0x28: f"ibeq vi{it:02d}, vi{is_:02d}, 0x{tgt:03x}",
         0x29: f"ibne vi{it:02d}, vi{is_:02d}, 0x{tgt:03x}", 0x2C: f"ibltz vi{is_:02d}, 0x{tgt:03x}",
         0x2D: f"ibgtz vi{is_:02d}, 0x{tgt:03x}", 0x2E: f"iblez vi{is_:02d}, 0x{tgt:03x}",
         0x2F: f"ibgez vi{is_:02d}, 0x{tgt:03x}"}
    return m.get(op7, f"?l{op7:02x} {w:08x}")


def disasm(code, base):
    out = []
    for i in range(len(code) // 8):
        lo, up = struct.unpack_from("<II", code, i * 8)
        pc = base + i
        u, flags = upper(up)
        if "I" in flags:
            ls = f"loi {struct.unpack('<f', struct.pack('<I', lo))[0]!r} (0x{lo:08x})"
        else:
            ls = lower(lo, pc)
        out.append(f"{pc:03x}: {u:<44} {ls}")
    return out


def vif_blocks(e, addr):
    """Parses a .vutext packet: DMA tag, then VIF codes; yields (load address, code bytes)."""
    tag = e.u32(addr)
    qwc = tag & 0xFFFF
    data = e.read(addr + 8, qwc * 16 + 8)  # VIF codes start in the tag's upper half
    pos = 0
    while pos + 4 <= len(data):
        code = struct.unpack_from("<I", data, pos)[0]
        cmd = code >> 24 & 0x7F
        pos += 4
        if cmd == 0x4A:  # MPG
            num = code >> 16 & 0xFF or 256
            load = code & 0xFFFF
            yield load, data[pos:pos + num * 8]
            pos += num * 8
        elif cmd in (0x00, 0x10, 0x11, 0x13, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07):
            continue  # NOP, FLUSHE, FLUSH, FLUSHA, STCYCL, OFFSET, BASE, ITOP, STMOD, MSKPATH3, MARK
        else:
            break


if __name__ == "__main__":
    args = sys.argv[1:]
    if "--extracted" in args:
        i = args.index("--extracted")
        del args[i:i + 2]
    raw = base = None
    if "--raw" in args:
        i = args.index("--raw")
        raw = int(args[i + 1], 0)
        del args[i:i + 2]
    if "--base" in args:
        i = args.index("--base")
        base = int(args[i + 1], 0)
        del args[i:i + 2]
    e = elf()
    addr = int(args[0], 16)
    if raw is not None:
        for line in disasm(e.read(addr, raw * 16), base or 0):
            print(line)
    else:
        for load, code in vif_blocks(e, addr):
            print(f"; MPG load address 0x{load:03x}, {len(code) // 8} pairs")
            for line in disasm(code, load):
                print(line)
