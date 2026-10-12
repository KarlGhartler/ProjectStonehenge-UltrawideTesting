"""Callee tree of game functions with their coverage status (Ghidra call graph from out/research/funcs.json).

Usage: python tools/research/calltree.py ADDR [ADDR...] [DEPTH] [--unk]
--unk prints only functions not marked understood/dead. Each function is printed once.
"""
import sys
from common import funcs, coverage

sys.stdout.reconfigure(encoding="utf-8", errors="replace")
G = funcs()
cov = coverage()
args = [a for a in sys.argv[1:] if not a.startswith("-")]
depth = int(args[-1]) if args and args[-1].isdigit() else 3
roots = [int(a, 16) for a in args if not a.isdigit()]
only_unk = "--unk" in sys.argv
seen = set()


def size(a):
    return int(G[a]["body_end"], 16) - int(G[a]["body_start"], 16) + 1


def walk(a, lv):
    if a in seen or a not in G:
        return
    seen.add(a)
    c = cov.get(a, {})
    st = c.get("status", "?")
    if not only_unk or st not in ("understood", "dead"):
        print("  " * lv + f"{a:08X} {c.get('name', '?')} [{c.get('subsystem', '?')}/{st}] {size(a)}B  {c.get('note', '')[:70]}")
    if lv < depth:
        for cc in G[a].get("callees", []):
            walk(int(cc["address"], 16), lv + 1)


for r in roots:
    walk(r, 0)
