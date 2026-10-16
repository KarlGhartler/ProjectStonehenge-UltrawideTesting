"""Check the terrain block layout of the mission entries (docs/formats/MISSION.md, "Terrain blocks 5-18") against the data.

For every mission entry it checks the page tables against the page counts, the palette/CLUT sizes, the texture ids in the
cell attributes against the mip lists (block 7) and the static-object indexes against the model count (block 16).

Usage: python -I tools/research/terrain_check.py [FIRST LAST] [--dir out/cdp/DATA]   (folder written by tools/cdp_extract.py)
"""
import os
import struct
import sys

DIR = sys.argv[sys.argv.index('--dir') + 1] if '--dir' in sys.argv else os.path.join('out', 'cdp', 'DATA')
args = [a for a in sys.argv[1:] if not a.startswith('--') and (sys.argv.index(a) == 0 or sys.argv[sys.argv.index(a) - 1] != '--dir')]
FIRST, LAST = (int(args[0]), int(args[1])) if len(args) >= 2 else (1, 164)


def blocks(i):
    b = open(os.path.join(DIR, f"{i:04d}.bin"), 'rb').read()
    n = struct.unpack_from('<I', b, 0)[0]
    offs = list(struct.unpack_from(f'<{n}I', b, 4)) + [len(b)]
    return [b[offs[k]:offs[k + 1]] for k in range(n)]


def container(blk):
    """Items of a count+offsets container (offsets relative to the block), or None."""
    if len(blk) < 8:
        return None
    c = struct.unpack_from('<I', blk, 0)[0]
    if c == 0:
        return []
    if not 0 < c < 0x10000 or 4 + 4 * c > len(blk):
        return None
    offs = list(struct.unpack_from(f'<{c}I', blk, 4))
    if offs[0] < 4 + 4 * c or any(offs[j] > offs[j + 1] for j in range(c - 1)) or offs[-1] > len(blk):
        return None
    ends = offs[1:] + [len(blk)]
    return [blk[offs[j]:ends[j]] for j in range(c)]


def check(i):
    bl = blocks(i)
    if len(bl) < 19:
        return None
    out = []
    b = {k: bl[k] for k in range(5, 19)}
    if not b[9]:
        return ['empty terrain']
    for k in (6, 10, 12, 15):
        if len(b[k]) != 256:
            out.append(f'block {k} size {len(b[k])}')
    if len(b[13]) != 1024:
        out.append(f'block 13 size {len(b[13])}')
    pages = {}
    for data, table, unit in ((9, 10, 0x400), (11, 12, 0x400), (5, 6, 0x200), (14, 15, 0x100)):
        n = len(b[data]) // unit
        pages[data] = n
        if len(b[data]) % unit:
            out.append(f'block {data} size {len(b[data])} not a multiple of {unit:#x}')
        if b[table] and max(b[table]) >= n:
            out.append(f'block {table} max {max(b[table])} >= pages {n} of block {data}')
    lists = container(b[7])
    ntex = None
    if lists is None or len(lists) != 4:
        out.append('block 7 not a container of 4')
    else:
        counts = []
        for k, lst in enumerate(lists):
            items = container(lst)
            if items is None:
                out.append(f'block 7 list {k} not a container')
                continue
            counts.append(len(items))
            want = 4096 >> (2 * k)
            bad = [len(t) for t in items if len(t) < want]
            if bad:
                out.append(f'block 7 level {k}: {len(bad)} items shorter than {want:#x}')
        if counts:
            ntex = min(counts)
            if len(set(counts)) != 1:
                out.append(f'block 7 list counts differ {counts}')
    attrs = struct.unpack_from(f'<{len(b[5]) // 2}H', b[5], 0)
    used = sorted({a & 0x3FF for a in attrs})
    if ntex is not None and used and used[-1] >= ntex:
        out.append(f'texture id {used[-1]} >= block 7 count {ntex}')
    models = container(b[16])
    nmod = len(models) if models else 0
    objs = sorted({v for v in b[14] if v != 0xFF})
    if objs and objs[-1] >= nmod:
        out.append(f'object index {objs[-1]} >= block 16 models {nmod}')
    groups = container(b[17])
    if b[17] and groups is None:
        out.append('block 17 not a container')
    elif groups:
        bad = [j for j, g in enumerate(groups) if (container(g) is None or len(container(g)) != 4)]
        if bad:
            out.append(f'block 17: {len(bad)} groups are not containers of 4 (first {bad[0]})')
    flags = sorted({a >> 12 for a in attrs})
    info = (f'pages h{pages[9]} c{pages[11]} a{pages[5]} o{pages[14]}  textures {ntex} (ids used {len(used)})  '
            f'models {nmod} (used {len(objs)})  groups {len(groups) if groups else 0}  attr flags {flags}')
    return out, info


def main():
    bad = 0
    for i in range(FIRST, LAST + 1):
        if not os.path.exists(os.path.join(DIR, f"{i:04d}.bin")):
            continue
        r = check(i)
        if r is None:
            continue
        if isinstance(r, list):
            print(f'{i:4d}: {r[0]}')
            continue
        problems, info = r
        print(f'{i:4d}: {"OK " if not problems else "BAD"} {info}')
        for p in problems:
            print(f'        {p}')
        bad += bool(problems)
    print(f'entries with problems: {bad}')


if __name__ == '__main__':
    main()
