"""Assign every game function to exactly one translation slice (ADR-007, docs/port/slices.md).

Usage:
  python tools/research/slices.py            summary per slice and subsystem; writes out/research/slices.csv
  python tools/research/slices.py --list S   functions of slice S (e.g. S5f), with subsystem and status
  python tools/research/slices.py --vs       functions whose decompiled code reads game[0x25] / game[0x26]

Needs: out/research/reach.json (reach.py), docs/port/coverage-functions.csv,
docs/port/translated-functions.csv, the ELF (scene sub-state tables), out/research/decomp/ for --vs.

Assignment, in this order (first match wins):
  1. already translated (docs/port/translated-functions.csv)            -> S0
  2. dead (coverage status)                                            -> S8
  3. pinned functions (slice-specific code reached from shared roots)   -> their slice
  4. slice rules in order: closure of the rule's roots over the raw call graph (reach.json edges:
     jal/j and lui+addiu address builds), optionally limited to some subsystems
  5. fallback by subsystem (code reached only through data tables)      -> SUBSYSTEM_FALLBACK
Writes only out/research/slices.csv (addresses, names, slice ids; no game data).
"""
import csv
import os
import re
import sys

from common import ROOT, COVERAGE, DECOMP, elf, load_json, work

sys.stdout.reconfigure(encoding="utf-8", errors="replace")

TRANSLATED = os.path.join(ROOT, "docs", "port", "translated-functions.csv")


def table(addr, n):
    e = elf()
    return [e.u32(addr + 4 * i) for i in range(n)]


SCENE2 = table(0x002A1620, 31)   # scene 2 sub-state handlers
SCENE4 = table(0x00384640, 90)   # scene 4 sub-state handlers
SCENE5 = table(0x003B0190, 7)
SCENE6 = table(0x003F8708, 10)


def s4(*subs):
    return sorted({SCENE4[s] for s in subs})


def s2(*subs):
    return sorted({SCENE2[s] for s in subs})


FLIGHT_FRAME = 0x0010B150        # FlightPlayFrame (subs 0xA-0xC)
SIM_STEP = 0x0010C320            # FlightSimStep
RECORD_REPLAY = 0x0016E8B8       # RecordReplayFrame
WORLD_DRAW = [0x00139FE8, 0x00144CC0, 0x0013EC20, 0x0013EC38]

# (slice id, title, [(roots, subsystems or None)])
SLICES = [
    ("S1", "scene 6 startup", [([0x002613F0] + SCENE6, None)]),
    ("S2a", "front end: menu framework, title, main menu", [([0x002262D0] + s4(0, 1, 2, 3, 4, 5, 6), None)]),
    ("S2b", "front end: options, controller, BGM player, records, sound options, save/load",
     [(s4(*range(41, 52), *range(55, 62), *range(74, 90)), None)]),
    ("S3", "movies (in-menu player, interlude viewer, SMV)", [(s4(*range(7, 14), 52, 53, 54), None)]),
    ("S4", "front end: mission title, briefing, campaign menu, hangar, mission select",
     [(s4(*range(14, 37), *range(65, 74)), None)]),
    ("S5a", "mission load: scene object, data, sound banks, loading screen, create/start systems",
     [(s2(0, 1, 2, 3, 4, 5, 6, 7, 8, 0x1E), None)]),
    ("S5b", "static world rendering: terrain, sky, sea, clouds, objects, cameras",
     [([FLIGHT_FRAME] + WORLD_DRAW, {"render", "core", "file", "aircraft-data", "camera"})]),
    ("S5c", "player flight", [([FLIGHT_FRAME, SIM_STEP], {"flight", "collision", "camera", "input"}),
                              ([RECORD_REPLAY], {"replay"})]),
    ("S5d", "weapons and effect simulation", [([SIM_STEP], {"weapons", "effects"})]),
    ("S5e", "AI, mission rules and phases, radio selection", [([FLIGHT_FRAME], {"ai", "mission", "radio"})]),
    ("S5f", "effect drawing", [([FLIGHT_FRAME], {"effects"})]),
    ("S5g", "HUD", [([FLIGHT_FRAME], {"hud", "text"})]),
    ("S5h", "mission sound", [([FLIGHT_FRAME], {"sound", "radio"})]),
    ("S5i", "cutscenes and in-engine sequences", [(s2(0xD, 0xE, 0xF, 0x13), None), ([FLIGHT_FRAME], {"sequence"})]),
    ("S5j", "replay playback, carrier, game over, rearm, draw-only frame",
     [(s2(9, 0x10, 0x11, 0x12, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D), None),
      ([FLIGHT_FRAME], None)]),
    ("S6", "VS mode", []),
    ("S7", "scene 5 results and post-mission menu", [([0x00246E18] + SCENE5 + s4(37, 38, 39, 40), None)]),
    ("S8", "remaining code: dead code, HDD, scene 7, unreached", [([0x00261680] + s4(62, 63, 64), None)]),
]

