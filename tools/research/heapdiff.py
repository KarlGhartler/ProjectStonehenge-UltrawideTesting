#!/usr/bin/env python3
"""Work-heap differences between a port RAM dump and a PCSX2 save state, per allocation.

Inputs: the port's allocation log (--log-allocs, <user data>/dumps/allocs.txt), a port RAM dump
(--dump-ram-at ...) taken in the same run, the PCSX2 state, and the port executable (its PDB names
the function that made each allocation; Windows dbghelp through ctypes, no dependencies).

Only allocations still live at the dump are used: walking the log backwards, an allocation is
dropped when a later one overlaps it (the heap was rewound in between). Allocations made after
the dump (later vblank than --dump-vblank) are ignored when that option is given.

Usage: python -I tools/research/heapdiff.py ALLOCS.txt DUMP.bin STATE.p2s EXE [--dump-vblank N]
                                            [--top N] [--by-caller] [--min-diff N]
                                            [--detail CALLER [--values]] [--mask DUMP2.bin]
--mask: a second port dump of the same run made with another --heap-fill byte; bytes that differ
between the two dumps were never written by the game since the last heap reset (leftovers) and
are not compared.
--detail lists the differing byte ranges (offsets inside the allocation) of the first --top
allocations made by CALLER; --values adds the differing words of both sides (game memory: terminal
only, never commit). Without --values it prints addresses, sizes and counts only.
"""
import bisect
import ctypes
import ctypes.wintypes as wt
import struct
import sys
import zipfile
from collections import defaultdict

RAM = 32 * 1024 * 1024


class SYMBOL_INFO(ctypes.Structure):
    _fields_ = [("SizeOfStruct", wt.ULONG), ("TypeIndex", wt.ULONG), ("Reserved", ctypes.c_uint64 * 2),
                ("Index", wt.ULONG), ("Size", wt.ULONG), ("ModBase", ctypes.c_uint64), ("Flags", wt.ULONG),
                ("Value", ctypes.c_uint64), ("Address", ctypes.c_uint64), ("Register", wt.ULONG),
                ("Scope", wt.ULONG), ("Tag", wt.ULONG), ("NameLen", wt.ULONG), ("MaxNameLen", wt.ULONG),
                ("Name", ctypes.c_char * 512)]


class Symbols:
    BASE = 0x140000000

    def __init__(self, exe):
        self.dbg = ctypes.WinDLL("dbghelp")
        self.h = wt.HANDLE(0x4AC04)
        self.dbg.SymSetOptions(0x2)  # SYMOPT_UNDNAME
        self.dbg.SymInitialize.argtypes = [wt.HANDLE, ctypes.c_char_p, wt.BOOL]
        self.dbg.SymLoadModuleEx.restype = ctypes.c_uint64
        self.dbg.SymLoadModuleEx.argtypes = [wt.HANDLE, wt.HANDLE, ctypes.c_char_p, ctypes.c_char_p,
                                             ctypes.c_uint64, wt.DWORD, ctypes.c_void_p, wt.DWORD]
        self.dbg.SymFromAddr.argtypes = [wt.HANDLE, ctypes.c_uint64, ctypes.POINTER(ctypes.c_uint64),
                                         ctypes.POINTER(SYMBOL_INFO)]
        if not self.dbg.SymInitialize(self.h, None, False):
            raise SystemExit("SymInitialize failed")
        if not self.dbg.SymLoadModuleEx(self.h, None, exe.encode(), None, self.BASE, 0, None, 0):
            raise SystemExit("SymLoadModuleEx failed for " + exe)
        self.cache = {}

    def name(self, rva):
        if rva in self.cache:
            return self.cache[rva]
        sym = SYMBOL_INFO()
        sym.SizeOfStruct = 88
        sym.MaxNameLen = 511
        disp = ctypes.c_uint64()
        ok = self.dbg.SymFromAddr(self.h, self.BASE + rva, ctypes.byref(disp), ctypes.byref(sym))
        n = sym.Name.decode(errors="replace").replace("ac04::game::", "") if ok else "rva_%X" % rva
        self.cache[rva] = n
        return n


