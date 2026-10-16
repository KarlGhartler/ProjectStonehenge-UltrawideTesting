"""Decode the FLAC audio track of an MP4 file (PCSX2 media capture with the FLAC codec) to a WAV file.

Usage: python tools/research/flac_mp4.py <capture.mp4> <out.wav>

Pure Python (no dependencies): reads the 'dfLa' STREAMINFO and the sample table (stsz/stsc/stco) of the
first audio track, then decodes every FLAC frame (CONSTANT, VERBATIM, FIXED and LPC subframes, Rice
residuals, the four stereo channel assignments). Slow (about a minute per minute of stereo audio) but
enough for reference captures. Research only; the input and output are game-derived data and stay in
out/ or the scratchpad.
"""
import struct
import sys
import wave


def boxes(d, o, e):
    while o + 8 <= e:
        size, typ = struct.unpack(">I4s", d[o:o + 8])
        hdr = 8
        if size == 1:
            size = struct.unpack(">Q", d[o + 8:o + 16])[0]
            hdr = 16
        elif size == 0:
            size = e - o
        yield typ, o + hdr, o + size
        o += size


def find(d, o, e, path):
    for typ, s, end in boxes(d, o, e):
        if typ == path[0]:
            return (s, end) if len(path) == 1 else find(d, s, end, path[1:])
    return None


class Bits:
    def __init__(self, data, pos=0):
        self.d = data
        self.p = pos * 8

    def read(self, n):
        v = 0
        for _ in range(n):
            v = (v << 1) | ((self.d[self.p >> 3] >> (7 - (self.p & 7))) & 1)
            self.p += 1
        return v

    def signed(self, n):
        v = self.read(n)
        return v - (1 << n) if n and v & (1 << (n - 1)) else v

    def unary(self):
        n = 0
        while self.read(1) == 0:
            n += 1
        return n

    def align(self):
        self.p = (self.p + 7) & ~7

    def utf8(self):
        b = self.read(8)
        n = 0
        while b & (0x80 >> n):
            n += 1
        v = b & (0x7F >> n) if n else b
        for _ in range(max(0, n - 1)):
            v = (v << 6) | (self.read(8) & 0x3F)
        return v


def residual(br, order, block):
    method = br.read(2)
    pbits = 4 if method == 0 else 5
    escape = 15 if method == 0 else 31
    porder = br.read(4)
    parts = 1 << porder
    out = []
    for p in range(parts):
        n = (block >> porder) - (order if p == 0 else 0)
        k = br.read(pbits)
        if k == escape:
            bits = br.read(5)
            out.extend(br.signed(bits) for _ in range(n))
        else:
            for _ in range(n):
                q = br.unary()
                u = (q << k) | br.read(k)
                out.append((u >> 1) ^ -(u & 1))
    return out


FIXED = [[], [1], [2, -1], [3, -3, 1], [4, -6, 4, -1]]


def subframe(br, block, bps):
    br.read(1)
    typ = br.read(6)
    wasted = 0
    if br.read(1):
        wasted = 1 + br.unary()
        bps -= wasted
    if typ == 0:
        s = [br.signed(bps)] * block
    elif typ == 1:
        s = [br.signed(bps) for _ in range(block)]
    elif 8 <= typ <= 12:
        order = typ - 8
        s = [br.signed(bps) for _ in range(order)]
        res = residual(br, order, block)
        c = FIXED[order]
        for r in res:
            s.append(r + sum(c[j] * s[-1 - j] for j in range(order)))
    elif typ >= 32:
        order = typ - 31
        s = [br.signed(bps) for _ in range(order)]
        prec = br.read(4) + 1
        shift = br.signed(5)
        coefs = [br.signed(prec) for _ in range(order)]
        res = residual(br, order, block)
        for r in res:
            acc = 0
            for j in range(order):
                acc += coefs[j] * s[-1 - j]
            s.append(r + (acc >> shift))
    else:
        raise ValueError("reserved subframe type %d" % typ)
    if wasted:
        s = [v << wasted for v in s]
    return s


