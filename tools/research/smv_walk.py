"""Walk the Namco SMV movie containers as the game's reader (0x00212630-0x00212960) does and check that every
chunk chain ends exactly at the end of the file.

Usage: python tools/research/smv_walk.py <ExtractedGameFiles dir>   (reads MIS/*.SMV and SMV/*.SMV, read-only)
"""
import glob, os, struct, sys
G = sys.argv[1]
ok_all = True
for p in sorted(glob.glob(os.path.join(G, "MIS", "*.SMV")) + glob.glob(os.path.join(G, "SMV", "*.SMV"))):
    size = os.path.getsize(p)
    f = open(p, "rb")
    first = f.read(0x800)
    nxt = struct.unpack_from("<I", first)[0]
    rest_zero = first[4:] == bytes(0x7FC)
    off = 0x800; n = 0; vid = aud = 0; problems = []; maxc = 0; aud_chunks = 0
    while nxt:
        f.seek(off); h = f.read(16)
        c_next, vbytes, aoff, abytes = struct.unpack("<4I", h)
        if nxt % 0x800: problems.append(f"chunk {n} size {nxt:#x} not sector multiple")
        if not (0x10 + vbytes <= aoff + (0 if abytes else 0x10**9)): problems.append(f"chunk {n}: video end {0x10+vbytes:#x} > audio off {aoff:#x}")
        if aoff + abytes > nxt: problems.append(f"chunk {n}: audio end {aoff+abytes:#x} > chunk {nxt:#x}")
        if abytes % 0x4000: problems.append(f"chunk {n}: audio {abytes:#x} not 0x4000 multiple")
        if nxt > 0x180000: problems.append(f"chunk {n}: {nxt:#x} > 0x180000 buffer")
        f.seek(off + 0x10); es = f.read(4)
        if n == 0 and es != b"\0\0\x01\xb3": problems.append("chunk 0 video not seq header")
        vid += vbytes; aud += abytes; aud_chunks += bool(abytes); maxc = max(maxc, nxt)
        off += nxt; n += 1; nxt = c_next
    end_ok = off == (size + 0x7FF) // 0x800 * 0x800  # last chunk size is sector-rounded; file is not padded
    ok = end_ok and not problems and rest_zero
    ok_all &= ok
    print(f"{os.path.basename(os.path.dirname(p))}/{os.path.basename(p):9s} size={size:10d} chunks={n:4d} max_chunk={maxc:#8x} "
          f"video={vid:10d} audio={aud:10d} audio_chunks={aud_chunks:4d} end==EOF:{end_ok} hdr_zero:{rest_zero} {'OK' if ok else 'FAIL'}")
    for pr in problems[:3]: print("   !", pr)
print("ALL OK" if ok_all else "SOME FAIL")
