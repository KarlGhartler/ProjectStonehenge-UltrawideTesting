"""List the game functions translated into src/game (the `// EE: 0x...` line above each one) and
write docs/port/translated-functions.csv (address, name, file, coverage status).

Usage: python tools/research/translated.py
Reads only the repository (src/game, docs/port/coverage-functions.csv); no game data.
"""
import csv
import glob
import os
import re

from common import ROOT, COVERAGE

OUT = os.path.join(ROOT, "docs", "port", "translated-functions.csv")
EE = re.compile(r"^// EE: (0x[0-9A-Fa-f]{8})\s*$")
DEF = re.compile(r"^[A-Za-z_][\w:<>\*& ]*?\s+\*?(\w+)\(")

status = {int(r["address"], 16): r for r in csv.DictReader(open(COVERAGE, encoding="utf-8"))}
rows = []
for path in sorted(glob.glob(os.path.join(ROOT, "src", "game", "*.cpp"))):
    lines = open(path, encoding="utf-8").read().splitlines()
    for i, line in enumerate(lines):
        m = EE.match(line)
        if not m:
            continue
        name = "?"
        for nxt in lines[i + 1:i + 6]:
            if nxt.startswith("//"):
                continue
            d = DEF.match(nxt)
            if d:
                name = d.group(1)
                break
        addr = int(m.group(1), 16)
        cov = status.get(addr)
        rows.append({"address": f"0x{addr:08X}", "name": name, "file": "src/game/" + os.path.basename(path),
                     "coverage_status": cov["status"] if cov else "not-in-coverage"})
rows.sort(key=lambda r: r["address"])
with open(OUT, "w", newline="", encoding="utf-8") as f:
    w = csv.DictWriter(f, fieldnames=["address", "name", "file", "coverage_status"], lineterminator="\n")
    w.writeheader()
    w.writerows(rows)
dead = sum(r["coverage_status"] == "dead" for r in rows)
missing = [r for r in rows if r["coverage_status"] == "not-in-coverage"]
print(f"{len(rows)} translated functions ({dead} dead), written to {os.path.relpath(OUT, ROOT)}")
for r in missing:
    print("  not in coverage list:", r["address"], r["name"])
