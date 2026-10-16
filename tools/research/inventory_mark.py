"""Mark functions of docs/port/s5-blockA-inventory.csv as documented (research aid).

Usage: python tools/research/inventory_mark.py LIST_FILE [STATUS]
LIST_FILE holds hex addresses (without 0x) separated by whitespace; STATUS defaults to documented-full.
Prints the counts per status afterwards.
"""
import collections
import csv
import sys

path = "docs/port/s5-blockA-inventory.csv"
done = {int(t, 16) for t in open(sys.argv[1]).read().split()}
status = sys.argv[2] if len(sys.argv) > 2 else "documented-full"
rows = list(csv.reader(open(path, encoding="utf-8")))
for r in rows[1:]:
    if int(r[0], 16) in done:
        r[6] = status
with open(path, "w", newline="", encoding="utf-8") as f:
    csv.writer(f).writerows(rows)
print(collections.Counter(r[6] for r in rows[1:]))
