// VU0 in micro mode for the sequence character engine (docs/subsystems/vu-programs.md "Skinning
// program S"): the VU0 register file and data memory, the VIF0 DMA chains that fill it (UNPACK
// V3-16 / V4-8, MPG of program S), and a C++ rewrite of the nine entries of program S, pair by
// pair. Arithmetic is VU micro mode (vu0::micro, chop) inside a ChopScope; the results reach guest
// RAM (the skinned vertex buffers), so they are checked against the PCSX2 vectors
// (docs/pcsx2/sessions-05.md).
#ifdef _MSC_VER
#pragma fenv_access(on)
#endif
#include "platform/vu0/vu0_hle.h"

#include <cfloat>
#include <cstring>

#include <vector>

#include "common/guest_ref.h"
#include "common/vu0_math.h"
#include "platform/log.h"

namespace ac04::vu0hle {

namespace m = ac04::vu0::micro;
using guest::GRef;

namespace {

struct Reg {
    f32 v[4];
};

struct State {
    Reg vf[32] = {{{0.0f, 0.0f, 0.0f, 1.0f}}};
    Reg acc = {};
    f32 i = 0.0f;
    f32 q = 1.0f;
    u16 vi[16] = {};
    u32 mem[256][4] = {};  // 4 KB data memory, qword addresses wrap
    bool program_s = false;
};

State& St() {
    static State s;
    return s;
}

u32 Bits(f32 f) {
    u32 b;
    std::memcpy(&b, &f, 4);
    return b;
}

f32 FromBits(u32 b) {
    f32 f;
    std::memcpy(&f, &b, 4);
    return f;
}

// A raw register lane as a VU operand: exponent 255 is a large number, not Inf/NaN, on the VU.
f32 Operand(f32 f) {
    const u32 b = Bits(f);
    if ((b & 0x7F800000u) == 0x7F800000u) return (b & 0x80000000u) != 0 ? -FLT_MAX : FLT_MAX;
    return f;
}

// lqi: load lanes `mask` (bit 0 = x .. bit 3 = w) of VU memory at VI[r], then VI[r] += 1.
void Lqi(State& s, int fd, int r, u32 mask) {
    const u32* q = s.mem[s.vi[r] & 0xFF];
    for (int k = 0; k < 4; ++k)
        if ((mask >> k) & 1) s.vf[fd].v[k] = FromBits(q[k]);
    s.vi[r] = static_cast<u16>(s.vi[r] + 1);
}

// itof0 / itof12 of lanes `mask`.
void Itof(State& s, int r, u32 mask, int shift) {
    for (int k = 0; k < 4; ++k) {
        if (((mask >> k) & 1) == 0) continue;
        const s32 i = static_cast<s32>(Bits(s.vf[r].v[k]));
        f32 f = m::IntToFloat(i);
        if (shift == 12) f = m::Mul(f, 1.0f / 4096.0f);
        s.vf[r].v[k] = f;
    }
}

// ftoi0 / ftoi4: v * 2^shift truncated toward zero, saturated.
u32 Ftoi(f32 v, int shift) {
    const double d = static_cast<double>(Operand(v)) * static_cast<double>(1 << shift);
    if (d >= 2147483647.0) return 0x7FFFFFFFu;
    if (d <= -2147483648.0) return 0x80000000u;
    return static_cast<u32>(static_cast<s32>(d));
}

// VU max / min: a total order on the float bit patterns (sign-magnitude), so -0 < +0.
bool Less(f32 a, f32 b) {
    const u32 x = Bits(a), y = Bits(b);
    const bool nx = (x & 0x80000000u) != 0, ny = (y & 0x80000000u) != 0;
    if (nx != ny) return nx;
    if (!nx) return x < y;
    return x > y;
}
f32 Max(f32 a, f32 b) { return Less(a, b) ? b : a; }
f32 Min(f32 a, f32 b) { return Less(a, b) ? a : b; }

// ACC chain fd = M (4 columns c0..c3) x (x, y, z, w) for all four lanes: mulax, madday, maddaz,
// maddw (each product and each sum chopped).
Reg MulChain(const Reg& c0, const Reg& c1, const Reg& c2, const Reg& c3, f32 x, f32 y, f32 z, f32 w) {
    Reg r;
    for (int k = 0; k < 4; ++k) {
        f32 a = m::Mul(c0.v[k], x);
        a = m::MulAdd(a, c1.v[k], y);
        a = m::MulAdd(a, c2.v[k], z);
        r.v[k] = m::MulAdd(a, c3.v[k], w);
    }
    return r;
}

// Three-column chain (mulax, madday, maddz): fd = c0 x + c1 y + c2 z, all lanes.
Reg MulChain3(const Reg& c0, const Reg& c1, const Reg& c2, f32 x, f32 y, f32 z) {
    Reg r;
    for (int k = 0; k < 4; ++k) {
        f32 a = m::Mul(c0.v[k], x);
        a = m::MulAdd(a, c1.v[k], y);
        r.v[k] = m::MulAdd(a, c2.v[k], z);
    }
    return r;
}

// Tail P (pairs 0x028-0x042) on vf06: perspective divide, fog, bounds flags; vf01 = result.
void TailP(State& s) {
    Reg& v6 = s.vf[6];
    const Reg& v7 = s.vf[7];
    const Reg& v9 = s.vf[9];
    f32 fog = m::Sub(v7.v[3], v6.v[3]);                      // 0x02B subw.w vf03, vf07, vf06w
    s.q = m::Div(1.0f, v6.v[3]);                            // 0x02B div Q, vf00w, vf06w
    for (int k = 0; k < 3; ++k) v6.v[k] = m::Mul(v6.v[k], s.q);  // 0x02C mulq.xyz
    fog = m::Mul(fog, v7.v[2]);                             // 0x02D mulz.w
    fog = Min(fog, v7.v[0]);                                // 0x031 minix.w
    fog = Max(fog, v7.v[1]);                                // 0x035 maxy.w
    // 0x036 sub.xyw vf00, vf00, vf06 and 0x037 sub.xyw vf00, vf06, vf09: MAC sign flags (bit 7 x,
    // 6 y, 4 w), read by the fmand pair at 0x03A / 0x03B (four pairs later each).
    const auto sign = [](f32 r) { return (Bits(r) & 0x80000000u) != 0; };
    u32 mac1 = 0, mac2 = 0;
    if (sign(m::Sub(0.0f, v6.v[0]))) mac1 |= 0x80;
    if (sign(m::Sub(0.0f, v6.v[1]))) mac1 |= 0x40;
    if (sign(m::Sub(1.0f, v6.v[3]))) mac1 |= 0x10;
    if (sign(m::Sub(v6.v[0], v9.v[0]))) mac2 |= 0x80;
    if (sign(m::Sub(v6.v[1], v9.v[1]))) mac2 |= 0x40;
    if (sign(m::Sub(v6.v[3], v9.v[3]))) mac2 |= 0x10;
    // 0x037 mfir.w vf04, vi04 (sign-extended); 0x038 / 0x039 ftoi4.xyz vf03 / vf04; 0x03A ftoi4.w.
    u32 out3[4], out4[4];
    for (int k = 0; k < 3; ++k) out3[k] = out4[k] = Ftoi(v6.v[k], 4);
    out4[3] = static_cast<u32>(static_cast<s32>(static_cast<s16>(s.vi[4])));
    out3[3] = Ftoi(fog, 4);
    u16 vi6 = static_cast<u16>(mac1 & s.vi[5]);
    vi6 = static_cast<u16>(mac2 & vi6);
    s.vi[6] = vi6;
    const u32* r = vi6 == s.vi[5] ? out3 : out4;  // 0x03C ibne vi06, vi05; 0x03D / 0x03E move
    for (int k = 0; k < 4; ++k) {
        s.vf[3].v[k] = FromBits(out3[k]);
        s.vf[4].v[k] = FromBits(out4[k]);
        s.vf[1].v[k] = FromBits(r[k]);
    }
}

// Tail L (pairs 0x079-0x084) on vf01 (normal): lighting, colour scale, clamp, ftoi0.
void TailL(State& s) {
    Reg n = s.vf[1];
    for (int k = 0; k < 3; ++k) n.v[k] = Max(n.v[k], 0.0f);  // 0x079 maxx.xyz vf01, vf01, vf00x
    const Reg c = MulChain(s.vf[12], s.vf[13], s.vf[14], s.vf[15], n.v[0], n.v[1], n.v[2], 1.0f);
    Reg d;
    for (int k = 0; k < 4; ++k) d.v[k] = m::Mul(c.v[k], s.vf[11].v[k]);  // 0x07E mul.xyzw
    for (int k = 0; k < 3; ++k) d.v[k] = Max(d.v[k], 0.0f);              // 0x07F maxz.xyz (vf00z)
    d.v[3] = Max(d.v[3], s.vf[11].v[2]);                                 // 0x080 maxz.w (vf11z)
    for (int k = 0; k < 3; ++k) d.v[k] = Min(d.v[k], s.vf[11].v[2]);     // 0x081 miniz.xyz
    s.vf[2] = d;
    for (int k = 0; k < 4; ++k) s.vf[1].v[k] = FromBits(Ftoi(d.v[k], 0));
}

// Tail E (pairs 0x0AC-0x0B0): (u + 1.0) x vf09.z, ftoi4.
void TailE(State& s) {
    for (int k = 0; k < 4; ++k) {
        const f32 u = m::Mul(m::Add(s.vf[1].v[k], 1.0f), s.vf[9].v[2]);
        s.vf[1].v[k] = FromBits(Ftoi(u, 4));
    }
}

// Positions: entries 0x00 / 0x02 / 0x06 up to the transform.
void Position(State& s, int entry) {
    if (entry == 0x00) {
        Lqi(s, 1, 1, 0x7);                       // 0x018 lqi.xyz vf01
        s.vf[1].v[3] = 1.0f;                     // 0x019 move.w vf01, vf00
        Itof(s, 1, 0x7, 0);                      // 0x01C
        for (int k = 0; k < 3; ++k) s.vf[1].v[k] = m::Mul(s.vf[1].v[k], s.vf[10].v[0]);  // 0x020
        const Reg& p = s.vf[1];
        s.vf[6] = MulChain(s.vf[16], s.vf[17], s.vf[18], s.vf[19], p.v[0], p.v[1], p.v[2], p.v[3]);
        return;
    }
    if (entry == 0x02) {
        Lqi(s, 1, 1, 0x7);                       // 0x045
        Lqi(s, 2, 3, 0xF);                       // 0x046
        s.vf[1].v[3] = 1.0f;                     // 0x047
        Itof(s, 1, 0x7, 0);                      // 0x049
        Itof(s, 2, 0xF, 0);                      // 0x04A
        s.i = FromBits(0x3B808080u);             // 0x04A loi 1/255
    } else {
        Lqi(s, 1, 1, 0xF);                       // 0x066
        Lqi(s, 5, 2, 0xF);                       // 0x067
        Lqi(s, 2, 3, 0xF);                       // 0x068
        Itof(s, 1, 0x7, 0);                      // 0x06A
        Itof(s, 5, 0x7, 0);                      // 0x06B
        s.vf[1].v[3] = 1.0f;
        Itof(s, 2, 0xF, 0);                      // 0x06C
        s.vf[5].v[3] = 1.0f;
        for (int k = 0; k < 3; ++k) {
            s.vf[1].v[k] = m::Mul(s.vf[1].v[k], s.vf[10].v[2]);  // 0x06E mulz.xyz
            s.vf[5].v[k] = m::Mul(s.vf[5].v[k], s.vf[10].v[3]);  // 0x06F mulw.xyz
        }
        for (int k = 0; k < 3; ++k) s.vf[1].v[k] = m::Add(s.vf[1].v[k], s.vf[5].v[k]);  // 0x073 (delay)
        // Joins entry 0x02 at 0x04B: the weights are scaled by the current I (set only by 0x02).
    }
    for (int k = 0; k < 4; ++k) s.vf[2].v[k] = m::Mul(s.vf[2].v[k], s.i);                 // 0x04E muli
    for (int k = 0; k < 3; ++k) s.vf[1].v[k] = m::Mul(s.vf[1].v[k], s.vf[10].v[0]);       // 0x04F mulx
    const Reg p = s.vf[1];
    s.vf[3] = MulChain(s.vf[16], s.vf[17], s.vf[18], s.vf[19], p.v[0], p.v[1], p.v[2], p.v[3]);
    s.vf[4] = MulChain(s.vf[20], s.vf[21], s.vf[22], s.vf[23], p.v[0], p.v[1], p.v[2], p.v[3]);
    s.vf[5] = MulChain(s.vf[24], s.vf[25], s.vf[26], s.vf[27], p.v[0], p.v[1], p.v[2], p.v[3]);
    s.vf[6] = MulChain(s.vf[28], s.vf[29], s.vf[30], s.vf[31], p.v[0], p.v[1], p.v[2], p.v[3]);
    const Reg w = s.vf[2];
    s.vf[6] = MulChain(s.vf[3], s.vf[4], s.vf[5], s.vf[6], w.v[0], w.v[1], w.v[2], w.v[3]);  // 0x060-0x063
}

// Normals: entries 0x08 / 0x0A / 0x0E up to tail L.
void Normal(State& s, int entry) {
    if (entry == 0x08) {
        Lqi(s, 1, 1, 0xF);                       // 0x074
        Itof(s, 1, 0x7, 12);                     // 0x075
        const Reg n = s.vf[1];
        s.vf[1] = MulChain3(s.vf[16], s.vf[17], s.vf[18], n.v[0], n.v[1], n.v[2]);  // 0x076-0x078
        return;
    }
    if (entry == 0x0A) {
        Lqi(s, 1, 1, 0xF);                       // 0x086
        Lqi(s, 2, 3, 0xF);                       // 0x087
        Itof(s, 1, 0x7, 12);                     // 0x088
        Itof(s, 2, 0xF, 0);                      // 0x089
    } else {
        Lqi(s, 1, 1, 0xF);                       // 0x09B
        Lqi(s, 5, 2, 0xF);                       // 0x09C
        Lqi(s, 2, 3, 0xF);                       // 0x09D
        Itof(s, 1, 0x7, 12);                     // 0x09E
        Itof(s, 5, 0x7, 12);                     // 0x09F
        Itof(s, 2, 0xF, 0);                      // 0x0A0
        for (int k = 0; k < 3; ++k) {
            s.vf[1].v[k] = m::Mul(s.vf[1].v[k], s.vf[10].v[2]);  // 0x0A1
            s.vf[5].v[k] = m::Mul(s.vf[5].v[k], s.vf[10].v[3]);  // 0x0A2
        }
        for (int k = 0; k < 3; ++k) s.vf[1].v[k] = m::Add(s.vf[1].v[k], s.vf[5].v[k]);  // 0x0A3, joins 0x08A
    }
    for (int k = 0; k < 4; ++k) s.vf[2].v[k] = m::Mul(s.vf[2].v[k], s.vf[10].v[1]);   // 0x08A muly
    const Reg n = s.vf[1];
    s.vf[3] = MulChain3(s.vf[16], s.vf[17], s.vf[18], n.v[0], n.v[1], n.v[2]);         // 0x08B-0x08D
    // 0x08E-0x090: ACC = vf20 x + vf21 y; vf04 = ACC + vf22 z.
    Reg acc;
    for (int k = 0; k < 4; ++k) acc.v[k] = m::MulAdd(m::Mul(s.vf[20].v[k], n.v[0]), s.vf[21].v[k], n.v[1]);
    for (int k = 0; k < 4; ++k) s.vf[4].v[k] = m::MulAdd(acc.v[k], s.vf[22].v[k], n.v[2]);
    // 0x091-0x092 (original quirk: no mulax for matrix 2): ACC += vf25 y; vf05 = ACC + vf26 z.
    for (int k = 0; k < 4; ++k) acc.v[k] = m::MulAdd(acc.v[k], s.vf[25].v[k], n.v[1]);
    for (int k = 0; k < 4; ++k) s.vf[5].v[k] = m::MulAdd(acc.v[k], s.vf[26].v[k], n.v[2]);
    s.vf[6] = MulChain3(s.vf[28], s.vf[29], s.vf[30], n.v[0], n.v[1], n.v[2]);         // 0x093-0x095
    const Reg w = s.vf[2];
    s.vf[1] = MulChain(s.vf[3], s.vf[4], s.vf[5], s.vf[6], w.v[0], w.v[1], w.v[2], w.v[3]);  // 0x096-0x099
}

// Environment vectors: entries 0x10 / 0x12 / 0x16 up to tail E. The w lane adds vf00 x the raw w
// bits of the loaded vector (not converted).
void Environment(State& s, int entry) {
    const Reg zero1 = {{0.0f, 0.0f, 0.0f, 1.0f}};
    if (entry == 0x10) {
        Lqi(s, 1, 1, 0xF);                       // 0x0A5
        Itof(s, 1, 0x7, 12);                     // 0x0A7
        const Reg u = s.vf[1];
        s.vf[1] = MulChain(s.vf[16], s.vf[17], s.vf[18], zero1, u.v[0], u.v[1], u.v[2], Operand(u.v[3]));
        return;
    }
    if (entry == 0x12) {
        Lqi(s, 1, 1, 0xF);                       // 0x0B2
        Lqi(s, 2, 3, 0xF);                       // 0x0B3
        Itof(s, 1, 0x7, 12);                     // 0x0B4
        Itof(s, 2, 0xF, 0);                      // 0x0B5
    } else {
        Lqi(s, 1, 1, 0xF);                       // 0x0CD
        Lqi(s, 5, 2, 0xF);                       // 0x0CE
        Lqi(s, 2, 3, 0xF);                       // 0x0CF
        Itof(s, 1, 0x7, 12);                     // 0x0D0
        Itof(s, 5, 0x7, 12);                     // 0x0D1
        Itof(s, 2, 0xF, 0);                      // 0x0D2
        for (int k = 0; k < 3; ++k) {
            s.vf[1].v[k] = m::Mul(s.vf[1].v[k], s.vf[10].v[2]);  // 0x0D3
            s.vf[5].v[k] = m::Mul(s.vf[5].v[k], s.vf[10].v[3]);  // 0x0D4
        }
        for (int k = 0; k < 3; ++k) s.vf[1].v[k] = m::Add(s.vf[1].v[k], s.vf[5].v[k]);  // 0x0D5, joins 0x0B6
    }
    for (int k = 0; k < 4; ++k) s.vf[2].v[k] = m::Mul(s.vf[2].v[k], s.vf[10].v[1]);   // 0x0B6 muly
    const Reg u = s.vf[1];
    const f32 w = Operand(u.v[3]);
    s.vf[3] = MulChain(s.vf[16], s.vf[17], s.vf[18], zero1, u.v[0], u.v[1], u.v[2], w);
    s.vf[4] = MulChain(s.vf[20], s.vf[21], s.vf[22], zero1, u.v[0], u.v[1], u.v[2], w);
    s.vf[5] = MulChain(s.vf[24], s.vf[25], s.vf[26], zero1, u.v[0], u.v[1], u.v[2], w);
    s.vf[6] = MulChain(s.vf[28], s.vf[29], s.vf[30], zero1, u.v[0], u.v[1], u.v[2], w);
    const Reg g = s.vf[2];
    s.vf[1] = MulChain(s.vf[3], s.vf[4], s.vf[5], s.vf[6], g.v[0], g.v[1], g.v[2], g.v[3]);  // 0x0C7-0x0CA
}

// ---- VIF0 -----------------------------------------------------------------------------------

// Guest address of a DMA tag / data address (bit 31: scratchpad).
u32 DmaAddress(u32 a) { return (a & 0x80000000u) != 0 ? 0x70000000u | (a & 0x3FF0u) : a & 0x01FFFFF0u; }

// VIF0 code interpreter over a word stream: NOP, MPG (program S only), UNPACK V3-16 / V4-8 with
// CL == WL (VIF0 CYCLE is 0x404 in the reference state, docs/pcsx2/sessions-05.md).
class Vif0 {
public:
    void Feed(const u32* words, u32 n, u32 guest_src) {
        for (u32 k = 0; k < n; ++k) Word(words[k], guest_src + k * 4);
    }

private:
    void Word(u32 w, u32 src) {
        if (left_ != 0) {
            Data(w, src);
            return;
        }
        const u32 cmd = (w >> 24) & 0x7F;
        if (cmd == 0x00) return;  // NOP
        if (cmd == 0x4A) {        // MPG: NUM pairs follow
            mpg_ = true;
            left_ = ((w >> 16) & 0xFF) * 2;
            if (left_ == 0) left_ = 512;
            mpg_src_ = 0;
            return;
        }
        if ((cmd & 0x60) == 0x60) {
            mpg_ = false;
            format_ = cmd & 0xF;
            usn_ = (w & 0x4000) != 0;
            addr_ = w & 0x3FF;
            num_ = (w >> 16) & 0xFF;
            if (num_ == 0) num_ = 256;
            const u32 bytes = format_ == 0x9 ? 6 : format_ == 0xE ? 4 : 0;
            if (bytes == 0) {
                log::WriteOnce("vu0-unpack", log::Level::Error, "vu0", "VIF0: unsupported UNPACK format 0x%X", format_);
                return;
            }
            left_ = (num_ * bytes + 3) / 4;
            buf_.clear();
            return;
        }
        log::WriteOnce("vu0-vifcode", log::Level::Error, "vu0", "VIF0: unsupported VIF code 0x%08X", w);
    }

