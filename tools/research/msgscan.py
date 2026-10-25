#!/usr/bin/env python3
"""Decode the messages of every font block in DATA.CDP and print those matching a pattern (research only).

Usage: msgscan.py <ExtractedGameFiles> [regex]
Glyph codes of the message font (decoded from the disc, docs/subsystems/sound.md "Text engine"): 0x0D space,
0x1D-0x26 digits, 0x27-0x40 A-Z, 0x41-0x5A a-z, 0xB1 "(", 0x0E ")"; others are shown as {xx}; 0xFFFD new line "/".
"""
# 0xFFFD new line '/', 0xFFFE end, 0xFFFF end (continued).
import os, re, struct, sys

root = sys.argv[1]
pattern = re.compile(sys.argv[2], re.I) if len(sys.argv) > 2 else None
tbl = open(os.path.join(root, 'BIN', 'DATA.TBL'), 'rb').read()
cdp = open(os.path.join(root, 'BIN', 'DATA.CDP'), 'rb')

def ch(c):
    if c == 0x0D: return ' '
    if 0x1D <= c <= 0x26: return chr(ord('0') + c - 0x1D)
    if 0x27 <= c <= 0x40: return chr(ord('A') + c - 0x27)
    if 0x41 <= c <= 0x5A: return chr(ord('a') + c - 0x41)
    if c == 0xFFFD: return '/'
    if c >= 0xFFFE: return ''
    return '{%02x}' % c

def messages(block):
    if len(block) < 0x14: return None
    foff, moff = struct.unpack_from('<II', block, 4)
    if not (0 < foff < len(block) and 0 < moff < len(block) - 4): return None
    if block[foff:foff + 4] not in (b'go18', b'go28'): return None
    n = struct.unpack_from('<i', block, moff)[0]
    if not 0 < n < 0x200: return None
    out, p = [], moff + 4
    for _ in range(n):
        codes = []
        while p + 2 <= len(block):
            c = struct.unpack_from('<H', block, p)[0]; p += 2; codes.append(c)
            if c >= 0xFFFE: break
        out.append(codes)
    return out

for entry in range(len(tbl) // 8):
    sector, size = struct.unpack_from('<II', tbl, entry * 8)
    cdp.seek(sector * 2048); data = cdp.read(size)
    if len(data) < 8: continue
    count = struct.unpack_from('<I', data, 0)[0]
    if not 0 < count < 4096 or 4 + 4 * count > len(data): continue
    offs = list(struct.unpack_from(f'<{count}I', data, 4)) + [len(data)]
    for item in range(count):
        if not offs[item] < offs[item + 1] <= len(data): continue
        msgs = messages(data[offs[item]:offs[item + 1]])
        if not msgs: continue
        for i, codes in enumerate(msgs):
            text = ''.join(ch(c) for c in codes)
            if pattern is None or pattern.search(text):
                print(f'DATA {entry:#05x} item {item} msg {i:3d}: {text}')
