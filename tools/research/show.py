"""Print cached decompiled functions (out/research/decomp/).

Usage:
  python tools/research/show.py ADDR [ADDR...]
  python tools/research/show.py --range LO HI [--sub SUBSYSTEM]   (subsystem from docs/port/coverage-functions.csv)
"""
import os
import sys
from common import DECOMP, coverage

sys.stdout.reconfigure(encoding="utf-8", errors="replace")
args = sys.argv[1:]
if args and args[0] == "--range":
    lo, hi = int(args[1], 16), int(args[2], 16)
    sub = args[4] if len(args) > 4 and args[3] == "--sub" else None
    addrs = [a for a, r in sorted(coverage().items()) if lo <= a < hi and (sub is None or r["subsystem"] == sub)]
else:
    addrs = [int(a, 16) for a in args]
for a in addrs:
    p = os.path.join(DECOMP, f"{a:08X}.c")
    if not os.path.exists(p):
        print(f"// {a:08X} MISSING (run decomp_dump.py)")
        continue
    print(f"// ===== {a:08X}")
    print("\n".join(l for l in open(p, encoding="utf-8").read().split("\n") if l.strip()))