def frame(data, pos, info):
    br = Bits(data, pos)
    if br.read(14) != 0x3FFE:
        raise ValueError("no frame sync at %d" % pos)
    br.read(2)
    bs_code = br.read(4)
    sr_code = br.read(4)
    chan = br.read(4)
    ss_code = br.read(3)
    br.read(1)
    br.utf8()
    if bs_code == 1:
        block = 192
    elif 2 <= bs_code <= 5:
        block = 576 << (bs_code - 2)
    elif bs_code == 6:
        block = br.read(8) + 1
    elif bs_code == 7:
        block = br.read(16) + 1
    else:
        block = 256 << (bs_code - 8)
    if sr_code == 12:
        br.read(8)
    elif sr_code in (13, 14):
        br.read(16)
    br.read(8)  # CRC-8
    bps = info["bps"] if ss_code == 0 else {1: 8, 2: 12, 4: 16, 5: 20, 6: 24}[ss_code]
    if chan < 8:
        ch = [subframe(br, block, bps) for _ in range(chan + 1)]
    else:
        a = subframe(br, block, bps + (1 if chan == 9 else 0))
        b = subframe(br, block, bps + (0 if chan == 9 else 1))
        if chan == 8:  # left/side
            ch = [a, [x - y for x, y in zip(a, b)]]
        elif chan == 9:  # side/right
            ch = [[x + y for x, y in zip(a, b)], b]
        else:  # mid/side
            l, r = [], []
            for m, s in zip(a, b):
                m = (m << 1) | (s & 1)
                l.append((m + s) >> 1)
                r.append((m - s) >> 1)
            ch = [l, r]
    return ch


def main():
    d = open(sys.argv[1], "rb").read()
    moov = find(d, 0, len(d), [b"moov"])
    for typ, s, e in boxes(d, moov[0], moov[1]):
        if typ != b"trak":
            continue
        stbl = find(d, s, e, [b"mdia", b"minf", b"stbl"])
        if stbl is None:
            continue
        stsd = find(d, stbl[0], stbl[1], [b"stsd"])
        entry = stsd[0] + 8
        if d[entry + 4:entry + 8] != b"fLaC":
            continue
        dfla = find(d, entry + 8 + 28, stsd[1], [b"dfLa"])
        si = d[dfla[0] + 4 + 4:dfla[0] + 4 + 4 + 34]
        v = int.from_bytes(si[10:18], "big")
        info = {"rate": v >> 44, "channels": ((v >> 41) & 7) + 1, "bps": ((v >> 36) & 31) + 1}
        sizes = []
        s0, e0 = find(d, stbl[0], stbl[1], [b"stsz"])
        fixed, count = struct.unpack(">4xII", d[s0:s0 + 12])
        sizes = [fixed] * count if fixed else list(struct.unpack(">%dI" % count, d[s0 + 12:s0 + 12 + 4 * count]))
        s1, _ = find(d, stbl[0], stbl[1], [b"stsc"])
        n = struct.unpack(">4xI", d[s1:s1 + 8])[0]
        stsc = [struct.unpack(">III", d[s1 + 8 + 12 * i:s1 + 20 + 12 * i]) for i in range(n)]
        s2, _ = find(d, stbl[0], stbl[1], [b"stco"])
        n = struct.unpack(">4xI", d[s2:s2 + 8])[0]
        offsets = list(struct.unpack(">%dI" % n, d[s2 + 8:s2 + 8 + 4 * n]))
        # Sample positions from the chunk table.
        pos = []
        si_ = 0
        for c, off in enumerate(offsets, 1):
            per = [e for e in stsc if e[0] <= c][-1][1]
            for _ in range(per):
                if si_ >= len(sizes):
                    break
                pos.append(off)
                off += sizes[si_]
                si_ += 1
        out = wave.open(sys.argv[2], "wb")
        out.setnchannels(info["channels"])
        out.setsampwidth(2)
        out.setframerate(info["rate"])
        for i, p in enumerate(pos):
            ch = frame(d, p, info)
            pcm = bytearray()
            for j in range(len(ch[0])):
                for c in ch:
                    pcm += struct.pack("<h", max(-32768, min(32767, c[j])))
            out.writeframes(bytes(pcm))
        out.close()
        print("decoded %d frames, %d Hz, %d channels, %d bits" % (len(pos), info["rate"], info["channels"], info["bps"]))
        return
    print("no FLAC track")


if __name__ == "__main__":
    main()