def main():
    args = sys.argv[1:]
    opts = {"--dump-vblank": None, "--top": "40", "--min-diff": "1", "--detail": None, "--mask": None}
    flags = set()
    pos = []
    i = 0
    while i < len(args):
        a = args[i]
        if a in opts:
            opts[a] = args[i + 1]
            i += 2
        elif a.startswith("--"):
            flags.add(a)
            i += 1
        else:
            pos.append(a)
            i += 1
    if len(pos) != 4:
        print(__doc__)
        return 2
    allocs_path, dump_path, state_path, exe = pos
    limit = int(opts["--dump-vblank"]) if opts["--dump-vblank"] else None
    allocs = []
    for line in open(allocs_path):
        if line.startswith("#"):
            continue
        v, addr, size, align, rva = line.split()
        v, addr, size, rva = int(v), int(addr, 16), int(size, 16), int(rva, 16)
        if limit is not None and v > limit:
            continue
        if size <= 0 or addr == 0:
            continue
        allocs.append((v, addr, size, rva))
    # Live set: walk backwards, keep allocations not overlapped by a later one.
    live, starts, ends = [], [], []  # covered intervals: disjoint, sorted by start
    for a in reversed(allocs):
        lo, hi = a[1], a[1] + a[2]
        k = bisect.bisect_left(starts, hi)  # intervals starting before hi: indexes < k
        if k > 0 and ends[k - 1] > lo:
            continue
        starts.insert(k, lo)
        ends.insert(k, hi)
        live.append(a)
    live.reverse()
    port = open(dump_path, "rb").read()[:RAM]
    with zipfile.ZipFile(state_path) as z:
        ref = z.read("eeMemory.bin")[:RAM]
    if opts["--mask"]:
        other = open(opts["--mask"], "rb").read()[:RAM]
        port = bytearray(port)
        ref = bytearray(ref)
        for k in range(len(port)):
            if port[k] != other[k]:
                port[k] = ref[k]  # leftover byte: treated as equal
        port, ref = bytes(port), bytes(ref)
    syms = Symbols(exe)
    rows = []
    for v, addr, size, rva in live:
        p, r = port[addr:addr + size], ref[addr:addr + size]
        diff = sum(1 for x, y in zip(p, r) if x != y) if p != r else 0
        rows.append((addr, size, diff, syms.name(rva), v))
    total = sum(r[1] for r in rows)
    print("live allocations: %d, %d bytes, %d differing bytes in %d allocations" % (
        len(rows), total, sum(r[2] for r in rows), sum(1 for r in rows if r[2])))
    min_diff = int(opts["--min-diff"])
    if opts["--detail"]:
        shown = 0
        for addr, size, diff, name, v in rows:
            if name != opts["--detail"] or not diff:
                continue
            spans, k = [], 0
            while k < size:
                if port[addr + k] != ref[addr + k]:
                    j = k
                    while j < size and port[addr + j] != ref[addr + j]:
                        j += 1
                    spans.append((k, j))
                    k = j
                else:
                    k += 1
            print("0x%08X size %X: %s" % (addr, size, " ".join("+%X..%X" % sp for sp in spans[:16])))
            if "--values" in flags:
                for lo, hi in spans[:8]:
                    w = lo & ~3
                    print("   +%-5X port %s  pcsx2 %s" % (w, port[addr + w:addr + w + 8].hex(), ref[addr + w:addr + w + 8].hex()))
            shown += 1
            if shown >= int(opts["--top"]):
                break
        return 0
    if "--by-caller" in flags:
        agg = defaultdict(lambda: [0, 0, 0, 0])
        for addr, size, diff, name, v in rows:
            g = agg[name]
            g[0] += 1
            g[1] += size
            g[2] += diff
            g[3] += 1 if diff else 0
        print("\n%-60s %6s %10s %10s %6s" % ("caller", "allocs", "bytes", "diff", "ndiff"))
        for name, g in sorted(agg.items(), key=lambda kv: -kv[1][2]):
            if g[2] >= min_diff:
                print("%-60s %6d %10d %10d %6d" % (name[:60], g[0], g[1], g[2], g[3]))
    else:
        print("\n%-10s %8s %8s  %s" % ("address", "size", "diff", "caller"))
        for addr, size, diff, name, v in sorted(rows, key=lambda r: -r[2])[:int(opts["--top"])]:
            if diff >= min_diff:
                print("0x%08X %8X %8d  %s" % (addr, size, diff, name))
    return 0


if __name__ == "__main__":
    sys.exit(main())
