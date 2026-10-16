#!/usr/bin/env python3
"""Summarise a --dump-dma-at capture (vif1_<v>.bin, gif_<v>.bin) of the port.

vif1_<v>.bin: pairs of little-endian u32 (guest address, word) in the order the VIF1 received them.
gif_<v>.bin: the GIF data (PATH2 from DIRECT and PATH3) produced in that step.

Usage: dmacap.py <dumps dir> <vblank> [--vif] [--gif] [--regs]
Research only: the captures contain game data; keep them under out/.
"""
import struct
import sys
from pathlib import Path

VIF_NAMES = {0x00: "NOP", 0x01: "STCYCL", 0x02: "OFFSET", 0x03: "BASE", 0x04: "ITOP", 0x05: "STMOD",
             0x06: "MSKPATH3", 0x07: "MARK", 0x10: "FLUSHE", 0x11: "FLUSH", 0x13: "FLUSHA", 0x14: "MSCAL",
             0x15: "MSCALF", 0x17: "MSCNT", 0x20: "STMASK", 0x30: "STROW", 0x31: "STCOL", 0x4A: "MPG",
             0x50: "DIRECT", 0x51: "DIRECTHL"}
GS_REGS = {0x00: "PRIM", 0x01: "RGBAQ", 0x02: "ST", 0x03: "UV", 0x04: "XYZF2", 0x05: "XYZ2", 0x06: "TEX0_1",
           0x07: "TEX0_2", 0x08: "CLAMP_1", 0x09: "CLAMP_2", 0x0A: "FOG", 0x0C: "XYZF3", 0x0D: "XYZ3",
           0x14: "TEX1_1", 0x15: "TEX1_2", 0x16: "TEX2_1", 0x17: "TEX2_2", 0x18: "XYOFFSET_1",
           0x19: "XYOFFSET_2", 0x1A: "PRMODECONT", 0x1B: "PRMODE", 0x1C: "TEXCLUT", 0x22: "SCANMSK",
           0x34: "MIPTBP1_1", 0x3B: "TEXA", 0x3D: "FOGCOL", 0x3F: "TEXFLUSH", 0x40: "SCISSOR_1",
           0x41: "SCISSOR_2", 0x42: "ALPHA_1", 0x43: "ALPHA_2", 0x44: "DIMX", 0x45: "DTHE", 0x46: "COLCLAMP",
           0x47: "TEST_1", 0x48: "TEST_2", 0x49: "PABE", 0x4A: "FBA_1", 0x4B: "FBA_2", 0x4C: "FRAME_1",
           0x4D: "FRAME_2", 0x4E: "ZBUF_1", 0x4F: "ZBUF_2", 0x50: "BITBLTBUF", 0x51: "TRXPOS", 0x52: "TRXREG",
           0x53: "TRXDIR", 0x54: "HWREG", 0x60: "SIGNAL", 0x61: "FINISH", 0x62: "LABEL"}


def unpack_words(cmd, num):
    vn, vl = (cmd >> 2) & 3, cmd & 3
    bits = 16 if vl == 3 else (32 >> vl) * (vn + 1)
    n = 256 if num == 0 else num
    return (n * bits + 31) // 32


def vif_stream(pairs, show):
    i, counts = 0, {}
    while i < len(pairs):
        addr, w = pairs[i]
        cmd, num, imm = (w >> 24) & 0x7F, (w >> 16) & 0xFF, w & 0xFFFF
        i += 1
        name = VIF_NAMES.get(cmd, "UNPACK" if cmd >= 0x60 else "?%02X" % cmd)
        counts[name] = counts.get(name, 0) + 1
        extra = 0
        if cmd >= 0x60:
            extra = unpack_words(cmd, num)
        elif cmd in (0x50, 0x51):
            extra = (0x10000 if imm == 0 else imm) * 4
        elif cmd == 0x4A:
            extra = (256 if num == 0 else num) * 2
        elif cmd == 0x20:
            extra = 1
        elif cmd in (0x30, 0x31):
            extra = 4
        if show:
            print("%08X  %08X  %-8s num=%3d imm=%04X  +%d words" % (addr, w, name, num, imm, extra))
        i += extra
    return counts


def gif_stream(data, show_regs):
    off, packets, prims, regs = 0, 0, {}, {}
    while off + 16 <= len(data):
        lo, hi = struct.unpack_from("<QQ", data, off)
        nloop, eop, pre, prim = lo & 0x7FFF, (lo >> 15) & 1, (lo >> 46) & 1, (lo >> 47) & 0x7FF
        flg, nreg = (lo >> 58) & 3, (lo >> 60) & 0xF
        nreg = 16 if nreg == 0 else nreg
        off += 16
        packets += 1
        if pre:
            prims[prim & 7] = prims.get(prim & 7, 0) + 1
        if flg == 0:  # PACKED
            for _ in range(nloop):
                for r in range(nreg):
                    reg = (hi >> (4 * r)) & 0xF
                    a, b = struct.unpack_from("<QQ", data, off)
                    if reg == 0xE:  # A+D
                        name = GS_REGS.get(b & 0xFF, "%02X" % (b & 0xFF))
                        regs[name] = regs.get(name, 0) + 1
                        if show_regs:
                            print("  A+D %-10s %016X" % (name, a))
                        if (b & 0xFF) == 0 and True:
                            prims[a & 7] = prims.get(a & 7, 0) + 1
                    off += 16
        elif flg == 1:  # REGLIST
            words = nloop * nreg
            for k in range(words):
                reg = (hi >> (4 * (k % nreg))) & 0xF
                val = struct.unpack_from("<Q", data, off + 8 * k)[0]
                if reg == 0:
                    prims[val & 7] = prims.get(val & 7, 0) + 1
                    if show_regs:
                        print("  REGLIST PRIM %03X" % (val & 0x7FF))
                elif reg == 1 and show_regs and k < 3 * nreg:
                    print("  REGLIST RGBAQ %08X" % (val & 0xFFFFFFFF))
                elif reg in (4, 5) and show_regs and k < 3 * nreg:
                    print("  REGLIST XYZ x=%.1f y=%.1f z=%08X" % ((val & 0xFFFF) / 16, ((val >> 16) & 0xFFFF) / 16, val >> 32))
            off += ((words + 1) // 2) * 16
        else:  # IMAGE
            off += nloop * 16
    return packets, prims, regs


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    d, v = Path(sys.argv[1]), int(sys.argv[2])
    raw = (d / ("vif1_%06d.bin" % v)).read_bytes()
    pairs = [struct.unpack_from("<II", raw, o) for o in range(0, len(raw) - 7, 8)]
    counts = vif_stream(pairs, "--vif" in sys.argv)
    print("VIF1 words:", len(pairs), "codes:", counts)
    gif = (d / ("gif_%06d.bin" % v)).read_bytes()
    packets, prims, regs = gif_stream(gif, "--regs" in sys.argv)
    print("GIF bytes:", len(gif), "tags:", packets, "prims:", prims)
    print("A+D registers:", regs)
    return 0


if __name__ == "__main__":
    sys.exit(main())
