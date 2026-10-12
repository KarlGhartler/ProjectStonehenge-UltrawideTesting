"""Integrity checks of the decompilation records (read-only).

Checks:
  1. docs/symbols.csv names against Ghidra (functions from funcs.json, data/global labels from list_globals);
     coverage-functions.csv names against Ghidra.
  2. Coverage totals: functions = understood + partly + unknown + dead, per subsystem, against the
     table in docs/STATUS.md; symbol counts against STATUS.md.
  3. Classification: every Ghidra function inside .text is either in the game range (and then has a
     coverage row) or library code (outside it); symbols.csv kinds agree with the range.
  4. Dead-code consistency: reachability from main over the raw call graph (reach.py), and again with
     the never-requested scenes 1 and 3 removed from the roots (they are reached only through the scene
     table); compared with the coverage "dead" status.

Usage: python tools/research/validate.py      (needs out/research/funcs.json and reach.json, Ghidra running)
Output: report on stdout; nothing is written to the repo.
"""
import csv
import json
import re
import urllib.parse
import urllib.request
from collections import Counter, defaultdict
from common import COVERAGE, SYMBOLS, ROOT, GHIDRA_URL, PROGRAM, GAME_LO, GAME_HI, TEXT_LO, TEXT_HI, load_json

SCENE_ONLY = {0x00211C58: "scene 1 SceneMenuEntry", 0x00211CA8: "scene 3 SceneMovie"}
problems = []


def bad(msg):
    problems.append(msg)
    print("  !", msg)


def ghidra_globals():
    out, off = {}, 0
    while True:
        q = urllib.parse.urlencode({"program": PROGRAM, "limit": 500, "offset": off, "filter": "all"})
        with urllib.request.urlopen(GHIDRA_URL + "list_globals?" + q, timeout=120) as r:
            d = json.loads(r.read().decode("utf-8"))
        for g in d["globals"]:
            m = re.match(r"(\S+) @ ([0-9a-fA-F]+)", g)
            if m:
                out.setdefault(int(m.group(2), 16), set()).add(m.group(1))
        off += 500
        if off >= d["total"]:
            return out


funcs = load_json("funcs.json")
gfun = {int(f["address"], 16): f["name"] for f in funcs["all"]}
sym = list(csv.DictReader(open(SYMBOLS, encoding="utf-8")))
cov = list(csv.DictReader(open(COVERAGE, encoding="utf-8")))
covd = {int(r["address"], 16): r for r in cov}

print("1. Names")
glob = ghidra_globals()
default = re.compile(r"^(FUN|DAT|LAB|thunk_FUN)_[0-9a-fA-F]{8}$")
for r in sym:
    a = int(r["address"], 16)
    if r["kind"] in ("data", "global"):
        names = glob.get(a, set())
        if r["name"] not in names:
            bad(f"symbols.csv {r['address']} {r['name']} ({r['kind']}): Ghidra labels {sorted(names) or 'none'}")
        continue
    g = gfun.get(a)
    if g is None:
        bad(f"symbols.csv {r['address']} {r['name']}: no Ghidra function at this address")
    elif r["status"] == "hypothesis":
        if g != r["name"] and not default.match(g):
            bad(f"symbols.csv {r['address']} hypothesis {r['name']}: Ghidra has {g}")
    elif g != r["name"]:
        bad(f"symbols.csv {r['address']} {r['name']}: Ghidra has {g}")
for a, r in covd.items():
    g = gfun.get(a)
    if g is None:
        bad(f"coverage {r['address']}: no Ghidra function")
    elif g.lower() != r["name"].lower():
        bad(f"coverage {r['address']} {r['name']}: Ghidra has {g}")
symnames = Counter(r["name"] for r in sym)
for n, c in symnames.items():
    if c > 1:
        bad(f"symbols.csv: name {n} used {c} times")

print("2. Totals")
st = Counter(r["status"] for r in cov)
print("  coverage rows", len(cov), dict(st))
if len(cov) != sum(st.values()):
    bad("status counts do not add up")
status_md = open(ROOT + "/docs/STATUS.md", encoding="utf-8").read()
m = re.search(r"\*\*([\d,]+) functions\*\*", status_md)
if m and int(m.group(1).replace(",", "")) != len(cov):
    bad(f"STATUS.md says {m.group(1)} functions, coverage has {len(cov)}")
