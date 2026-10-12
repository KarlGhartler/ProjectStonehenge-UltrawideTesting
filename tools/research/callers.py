"""Direct callers (jal/j sites) and data references of functions, from out/research/rawcalls.json.

Usage: python tools/research/callers.py ADDR [ADDR...]
"""
import sys
from common import load_json, coverage

J = load_json("rawcalls.json")
cov = coverage()
for x in sys.argv[1:]:
    a = int(x, 16)
    cs = J["calls"].get(f"{a:08X}", [])
    owners = sorted({o for s, o in cs if o is not None})
    print(f"{a:08X} {cov.get(a, {}).get('name', '')}: {len(owners)} caller functions")
    for o in owners:
        r = cov.get(o, {})
        print(f"   {o:08X} [{r.get('subsystem', '?')}/{r.get('status', '?')}] {r.get('note', '')[:70]}")
    for d in J["data"].get(f"{a:08X}", []):
        print(f"   data pointer at {d:08X}")