    void Data(u32 w, u32 src) {
        --left_;
        if (mpg_) {
            if (mpg_src_ == 0) mpg_src_ = src;
            if (left_ == 0) {
                // Program S: the MPG data of the .vutext chain 0x0029C200 (216 pairs at 0x0029C210).
                if (mpg_src_ == 0x0029C210u) St().program_s = true;
                else log::WriteOnce("vu0-mpg", log::Level::Error, "vu0", "VIF0: MPG from 0x%08X is not program S", mpg_src_);
            }
            return;
        }
        for (int b = 0; b < 4; ++b) buf_.push_back(static_cast<u8>(w >> (b * 8)));
        if (left_ == 0) Unpack();
    }

    void Unpack() {
        State& s = St();
        for (u32 i = 0; i < num_; ++i) {
            u32* q = s.mem[(addr_ + i) & 0xFF];
            if (format_ == 0x9) {  // V3-16, sign- or zero-extended
                for (int k = 0; k < 3; ++k) {
                    u16 h;
                    std::memcpy(&h, &buf_[i * 6 + k * 2], 2);
                    q[k] = usn_ ? h : static_cast<u32>(static_cast<s32>(static_cast<s16>(h)));
                }
                // HYPOTHESIS: w = the next halfword of the stream (0 past the end). PCSX2 differs at
                // some vertices (sessions-05 state); no program output reads this lane.
                u16 h = 0;
                if (i * 6 + 8 <= buf_.size()) std::memcpy(&h, &buf_[i * 6 + 6], 2);
                q[3] = usn_ ? h : static_cast<u32>(static_cast<s32>(static_cast<s16>(h)));
            } else {  // V4-8
                for (int k = 0; k < 4; ++k) {
                    const u8 b = buf_[i * 4 + k];
                    q[k] = usn_ ? b : static_cast<u32>(static_cast<s32>(static_cast<s8>(b)));
                }
            }
        }
    }

