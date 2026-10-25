"""Run many port instances in parallel from a job file and summarise them (research/testing only).

Usage: python -I tools/research/runmany.py JOBS.json [--only NAME[,NAME...]] [--jobs N]
                                           [--save-baseline DIR] [--baseline DIR] [--keep]
                                           [--suite NAME] [--extra-args "ARGS"] [--shots-only]

JOBS.json:
  {
    "suite": "regress",                       # output folder out/runs/<suite>/
    "exe": "build/windows/src/RelWithDebInfo/ProjectStonehenge.exe",
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
comparison; PNG decoding of tools/research/imgdiff.py), the RAM checkpoints that differ (byte
comparison of the ram_*.bin dumps) and whether the --hash-log output equals the baseline's at every
common vblank (hashdiff.py). --save-baseline copies every job's screenshots, RAM checkpoints and hash
log into DIR/<name>/ for later runs. --extra-args appends arguments to every job (e.g.
"--internal-scale 4"), --suite overrides the output folder name, and --shots-only skips the
screenshot comparison (screenshots at another internal scale differ in size by design; RAM and
hashes must still be identical). --tolerance N accepts screenshots whose channels differ by at
most N (the loading-screen fade depends on host presentation timing by a few levels). "same_hashes_as" compares the two jobs' RAM hash
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
    args += list(cfg.get("args", [])) + list(job.get("args", [])) + list(cfg.get("extra_args", []))
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


def pixels_equal(a, b, tolerance=0):
    wa, ha, pa = read_png(a)
    wb, hb, pb = read_png(b)
    if (wa, ha) != (wb, hb):
        return False
    if pa == pb:
        return True
    return tolerance > 0 and len(pa) == len(pb) and max(abs(x - y) for x, y in zip(pa, pb)) <= tolerance


def files_equal(a, b):
    if os.path.getsize(a) != os.path.getsize(b):
        return False
    with open(a, "rb") as fa, open(b, "rb") as fb:
        while True:
            x, y = fa.read(1 << 20), fb.read(1 << 20)
            if x != y:
                return False
            if not x:
                return True


def hashes_equal(a, b):
    # hashdiff.py compares the vblanks both logs have (a run can end one vblank later).
    hd = subprocess.run([sys.executable, "-I", os.path.join(os.path.dirname(os.path.abspath(__file__)), "hashdiff.py"),
                         a, b], capture_output=True, text=True)
    return "identical at every common vblank" in hd.stdout


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("jobs")
    ap.add_argument("--only")
    ap.add_argument("--jobs", dest="parallel", type=int, default=max(1, (os.cpu_count() or 2) // 2))
    ap.add_argument("--save-baseline")
    ap.add_argument("--baseline")
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--suite")
    ap.add_argument("--extra-args", default="")
    ap.add_argument("--shots-only", action="store_true", help="do not compare screenshots with the baseline")
    ap.add_argument("--tolerance", type=int, default=0,
                    help="screenshots equal when no channel differs by more than this (presentation timing)")
    a = ap.parse_args()
    cfg = json.load(open(a.jobs, encoding="utf-8"))
    if a.suite:
        cfg["suite"] = a.suite
    cfg["extra_args"] = a.extra_args.split()
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
            for s in ([] if a.shots_only else shots):
                b = os.path.join(a.baseline, name, s)
                if not os.path.isfile(b) or not pixels_equal(os.path.join(shots_dir, s), b, a.tolerance):
                    diff.append(s)
            base_dir = os.path.join(a.baseline, name)
            base_files = os.listdir(base_dir) if os.path.isdir(base_dir) else []
            missing = [] if a.shots_only else [s for s in base_files if s.endswith(".png") and s not in shots]
            for d in dumps:
                b = os.path.join(base_dir, d)
                if not os.path.isfile(b) or not files_equal(os.path.join(dumps_dir, d), b):
                    diff.append(d)
            missing += [d for d in base_files if d.startswith("ram_") and d not in dumps]
            base_hash = os.path.join(base_dir, "hashes.ac4h")
            mine_hash = os.path.join(dumps_dir, "hashes.ac4h")
            if os.path.isfile(base_hash):
                if not os.path.isfile(mine_hash) or not hashes_equal(base_hash, mine_hash):
                    diff.append("hashes.ac4h")
            r["baseline_diff"] = diff + [m + " (missing)" for m in missing]
            line += f"  baseline: {len(r['baseline_diff'])} differ"
            bad = bad or bool(r["baseline_diff"])
        if "same_hashes_as" in j:
            other = os.path.join(out_root, j["same_hashes_as"], "dumps", "hashes.ac4h")
            mine = os.path.join(dumps_dir, "hashes.ac4h")
            same = os.path.isfile(mine) and os.path.isfile(other) and hashes_equal(other, mine)
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
            for d in dumps + (["hashes.ac4h"] if os.path.isfile(os.path.join(dumps_dir, "hashes.ac4h")) else []):
                shutil.copy2(os.path.join(dumps_dir, d), os.path.join(dst, d))
    json.dump(results, open(os.path.join(out_root, "summary.json"), "w"), indent=1)
    print(f"\n{failed} of {len(jobs)} jobs need attention; summary in {os.path.relpath(out_root, REPO)}/summary.json")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
