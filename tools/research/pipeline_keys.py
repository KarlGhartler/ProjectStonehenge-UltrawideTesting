"""Merge the GS pipeline keys of port session logs (research/testing only).

Usage: python -I tools/research/pipeline_keys.py LOG_OR_FOLDER [...] [--header]

Reads every session.log given (folders are searched recursively) and collects the keys of the
renderer's shutdown lines "GS pipelines: N in use; keys: k1 k2 ..." and "GS pipelines: N built
during frames (...): k1 k2 ..." (gs_vulkan.cpp). Prints the merged sorted set with the number of logs each key came
from, or with --header the body of the key table of src/platform/render/gs/gs_pipeline_keys.h.

A key is the renderer's own draw state (gs_vulkan.cpp PipelineKey: blend enable, Vulkan blend
factors and op, colour write mask, depth compare op, depth write), not game data.
"""
import argparse
import os
import re
import sys

LINE = re.compile(r"GS pipelines: .*?(?:keys|built during frames[^:]*): ([0-9a-f ]+)")


def logs(paths):
    for p in paths:
        if os.path.isdir(p):
            for root, _, files in os.walk(p):
                for f in files:
                    if f == "session.log":
                        yield os.path.join(root, f)
        else:
            yield p


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("paths", nargs="+")
    ap.add_argument("--header", action="store_true")
    a = ap.parse_args()
    seen = {}
    n = 0
    for path in logs(a.paths):
        with open(path, encoding="utf-8", errors="replace") as f:
            keys = set()
            for line in f:
                for m in LINE.finditer(line):
                    keys.update(int(k, 16) for k in m.group(1).split())
        if keys:
            n += 1
            for k in keys:
                seen[k] = seen.get(k, 0) + 1
    if not seen:
        print("no GS pipeline keys found", file=sys.stderr)
        return 1
    print(f"{len(seen)} keys from {n} logs", file=sys.stderr)
    for k in sorted(seen):
        if a.header:
            print(f"    0x{k:08X},")
        else:
            print(f"{k:08x}  {seen[k]}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
