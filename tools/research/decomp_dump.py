"""Cache Ghidra decompiler output (read-only, via the ghidra-mcp HTTP plugin) in out/research/decomp/<ADDR>.c.

Usage: python tools/research/decomp_dump.py [address-list-file] [-f]
Without a list: every game function in out/research/funcs.json that is not cached yet. -f re-decompiles.
"""
import json
import os
import sys
import urllib.parse
import urllib.request
from common import GHIDRA_URL, PROGRAM, DECOMP, load_json

os.makedirs(DECOMP, exist_ok=True)
files = [a for a in sys.argv[1:] if not a.startswith("-")]
addrs = sorted(load_json("funcs.json")["game"]) if not files else [l.strip() for l in open(files[0]) if l.strip()]
force = "-f" in sys.argv
todo = [a for a in addrs if force or not os.path.exists(os.path.join(DECOMP, f"{int(a, 16):08X}.c"))]
for i in range(0, len(todo), 10):
    ch = todo[i:i + 10]
    q = urllib.parse.urlencode({"program": PROGRAM, "functions": ",".join(ch), "fields": "decompiled_code",
                                "include_call_context": "false"})
    try:
        r = json.loads(urllib.request.urlopen(GHIDRA_URL + "get_functions?" + q, timeout=600).read().decode("utf-8"))
    except Exception as e:  # keep going; rerun later for the failures
        print("ERR", ch, e)
        continue
    for k, v in r["functions"].items():
        code = (v.get("decompiled_code") or "").replace("\r\n", "\n").strip() + "\n"
        open(os.path.join(DECOMP, f"{int(k, 16):08X}.c"), "w", encoding="utf-8").write(code)
print("done", len(todo))
