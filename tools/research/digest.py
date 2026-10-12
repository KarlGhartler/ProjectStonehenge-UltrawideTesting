"""Structural digest of cached decompiled functions, for reading large functions quickly.

Usage: python tools/research/digest.py ADDR [ADDR ...] [--head N]
Prints per function: size, callees (with current names), data addresses read (0x002Bxxxx/0x0040xxxx...),
GIF/GS constants (PRIM-like values, A+D register ids, packed tags), random sources (NextRandomFloat, rand,
effect LCG multipliers 0x10DCD/0xEE29), libm and VU0 calls, record field offsets of param_1, loop count,
float constants, and the first N lines of the body (default 25).
Needs out/research/decomp/ (decomp_dump.py) and out/research/funcs.json (ghidra_dump.py).
"""
import collections, json, os, re, sys
from common import WORK

sys.stdout.reconfigure(encoding="utf-8")
args = [a for a in sys.argv[1:]]
head = 25
if "--head" in args:
    i = args.index("--head"); head = int(args[i + 1]); del args[i:i + 2]
funcs = json.load(open(os.path.join(WORK, "funcs.json")))
names = {int(f["address"], 16): f["name"] for f in funcs["all"]}

for a in args:
    addr = int(a, 16)
    p = os.path.join(WORK, "decomp", f"{addr:08X}.c")
    src = open(p, encoding="utf-8", errors="replace").read()
    body = "\n".join(l for l in src.splitlines() if not re.match(r"^\s*(undefined\d*|int|uint|float|char|byte|short|ushort|long|ulong|bool|qword|code)\s*\*?\s*\w+(\s*\[\d+\])?;\s*$", l))
    calls = collections.Counter(re.findall(r"\b(FUN_[0-9a-f]{8}|sce\w+|[a-z]+f|__\w+|rand|srand|NextRandomFloat)\s*\(", body))
    calls = {(names.get(int(c[4:], 16), c) if c.startswith("FUN_") else c): n for c, n in calls.items() if c not in ("if", "while", "for", "switch", "return", "sizeof")}
    data = collections.Counter(re.findall(r"DAT_(00[2-4][0-9a-f]{5}|7000[0-9a-f]{4})", body))
    consts = collections.Counter(re.findall(r"0x([0-9a-f]{3,16})\b", body))
    prim_like = sorted({c for c in consts if re.fullmatch(r"(5)?[0-9a-f]{0,3}(5|1)[0-9a-f]{2}", c) and len(c) <= 6} & {c for c in consts if int(c, 16) < 0x800}, key=lambda x: int(x, 16))
    tags = sorted({c for c in consts if len(c) >= 15})
    fields = sorted({int(x, 16) for x in re.findall(r"param_1 \+ 0x([0-9a-f]+)", body)})
    loops = len(re.findall(r"\bdo \{|\bwhile\s*\(", body))
    floats = sorted({f for f in re.findall(r"\b(\d+\.\d+)\b", body)}, key=float)
    lcg = [k for k in ("0x10dcd", "0xee29") if k in body]
    print(f"### {addr:08X} {names.get(addr, '')}  {len(src)} B, {loops} loops")
    print("  calls:", ", ".join(f"{k}×{v}" for k, v in sorted(calls.items(), key=lambda x: -x[1])))
    if data: print("  data:", ", ".join(f"{k}×{v}" for k, v in sorted(data.items())))
    if tags: print("  64-bit constants:", ", ".join(tags[:12]))
    if prim_like: print("  small hex constants:", ", ".join(prim_like[:24]))
    if lcg: print("  effect LCG:", lcg)
    if fields: print("  param_1 fields:", ", ".join(hex(f) for f in fields[:40]))
    if floats: print("  float constants:", ", ".join(floats[:24]))
    gw = collections.Counter(m for m in re.findall(r"(DAT_00[0-9a-f]{6}|_DAT_00[0-9a-f]{6}|g_\w+)(?:\[[^\]]*\])?\s*=[^=]", body))
    if gw: print("  global writes:", ", ".join(f"{k}×{v}" for k, v in gw.items()))
    rw = sorted({int(x, 16) for x in re.findall(r"\*\([^)]*\)\((?:iVar\d+|param_1|\(int\)param_1) \+ 0x([0-9a-f]+)\)\s*=[^=]", body)})
    if rw: print("  writes through param_1/locals at offsets:", ", ".join(hex(x) for x in rw[:30]))
    rnd = {k: len(re.findall(r"" + k + r"\(", body)) for k in ("FUN_00100dc8", "rand", "NextRandomFloat")}
    rnd = {k: v for k, v in rnd.items() if v}
    if rnd: print("  random calls:", rnd)
    eset = sorted(set(re.findall(r"0x6259[0-8]", body)))
    if eset: print("  effect settings read:", ", ".join(eset))
    prims = sorted(set(re.findall(r"= (0x(?:[0-9a-f]?[15][0-9a-f]{2}|5151\w*|531\w+|5353\w+|d31\w+))[;)]", body)))
    if prims: print("  PRIM/REGLIST values:", ", ".join(prims[:16]))
    tests = sorted(set(re.findall(r"0x20054[0-9a-f]{11}|0x[0-9a-f]{2}00000044|0x8000000044|0x8000000048|0x8000000049|0x800000004a", body)))
    if tests: print("  TEST/ALPHA values:", ", ".join(tests))
    lines = [l for l in body.splitlines() if l.strip()][:head]
    print("\n".join("    " + l for l in lines))
