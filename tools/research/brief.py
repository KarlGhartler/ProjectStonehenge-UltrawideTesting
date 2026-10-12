"""Brief view of not-yet-understood functions in a range: size, callees, first N body lines.

Usage: python tools/research/brief.py LO HI [N]   (skips functions marked understood or dead in coverage)
"""
import sys, re, os
from common import DECOMP, funcs, coverage
sys.stdout.reconfigure(encoding='utf-8', errors='replace')
lo, hi = int(sys.argv[1],16), int(sys.argv[2],16); N = int(sys.argv[3]) if len(sys.argv)>3 else 30
G = funcs()
cov = coverage()
names = {a: r['name'] for a,r in cov.items()}
for a in sorted(G):
    if not (lo <= a < hi): continue
    if cov[a]['status'] in ('understood','dead'): continue
    v = G[a]
    size = int(v['body_end'],16) - int(v['body_start'],16) + 1
    calls = ','.join(names.get(int(c['address'],16), c['name']) for c in v.get('callees',[]))
    p = os.path.join(DECOMP, f'{a:08X}.c')
    body = open(p, encoding='utf-8').read().split('\n') if os.path.exists(p) else []
    out = []
    for l in body:
        t = l.strip()
        if not t or re.match(r'^(undefined\d?|u?int|u?long|u?short|byte|char|float|bool|code|MainContext|void|uchar|ushort|ulong|longlong|undefined|size_t|wchar_t)\s*\*?\s*\w+(\s*\[\w+\])?;$', t) or t.startswith('// WARNING'): continue
        out.append(l.replace('    ',' ').replace('  ',' '))
    print(f'// ===== {a:08X} size={size} lines={len(out)} calls=[{calls}]')
    print('\n'.join(out[:N]) + ('\n  ...' if len(out) > N else ''))
