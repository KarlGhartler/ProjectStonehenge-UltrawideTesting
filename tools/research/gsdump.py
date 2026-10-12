"""Statistics of a PCSX2 single-frame GS dump (.gs, uncompressed): which GS features the game really uses.

Usage: python tools/research/gsdump.py DUMP.gs [DUMP.gs ...] [--targets] [--json OUT]
  --targets   also list every draw target (FRAME FBP/PSM) with its draw count per GIF path
  --json OUT  write the merged statistics to OUT (must be under out/)

Reads dumps from wherever PCSX2 saved them (keep them outside the repo: they contain game data).
Nothing from the dump is written to the repo; only counts and register values are printed.

Format (PCSX2 2.x GSDumpFile): u32 0xFFFFFFFF, u32 header_size, header {state_version, state_size,
serial_offset, serial_size, crc, shot_w, shot_h, shot_offset, shot_size} + serial + screenshot,
then the GS state (state_size bytes), the privileged registers (8192 bytes) and packets:
  u8 0 Transfer: u8 path, u32 size, data   (PCSX2 2.8 dumps tag every transfer 3: the GIF unit output, paths merged)
  u8 1 VSync: u8 field   u8 2 ReadFIFO2: u32 size   u8 3 Registers: 8192 bytes
"""
import collections, json, os, struct, sys

PRIM_TYPES = ["point", "line", "linestrip", "tri", "tristrip", "trifan", "sprite", "prim7"]
PSM = {0x00: "CT32", 0x01: "CT24", 0x02: "CT16", 0x0A: "CT16S", 0x13: "T8", 0x14: "T4", 0x1B: "T8H",
       0x24: "T4HL", 0x2C: "T4HH", 0x30: "Z32", 0x31: "Z24", 0x32: "Z16", 0x3A: "Z16S"}
REGNAME = {0x00: "PRIM", 0x01: "RGBAQ", 0x02: "ST", 0x03: "UV", 0x04: "XYZF2", 0x05: "XYZ2", 0x06: "TEX0_1",
           0x07: "TEX0_2", 0x08: "CLAMP_1", 0x09: "CLAMP_2", 0x0A: "FOG", 0x0C: "XYZF3", 0x0D: "XYZ3",
           0x14: "TEX1_1", 0x15: "TEX1_2", 0x16: "TEX2_1", 0x17: "TEX2_2", 0x18: "XYOFFSET_1", 0x19: "XYOFFSET_2",
           0x1A: "PRMODECONT", 0x1B: "PRMODE", 0x1C: "TEXCLUT", 0x22: "SCANMSK", 0x34: "MIPTBP1_1",
           0x35: "MIPTBP1_2", 0x36: "MIPTBP2_1", 0x37: "MIPTBP2_2", 0x3B: "TEXA", 0x3D: "FOGCOL", 0x3F: "TEXFLUSH",
           0x40: "SCISSOR_1", 0x41: "SCISSOR_2", 0x42: "ALPHA_1", 0x43: "ALPHA_2", 0x44: "DIMX", 0x45: "DTHE",
           0x46: "COLCLAMP", 0x47: "TEST_1", 0x48: "TEST_2", 0x49: "PABE", 0x4A: "FBA_1", 0x4B: "FBA_2",
           0x4C: "FRAME_1", 0x4D: "FRAME_2", 0x4E: "ZBUF_1", 0x4F: "ZBUF_2", 0x50: "BITBLTBUF", 0x51: "TRXPOS",
           0x52: "TRXREG", 0x53: "TRXDIR", 0x54: "HWREG", 0x60: "SIGNAL", 0x61: "FINISH", 0x62: "LABEL"}
PATHS = {0: "PATH1", 1: "PATH2", 2: "PATH3", 3: "GIF"}  # PCSX2 2.x tags all GIF-unit output as 3


def bits(v, lo, n):
    return (v >> lo) & ((1 << n) - 1)


