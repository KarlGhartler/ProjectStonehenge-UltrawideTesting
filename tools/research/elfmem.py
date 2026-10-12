"""Read words and strings from SLUS_201.52 by virtual address (read-only).

Usage:
  python tools/research/elfmem.py u32  ADDR COUNT    dump u32 words
  python tools/research/elfmem.py ptrs ADDR COUNT    u32 words, resolving pointers to C strings
  python tools/research/elfmem.py str  ADDR          C string at ADDR
  python tools/research/elfmem.py strs LO HI         all C strings in [LO, HI)
Options: --extracted DIR (default: CLAUDE.local.md).
"""
import sys
from common import elf


def isstr(e, va):
    try:
        s = e.cstr(va)
    except ValueError:
        return None
    return s if len(s) >= 3 and all(32 <= ord(c) < 127 for c in s) else None


if __name__ == "__main__":
    args = [a for a in sys.argv[1:] if a != "--extracted"]
    if "--extracted" in sys.argv:
        args.remove(sys.argv[sys.argv.index("--extracted") + 1])
    cmd, a = args[0], int(args[1], 16)
    e = elf()
    if cmd in ("u32", "ptrs"):
        for i in range(int(args[2], 0)):
            v = e.u32(a + 4 * i)
            s = isstr(e, v) if cmd == "ptrs" and v and 0x100000 <= v < 0x2000000 else None
            print(f"{a + 4 * i:08X}: {v:08X}" + (f"  {s!r}" if s else ""))
    elif cmd == "str":
        print(repr(e.cstr(a)))
    elif cmd == "strs":
        hi = int(args[2], 16)
        b = e.read(a, hi - a)
        i = 0
        while i < len(b):
            j = b.find(b"\0", i)
            j = len(b) if j < 0 else j
            if j - i >= 3:
                print(f"{a + i:08X}: {b[i:j].decode('latin1')!r}")
            i = j + 1
