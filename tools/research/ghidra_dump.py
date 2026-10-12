"""Dump the Ghidra function list (via the ghidra-mcp HTTP plugin, read-only) to out/research/funcs.json.

Usage: python tools/research/ghidra_dump.py
Output: {"all": [...every function...], "game": {"0x...": {name, body_start, body_end, callers, callees, plate_comment}}}
for the game range 0x001000D8-0x002616E7. Needed by most other research tools.
"""
import json
import urllib.parse
import urllib.request
from common import GHIDRA_URL, PROGRAM, GAME_LO, GAME_HI, work


def get(ep, **kw):
    kw.setdefault("program", PROGRAM)
    with urllib.request.urlopen(GHIDRA_URL + ep + "?" + urllib.parse.urlencode(kw), timeout=300) as r:
        return json.loads(r.read().decode("utf-8"))


funcs, off = [], 0
while True:
    d = get("find_functions", limit=500, offset=off)
    funcs += d["functions"]
    off += 500
    if off >= d["total"]:
        break
print(len(funcs), "functions")
game = [f for f in funcs if GAME_LO <= int(f["address"], 16) < GAME_HI]
print(len(game), "in game range")
out = {}
for i in range(0, len(game), 20):
    chunk = game[i:i + 20]
    d = get("get_functions", functions=",".join("0x" + f["address"] for f in chunk),
            fields="callers,callees,plate_comment,body_start,body_end", include_call_context="false")
    out.update(d["functions"])
json.dump({"all": funcs, "game": out}, open(work("funcs.json"), "w"))
print("details", len(out))