class Stats:
    def __init__(self):
        self.c = collections.defaultdict(collections.Counter)
        self.ctx = [{"FRAME": 0, "TEX0": 0, "ALPHA": 0, "TEST": 0, "ZBUF": 0, "TEX1": 0, "CLAMP": 0}, None]
        self.ctx[1] = dict(self.ctx[0])
        self.prim = 0
        self.prmodecont = 1
        self.prmode = 0
        self.vcount = 0
        self.fog = self.fogcol = None
        self.texa = None

    def reg(self, path, r, v):
        name = REGNAME.get(r, f"reg{r:02X}")
        self.c["reg_" + PATHS[path]][name] += 1
        if r == 0x00:
            self.prim = v; self.vcount = 0
        elif r == 0x1B:
            self.prmode = v
        elif r == 0x1A:
            self.prmodecont = v & 1
        elif r in (0x04, 0x05, 0x0C, 0x0D):
            if r in (0x05, 0x04):  # drawing kick
                self.vertex(path)
        elif r in (0x06, 0x07):
            self.ctx[r - 6]["TEX0"] = v
        elif r in (0x08, 0x09):
            self.ctx[r - 8]["CLAMP"] = v
            self.c["clamp"][f"wms{bits(v,0,2)} wmt{bits(v,2,2)}"] += 1
        elif r in (0x14, 0x15):
            self.ctx[r - 0x14]["TEX1"] = v
        elif r in (0x42, 0x43):
            self.ctx[r - 0x42]["ALPHA"] = v
        elif r in (0x47, 0x48):
            self.ctx[r - 0x47]["TEST"] = v
        elif r in (0x4C, 0x4D):
            self.ctx[r - 0x4C]["FRAME"] = v
        elif r in (0x4E, 0x4F):
            self.ctx[r - 0x4E]["ZBUF"] = v
        elif r == 0x50:
            self.c["upload_dpsm"][PSM.get(bits(v, 56, 6), hex(bits(v, 56, 6)))] += 1
            self.c["upload_spsm"][PSM.get(bits(v, 24, 6), hex(bits(v, 24, 6)))] += 1
        elif r == 0x53:
            self.c["trxdir"][["host->local", "local->host", "local->local", "off"][v & 3]] += 1
        elif r == 0x3D:
            self.c["fogcol"][hex(v & 0xFFFFFF)] += 1
        elif r == 0x3B:
            self.c["texa"][f"ta0={bits(v,0,8):#x} aem={bits(v,15,1)} ta1={bits(v,32,8):#x}"] += 1
        elif r == 0x45:
            self.c["dthe"][v & 1] += 1
        elif r == 0x46:
            self.c["colclamp"][v & 1] += 1
        elif r == 0x49:
            self.c["pabe"][v & 1] += 1
        elif r in (0x4A, 0x4B):
            self.c["fba"][v & 1] += 1
        elif r == 0x22:
            self.c["scanmsk"][v & 3] += 1
        elif r == 0x1C:
            self.c["texclut"][1] += 1

    def vertex(self, path):
        self.vcount += 1
        attr = self.prim if self.prmodecont else (self.prim & 7) | (self.prmode & ~7)
        t = attr & 7
        need = {0: 1, 1: 2, 2: 2, 3: 3, 4: 3, 5: 3, 6: 2}.get(t, 1)
        first = self.vcount >= need and (t not in (0, 1, 3, 6) or self.vcount % need == 0)
        if not first:
            return
        ctx = self.ctx[bits(attr, 9, 1)]
        key = (f"{PRIM_TYPES[t]} iip{bits(attr,3,1)} tme{bits(attr,4,1)} fge{bits(attr,5,1)} "
               f"abe{bits(attr,6,1)} aa1{bits(attr,7,1)} fst{bits(attr,8,1)} ctxt{bits(attr,9,1)}")
        self.c["prim_" + PATHS[path]][key] += 1
        fr = ctx["FRAME"]
        target = f"FBP {bits(fr,0,9):#05x} FBW {bits(fr,16,6)} {PSM.get(bits(fr,24,6), hex(bits(fr,24,6)))} FBMSK {bits(fr,32,32):#x}"
        self.c["target_" + PATHS[path]][target] += 1
        if bits(fr, 32, 32):
            self.c["fbmsk"][hex(bits(fr, 32, 32))] += 1
        if bits(attr, 4, 1):
            tx = ctx["TEX0"]
            self.c["tex_psm"][f"{PSM.get(bits(tx,20,6), hex(bits(tx,20,6)))} tcc{bits(tx,34,1)} tfx{bits(tx,35,2)} cpsm{PSM.get(bits(tx,51,4), bits(tx,51,4))}"] += 1
            t1 = ctx["TEX1"]
            self.c["tex1"][f"lcm{bits(t1,0,1)} mxl{bits(t1,2,3)} mmag{bits(t1,5,1)} mmin{bits(t1,6,3)}"] += 1
            self.c["tex_size"][f"{1 << bits(tx,26,4)}x{1 << bits(tx,30,4)}"] += 1
        if bits(attr, 6, 1):
            a = ctx["ALPHA"]
            self.c["alpha"][f"A{bits(a,0,2)} B{bits(a,2,2)} C{bits(a,4,2)} D{bits(a,6,2)} FIX{bits(a,32,8):#x}"] += 1
        te = ctx["TEST"]
        self.c["test"][(f"ate{bits(te,0,1)} atst{bits(te,1,3)} aref{bits(te,4,8):#x} afail{bits(te,12,2)} "
                        f"date{bits(te,14,1)} datm{bits(te,15,1)} zte{bits(te,16,1)} ztst{bits(te,17,2)}")] += 1
        zb = ctx["ZBUF"]
        self.c["zbuf"][f"{PSM.get(bits(zb,24,4) | 0x30, hex(bits(zb,24,4)))} zmsk{bits(zb,32,1)}"] += 1

    def gif(self, path, data):
        o = 0
        n = len(data)
        while o + 16 <= n:
            lo, hi = struct.unpack_from("<QQ", data, o)
            o += 16
            nloop = bits(lo, 0, 15); pre = bits(lo, 46, 1); prim = bits(lo, 47, 11)
            flg = bits(lo, 58, 2); nreg = bits(lo, 60, 4) or 16
            regs = [bits(hi, 4 * i, 4) for i in range(nreg)]
            self.c["giftag_" + PATHS[path]][["PACKED", "REGLIST", "IMAGE", "IMAGE2"][flg]] += 1
            if pre and flg == 0:
                self.reg(path, 0x00, prim)
            if flg == 0:
                for _ in range(nloop):
                    for r in regs:
                        if o + 16 > n:
                            return
                        a, b = struct.unpack_from("<QQ", data, o); o += 16
                        if r == 0xE:
                            self.reg(path, b & 0xFF, a)
                        elif r == 0x0:
                            self.reg(path, 0x00, a & 0x7FF)
                        elif r == 0x1:
                            pass
                        elif r in (0x4, 0x5, 0xC, 0xD):
                            adc = bits(b, 47, 1)
                            if not adc:
                                self.reg(path, {0x4: 0x04, 0x5: 0x05, 0xC: 0x04, 0xD: 0x05}[r], 0)
                            else:
                                self.c["reg_" + PATHS[path]]["XYZ(no kick)"] += 1
                        elif r in (0x6, 0x7, 0x8, 0x9):
                            self.reg(path, r, a)
                        elif r == 0xA:
                            self.c["reg_" + PATHS[path]]["FOG"] += 1
            elif flg == 1:
                cnt = nloop * nreg
                for i in range(cnt):
                    if o + 8 > n:
                        return
                    v = struct.unpack_from("<Q", data, o)[0]; o += 8
                    r = regs[i % nreg]
                    if r in (0x4, 0x5, 0xC, 0xD):
                        self.reg(path, 0x05, 0)
                    elif r in (0x0, 0x6, 0x7, 0x8, 0x9):
                        self.reg(path, r, v)
                if cnt & 1:
                    o += 8
            else:
                o += 16 * nloop
                self.c["image_qwords"][PATHS[path]] += nloop


