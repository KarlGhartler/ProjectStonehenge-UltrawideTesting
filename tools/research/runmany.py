"""Run many port instances in parallel from a job file and summarise them (research/testing only).

Usage: python -I tools/research/runmany.py JOBS.json [--only NAME[,NAME...]] [--jobs N]
                                           [--save-baseline DIR] [--baseline DIR] [--keep]

JOBS.json:
  {
    "suite": "regress",                       # output folder out/runs/<suite>/
    "exe": "build/windows/src/RelWithDebInfo/ac04port.exe",
    "iso": "D:/.../ACE COMBAT 04 SHATTERED SKIES.ISO",   # or set AC04_ISO in the environment
    "memcard": "out/s5/cardS_fast5",          # default card folder, copied per job ("" = none)
    "args": ["--freeze-sim"],                  # arguments for every job
    "timeout": 900,                            # seconds per job
    "jobs": [
      {"name": "m3", "args": ["--mission", "3", "--screenshot-at", "2000", "--fast-until", "2000",
                              "--exit-at", "2010"], "memcard": "...optional..."},
      {"name": "m3_stress", "args": [...], "same_hashes_as": "m3"},  # compare --hash-log outputs
      {"name": "m12", "args": [...], "known": "why it fails"}         # reported as KNOWN, not counted
    ]
  }

Each job gets its own user-data folder out/runs/<suite>/<name>/ (old contents removed unless --keep)
and its own copy of the card folder, so jobs never share state. The summary lists per job: exit
code, wall time, warnings and errors in the session log (distinct texts), RAM checkpoints written,
screenshots, and with --baseline the screenshots that differ from the baseline (exact pixel
comparison; PNG decoding of tools/research/imgdiff.py). --save-baseline copies every job's
screenshots into DIR/<name>/ for later runs. "same_hashes_as" compares the two jobs' RAM hash
logs (--hash-log) at every vblank both have, with hashdiff.py.

Output goes to out/ only. Runs contain game data (dumps, screenshots): never commit them.
"""
import argparse
import concurrent.futures
import json
import os
import re
import shutil
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from imgdiff import read_png  # noqa: E402

REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))


def resolve(p):
    return p if os.path.isabs(p) else os.path.join(REPO, p)


def run_job(cfg, job, out_root, keep):
    name = job["name"]
    ud = os.path.join(out_root, name)
    if os.path.isdir(ud) and not keep:
        shutil.rmtree(ud)
    os.makedirs(ud, exist_ok=True)
    args = [resolve(cfg["exe"])]
    iso = cfg.get("iso") or os.environ.get("AC04_ISO", "")
    if iso:
        args += ["--iso", iso]
    args += ["--user-data", ud]
    card = job.get("memcard", cfg.get("memcard", ""))
    if card:
        dst = os.path.join(out_root, name + "_card")
        if os.path.isdir(dst):
            shutil.rmtree(dst)
        shutil.copytree(resolve(card), dst)
        args += ["--memcard", dst]
    args += list(cfg.get("args", [])) + list(job.get("args", []))
    t0 = time.time()
    try:
        with open(os.path.join(out_root, name + ".stdout.txt"), "w", encoding="utf-8", errors="replace") as f:
            rc = subprocess.run(args, stdout=f, stderr=subprocess.STDOUT, timeout=cfg.get("timeout", 900)).returncode
    except subprocess.TimeoutExpired:
        rc = "timeout"
    return name, rc, time.time() - t0


def log_problems(ud):
    probs = {}
    logs = os.path.join(ud, "logs")
    if not os.path.isdir(logs):
        return probs
    for fn in os.listdir(logs):
        for line in open(os.path.join(logs, fn), encoding="utf-8", errors="replace"):
            m = re.match(r"\[\s*[\d.]+\]\s+(WARN|ERROR)\s+(.*)", line)
            if m:
                key = m.group(1) + " " + m.group(2).strip()
                probs[key] = probs.get(key, 0) + 1
    return probs


