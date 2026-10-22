#!/usr/bin/env python3
"""Compare the port's GS local memory (--dump-vram, gs_memory.h layout: blocks placed as on the GS,
pixels row-major inside a block) with a PCSX2 VRAM image (GS.bin of a save state, or the GS state of a
.gs dump), block by block. Each 256-byte port block is converted to the GS column order of every
pixel format and compared with the PCSX2 block at the same address.

Usage: python -I tools/research/gsvram.py PORT_VRAM.bin (STATE.p2s | DUMP.gs) [--offset N] [--list N]
  --offset N  byte offset of VRAM inside the GS state (default: found by trying candidates)
  --list N    print the first N blocks that match under no format (block numbers only)

Prints counts and block numbers only, never memory contents.
"""
import struct
import sys
import zipfile

VRAM = 4 * 1024 * 1024

# GS column order inside a block (GS User's Manual; the same tables as in PCSX2's GSTables):
# index [y][x] -> element (word / halfword / byte / nibble) number inside the 256-byte block.
COL32 = [[0, 1, 4, 5, 8, 9, 12, 13], [2, 3, 6, 7, 10, 11, 14, 15],
         [16, 17, 20, 21, 24, 25, 28, 29], [18, 19, 22, 23, 26, 27, 30, 31]]
COL32 += [[v + 32 for v in r] for r in COL32]
COL16 = [[0, 2, 8, 10, 16, 18, 24, 26, 1, 3, 9, 11, 17, 19, 25, 27],
         [4, 6, 12, 14, 20, 22, 28, 30, 5, 7, 13, 15, 21, 23, 29, 31],
         [32, 34, 40, 42, 48, 50, 56, 58, 33, 35, 41, 43, 49, 51, 57, 59],
         [36, 38, 44, 46, 52, 54, 60, 62, 37, 39, 45, 47, 53, 55, 61, 63]]
COL16 += [[v + 64 for v in r] for r in COL16]
COL8 = [[0, 4, 16, 20, 32, 36, 48, 52, 2, 6, 18, 22, 34, 38, 50, 54],
        [8, 12, 24, 28, 40, 44, 56, 60, 10, 14, 26, 30, 42, 46, 58, 62],
        [33, 37, 49, 53, 1, 5, 17, 21, 35, 39, 51, 55, 3, 7, 19, 23],
        [41, 45, 57, 61, 9, 13, 25, 29, 43, 47, 59, 63, 11, 15, 27, 31],
        [96, 100, 112, 116, 64, 68, 80, 84, 98, 102, 114, 118, 66, 70, 82, 86],
        [104, 108, 120, 124, 72, 76, 88, 92, 106, 110, 122, 126, 74, 78, 90, 94],
        [65, 69, 81, 85, 97, 101, 113, 117, 67, 71, 83, 87, 99, 103, 115, 119],
        [73, 77, 89, 93, 105, 109, 121, 125, 75, 79, 91, 95, 107, 111, 123, 127]]
COL8 += [[v + 128 for v in r] for r in COL8]
COL4 = [[0, 8, 32, 40, 64, 72, 96, 104, 2, 10, 34, 42, 66, 74, 98, 106, 4, 12, 36, 44, 68, 76, 100, 108, 6, 14, 38, 46, 70, 78, 102, 110],
        [16, 24, 48, 56, 80, 88, 112, 120, 18, 26, 50, 58, 82, 90, 114, 122, 20, 28, 52, 60, 84, 92, 116, 124, 22, 30, 54, 62, 86, 94, 118, 126],
        [65, 73, 97, 105, 1, 9, 33, 41, 67, 75, 99, 107, 3, 11, 35, 43, 69, 77, 101, 109, 5, 13, 37, 45, 71, 79, 103, 111, 7, 15, 39, 47],
        [81, 89, 113, 121, 17, 25, 49, 57, 83, 91, 115, 123, 19, 27, 51, 59, 85, 93, 117, 125, 21, 29, 53, 61, 87, 95, 119, 127, 23, 31, 55, 63],
        [192, 200, 224, 232, 128, 136, 160, 168, 194, 202, 226, 234, 130, 138, 162, 170, 196, 204, 228, 236, 132, 140, 164, 172, 198, 206, 230, 238, 134, 142, 166, 174],
        [208, 216, 240, 248, 144, 152, 176, 184, 210, 218, 242, 250, 146, 154, 178, 186, 212, 220, 244, 252, 148, 156, 180, 188, 214, 222, 246, 254, 150, 158, 182, 190],
        [129, 137, 161, 169, 193, 201, 225, 233, 131, 139, 163, 171, 195, 203, 227, 235, 133, 141, 165, 173, 197, 205, 229, 237, 135, 143, 167, 175, 199, 207, 231, 239],
        [145, 153, 177, 185, 209, 217, 241, 249, 147, 155, 179, 187, 211, 219, 243, 251, 149, 157, 181, 189, 213, 221, 245, 253, 151, 159, 183, 191, 215, 223, 247, 255]]