def parse(path, st):
    d = open(path, "rb").read()
    o = 0
    crc = struct.unpack_from("<I", d, o)[0]; o += 4
    if crc != 0xFFFFFFFF:
        raise SystemExit("old or compressed dump format: save the dump uncompressed (.gs)")
    hsize = struct.unpack_from("<I", d, o)[0]; o += 4
    hdr = struct.unpack_from("<9I", d, o)
    serial = d[o + hdr[2]:o + hdr[2] + hdr[3]].split(b"\0")[0].decode()
    o += hsize
    state_size = hdr[1]
    o += state_size + 8192
    vs = 0
    while o < len(d):
        t = d[o]; o += 1
        if t == 0:
            p = d[o]; size = struct.unpack_from("<I", d, o + 1)[0]; o += 5
            st.c["transfer_bytes"][PATHS.get(p, p)] += size
            st.gif(p if p < 3 else 3, d[o:o + size]); o += size
        elif t == 1:
            o += 1; vs += 1
        elif t == 2:
            o += 4
        elif t == 3:
            o += 8192
        else:
            raise SystemExit(f"unknown packet {t} at {o - 1:#x}")
    return serial, vs


def main():
    args = sys.argv[1:]
    targets = "--targets" in args
    out = None
    if "--json" in args:
        out = args[args.index("--json") + 1]
    files = [a for a in args if a.endswith(".gs")]
    allst = Stats()
    for f in files:
        st = Stats()
        serial, vs = parse(f, st)
        print(f"== {os.path.basename(os.path.dirname(f)) or f}: serial {serial}, vsyncs {vs}")
        for k in sorted(st.c):
            if k.startswith("target_") and not targets:
                continue
            print(f"  {k}: " + ", ".join(f"{a}={b}" for a, b in st.c[k].most_common(12)))
        for k, v in st.c.items():
            allst.c[k].update(v)
    if out:
        assert os.path.abspath(out).startswith(os.path.abspath("out")), "write only under out/"
        json.dump({k: {str(a): b for a, b in v.items()} for k, v in allst.c.items()}, open(out, "w"), indent=1)


if __name__ == "__main__":
    main()
