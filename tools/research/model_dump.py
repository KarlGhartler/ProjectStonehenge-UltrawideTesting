"""Dump a 3D model from DATA.CDP (research only; layout from BindModelData 0x00144618,
BindModelNodes 0x00144340, CreateModelInstance 0x0013F018, EmitModelInstance 0x001404A8).

Usage:
  python tools/research/model_dump.py ENTRY ITEM [--vif] [--verts N]
    ENTRY  DATA.CDP entry index (e.g. 0xCE for aircraft 1's models)
    ITEM   item of that entry's count+offsets container; nested items as 0.0
    --vif  list the DMA tags and VIF codes of every node's mesh packet
    --verts N  also print the first N unpacked qwords of each UNPACK (s32/f32 views)
Options: --extracted DIR (default: CLAUDE.local.md). Output goes to stdout (redirect into out/).
"""
import os
import struct
import sys

from common import extracted_dir


def read_entry(index):
    d = extracted_dir()
    tbl = open(os.path.join(d, "BIN", "DATA.TBL"), "rb").read()
    sector, size = struct.unpack_from("<II", tbl, index * 8)
    with open(os.path.join(d, "BIN", "DATA.CDP"), "rb") as f:
        f.seek(sector * 0x800)
        return f.read(size)


def item(data, i):
    count = struct.unpack_from("<I", data, 0)[0]
    offs = list(struct.unpack_from(f"<{count}I", data, 4)) + [len(data)]
    return data[offs[i]:offs[i + 1]], count


VIF_NAMES = {0x00: "NOP", 0x01: "STCYCL", 0x02: "OFFSET", 0x03: "BASE", 0x04: "ITOP", 0x05: "STMOD",
             0x06: "MSKPATH3", 0x07: "MARK", 0x10: "FLUSHE", 0x11: "FLUSH", 0x13: "FLUSHA", 0x14: "MSCAL",
             0x15: "MSCALF", 0x17: "MSCNT", 0x20: "STMASK", 0x30: "STROW", 0x31: "STCOL", 0x4A: "MPG",
             0x50: "DIRECT", 0x51: "DIRECTHL"}
UNPACK_FMT = {0x0: "S-32", 0x1: "S-16", 0x2: "S-8", 0x4: "V2-32", 0x5: "V2-16", 0x6: "V2-8", 0x8: "V3-32",
              0x9: "V3-16", 0xA: "V3-8", 0xC: "V4-32", 0xD: "V4-16", 0xE: "V4-8", 0xF: "V4-5"}
UNPACK_BITS = {0x0: 32, 0x1: 16, 0x2: 8, 0x4: 64, 0x5: 32, 0x6: 16, 0x8: 96, 0x9: 48, 0xA: 24, 0xC: 128,
               0xD: 64, 0xE: 32, 0xF: 16}


def vif_codes(buf, pos, end, show_verts, out):
    """Decodes VIF codes in buf[pos:end] (4-byte aligned)."""
    while pos + 4 <= end:
        code = struct.unpack_from("<I", buf, pos)[0]
        cmd, num, imm = code >> 24 & 0x7F, code >> 16 & 0xFF, code & 0xFFFF
        irq = " (i)" if code >> 31 else ""
        pos += 4
        if cmd >= 0x60:
            vn, vl = cmd >> 2 & 3, cmd & 3
            fmt = cmd & 0xF
            n = num or 256
            size = (n * UNPACK_BITS.get(fmt, 32) + 31) // 32 * 4
            usn = imm >> 14 & 1
            flg = imm >> 15 & 1
            out.append(f"    UNPACK {UNPACK_FMT.get(fmt, hex(fmt))} num={n} addr=0x{imm & 0x3FF:03x}"
                       f"{' +TOPS' if flg else ''}{' unsigned' if usn else ''}{' masked' if cmd & 0x10 else ''}{irq}")
            if show_verts and fmt == 0xC:
                for k in range(min(show_verts, n)):
                    w = struct.unpack_from("<4i", buf, pos + 16 * k)
                    f = struct.unpack_from("<4f", buf, pos + 16 * k)
                    out.append(f"      [{k:3d}] i {w}  f ({f[0]:.4g}, {f[1]:.4g}, {f[2]:.4g}, {f[3]:.4g})")
            pos += size
        elif cmd in (0x50, 0x51):
            n = imm or 0x10000
            out.append(f"    {VIF_NAMES[cmd]} {n} qwords{irq}")
            pos += n * 16
        elif cmd == 0x4A:
            n = num or 256
            out.append(f"    MPG {n} pairs at 0x{imm:03x}{irq}")
            pos += n * 8
        elif cmd in (0x20,):
            out.append(f"    STMASK {struct.unpack_from('<I', buf, pos)[0]:08x}{irq}")
            pos += 4
        elif cmd in (0x30, 0x31):
            out.append(f"    {VIF_NAMES[cmd]} {struct.unpack_from('<4I', buf, pos)}{irq}")
            pos += 16
        else:
            name = VIF_NAMES.get(cmd, f"?{cmd:02x}")
            arg = f" 0x{imm:x}" if cmd in (0x01, 0x02, 0x03, 0x04, 0x05, 0x14, 0x15) else ""
            if cmd == 0x01:
                arg = f" CL={imm & 0xFF} WL={imm >> 8 & 0xFF}"
            if cmd != 0x00:
                out.append(f"    {name}{arg}{irq}")
    return pos