COL4 += [[v + 256 for v in r] for r in COL4]

# (name, block width, block height, bits, table)
FORMATS = [("32", 8, 8, 32, COL32), ("16", 16, 8, 16, COL16), ("8", 16, 16, 8, COL8), ("4", 32, 16, 4, COL4)]


def check_tables():
    for name, w, h, bits, t in FORMATS:
        flat = sorted(v for row in t for v in row)
        assert len(t) == h and all(len(r) == w for r in t), name
        assert flat == list(range(w * h)), "column table %s is not a permutation" % name


def get(block, bits, i):
    if bits == 32:
        return block[4 * i:4 * i + 4]
    if bits == 16:
        return block[2 * i:2 * i + 2]
    if bits == 8:
        return block[i:i + 1]
    b = block[i >> 1]
    return bytes([(b >> 4) if i & 1 else (b & 15)])


def to_gs(block, fmt):
    """Port block (row-major) -> GS column order."""
    name, w, h, bits, t = fmt
    if bits == 4:
        out = bytearray(256)
        for y in range(h):
            for x in range(w):
                v = get(block, 4, y * w + x)[0]
                n = t[y][x]
                out[n >> 1] |= v << 4 if n & 1 else v
        return bytes(out)
    size = bits // 8
    out = bytearray(256)
    for y in range(h):
        for x in range(w):
            n = t[y][x]
            out[n * size:(n + 1) * size] = get(block, bits, y * w + x)
    return bytes(out)


def load_ref(path):
    if path.lower().endswith(".p2s"):
        return zipfile.ZipFile(path).read("GS.bin")
    d = open(path, "rb").read()
    hsize = struct.unpack_from("<I", d, 4)[0]
    hdr = struct.unpack_from("<9I", d, 8)
    o = 8 + hsize
    return d[o:o + hdr[1]]


def main():
    args = sys.argv[1:]
    if len(args) < 2:
        print(__doc__)
        return 2
    check_tables()
    port = open(args[0], "rb").read()
    state = load_ref(args[1])
    offset = int(args[args.index("--offset") + 1], 0) if "--offset" in args else None
    nlist = int(args[args.index("--list") + 1]) if "--list" in args else 0
    candidates = [offset] if offset is not None else range(0x180, 0x200)
    best = None
    for off in candidates:
        ref = state[off:off + VRAM]
        if len(ref) < VRAM:
            continue
        # quick score on a sample of non-uniform port blocks: matches under CT32
        score = 0
        for b in range(0, VRAM // 256, 64):
            pb = port[b * 256:(b + 1) * 256]
            if len(set(pb)) > 1 and to_gs(pb, FORMATS[0]) == ref[b * 256:(b + 1) * 256]:
                score += 1
            elif len(set(pb)) > 1 and any(to_gs(pb, f) == ref[b * 256:(b + 1) * 256] for f in FORMATS[1:]):
                score += 1
        if best is None or score > best[0]:
            best = (score, off)
    off = best[1]
    ref = state[off:off + VRAM]
    print("VRAM offset in the GS state: 0x%X (sample score %d)" % (off, best[0]))
    counts = {"uniform-equal": 0, "raw-equal": 0, "none": 0}
    for f in FORMATS:
        counts["fmt" + f[0]] = 0
    unmatched = []
    for b in range(VRAM // 256):
        pb, rb = port[b * 256:(b + 1) * 256], ref[b * 256:(b + 1) * 256]
        if pb == rb:
            counts["uniform-equal" if len(set(pb)) == 1 else "raw-equal"] += 1
            continue
        for f in FORMATS:
            if to_gs(pb, f) == rb:
                counts["fmt" + f[0]] += 1
                break
        else:
            counts["none"] += 1
            unmatched.append(b)
    print("blocks:", counts)
    if nlist:
        print("unmatched blocks:", " ".join("0x%X" % b for b in unmatched[:nlist]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
