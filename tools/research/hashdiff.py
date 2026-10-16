"""Compare two RAM hash logs of the port (--hash-log, docs/port/s5-plan.md P1).

Usage: python tools/research/hashdiff.py A.ac4h B.ac4h [--max N]

Prints the first vblank where the two runs differ and the 64 KiB blocks that differ there (guest
address ranges, with the ELF section or "heap"/"stack"/"scratchpad"), then how many later vblanks
differ. The logs hold only hashes, no game data.
"""
import argparse
import struct
import sys


def read_log(path):
    with open(path, "rb") as f:
        data = f.read()
    magic, version, block_size, blocks = struct.unpack_from("<4I", data, 0)
    if magic != 0x48344341 or version != 1:
        sys.exit(f"{path}: not an AC4H v1 hash log")
    rec = 16 + 8 * blocks
    out = {}
    for off in range(16, len(data) - rec + 1, rec):
        vblank, total = struct.unpack_from("<QQ", data, off)
        out[vblank] = (total, struct.unpack_from(f"<{blocks}Q", data, off + 16))
    return block_size, blocks, out


def region(addr):
    if addr < 0x00100000:
        return "low/kernel"
    if addr < 0x00297B34:
        return ".text"
    if addr < 0x004021E8:
        return ".data/.rodata"
    if addr < 0x004CC760:
        return ".sdata/.bss"
    if addr >= 0x01FF0000:
        return "stack"
    return "heap"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("a")
    ap.add_argument("b")
    ap.add_argument("--max", type=int, default=20)
    args = ap.parse_args()
    bs, nb, a = read_log(args.a)
    bs2, nb2, b = read_log(args.b)
    if (bs, nb) != (bs2, nb2):
        sys.exit("different block layouts")
    common = sorted(set(a) & set(b))
    print(f"{len(a)} / {len(b)} vblanks, {len(common)} in both")
    diff = [v for v in common if a[v][0] != b[v][0]]
    if not diff:
        print("identical at every common vblank")
        return
    first = diff[0]
    print(f"first difference at vblank {first}; {len(diff)} differing vblanks in total")
    blocks = [i for i in range(nb) if a[first][1][i] != b[first][1][i]]
    for i in blocks[: args.max]:
        if i == nb - 1:
            print("  scratchpad 0x70000000-0x70004000")
        else:
            lo = i * bs
            print(f"  0x{lo:08X}-0x{lo + bs:08X} {region(lo)}")
    if len(blocks) > args.max:
        print(f"  ... {len(blocks) - args.max} more blocks")


if __name__ == "__main__":
    main()