def pixels_equal(a, b):
    wa, ha, pa = read_png(a)
    wb, hb, pb = read_png(b)
    return (wa, ha) == (wb, hb) and pa == pb


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("jobs")
    ap.add_argument("--only")
    ap.add_argument("--jobs", dest="parallel", type=int, default=max(1, (os.cpu_count() or 2) // 2))
    ap.add_argument("--save-baseline")
    ap.add_argument("--baseline")
    ap.add_argument("--keep", action="store_true")
    a = ap.parse_args()
    cfg = json.load(open(a.jobs, encoding="utf-8"))
    jobs = cfg["jobs"]
    if a.only:
        wanted = set(a.only.split(","))
        jobs = [j for j in jobs if j["name"] in wanted]
    out_root = os.path.join(REPO, "out", "runs", cfg.get("suite", "runs"))
    os.makedirs(out_root, exist_ok=True)
    results = {}
    t0 = time.time()
    with concurrent.futures.ThreadPoolExecutor(max_workers=a.parallel) as ex:
        for name, rc, dt in ex.map(lambda j: run_job(cfg, j, out_root, a.keep), jobs):
            results[name] = {"exit": rc, "seconds": round(dt, 1)}
            print(f"  finished {name}: exit {rc}, {dt:.1f} s", flush=True)
    failed = 0
    print(f"\n{len(jobs)} jobs in {time.time() - t0:.1f} s ({a.parallel} in parallel)")
    for j in jobs:
        name = j["name"]
        ud = os.path.join(out_root, name)
        r = results[name]
        shots_dir = os.path.join(ud, "screenshots")
        shots = sorted(os.listdir(shots_dir)) if os.path.isdir(shots_dir) else []
        dumps_dir = os.path.join(ud, "dumps")
        dumps = sorted(f for f in os.listdir(dumps_dir) if f.startswith("ram_")) if os.path.isdir(dumps_dir) else []
        probs = log_problems(ud)
        r.update(screenshots=shots, checkpoints=dumps, problems=probs)
        line = f"{name:24s} exit {r['exit']!s:7s} {r['seconds']:6.1f} s  shots {len(shots)}  checkpoints {len(dumps)}"
        bad = r["exit"] != 0 or any(k.startswith("ERROR") for k in probs)
        if a.baseline:
            diff = []
            for s in shots:
                b = os.path.join(a.baseline, name, s)
                if not os.path.isfile(b) or not pixels_equal(os.path.join(shots_dir, s), b):
                    diff.append(s)
            missing = [s for s in (os.listdir(os.path.join(a.baseline, name)) if os.path.isdir(os.path.join(a.baseline, name)) else []) if s not in shots]
            r["baseline_diff"] = diff + [m + " (missing)" for m in missing]
            line += f"  baseline: {len(r['baseline_diff'])} differ"
            bad = bad or bool(r["baseline_diff"])
        if "same_hashes_as" in j:
            other = os.path.join(out_root, j["same_hashes_as"], "dumps", "hashes.ac4h")
            mine = os.path.join(dumps_dir, "hashes.ac4h")
            # hashdiff.py compares the vblanks both logs have (a run can end one vblank later).
            same = False
            if os.path.isfile(mine) and os.path.isfile(other):
                hd = subprocess.run([sys.executable, "-I", os.path.join(os.path.dirname(os.path.abspath(__file__)), "hashdiff.py"),
                                     other, mine], capture_output=True, text=True)
                same = "identical at every common vblank" in hd.stdout
            r["hashes_equal"] = same
            line += f"  hashes {'equal' if same else 'DIFFER'} to {j['same_hashes_as']}"
            bad = bad or not same
        known = bad and "known" in j
        failed += bad and not known
        print(("KNOWN " if known else "FAIL  " if bad else "ok    ") + line)
        if known:
            print(f"       known: {j['known']}")
        for k, n in sorted(probs.items()):
            print(f"       {n:4d} x {k[:150]}")
        for s in r.get("baseline_diff", []):
            print(f"       differs from baseline: {s}")
        if a.save_baseline:
            dst = os.path.join(a.save_baseline, name)
            os.makedirs(dst, exist_ok=True)
            for s in shots:
                shutil.copy2(os.path.join(shots_dir, s), os.path.join(dst, s))
    json.dump(results, open(os.path.join(out_root, "summary.json"), "w"), indent=1)
    print(f"\n{failed} of {len(jobs)} jobs need attention; summary in {os.path.relpath(out_root, REPO)}/summary.json")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
