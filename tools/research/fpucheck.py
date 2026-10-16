"""Check the float add/sub rule of the translated code (research only).

Usage: python tools/research/fpucheck.py [--all]

For every function in src/game with a `// EE: 0x...` line, counts the COP1 single-precision
add-type instructions of the original (add.s, sub.s, adda.s, suba.s, madd.s, msub.s, madda.s,
msuba.s; function end = next function start in out/research/funcs.json) and the fpu::Add / fpu::Sub
/ fpu::MulAdd / fpu::UnsignedToFloat (one add.s) calls in the C++ body (up to the next `// EE:`
line). Prints the functions where they differ (all functions with --all). A difference is not always an error: an add duplicated into a
branch-likely delay slot is one source operation, and a helper shared by several functions holds
calls of more than one EE function. Check each listed function against its disassembly
(.claude/rules/game-code.md, Floating point). A function whose helpers sit in loops or lambdas (so
the call sites cannot match the instruction count) carries a line `// fpucheck: verified N` in its
body after it was checked by hand against the disassembly; it is not listed while N equals the
instruction count. Reads the ELF and the repository; no game data.
"""
import bisect
import glob
import json
import os
import re
import sys

from common import ROOT, elf

OPS = {0x00, 0x01, 0x18, 0x19, 0x1C, 0x1D, 0x1E, 0x1F}
EE = re.compile(r"^// EE: (0x[0-9A-Fa-f]{8})\s*$")
CALL = re.compile(r"\bfpu::(Add|Sub|MulAdd|UnsignedToFloat)\(")
VERIFIED = re.compile(r"//\s*fpucheck: verified (\d+)")


def main():
    show_all = "--all" in sys.argv
    funcs = json.load(open(os.path.join(ROOT, "out", "research", "funcs.json")))["all"]
    starts = sorted(int(f["address"], 16) for f in funcs)
    e = elf()
    rows = []
    for path in sorted(glob.glob(os.path.join(ROOT, "src", "game", "*.cpp"))):
        lines = open(path, encoding="utf-8").read().splitlines()
        marks = [(i, int(m.group(1), 16)) for i, l in enumerate(lines) if (m := EE.match(l))]
        for k, (i, addr) in enumerate(marks):
            body = lines[i + 1:marks[k + 1][0] if k + 1 < len(marks) else len(lines)]
            calls = sum(len(CALL.findall(l)) for l in body if not l.lstrip().startswith("//"))
            j = bisect.bisect_right(starts, addr)
            end = starts[j] if j < len(starts) else addr + 0x1000
            ops = 0
            for x in range(addr, end, 4):
                w = e.u32(x)
                if w >> 26 == 0x11 and (w >> 21) & 31 == 0x10 and (w & 63) in OPS:
                    ops += 1
            verified = [int(m.group(1)) for l in body if (m := VERIFIED.search(l))]
            if show_all or (ops != calls and verified != [ops]):
                rows.append((os.path.relpath(path, ROOT), addr, ops, calls))
    for path, addr, ops, calls in rows:
        print(f"{path:32} 0x{addr:08X}  original {ops:3}  fpu calls {calls:3}")
    print(f"{len(rows)} functions listed")


if __name__ == "__main__":
    main()
