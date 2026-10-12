"""Raw call graph from the ELF: every jal/j site in .text and every data word that points at a function start.

Usage: python tools/research/rawcalls.py
Input: out/research/funcs.json (function entries). Output: out/research/rawcalls.json
  {"calls": {"TARGET": [[site, owner], ...]}, "data": {"TARGET": [data address, ...]}}
The owner of a site is the nearest function entry at or below it, so switch-case code outside a Ghidra body still counts.
"""
import bisect
import json
from common import elf, funcs, work, TEXT_LO, TEXT_HI

G = funcs()
entries = sorted(G)
F = set(entries)


def owner(a):
    i = bisect.bisect_right(entries, a) - 1
    return entries[i] if i >= 0 else None


calls, data = {}, {}
for a, w in elf().words():
    if TEXT_LO <= a < 0x00297C00:
        if (w >> 26) in (2, 3):
            t = (w & 0x3FFFFFF) << 2
            if t in F:
                calls.setdefault(f"{t:08X}", []).append([a, owner(a)])
    elif w in F:
        data.setdefault(f"{w:08X}", []).append(a)
json.dump({"calls": calls, "data": data}, open(work("rawcalls.json"), "w"))
print(len(calls), "called functions,", len(data), "data-referenced functions")
