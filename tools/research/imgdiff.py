"""Compare two screenshots (port native screenshot vs PCSX2 screenshot), research only.

Usage:
  python -I tools/research/imgdiff.py PORT.png PCSX2.png [--threshold N] [--diff OUT.png] [--offset DX DY]
                                      [--blend-fields]

Reads 8-bit RGB/RGBA PNGs with the standard library (zlib, PNG filters 0-4). Prints the size, the
per-channel mean and maximum absolute difference, and the share of pixels whose largest channel
difference exceeds the threshold (default 16). --diff writes a grey image of the per-pixel maximum
difference (x4) to the given path (keep it under out/: screenshots show game content).
--offset shifts the PCSX2 image before comparing (sub-pixel origin differences between renderers).
--blend-fields first averages each port row with the row above it ((a + b + 1) / 2), which models how
PCSX2 shows the 224-line field output deinterlaced: on the S2a title/menu screens (fine dot pattern)
this drops the pixels over 16 from 1-2.5 % to 0-0.1 % (validation.md S2a-4).
Validation tolerances: docs/port/validation-strategy.md §2.2.
"""
import argparse
import struct
import sys
import zlib


def read_png(path):
    data = open(path, "rb").read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        sys.exit(f"{path}: not a PNG")
    pos, idat, w = 8, b"", 0
    while pos < len(data):
        n, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        pos += 12 + n
        if kind == b"IHDR":
            w, h, depth, ctype, _, _, interlace = struct.unpack(">IIBBBBB", body)
            if depth != 8 or ctype not in (2, 6) or interlace:
                sys.exit(f"{path}: only 8-bit RGB/RGBA, non-interlaced PNGs are supported")
            bpp = 3 if ctype == 2 else 4
        elif kind == b"IDAT":
            idat += body
    raw = zlib.decompress(idat)
    stride = w * bpp
    out = bytearray(w * h * 3)
    prev = bytearray(stride)
    p = 0
    for y in range(h):
        f = raw[p]
        line = bytearray(raw[p + 1:p + 1 + stride])
        p += 1 + stride
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if f == 1:
                line[i] = (line[i] + a) & 255
            elif f == 2:
                line[i] = (line[i] + b) & 255
            elif f == 3:
                line[i] = (line[i] + ((a + b) >> 1)) & 255
            elif f == 4:
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                pr = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 255
        for x in range(w):
            out[(y * w + x) * 3:(y * w + x) * 3 + 3] = line[x * bpp:x * bpp + 3]
        prev = line
    return w, h, out


def write_grey(path, w, h, values):
    raw = b"".join(b"\x00" + bytes(values[y * w:(y + 1) * w]) for y in range(h))
    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 0, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b"")
    open(path, "wb").write(png)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("port")
    ap.add_argument("pcsx2")
    ap.add_argument("--threshold", type=int, default=16)
    ap.add_argument("--diff")
    ap.add_argument("--offset", nargs=2, type=int, default=(0, 0))
    ap.add_argument("--blend-fields", action="store_true")
    a = ap.parse_args()
    w1, h1, p1 = read_png(a.port)
    if a.blend_fields:
        src = bytes(p1)
        for i in range(w1 * 3, w1 * h1 * 3):
            p1[i] = (src[i] + src[i - w1 * 3] + 1) >> 1
    w2, h2, p2 = read_png(a.pcsx2)
    print(f"port {w1}x{h1}, pcsx2 {w2}x{h2}")
    w, h = min(w1, w2), min(h1, h2)
    dx, dy = a.offset
    sums, maxs, over = [0, 0, 0], [0, 0, 0], 0
    grey = bytearray(w * h)
    n = 0
    for y in range(h):
        for x in range(w):
            x2, y2 = x + dx, y + dy
            if not (0 <= x2 < w2 and 0 <= y2 < h2):
                continue
            i1, i2 = (y * w1 + x) * 3, (y2 * w2 + x2) * 3
            m = 0
            for c in range(3):
                d = abs(p1[i1 + c] - p2[i2 + c])
                sums[c] += d
                maxs[c] = max(maxs[c], d)
                m = max(m, d)
            grey[y * w + x] = min(255, m * 4)
            over += m > a.threshold
            n += 1
    print("mean abs diff R/G/B: " + " ".join(f"{s / max(n, 1):.2f}" for s in sums))
    print("max abs diff  R/G/B: " + " ".join(str(m) for m in maxs))
    print(f"pixels over {a.threshold}: {over} of {n} ({100.0 * over / max(n, 1):.3f} %)")
    if a.diff:
        write_grey(a.diff, w, h, grey)


if __name__ == "__main__":
    main()
