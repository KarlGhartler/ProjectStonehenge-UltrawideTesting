"""Compare a guest RAM dump of the port with a PCSX2 save state (research only).

The port writes dumps with development-build flags (e.g. --dump-ram-after-load) to
<user data>\\dumps\\: 32 MiB RAM followed by the 16 KiB scratchpad. The save state must be stored
uncompressed (tools/p2s_read.py). Neither file is ever copied into the repo.

Usage:
    python -I tools/research/ramdiff.py DUMP.bin STATE.p2s [--range START END] [--max N] [--sections]
                                        [--values] [--expected [CSV ...]]

(-I: tools/research/dis.py would otherwise shadow the standard library module.)

Output: differing byte ranges, merged when closer than 16 bytes, with the ELF section and the
nearest symbol from docs/symbols.csv. By default only addresses and counts are printed, never memory
contents. --values also prints the first 16 bytes of each range from both sides: that output
contains game memory, keep it in the terminal or under out/ and never commit it.
--expected classifies each range with docs/port/ramdiff-expected.csv plus any slice files given
(e.g. docs/port/ramdiff-expected-s1.csv): the bytes of a range covered by expected entries are
counted per category; the uncovered parts are listed as unexplained.
"""
import argparse
import bisect
import csv
import os
import sys
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
RAM_SIZE = 32 * 1024 * 1024
SPR_SIZE = 16 * 1024

# docs/platform/elf-sections.md
SECTIONS = [
    (0x00100000, 0x00297B34, ".text"),
    (0x00297C00, 0x0029C900, ".vutext"),
    (0x0029C980, 0x004021E8, ".data"),
    (0x004021F0, 0x004036F0, ".vudata"),
    (0x00403700, 0x0040D7D8, ".rodata"),
    (0x0040D800, 0x0040D9D0, ".gcc_except_table"),
    (0x0040DA00, 0x00410470, ".lit4"),
    (0x00410480, 0x0041172C, ".sdata"),
    (0x00411780, 0x0041193C, ".sbss"),
    (0x00411980, 0x004CC74C, ".bss"),
    (0x004CC74C, 0x01F00000, "heap"),
    (0x01F00000, 0x02000000, "stack"),
]


def section_of(addr):
    for lo, hi, name in SECTIONS:
        if lo <= addr < hi:
            return name
    return "low/kernel" if addr < 0x00100000 else "?"


def load_symbols():
    path = os.path.join(ROOT, "docs", "symbols.csv")
    rows = []
    with open(path, encoding="utf-8") as f:
        for r in csv.DictReader(f):
            rows.append((int(r["address"], 16), r["name"]))
    rows.sort()
    return rows


def nearest_symbol(symbols, keys, addr, limit=0x400):
    i = bisect.bisect_right(keys, addr) - 1
    if i < 0:
        return ""
    a, name = symbols[i]
    if addr - a > limit or section_of(a) != section_of(addr):
        return ""
    return f"{name}+0x{addr - a:X}"


def load_expected(path):
    rows = []
    with open(path, encoding="utf-8") as f:
        for r in csv.DictReader(f):
            rows.append((int(r["start"], 16), int(r["end"], 16), r["category"]))
    return rows


def classify(expected, s, e):
    """Splits [s, e) over the expected entries: returns ([(category, bytes)], [uncovered (s, e)])."""
    covered, rest, pos = [], [], s
    while pos < e:
        hit = next(((lo, hi, cat) for lo, hi, cat in expected if lo <= pos < hi), None)
        if hit is not None:
            end = min(e, hit[1])
            covered.append((hit[2], end - pos))
            pos = end
            continue
        nxt = min([lo for lo, _, _ in expected if pos < lo < e] + [e])
        rest.append((pos, nxt))
        pos = nxt
    return covered, rest


