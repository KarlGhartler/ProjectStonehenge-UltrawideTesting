"""Shared helpers for the research tools in tools/research/ (research aids only, never used by the port).

Paths
- Repository root: two levels above this file.
- Extracted disc folder: the `--extracted DIR` argument, else the AC04_EXTRACTED environment variable,
  else the "Extracted disc files" line of CLAUDE.local.md. SLUS_201.52 is read from there (read-only).
- Work/cache folder: out/research/ (gitignored): funcs.json, decomp/, rawcalls.json, reach.json, ...
- Data files written by the tools: docs/port/coverage-functions.csv, docs/symbols.csv, docs/rename-log.csv.
- Ghidra MCP plugin URL: GHIDRA_MCP_URL environment variable, default http://127.0.0.1:8089/.

No game data is embedded here; everything is read at run time from the user's extracted files.
"""
import os
import re
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
WORK = os.path.join(ROOT, "out", "research")
DECOMP = os.path.join(WORK, "decomp")
COVERAGE = os.path.join(ROOT, "docs", "port", "coverage-functions.csv")
SYMBOLS = os.path.join(ROOT, "docs", "symbols.csv")
RENAME_LOG = os.path.join(ROOT, "docs", "rename-log.csv")
GHIDRA_URL = os.environ.get("GHIDRA_MCP_URL", "http://127.0.0.1:8089").rstrip("/") + "/"
PROGRAM = "SLUS_201.52"

GAME_LO, GAME_HI = 0x001000D8, 0x002616E8      # game code range [lo, hi)
TEXT_LO, TEXT_HI = 0x00100000, 0x00297B34      # .text (game + SDK/libc)

os.makedirs(WORK, exist_ok=True)


def work(*parts):
    """Path inside out/research/."""
    return os.path.join(WORK, *parts)


def extracted_dir(argv=None):
    """Extracted-disc folder from --extracted, AC04_EXTRACTED or CLAUDE.local.md."""
    argv = sys.argv if argv is None else argv
    if "--extracted" in argv:
        return argv[argv.index("--extracted") + 1]
    if os.environ.get("AC04_EXTRACTED"):
        return os.environ["AC04_EXTRACTED"]
    local = os.path.join(ROOT, "CLAUDE.local.md")
    if os.path.exists(local):
        for line in open(local, encoding="utf-8"):
            if "Extracted disc files" in line:
                m = re.search(r"`([^`]+)`", line)
                if m:
                    return m.group(1).rstrip("\\/")
    sys.exit("extracted disc folder unknown: pass --extracted DIR or set AC04_EXTRACTED")


class Elf:
    """Read-only virtual-address view of SLUS_201.52 through its PT_LOAD program headers."""

    def __init__(self, path=None):
        path = path or os.path.join(extracted_dir(), "SLUS_201.52")
        self.data = open(path, "rb").read()
        phoff, = struct.unpack_from("<I", self.data, 0x1C)
        phentsize, phnum = struct.unpack_from("<HH", self.data, 0x2A)
        self.segs = []
        for i in range(phnum):
            typ, off, va, pa, filesz, memsz, *_ = struct.unpack_from("<8I", self.data, phoff + i * phentsize)
            if typ == 1:
                self.segs.append((va, off, filesz, memsz))

    def read(self, va, n):
        for sva, off, fsz, msz in self.segs:
            if sva <= va < sva + msz:
                o = va - sva
                b = self.data[off + o: off + min(o + n, fsz)]
                return b + bytes(n - len(b))
        raise ValueError(f"unmapped {va:08X}")

    def u32(self, va):
        try:
            return struct.unpack("<I", self.read(va, 4))[0]
        except ValueError:
            return None

    def cstr(self, va):
        return self.read(va, 256).split(b"\0")[0].decode("latin1")

    def words(self):
        """Yield (va, word) for every loaded word (file-backed part of each segment)."""
        for va, off, fsz, msz in self.segs:
            for a in range(va, va + fsz - 3, 4):
                yield a, struct.unpack_from("<I", self.data, off + a - va)[0]


_ELF = None


def elf():
    global _ELF
    if _ELF is None:
        _ELF = Elf()
    return _ELF


def load_json(name):
    import json
    p = work(name)
    if not os.path.exists(p):
        sys.exit(f"{p} missing: run the tool that produces it first (see tools/research/README.md)")
    return json.load(open(p, encoding="utf-8"))


def funcs():
    """Game functions from out/research/funcs.json (ghidra_dump.py): {address int: record}."""
    return {int(k, 16): v for k, v in load_json("funcs.json")["game"].items()}


def coverage():
    import csv
    return {int(r["address"], 16): r for r in csv.DictReader(open(COVERAGE, encoding="utf-8"))}
