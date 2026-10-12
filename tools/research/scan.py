"""Shared instruction-scan state for hwscan.py, hwregs.py, gs_scan.py and closure.py (library, no CLI).

W: {va: word} for .text; starts: approximate function starts (jal targets + entries of the known dispatch
tables listed in DISPATCH_TABLES); fn(a): nearest game function start at or below a.
"""
import bisect
from common import elf, GAME_LO as LO, GAME_HI as HI, TEXT_HI as TXT_HI

# (table address, entry count): scene table, flight sub-states, menu sub-states, results, startup, scene 7,
# replay camera styles, and two small menu tables. Addresses only; the entries are read from the ELF.
DISPATCH_TABLES = [(0x0029FDA0, 8), (0x002A1620, 31), (0x00384640, 90), (0x003B0190, 7), (0x003F8708, 10),
                   (0x00411720, 2), (0x002B77B0, 17), (0x0033DF24, 3), (0x0039DF54, 3)]
E = elf()
W = {a: E.u32(a) for a in range(0x00100000, TXT_HI, 4)}
starts = set()
for a, w in W.items():
    if w >> 26 == 3:
        t = (w & 0x3FFFFFF) << 2
        if 0x00100000 <= t < TXT_HI:
            starts.add(t)
for tb, n in DISPATCH_TABLES:
    for i in range(n):
        starts.add(E.u32(tb + 4 * i))
S = sorted(s for s in starts if LO <= s < HI)


def fn(a):
    i = bisect.bisect_right(S, a) - 1
    return S[i] if i >= 0 else None