    u32 left_ = 0;
    bool mpg_ = false;
    u32 mpg_src_ = 0;
    u32 format_ = 0;
    bool usn_ = false;
    u32 addr_ = 0;
    u32 num_ = 0;
    std::vector<u8> buf_;
};

}  // namespace

void LoadVf(int n, u32 addr) {
    if (n == 0) return;
    for (int k = 0; k < 4; ++k) St().vf[n].v[k] = FromBits(GRef<u32>(addr + k * 4));
}

void StoreVf(int n, u32 addr) {
    for (int k = 0; k < 4; ++k) GRef<u32>(addr + k * 4) = Bits(St().vf[n].v[k]);
}

void SetVi(int n, u32 value) {
    if (n == 0) return;
    St().vi[n] = static_cast<u16>(value);
}

u32 GetVi(int n) { return n == 0 ? 0u : St().vi[n]; }

void CallMs(u32 pair) {
    State& s = St();
    if (!s.program_s) {
        log::WriteOnce("vu0-noprog", log::Level::Error, "vu0", "VU0: vcallms 0x%X without program S", pair);
        return;
    }
    m::ChopScope chop;
    switch (pair) {
    case 0x00: case 0x02: case 0x06: Position(s, static_cast<int>(pair)); TailP(s); break;
    case 0x08: case 0x0A: case 0x0E: Normal(s, static_cast<int>(pair)); TailL(s); break;
    case 0x10: case 0x12: case 0x16: Environment(s, static_cast<int>(pair)); TailE(s); break;
    default: log::WriteOnce("vu0-entry", log::Level::Error, "vu0", "VU0: vcallms 0x%X is not an entry of program S", pair); break;
    }
}

u32 RunVif0Chain(u32 tadr) {
    Vif0 vif;
    for (int guard = 0; guard < 4096; ++guard) {
        const u32 tag = DmaAddress(tadr);
        const u32 w0 = GRef<u32>(tag), addr = GRef<u32>(tag + 4);
        const u32 qwc = w0 & 0xFFFF, id = (w0 >> 28) & 7;
        const u32 codes[2] = {GRef<u32>(tag + 8), GRef<u32>(tag + 12)};
        vif.Feed(codes, 2, tag + 8);  // TTE
        u32 data = 0;
        switch (id) {
        case 0: case 3: data = DmaAddress(addr); break;  // refe, ref
        case 1: case 7: data = tag + 0x10; break;        // cnt, end
        default: log::WriteOnce("vu0-tag", log::Level::Error, "vu0", "VIF0: DMA tag id %u not supported", id); return tadr;
        }
        for (u32 k = 0; k < qwc; ++k) {
            u32 qw[4];
            for (int j = 0; j < 4; ++j) qw[j] = GRef<u32>(data + k * 16 + j * 4);
            vif.Feed(qw, 4, data + k * 16);
        }
        if (id == 0 || id == 7) return tag + 0x10 + (id == 7 ? qwc * 16 : 0);
        tadr = id == 1 ? tag + 0x10 + qwc * 16 : (tadr + 0x10);
    }
    log::WriteOnce("vu0-chain", log::Level::Error, "vu0", "VIF0: DMA chain too long");
    return tadr;
}

}  // namespace ac04::vu0hle