def walk_chain(buf, start, show_verts, out, limit=4096):
    """Source-chain DMA tags from `start` (offsets inside buf); VIF codes ride in the tags' upper half."""
    pos = start
    for _ in range(limit):
        if pos + 16 > len(buf):
            out.append(f"  (tag at 0x{pos:x} beyond the item)")
            return
        tag = struct.unpack_from("<Q", buf, pos)[0]
        qwc, tid, addr = tag & 0xFFFF, tag >> 28 & 7, tag >> 32 & 0x7FFFFFFF
        names = ("refe", "cnt", "next", "ref", "refs", "call", "ret", "end")
        out.append(f"  0x{pos:06x}: DMA {names[tid]} qwc={qwc} addr=0x{addr:x}")
        vif_codes(buf, pos + 8, pos + 16, show_verts, out)
        if tid in (1, 6, 7, 2, 5):
            vif_codes(buf, pos + 16, pos + 16 + qwc * 16, show_verts, out)
        if tid == 1:
            pos += 16 + qwc * 16
        elif tid in (6, 7, 0):
            return
        elif tid in (3, 4):
            out.append(f"    (data by reference at 0x{addr:x}, {qwc} qwords)")
            pos += 16
        else:
            out.append("    (next/call: stop)")
            return


def main():
    args = [a for a in sys.argv[1:]]
    if "--extracted" in args:
        i = args.index("--extracted")
        del args[i:i + 2]
    show_vif = "--vif" in args
    if show_vif:
        args.remove("--vif")
    verts = 0
    if "--verts" in args:
        i = args.index("--verts")
        verts = int(args[i + 1], 0)
        del args[i:i + 2]
    entry = int(args[0], 0)
    m = read_entry(entry)
    for part in args[1].split("."):  # nested containers: e.g. 0.0
        m, count = item(m, int(part, 0))
    idx = args[1]
    u32 = lambda o: struct.unpack_from("<I", m, o)[0]
    print(f"DATA 0x{entry:x} item {idx} (of {count}): {len(m)} bytes; header words: "
          + " ".join(f"{u32(o):08x}" for o in range(0, 0x40, 4)))
    version = u32(4)
    print(f"version 0x{version:x}  +0x10 node table 0x{u32(0x10):x}  +0x14 root 0x{u32(0x14):x}")
    out = []

    def node(off, depth, index):
        children = u32(off + 0x30)
        child_tab = u32(off + 0x34) if version >= 0xB4 else off + 0x34
        mesh = u32(off + 0x24)
        out.append(f"{'  ' * depth}node {index} @0x{off:x}: T {struct.unpack_from('<3f', m, off)} "
                   f"R {struct.unpack_from('<3f', m, off + 0x10)} keys 0x{u32(off + 0x20):x} mesh 0x{mesh:x} "
                   f"+0x2c {u32(off + 0x2C)} part {u32(off + 0x3C)} children {children}")
        if mesh:
            h = struct.unpack_from("<4I", m, mesh)
            out.append(f"{'  ' * depth}  mesh header: vif 0x{h[0]:x} list 0x{h[1]:x} morph 0x{h[2]:x} flags 0x{h[3]:x}")
            if show_vif:
                walk_chain(m, h[0], verts, out)
                walk_chain(m, h[1], verts, out)
        nxt = index + 1
        for c in range(children):
            nxt = node(u32(child_tab + 4 * c), depth + 1, nxt)
        return nxt

    n = node(u32(0x14), 0, 0)
    print(f"{n} nodes")
    print("\n".join(out))


if __name__ == "__main__":
    main()
