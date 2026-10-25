"""Compare the port's screens with the User's PCSX2 screenshots of the slice batches (research only).

Usage: python -I tools/research/pcsx2_compare.py CONFIG.json --slices DIR [--only NAME[,NAME...]]
                                                [--extra-args "ARGS"] [--suite NAME] [--jobs N] [--reuse]

CONFIG.json:
  {
    "exe": "build/windows/src/RelWithDebInfo/ac04port.exe",
    "memcard": "out/cards/S",                       # default card folder ("" = none)
    "runs": [
      {"name": "s1_cardS", "args": ["--exit-at", "700"], "memcard": "...",
       "shots": [{"ref": "Slice1/s1_S_splash1.png", "from": 300, "to": 600, "step": 10,
                  "blend_fields": false}]},
      {"name": "gsdump_m1", "gsdump": "out/gs/s5_m1_frame.gs",
       "shots": [{"ref": "Slice5/s5_m1_frame.png", "blend_fields": true}]}
    ]
  }

Each run starts the port once (own user-data folder under out/runs/<suite>/, own copy of the card)
with --screenshot-at for every vblank of its windows and --fast-until, then compares each
reference with every screenshot of its window (tools/research/imgdiff.py metric: mean absolute
difference per channel, share of pixels over 16) and reports the best one. "gsdump" runs draw a
PCSX2 GS dump with --play-gsdump instead. At an internal scale above 1 (--extra-args
"--internal-scale N") the port writes native_<vblank>.png as the N x N box average of the image
it rendered, so the same references apply.

The PCSX2 screenshots stay in the Slices folder; nothing from them is written into the repo. The
port screenshots go to out/ (game content: never commit them).
"""
import argparse
import concurrent.futures
import json
import os
import shutil
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from imgdiff import read_png  # noqa: E402

REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))


def resolve(p):
    return p if os.path.isabs(p) else os.path.join(REPO, p)


def compare(port, ref, blend_fields):
    w1, h1, p1 = read_png(port)
    w2, h2, p2 = read_png(ref)
    if blend_fields:
        src = bytes(p1)
        for i in range(w1 * 3, w1 * h1 * 3):
            p1[i] = (src[i] + src[i - w1 * 3] + 1) >> 1
    w, h = min(w1, w2), min(h1, h2)
    sums = [0, 0, 0]
    over = 0
    for y in range(h):
        r1, r2 = y * w1 * 3, y * w2 * 3
        a = p1[r1:r1 + w * 3]
        b = p2[r2:r2 + w * 3]
        for x in range(0, w * 3, 3):
            d0, d1, d2 = abs(a[x] - b[x]), abs(a[x + 1] - b[x + 1]), abs(a[x + 2] - b[x + 2])
            sums[0] += d0
            sums[1] += d1
            sums[2] += d2
            over += max(d0, d1, d2) > 16
    n = w * h
    return [s / n for s in sums], 100.0 * over / n, (w1, h1), (w2, h2)


def windows(shot):
    if "at" in shot:
        return [shot["at"]]
    return list(range(shot["from"], shot["to"] + 1, shot.get("step", 1)))


def run(cfg, r, out_root, extra, iso, reuse):
    name = r["name"]
    ud = os.path.join(out_root, name)
    if not reuse:
        if os.path.isdir(ud):
            shutil.rmtree(ud)
        os.makedirs(ud, exist_ok=True)
        args = [resolve(cfg["exe"])]
        if iso:
            args += ["--iso", iso]
        args += ["--user-data", ud]
        if "gsdump" in r:
            args += ["--play-gsdump", resolve(r["gsdump"])]
        else:
            card = r.get("memcard", cfg.get("memcard", ""))
            if card:
                dst = os.path.join(out_root, name + "_card")
                if os.path.isdir(dst):
                    shutil.rmtree(dst)
                shutil.copytree(resolve(card), dst)
                args += ["--memcard", dst]
            vbs = sorted({v for s in r["shots"] for v in windows(s)})
            last = max(vbs)
            args += list(r.get("args", []))
            args += ["--screenshot-at", ",".join(str(v) for v in vbs), "--fast-until", str(last + 1),
                     "--exit-at", str(last + 2)]
        args += extra
        with open(os.path.join(out_root, name + ".stdout.txt"), "w", encoding="utf-8", errors="replace") as f:
            subprocess.run(args, stdout=f, stderr=subprocess.STDOUT, timeout=cfg.get("timeout", 1800))
    return name