def diff_ranges(a, b, base, lo, hi, gap=16):
    ranges = []
    i = lo
    while i < hi:
        if a[i] != b[i]:
            start = i
            last = i
            j = i + 1
            while j < hi and j - last <= gap:
                if a[j] != b[j]:
                    last = j
                j += 1
            ranges.append((base + start, base + last + 1))
            i = last + 1
        else:
            i += 1
    return ranges


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("dump")
    ap.add_argument("state")
    ap.add_argument("--range", nargs=2, metavar=("START", "END"))
    ap.add_argument("--max", type=int, default=200, help="ranges to list (default 200)")
    ap.add_argument("--sections", action="store_true", help="only print the per-section summary")
    ap.add_argument("--expected", nargs="*", default=None,
                    help="hide ranges explained in these CSVs (always docs/port/ramdiff-expected.csv, plus "
                         "slice files such as docs/port/ramdiff-expected-s1.csv)")
    ap.add_argument("--values", action="store_true",
                    help="also print the first 16 bytes of each range (port / PCSX2); contains game memory")
    args = ap.parse_args()

    data = open(args.dump, "rb").read()
    if len(data) < RAM_SIZE:
        sys.exit("dump is smaller than 32 MiB")
    port_ram, port_spr = data[:RAM_SIZE], data[RAM_SIZE:RAM_SIZE + SPR_SIZE]
    z = zipfile.ZipFile(args.state)
    ps2_ram = z.read("eeMemory.bin")
    ps2_spr = z.read("Scratchpad.bin")

    lo, hi = 0, RAM_SIZE
    if args.range:
        # END is exclusive: mask END - 1 so that 0x02000000 means "to the end of RAM".
        lo = int(args.range[0], 0) & 0x01FFFFFF
        hi = ((int(args.range[1], 0) - 1) & 0x01FFFFFF) + 1
    ranges = diff_ranges(port_ram, ps2_ram, 0, lo, hi)
    if not args.range and port_spr:
        ranges += [(0x70000000 + s, 0x70000000 + e) for s, e in diff_ranges(port_spr, ps2_spr, 0, 0, SPR_SIZE)]

    per_section = {}
    for s, e in ranges:
        name = "scratchpad" if s >= 0x70000000 else section_of(s)
        n, b = per_section.get(name, (0, 0))
        per_section[name] = (n + 1, b + (e - s))
    print(f"{len(ranges)} differing ranges")
    for name, (n, b) in sorted(per_section.items()):
        print(f"  {name:20} {n:7} ranges {b:10} bytes")
    if args.expected is not None:
        files = [os.path.join(ROOT, "docs", "port", "ramdiff-expected.csv")]
        files += [f for f in args.expected if os.path.abspath(f) != os.path.abspath(files[0])]
        expected = [row for f in files for row in load_expected(f)]
        per_cat, rest = {}, []
        def differs(a):
            if a >= 0x70000000:
                return port_spr[a - 0x70000000] != ps2_spr[a - 0x70000000]
            return port_ram[a] != ps2_ram[a]

        for s, e in ranges:
            covered, uncovered = classify(expected, s, e)
            # Merged ranges may include equal bytes; keep only uncovered parts that really differ.
            for us, ue in uncovered:
                hits = [a for a in range(us, ue) if differs(a)]
                if hits:
                    rest.append((hits[0], hits[-1] + 1))
            for cat, b in covered:
                n, total = per_cat.get(cat, (0, 0))
                per_cat[cat] = (n + 1, total + b)
        print("expected differences by category:")
        for cat, (n, b) in sorted(per_cat.items()):
            print(f"  {cat:20} {n:7} ranges {b:10} bytes")
        print(f"{len(rest)} unexplained ranges")
        ranges = rest
    if args.sections:
        return
    symbols = load_symbols()
    keys = [a for a, _ in symbols]
    for s, e in ranges[: args.max]:
        where = "scratchpad" if s >= 0x70000000 else section_of(s)
        print(f"  0x{s:08X}-0x{e:08X} ({e - s:6} bytes) {where:10} {nearest_symbol(symbols, keys, s)}")
        if args.values:
            n = min(e - s, 16)
            if s >= 0x70000000:
                a, b = port_spr[s - 0x70000000:][:n], ps2_spr[s - 0x70000000:][:n]
            else:
                a, b = port_ram[s:s + n], ps2_ram[s:s + n]
            print(f"      port  {a.hex(' ')}")
            print(f"      pcsx2 {b.hex(' ')}")
    if len(ranges) > args.max:
        print(f"  ... {len(ranges) - args.max} more")


if __name__ == "__main__":
    main()