# VS-specific code reached from shared roots (docs/subsystems/vs-mode.md).
PINNED = {
    0x00118CC0: "S6",   # VS win rules
    0x00119000: "S6",   # VS end sequence
    0x00167D68: "S6",   # VS score indicator
}

# Code reached only through data tables (callbacks, effect kinds, menu handlers), by subsystem.
SUBSYSTEM_FALLBACK = {
    "startup": "S1", "frontend": "S4", "save": "S2b", "movie": "S3", "core": "S5a", "render": "S5b",
    "aircraft-data": "S5a", "file": "S5a", "camera": "S5b", "flight": "S5c", "collision": "S5c",
    "input": "S5c", "weapons": "S5d", "effects": "S5f", "ai": "S5e", "mission": "S5e", "radio": "S5e",
    "hud": "S5g", "text": "S5g", "sound": "S5h", "sequence": "S5i", "replay": "S5j", "results": "S7",
    "hdd": "S8", "unreached": "S8", "boot": "S0",
}

ORDER = ["S0"] + [s[0] for s in SLICES]
TITLES = dict([("S0", "skeleton (done)")] + [(s[0], s[1]) for s in SLICES])


def load_graph():
    edges = load_json("reach.json")["edges"]
    return {int(k, 16): v for k, v in edges.items()}


def closure(graph, roots):
    seen, todo = set(), list(roots)
    while todo:
        f = todo.pop()
        if f in seen:
            continue
        seen.add(f)
        todo.extend(graph.get(f, ()))
    return seen


def assign():
    cov = {int(r["address"], 16): r for r in csv.DictReader(open(COVERAGE, encoding="utf-8"))}
    done = {int(r["address"], 16) for r in csv.DictReader(open(TRANSLATED, encoding="utf-8"))}
    graph = load_graph()
    out = {}
    for a, r in cov.items():
        if a in done:
            out[a] = ("S0", "translated")
        elif r["status"] == "dead":
            out[a] = ("S8", "dead")
        elif a in PINNED:
            out[a] = (PINNED[a], "pinned")
    for sid, _, rules in SLICES:
        for roots, subs in rules:
            for f in closure(graph, roots):
                if f in cov and f not in out and (subs is None or cov[f]["subsystem"] in subs):
                    out[f] = (sid, "closure")
    for a, r in cov.items():
        if a not in out:
            out[a] = (SUBSYSTEM_FALLBACK[r["subsystem"]], "fallback")
    return cov, out


def vs_readers():
    pat = re.compile(r"g_GameState\[0x2[56]\]")
    hits = []
    for name in sorted(os.listdir(DECOMP)):
        if name.endswith(".c") and pat.search(open(os.path.join(DECOMP, name), encoding="utf-8", errors="replace").read()):
            hits.append(int(name[:-2], 16))
    return hits


def main():
    args = sys.argv[1:]
    cov, out = assign()
    if args[:1] == ["--vs"]:
        for a in vs_readers():
            r = cov.get(a, {})
            print(f"0x{a:08X} {r.get('name', '?'):32} {r.get('subsystem', '?'):10} {out.get(a, ('-',))[0]}")
        return
    if args[:1] == ["--list"]:
        for a in sorted(a for a in out if out[a][0] == args[1]):
            r = cov[a]
            print(f"0x{a:08X} {r['name']:32} {r['subsystem']:13} {r['status']:10} {out[a][1]}")
        return
    with open(work("slices.csv"), "w", newline="", encoding="utf-8") as fh:
        w = csv.writer(fh)
        w.writerow(["address", "name", "subsystem", "status", "slice", "how"])
        for a in sorted(out):
            w.writerow([f"0x{a:08X}", cov[a]["name"], cov[a]["subsystem"], cov[a]["status"], *out[a]])
    total = len(cov)
    print(f"{'slice':5} {'functions':>9} {'closure':>7} {'fallbk':>6} {'pinned':>6}  title / subsystems")
    for sid in ORDER:
        fs = [a for a in out if out[a][0] == sid]
        how = lambda h: sum(1 for a in fs if out[a][1] == h)
        subs = {}
        for a in fs:
            subs[cov[a]["subsystem"]] = subs.get(cov[a]["subsystem"], 0) + 1
        top = ", ".join(f"{k} {v}" for k, v in sorted(subs.items(), key=lambda kv: -kv[1]))
        print(f"{sid:5} {len(fs):9} {how('closure'):7} {how('fallback'):6} {how('pinned'):6}  {TITLES[sid]}")
        print(f"{'':37}{top}")
    print(f"assigned {len(out)} / {total} game functions" + ("" if len(out) == total else "  ** MISSING **"))


if __name__ == "__main__":
    main()
