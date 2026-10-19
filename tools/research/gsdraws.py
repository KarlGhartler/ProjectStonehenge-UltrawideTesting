"""List the draws of a PCSX2 single-frame GS dump (or a port GIF capture) in order, with their GS state
and screen box. A "draw" is a run of primitive kicks under the same state (PRIM, FRAME, TEX0, ALPHA,
TEST, ZBUF, FBA, CLAMP of the primitive's context). For finding which draws cover a screen region
when the port's picture differs from PCSX2's (research only).

Usage: python -I tools/research/gsdraws.py FILE [--gs] [--region X0 Y0 X1 Y1] [--date] [--from N] [--count N]
  FILE      raw GIF stream (port capture) or, with --gs, an uncompressed PCSX2 .gs dump
  --region  only draws whose screen box (pixels, after XYOFFSET) overlaps this box
  --date    only draws with the destination alpha test on
  --from/--count  index range of the listing

Prints register values and counts only; nothing from the dump is written anywhere.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gifstrips import bits, gs_stream  # noqa: E402

PRIM_NAMES = ["pt", "line", "lstrip", "tri", "tstrip", "tfan", "sprite", "prim7"]
PSM = {0x00: "CT32", 0x01: "CT24", 0x02: "CT16", 0x0A: "CT16S", 0x13: "T8", 0x14: "T4", 0x1B: "T8H",
       0x24: "T4HL", 0x2C: "T4HH", 0x30: "Z32", 0x31: "Z24", 0x32: "Z16", 0x3A: "Z16S"}
# Context registers: A+D address of context 1; context 2 is +1.
CTX_REGS = {"TEX0": 0x06, "CLAMP": 0x08, "XYOFFSET": 0x18, "SCISSOR": 0x40, "ALPHA": 0x42, "TEST": 0x47,
            "FBA": 0x4A, "FRAME": 0x4C, "ZBUF": 0x4E}
CTX_BY_ADDR = {}
for name, a in CTX_REGS.items():
    CTX_BY_ADDR[a] = (name, 0)
    CTX_BY_ADDR[a + 1] = (name, 1)
CTX_BY_ADDR[0x06], CTX_BY_ADDR[0x07] = ("TEX0", 0), ("TEX0", 1)
CTX_BY_ADDR[0x08], CTX_BY_ADDR[0x09] = ("CLAMP", 0), ("CLAMP", 1)
CTX_BY_ADDR[0x14], CTX_BY_ADDR[0x15] = ("TEX1", 0), ("TEX1", 1)


class Gs:
    def __init__(self):
        self.ctx = [dict.fromkeys(list(CTX_REGS) + ["TEX1"], 0) for _ in range(2)]
        self.prim = 0
        self.pabe = 0
        self.colclamp = 1
        self.fogcol = 0
        self.draws = []
        self.cur = None

    def key(self):
        c = self.ctx[bits(self.prim, 9, 1)]
        return (self.prim, c["FRAME"], c["TEX0"] if bits(self.prim, 4, 1) else 0, c["ALPHA"] if bits(self.prim, 6, 1) else 0,
                c["TEST"], c["ZBUF"], c["FBA"], c["CLAMP"], self.pabe, self.colclamp)

    def vertex(self, x, y, kick):
        c = self.ctx[bits(self.prim, 9, 1)]
        ox, oy = bits(c["XYOFFSET"], 0, 16), bits(c["XYOFFSET"], 32, 16)
        px, py = (x - ox) / 16.0, (y - oy) / 16.0
        k = self.key()
        if self.cur is None or self.cur["key"] != k:
            self.cur = {"key": k, "kicks": 0, "verts": 0, "box": [1e9, 1e9, -1e9, -1e9], "scissor": c["SCISSOR"]}
            self.draws.append(self.cur)
        d = self.cur
        d["verts"] += 1
        if kick:
            d["kicks"] += 1
        b = d["box"]
        b[0], b[1], b[2], b[3] = min(b[0], px), min(b[1], py), max(b[2], px), max(b[3], py)

    def write(self, addr, v):
        if addr == 0x00:
            self.prim = v & 0x7FF
        elif addr in CTX_BY_ADDR:
            name, i = CTX_BY_ADDR[addr]
            self.ctx[i][name] = v
        elif addr == 0x49:
            self.pabe = v & 1
        elif addr == 0x46:
            self.colclamp = v & 1
        elif addr == 0x05:
            self.vertex(bits(v, 0, 16), bits(v, 16, 16), True)
        elif addr == 0x04:
            self.vertex(bits(v, 0, 16), bits(v, 16, 16), True)
        elif addr in (0x0C, 0x0D):
            self.vertex(bits(v, 0, 16), bits(v, 16, 16), False)


def parse(data):
    gs = Gs()
    o, n = 0, len(data)
    while o + 16 <= n:
        lo, hi = struct.unpack_from("<QQ", data, o)
        o += 16
        nloop, pre, prim = bits(lo, 0, 15), bits(lo, 46, 1), bits(lo, 47, 11)
        flg, nreg = bits(lo, 58, 2), bits(lo, 60, 4) or 16
        regs = [bits(hi, 4 * i, 4) for i in range(nreg)]
        if pre and flg != 2:
            gs.prim = prim
        if flg == 0:
            for _ in range(nloop):
                for r in regs:
                    if o + 16 > n:
                        return gs
                    a, b = struct.unpack_from("<QQ", data, o)
                    o += 16
                    if r == 0xE:
                        gs.write(b & 0xFF, a)
                    elif r in (4, 5):
                        gs.vertex(a & 0xFFFF, (a >> 32) & 0xFFFF, bits(b, 47, 1) == 0)
                    elif r in (0xC, 0xD):
                        gs.vertex(a & 0xFFFF, (a >> 32) & 0xFFFF, False)
                    elif r == 0:
                        gs.prim = a & 0x7FF
                    elif r in (6, 7, 8, 9):
                        gs.write(r, a)
        elif flg == 1:
            cnt = nloop * nreg
            for i in range(cnt):
                if o + 8 > n:
                    return gs
                v = struct.unpack_from("<Q", data, o)[0]
                o += 8
                r = regs[i % nreg]
                if r != 0xE and r != 0xF:
                    gs.write(r, v)
            if cnt & 1:
                o += 8
        else:
            o += 16 * nloop
    return gs


def describe(i, d):
    prim, frame, tex0, alpha, test, zbuf, fba, clamp, pabe, colclamp = d["key"]
    t = PRIM_NAMES[prim & 7]
    fl = "".join(c for c, b in (("G", 3), ("T", 4), ("F", 5), ("A", 6), ("U", 8)) if bits(prim, b, 1))
    ctx = bits(prim, 9, 1) + 1
    s = f"{i:5d} {t:6s} c{ctx} {fl:5s} n{d['kicks']:<5d} box {d['box'][0]:6.1f} {d['box'][1]:6.1f} {d['box'][2]:6.1f} {d['box'][3]:6.1f}"
    s += f" FRAME fbp {bits(frame, 0, 9):#x} fbw {bits(frame, 16, 6)} {PSM.get(bits(frame, 24, 6), hex(bits(frame, 24, 6)))} msk {bits(frame, 32, 32):#010x}"
    if bits(prim, 4, 1):
        s += f" TEX tbp {bits(tex0, 0, 14):#x} tbw {bits(tex0, 14, 6)} {PSM.get(bits(tex0, 20, 6), hex(bits(tex0, 20, 6)))} {1 << bits(tex0, 26, 4)}x{1 << bits(tex0, 30, 4)} tfx {bits(tex0, 35, 2)} tcc {bits(tex0, 34, 1)}"
    if bits(prim, 6, 1):
        s += f" ALPHA {bits(alpha, 0, 2)}{bits(alpha, 2, 2)}{bits(alpha, 4, 2)}{bits(alpha, 6, 2)} fix {bits(alpha, 32, 8):#x}"
    s += f" TEST ate{bits(test, 0, 1)} atst{bits(test, 1, 3)} aref{bits(test, 4, 8):#x} afail{bits(test, 12, 2)} date{bits(test, 14, 1)} datm{bits(test, 15, 1)} zte{bits(test, 16, 1)} ztst{bits(test, 17, 2)}"
    s += f" ZBUF zbp {bits(zbuf, 0, 9):#x} {PSM.get(0x30 | bits(zbuf, 24, 4), '?')} zmsk {bits(zbuf, 32, 1)}"
    if fba & 1:
        s += " FBA1"
    if pabe:
        s += " PABE"
    if not colclamp:
        s += " COLCLAMP0"
    return s


def main():
    args = sys.argv[1:]
    path = args[0]
    data = gs_stream(path) if "--gs" in args else open(path, "rb").read()
    region = None
    if "--region" in args:
        i = args.index("--region")
        region = [float(v) for v in args[i + 1:i + 5]]
    first = int(args[args.index("--from") + 1]) if "--from" in args else 0
    count = int(args[args.index("--count") + 1]) if "--count" in args else 1 << 30
    gs = parse(data)
    shown = 0
    for i, d in enumerate(gs.draws):
        if i < first or shown >= count:
            continue
        if "--date" in args and not bits(d["key"][4], 14, 1):
            continue
        b = d["box"]
        if region and (b[2] < region[0] or b[0] > region[2] or b[3] < region[1] or b[1] > region[3]):
            continue
        print(describe(i, d))
        shown += 1
    print(f"{len(gs.draws)} draws, {shown} listed")


if __name__ == "__main__":
    main()