for label in ("understood", "partly", "dead", "unknown"):
    m = re.search(r"\| " + label + r"[^|]*\| ([\d,]+) \|", status_md)
    if m and int(m.group(1).replace(",", "")) != st.get(label, 0):
        bad(f"STATUS.md {label} = {m.group(1)}, coverage = {st.get(label, 0)}")
per = defaultdict(Counter)
for r in cov:
    per[r["subsystem"]][r["status"]] += 1
for line in status_md.splitlines():
    m = re.match(r"\| ([a-z-]+) \| (\d+) \| (\d+)[^|]*\| (0|\d+)", line)
    if m and m.group(1) in per:
        s, n, u, d = m.group(1), int(m.group(2)), int(m.group(3)), int(m.group(4))
        c = per[s]
        if (n, u, d) != (sum(c.values()), c["understood"], c["dead"]):
            bad(f"STATUS.md row {s}: {n}/{u}/{d}, coverage {sum(c.values())}/{c['understood']}/{c['dead']}")
m = re.search(r"Names: ([\d,]+) symbols in `symbols.csv` \(([\d,]+) confirmed game names, ([\d,]+) game hypotheses\)", status_md)
gk = Counter((r["kind"], r["status"]) for r in sym)
print("  symbols", len(sym), "game confirmed", gk[("game", "confirmed")], "game hypothesis", gk[("game", "hypothesis")])
if m:
    want = (len(sym), gk[("game", "confirmed")], gk[("game", "hypothesis")])
    got = tuple(int(x.replace(",", "")) for x in m.groups())
    if want != got:
        bad(f"STATUS.md symbol counts {got}, symbols.csv {want}")

print("3. Classification")
text = [a for a in gfun if TEXT_LO <= a < TEXT_HI]
game = [a for a in text if GAME_LO <= a < GAME_HI]
lib = [a for a in text if not GAME_LO <= a < GAME_HI]
other = [a for a in gfun if not TEXT_LO <= a < TEXT_HI]
print(f"  Ghidra functions {len(gfun)}: .text {len(text)} (game {len(game)}, library {len(lib)}), outside .text {len(other)}")
for a in game:
    if a not in covd:
        bad(f"game function {a:08X} {gfun[a]} has no coverage row")
for a in covd:
    if a not in gfun or not GAME_LO <= a < GAME_HI:
        bad(f"coverage row {a:08X} not a game-range Ghidra function")
for r in sym:
    a = int(r["address"], 16)
    ingame = GAME_LO <= a < GAME_HI
    if r["kind"] == "game" and not ingame:
        bad(f"symbols.csv {r['address']} {r['name']} kind game outside the game range")
    if r["kind"] in ("sdk", "libc", "libgcc", "kernel") and ingame:
        bad(f"symbols.csv {r['address']} {r['name']} kind {r['kind']} inside the game range")
if other:
    print("  outside .text:", ", ".join(f"{a:08X} {gfun[a]}" for a in sorted(other)[:10]), "..." if len(other) > 10 else "")

print("4. Dead code")
rj = load_json("reach.json")
edges = {int(k, 16): v for k, v in rj["edges"].items()}
roots = set(rj["roots"])


def reach(rs):
    seen, stck = set(), list(rs)
    while stck:
        x = stck.pop()
        if x in seen:
            continue
        seen.add(x)
        stck += edges.get(x, ())
    return seen


full = reach(roots)
strict = reach(roots - set(SCENE_ONLY))
dead = {a for a, r in covd.items() if r["status"] == "dead"}
gset = set(covd)
print(f"  reachable (all data roots): {len(full & gset)}; without scenes 1/3 as roots: {len(strict & gset)}")
print(f"  coverage dead: {len(dead)}")
for a in sorted((gset - full) - dead):
    bad(f"{a:08X} unreachable but not dead")
for a in sorted(strict & dead):
    bad(f"{a:08X} {covd[a]['name']} reachable from main but marked dead")
only_scene = sorted((full - strict) & gset)
nd = [a for a in only_scene if covd[a]["status"] != "dead"]
print(f"  reachable only through scenes 1/3: {len(only_scene)} functions, {len(nd)} of them not marked dead:")
for a in nd:
    print(f"    {a:08X} {covd[a]['subsystem']}/{covd[a]['status']} {covd[a]['note'][:70]}")
dead_md = open(ROOT + "/docs/port/dead-code.md", encoding="utf-8").read()
missing = [a for a in only_scene if f"{a:08X}" not in dead_md.upper()]
print(f"  of those, not mentioned in dead-code.md: {len(missing)}", " ".join(f"{a:08X}" for a in missing))

print()
print("problems:", len(problems))
