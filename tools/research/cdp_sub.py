"""Summarise DATA.CDP entries as count+offset containers: sub-block counts, sub-block magics, runs of similar entries.

Usage: python tools/research/cdp_sub.py out/cdp/DATA   (the folder written by tools/cdp_extract.py)
"""
import os, struct, sys, collections
D = sys.argv[1]
def mag(b):
    m = b[:4]
    if all(32 <= c < 127 for c in m): return m.decode()
    return m.hex()
groups = []
allmag = collections.Counter()
for i in range(360):
    b = open(os.path.join(D, f"{i:04d}.bin"), "rb").read()
    n = struct.unpack_from("<I", b)[0]
    offs = list(struct.unpack_from(f"<{n}I", b, 4)) + [len(b)]
    mags = []
    for k in range(n):
        s = b[offs[k]:offs[k+1]]
        mm = mag(s) if len(s) >= 4 else 'empty'
        mags.append(mm); allmag[mm] += 1
    groups.append((i, n, len(b), mags))
# print compact runs
prev = None
for i, n, size, mags in groups:
    sig = (n, tuple(mags[:6]))
    print(f"{i:3d} 0x{i:03X} n={n:3d} size={size:9d} " + ' '.join(mags[:8]) + (' ...' if n > 8 else ''))
print('--- sub-block magic histogram (top 40)')
for m, c in allmag.most_common(40): print(f'{c:6d} {m}')
print('--- runs (same count and first-3 magics)')
runs = []
for i, n, size, mags in groups:
    key = (n, tuple(mags[:3]))
    if runs and runs[-1][1] == key:
        runs[-1][2] = i; runs[-1][3].append(size)
    else:
        runs.append([i, key, i, [size]])
for s, (n, m), e, sizes in runs:
    print(f'{s:3d}-{e:3d} (0x{s:03X}-0x{e:03X}) x{e-s+1:3d} n={n:3d} sizes {min(sizes)}..{max(sizes)} first: {" ".join(m)}')
