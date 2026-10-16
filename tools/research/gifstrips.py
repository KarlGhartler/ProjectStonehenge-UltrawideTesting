"""Summary of the 3D primitive packets (PACKED GIF tags with STQ/RGBAQ/XYZ(F)2) of one frame, per
texture (TEX0 TBP): number of packets and vertices, vertices with ADC (no kick), the screen box, the
Z range and the mean vertex colour. For comparing the port's VU1 HLE output with a PCSX2 GS dump of the
same scene (research only).

Usage: python tools/research/gifstrips.py FILE [--gs] [--packets N] [--tbp HEX]
  FILE     raw GIF stream (the port's capture) or, with --gs, a PCSX2 uncompressed .gs dump (all its
           transfers are read as one stream: dump a single frame)
  --packets N  also print the first N packets of the selected texture (vertices in full)
  --tbp HEX    texture to list with --packets (default: all)

Prints only counts and register values; nothing from the dump is written anywhere.
"""
import collections
import struct
import sys


def bits(v, lo, n):
    return (v >> lo) & ((1 << n) - 1)


def gs_stream(path):
    d = open(path, "rb").read()
    o = 4
    hsize = struct.unpack_from("<I", d, o)[0]
    o += 4
    hdr = struct.unpack_from("<9I", d, o)
    o += hsize + hdr[1] + 8192
    out = bytearray()
    while o < len(d):
        t = d[o]
        o += 1
        if t == 0:
            size = struct.unpack_from("<I", d, o + 1)[0]
            o += 5
            out += d[o:o + size]
            o += size
        elif t == 1:
            o += 1
        elif t == 2:
            o += 4
        elif t == 3:
            o += 8192
        else:
            raise SystemExit(f"unknown packet {t}")
    return bytes(out)


def main():
    args = sys.argv[1:]
    path = args[0]
    data = gs_stream(path) if "--gs" in args else open(path, "rb").read()
    npk = int(args[args.index("--packets") + 1]) if "--packets" in args else 0
    sel = int(args[args.index("--tbp") + 1], 16) if "--tbp" in args else None
    tex0 = [0, 0]
    stats = collections.OrderedDict()
    shown = 0
    o, n = 0, len(data)
    while o + 16 <= n:
        lo, hi = struct.unpack_from("<QQ", data, o)
        o += 16
        nloop, eop, pre, prim = bits(lo, 0, 15), bits(lo, 15, 1), bits(lo, 46, 1), bits(lo, 47, 11)
        flg, nreg = bits(lo, 58, 2), bits(lo, 60, 4) or 16
        regs = [bits(hi, 4 * i, 4) for i in range(nreg)]
        if flg == 0:
            is3d = pre and 2 in regs and any(r in (4, 5) for r in regs)
            verts = []
            cur = {}
            for _ in range(nloop):
                for r in regs:
                    if o + 16 > n:
                        return
                    a, b = struct.unpack_from("<QQ", data, o)
                    o += 16
                    if r == 0xE:
                        if (b & 0xFF) in (6, 7):
                            tex0[(b & 0xFF) - 6] = a
                    elif r == 2:
                        cur["s"] = struct.unpack("<f", struct.pack("<I", a & 0xFFFFFFFF))[0]
                        cur["t"] = struct.unpack("<f", struct.pack("<I", a >> 32))[0]
                        cur["q"] = struct.unpack("<f", struct.pack("<I", b & 0xFFFFFFFF))[0]
                    elif r == 1:
                        cur["rgba"] = (a & 0xFF, (a >> 32) & 0xFF, b & 0xFF, (b >> 32) & 0xFF)
                    elif r in (4, 5):
                        x, y = (a & 0xFFFF) / 16.0, ((a >> 32) & 0xFFFF) / 16.0
                        z = (b >> 4) & 0xFFFFFF if r == 4 else b & 0xFFFFFFFF
                        verts.append(dict(cur, x=x, y=y, z=z, adc=bits(b, 47, 1)))
            if is3d and verts:
                ctx = bits(prim, 9, 1)
                tbp = bits(tex0[ctx], 0, 14) if bits(prim, 4, 1) else -1
                key = (tbp, prim)
                s = stats.setdefault(key, collections.Counter())
                s["packets"] += 1
                s["verts"] += len(verts)
                s["adc"] += sum(v["adc"] for v in verts)
                for v in verts:
                    for c, val in zip("rgba", v.get("rgba", (0, 0, 0, 0))):
                        s["sum_" + c] += val
                for k, f in (("xmin", min), ("xmax", max), ("ymin", min), ("ymax", max), ("zmin", min), ("zmax", max)):
                    c = k[0]
                    vals = [v[c] for v in verts if not v["adc"]] or [0]
                    s[k] = f(vals) if k not in s else f(s[k], f(vals))
                if npk and shown < npk and (sel is None or tbp == sel):
                    shown += 1
                    print(f"packet tbp {tbp:#x} prim {prim:#x} nloop {nloop} regs {regs}")
                    for v in verts:
                        print("   adc%d xy %8.2f %8.2f z %8d stq %.4f %.4f %.4f rgba %s" % (
                            v["adc"], v["x"], v["y"], v["z"], v.get("s", 0), v.get("t", 0), v.get("q", 0), v.get("rgba")))
        elif flg == 1:
            cnt = nloop * nreg
            for i in range(cnt):
                v = struct.unpack_from("<Q", data, o)[0]
                o += 8
                r = regs[i % nreg]
                if r in (6, 7):
                    tex0[r - 6] = v
            if cnt & 1:
                o += 8
        else:
            o += 16 * nloop
    for (tbp, prim), s in stats.items():
        print(f"tbp {tbp:#06x} prim {prim:#05x}: {s['packets']} packets, {s['verts']} verts, {s['adc']} adc, "
              f"x {s['xmin']:.0f}..{s['xmax']:.0f} y {s['ymin']:.0f}..{s['ymax']:.0f} z {s['zmin']}..{s['zmax']} "
              f"mean rgba {' '.join('%.0f' % (s['sum_' + c] / max(s['verts'], 1)) for c in 'rgba')}")


if __name__ == "__main__":
    main()
