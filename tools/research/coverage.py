"""Coverage table of game functions (docs/port/coverage-functions.csv: address,name,subsystem,status,note).

Usage:
  python tools/research/coverage.py              print the per-subsystem table (Markdown)
  python tools/research/coverage.py --refresh    also refresh the name column from out/research/funcs.json
                                                 and add rows (status unknown) for functions not yet listed
"""
import collections
import csv
import sys
from common import COVERAGE, funcs

rows = {int(r["address"], 16): r for r in csv.DictReader(open(COVERAGE, encoding="utf-8"))}
if "--refresh" in sys.argv:
    for a, v in funcs().items():
        r = rows.setdefault(a, {"address": f"0x{a:08X}", "name": v["name"], "subsystem": "unsorted",
                                "status": "unknown", "note": ""})
        r["name"] = v["name"]
    with open(COVERAGE, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=["address", "name", "subsystem", "status", "note"], lineterminator="\n")
        w.writeheader()
        for a in sorted(rows):
            w.writerow(rows[a])
T = collections.defaultdict(collections.Counter)
for r in rows.values():
    T[r["subsystem"]][r["status"]] += 1
    T["TOTAL"][r["status"]] += 1
print("| Subsystem | Functions | understood | partly | unknown | dead |\n|---|---|---|---|---|---|")
for k in sorted(T, key=lambda k: (k == "TOTAL", -sum(T[k].values()))):
    c = T[k]
    print(f"| {k} | {sum(c.values())} | {c['understood']} | {c['partly']} | {c['unknown']} | {c['dead']} |")