def best_of(out_root, slices, r, shot):
    shots_dir = os.path.join(out_root, r["name"], "screenshots")
    ref = os.path.join(slices, shot["ref"])
    cands = (["gsdump_native.png"] if "gsdump" in r else [f"native_{v:06d}.png" for v in windows(shot)])
    best = None
    for c in cands:
        p = os.path.join(shots_dir, c)
        if not os.path.isfile(p):
            continue
        means, over, size1, size2 = compare(p, ref, shot.get("blend_fields", False))
        score = sum(means)
        if best is None or score < best[0]:
            best = (score, c, means, over, size1, size2)
    return r["name"], shot, best


def main():
    if len(sys.argv) == 6 and sys.argv[1] == "--worker":
        _, best = best_of(sys.argv[2], sys.argv[3], json.loads(sys.argv[4]), json.loads(sys.argv[5]))[1:]
        print(json.dumps(best))
        return
    ap = argparse.ArgumentParser()
    ap.add_argument("config")
    ap.add_argument("--slices", required=True)
    ap.add_argument("--only")
    ap.add_argument("--extra-args", default="")
    ap.add_argument("--suite", default="pcsx2_compare")
    ap.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 2) // 2))
    ap.add_argument("--reuse", action="store_true", help="compare the screenshots of the last run again")
    a = ap.parse_args()
    cfg = json.load(open(a.config, encoding="utf-8"))
    runs = cfg["runs"]
    if a.only:
        wanted = set(a.only.split(","))
        runs = [r for r in runs if r["name"] in wanted]
    out_root = os.path.join(REPO, "out", "runs", a.suite)
    os.makedirs(out_root, exist_ok=True)
    iso = cfg.get("iso") or os.environ.get("AC04_ISO", "")
    with concurrent.futures.ThreadPoolExecutor(max_workers=a.jobs) as ex:
        list(ex.map(lambda r: run(cfg, r, out_root, a.extra_args.split(), iso, a.reuse), runs))

    # Each comparison in its own isolated interpreter (-I): the research folder has modules named
    # like standard ones (dis.py), which multiprocessing's spawned workers would import instead.
    tasks = [(r, s) for r in runs for s in r["shots"]]

    def one(t):
        r, shot = t
        out = subprocess.run([sys.executable, "-I", os.path.abspath(__file__), "--worker", out_root, a.slices,
                              json.dumps(r), json.dumps(shot)], capture_output=True, text=True)
        best = json.loads(out.stdout) if out.returncode == 0 and out.stdout.strip() else None
        return r["name"], shot, best

    with concurrent.futures.ThreadPoolExecutor(max_workers=a.jobs) as ex:
        results = list(ex.map(one, tasks))
    print(f"{'reference':28s} {'run':14s} {'best port shot':26s} {'mean R/G/B':20s} {'> 16':>8s}")
    for name, shot, best in results:
        ref = os.path.basename(shot["ref"])
        if best is None:
            print(f"{ref:28s} {name:14s} no port screenshot")
            continue
        _, c, means, over, size1, size2 = best
        mean = "/".join(f"{m:.2f}" for m in means)
        note = "" if size1 == size2 else f"  (port {size1[0]}x{size1[1]}, ref {size2[0]}x{size2[1]})"
        bf = " blend" if shot.get("blend_fields") else ""
        print(f"{ref:28s} {name:14s} {c:26s} {mean:20s} {over:7.2f}%{bf}{note}")


if __name__ == "__main__":
    main()
