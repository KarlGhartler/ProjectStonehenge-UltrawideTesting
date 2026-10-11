#!/usr/bin/env python3
"""Extract the BIN/*.CDP archives of Ace Combat 04 (SLUS_201.52) using their .TBL index.

Format (docs/formats/CDP.md, from GetTblEntrySector 0x00215468 / GetTblEntrySize 0x00215488):
    TBL = array of {u32 sector, u32 size} (little-endian), one per entry, no header.
    Entry i occupies CDP bytes [sector*2048, sector*2048 + size).

Usage:
    python tools/cdp_extract.py <ExtractedGameFiles dir> [-o out/cdp] [--no-write]

Writes <out>/<ARCHIVE>/<index:04d>.<ext> and <out>/<ARCHIVE>/index.csv, and prints a summary
with the integrity checks (entry count, overlaps, bounds, coverage) and a magic-byte histogram.
"""
import argparse
import collections
import csv
import os
import struct
import sys

SECTOR = 2048
ARCHIVES = ("DATA", "BD")

# Known leading signatures -> (label, file extension)
MAGICS = [
    (b"TIM2", "TIM2 texture", "tm2"),
    (b"VAGp", "VAG ADPCM", "vag"),
    (b"\x00\x00\x01\xba", "MPEG pack", "mpg"),
    (b"\x00\x00\x01\xb3", "MPEG-2 video ES", "m2v"),
    (b"IECS", "IECS (Sony sound bank)", "iecs"),
    (b"RIFF", "RIFF", "riff"),
    (b"\x7fELF", "ELF", "elf"),
    (b"PK\x03\x04", "ZIP", "zip"),
]


def read_tbl(path):
    data = open(path, "rb").read()
    if len(data) % 8:
        raise ValueError(f"{path}: size {len(data)} is not a multiple of 8")
    return [struct.unpack_from("<II", data, i) for i in range(0, len(data), 8)]


def classify(head, size):
    """Return (label, ext) for an entry from its first bytes."""
    for magic, label, ext in MAGICS:
        if head.startswith(magic):
            return label, ext
    if size >= 8:
        # Offset table: u32 count, then count u32 offsets (ascending, inside the entry).
        count = struct.unpack_from("<I", head, 0)[0]
        if 0 < count < 4096 and 4 + 4 * count <= min(len(head), size):
            offs = struct.unpack_from(f"<{count}I", head, 4)
            if offs[0] >= 4 + 4 * count and all(a <= b for a, b in zip(offs, offs[1:])) and offs[-1] < size:
                return "offset table (count + offsets)", "bin"
    if head[:4] == b"\0\0\0\0":
        return "starts with zeros", "bin"
    return "unknown " + head[:4].hex(), "bin"


def process(game_dir, name, out_dir, write):
    tbl_path = os.path.join(game_dir, "BIN", f"{name}.TBL")
    cdp_path = os.path.join(game_dir, "BIN", f"{name}.CDP")
    entries = read_tbl(tbl_path)
    cdp_size = os.path.getsize(cdp_path)
    problems = []

    # Bounds
    for i, (sec, size) in enumerate(entries):
        if sec * SECTOR + size > cdp_size:
            problems.append(f"entry {i}: [{sec * SECTOR:#x}, +{size:#x}) beyond CDP size {cdp_size:#x}")
    # Overlaps (sorted by start)
    order = sorted(range(len(entries)), key=lambda i: entries[i][0])
    for a, b in zip(order, order[1:]):
        end_a = entries[a][0] * SECTOR + entries[a][1]
        if end_a > entries[b][0] * SECTOR:
            problems.append(f"entries {a} and {b} overlap")
    in_order = order == list(range(len(entries)))
    covered = sum(size for _, size in entries)
    sector_covered = sum((size + SECTOR - 1) // SECTOR * SECTOR for _, size in entries)
    zero_size = [i for i, (_, s) in enumerate(entries) if s == 0]

    hist = collections.Counter()
    rows = []
    sub = os.path.join(out_dir, name)
    if write:
        os.makedirs(sub, exist_ok=True)
    with open(cdp_path, "rb") as f:
        for i, (sec, size) in enumerate(entries):
            f.seek(sec * SECTOR)
            if write:
                blob = f.read(size)
                head = blob[:4096]
            else:
                head = f.read(min(size, 4096))
            label, ext = classify(head, size)
            hist[label] += 1
            fname = f"{i:04d}.{ext}"
            rows.append((i, sec, sec * SECTOR, size, label, head[:16].hex(), fname))
            if write:
                with open(os.path.join(sub, fname), "wb") as o:
                    o.write(blob)
    if write:
        with open(os.path.join(sub, "index.csv"), "w", newline="") as fh:
            w = csv.writer(fh)
            w.writerow(["index", "sector", "offset", "size", "type", "head16", "file"])
            w.writerows(rows)

    print(f"== {name}.CDP ({cdp_size:,} bytes) / {name}.TBL ({len(entries)} entries)")
    print(f"   entries sorted by sector: {in_order}; zero-size entries: {len(zero_size)} {zero_size[:10]}")
    print(f"   coverage: {covered:,} bytes = {100 * covered / cdp_size:.3f}% "
          f"(sector-rounded {100 * sector_covered / cdp_size:.3f}%)")
    print(f"   integrity: {'OK' if not problems else str(len(problems)) + ' problem(s)'}")
    for p in problems[:20]:
        print("     !", p)
    print("   types:")
    for label, n in hist.most_common():
        print(f"     {n:5d}  {label}")
    return not problems


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("game_dir", help="ExtractedGameFiles directory (contains BIN/)")
    ap.add_argument("-o", "--out", default=os.path.join("out", "cdp"), help="output directory (default out/cdp)")
    ap.add_argument("--no-write", action="store_true", help="only verify and summarise")
    args = ap.parse_args()
    ok = True
    for name in ARCHIVES:
        ok &= process(args.game_dir, name, args.out, not args.no_write)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
