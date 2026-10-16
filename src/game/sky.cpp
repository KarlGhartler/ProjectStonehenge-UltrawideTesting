// Sky, stars, clouds and weather (block A of S5, 0x0010FFB8-0x00118A00): the render settings of the
// current map entry (gp 0x004104D4, mission block 24), the sky state (gp 0x004104D0, 0x40 bytes)
// and the star / cloud tables (gp 0x004104E8). docs/subsystems/sky.md. Random reads: the star and
// cloud generators reseed libc rand (docs/game/random.md).
#include "game/game.h"

#include <cmath>

#include "common/ee_math.h"
#include "common/vu0_math.h"
#include "common/guest_stack.h"
#include "game/main_context.h"
#include "sce/sce.h"

namespace ac04::game {

using guest::GRef;

namespace {

u32 RenderSettings() { return GRef<u32>(0x004104D4); }
u32 SkyState() { return GRef<u32>(0x004104D0); }

}  // namespace

// EE: 0x00113548
// n stars: srand(seed), then per star x, y, z = rand() >> 15 (s16) and brightness
// (rand() & 0x7FF) + 0x800.
void func_00113548(u32 out, s32 n, u32 seed) {
    ee::srand(seed);
    for (; n > 0; --n, out += 8) {
        GRef<u16>(out + 0) = static_cast<u16>(static_cast<u32>(ee::rand()) >> 15);
        GRef<u16>(out + 2) = static_cast<u16>(static_cast<u32>(ee::rand()) >> 15);
        GRef<u16>(out + 4) = static_cast<u16>(static_cast<u32>(ee::rand()) >> 15);
        GRef<u16>(out + 6) = static_cast<u16>((static_cast<u32>(ee::rand()) & 0x7FF) + 0x800);
    }
}

namespace {

// (r1 + r2 + r3 + r4) / 4 - 0x4000 as the original computes it (signed divide, 16-bit result).
u16 CloudCoordinate() {
    const u32 a = static_cast<u32>(ee::rand());
    const u32 b = static_cast<u32>(ee::rand());
    const u32 c = static_cast<u32>(ee::rand());
    const u32 d = static_cast<u32>(ee::rand());
    const s32 sum = static_cast<s32>(a + b + c + d);
    return static_cast<u16>((sum / 4) - 0x4000);
}

}  // namespace

// EE: 0x001135C8
// n cloud puffs: srand(seed), x, y, z = mean of four rand() - 0x4000, size (rand() & 0xFF) + 0x100.
void func_001135C8(u32 out, s32 n, u32 seed) {
    ee::srand(seed);
    for (; n > 0; --n, out += 8) {
        GRef<u16>(out + 0) = CloudCoordinate();
        GRef<u16>(out + 2) = CloudCoordinate();
        GRef<u16>(out + 4) = CloudCoordinate();
        GRef<u16>(out + 6) = static_cast<u16>((static_cast<u32>(ee::rand()) & 0xFF) + 0x100);
    }
}

// EE: 0x00113750
// Regenerates the 400 stars and 128 cloud puffs from the render settings' seeds (+0x80, +0x132).
void func_00113750(u32 rs) {
    func_00113548(GRef<u32>(0x004104E8), 400, GRef<u16>(rs + 0x80));
    func_001135C8(GRef<u32>(0x004104E8) + 0xC80, 0x80, GRef<u16>(rs + 0x132));
}

// EE: 0x00113708
// Sky detail tables (0x1080 bytes: stars, clouds) and the cloud density map (block 26 + 0x70).
void func_00113708(u32 rs) {
    GRef<u32>(0x004104E8) = WorkHeapAlloc(0x1080, 0x10);
    GRef<u32>(0x004104EC) = GRef<u32>(g_FlightScene() + 0x16FC) + 0x70;
    func_00113750(rs);
}

// EE: 0x001117F8
// Per-mission sky flags in the render settings (+4): night 0x400 (missions 2, 0x11, 0x19),
// weather levels with the sky state's particle count (+0x18) and density (+0x1C).
void func_001117F8(u32 mission) {
    const u32 sky = SkyState();
    u32 bits;
    u16 count;
    u32 density;
    switch (mission) {
        case 2: case 0x11: case 0x19: GRef<u32>(RenderSettings() + 4) |= 0x400; return;
        case 0x12: case 0x28: GRef<u32>(RenderSettings() + 4) |= 0x100; return;
        case 0x14: GRef<u32>(RenderSettings() + 4) |= 0x500; return;
        case 4: count = 2; bits = 0x800; density = GRef<u32>(0x0040DC24); break;
        case 8: count = 2; bits = 0xA00; density = GRef<u32>(0x0040DC28); break;
        case 0xB: count = 3; bits = 0xC00; density = GRef<u32>(0x0040DC2C); break;
        case 0xC: count = 2; bits = 0x900; density = GRef<u32>(0x0040DC30); break;
        default: return;
    }
    GRef<u32>(RenderSettings() + 4) |= bits;
    GRef<u32>(sky + 0x1C) = density;
    GRef<u16>(sky + 0x18) = count;
}

// EE: 0x001118C8
// Render settings of the mission (block 24), sky detail, sky state (0x40), and the block 25 GS
// upload sent on DMA channel 2 (GIF) right away.
void func_001118C8() {
    GRef<u32>(0x004104D4) = GRef<u32>(g_FlightScene() + 0x16F4) & 0x0FFFFFFF;
    func_00113708(GRef<u32>(0x004104D4));
    GRef<u32>(0x004104D0) = WorkHeapAlloc(0x40, 0x10);
    FlushCache(0);
    sceGsSyncPath(0, 0);
    sceDmaSend(sceDmaGetChan(2), GRef<u32>(g_FlightScene() + 0x16F8) & 0x0FFFFFFF);
    sceGsSyncPath(0, 0);
}

// EE: 0x00111950
// Sky state reset (fog colour from the render settings +0x48, mode 8), mission flags, stars and
// clouds regenerated.
void func_00111950() {
    const u32 sky = SkyState();
    const u32 fog = GRef<u32>(RenderSettings() + 0x48);
    GRef<u32>(sky + 8) = 8;
    GRef<u32>(sky + 0xC) = 0;
    GRef<u32>(sky + 4) = fog & 0xFFFFFF;
    GRef<u32>(sky + 0x10) = 0;
    for (u32 o = 0x20; o < 0x30; o += 4) GRef<u32>(sky + o) = 0;
    func_001117F8(GRef<u8>(g_GameState().addr + 0x27));
    func_00113750(RenderSettings());
}

// EE: 0x00110170
// Horizon blend factor toward yaw (docs/subsystems/sky.md): M = RotY(d) · RotX(angles.x) ·
// RotZ(angles.z), d = angles.y − yaw wrapped once by the limits at 0x0040DBD4..0x0040DBE0;
// returns (M · (0, 0, −0.5, 0)).z + 0.5.
// fpucheck: 1 sub (d), 1 sub/add (wrap, when taken), 1 add (result)
f32 func_00110170(f32 yaw, u32 angles) {
    guest::GuestFrame frame(0x50);
    const u32 m = frame.Address(), v = m + 0x40;
    SetUnitMatrixVu0(m);
    sceVu0RotMatrixZ(m, m, GRef<f32>(angles + 8));
    sceVu0RotMatrixX(m, m, GRef<f32>(angles + 0));
    const f32 d = fpu::Sub(GRef<f32>(angles + 4), yaw);
    f32 a = d;
    if (GRef<f32>(0x0040DBD4) < d)
        a = fpu::Sub(d, GRef<f32>(0x0040DBD8));
    else if (d < GRef<f32>(0x0040DBDC))
        a = fpu::Add(d, GRef<f32>(0x0040DBE0));
    sceVu0RotMatrixY(m, m, a);
    for (u32 o = 0; o < 16; o += 4) GRef<u32>(v + o) = 0;
    GRef<f32>(v + 8) = -0.5f;
    sceVu0ApplyMatrix(v, m, v);
    return fpu::Add(GRef<f32>(v + 8), 0.5f);
}

// EE: 0x001100A0
// Fog / sky colour toward a direction (docs/subsystems/sky.md): R = *0x004104D4; the colour pair
// for the pitch (R+0x48/0x4C below R+0x10, R+0x78/0x7C above R+0x14, blended between) is blended
// by func_00110170. The colour words are loaded with lw (sign-extended); LerpColor works per byte,
// so only the low word of the result is used by the callers.
// fpucheck: 2 sub (pitch blend, when taken)
u64 func_001100A0(u32 angles, f32 yaw, f32 pitch) {
    const u32 rs = GRef<u32>(0x004104D4);
    const f32 p = -pitch;
    auto word = [&](u32 off) { return static_cast<u64>(static_cast<s64>(GRef<s32>(rs + off))); };
    const f32 t = func_00110170(yaw, angles);
    const f32 lo = GRef<f32>(rs + 0x10);
    u64 a, b;
    if (p <= lo) {
        b = word(0x4C);
        a = word(0x48);
    } else if (GRef<f32>(rs + 0x14) <= p) {
        b = word(0x7C);
        a = word(0x78);
    } else {
        const f32 span = fpu::Sub(GRef<f32>(rs + 0x14), lo);
        const f32 u = fpu::Div(fpu::Sub(p, lo), span);
        a = LerpColor(u, word(0x48), word(0x78));
        b = LerpColor(u, word(0x4C), word(0x7C));
    }
    return LerpColor(t, a, b);
}

// EE: 0x00112CA8
// Frame alpha remap (docs/subsystems/sky.md): uploads a 256-entry CLUT (entry i = (128 - i) & 0xFF
// below 128, else i, in the CLUT's swizzled order) to cbp, then redraws the view rectangle's frame
// alpha through it as a PSMT8H texture; restores draw-env qwords 2-3, 4-5, 10-11, 6-7.
void func_00112CA8(u32 list, s32 bucket, u32 cbp, u32 cam) {
    const u32 s = Ctx()->cur_buf.addr;
    const u64 cb = static_cast<u64>(cbp) << 32;
    u32 t = s + 0x10;
    GRef<u64>(t + 0x00) = 0x1000000000000004ull;
    GRef<u64>(t + 0x18) = 0x50;
    GRef<u64>(t + 0x30) = 0x0000001000000010ull;
    GRef<u64>(t + 0x08) = 0xE;
    GRef<u64>(t + 0x10) = cb | 0x1B01000000000000ull;
    GRef<u64>(t + 0x28) = 0x51;
    GRef<u64>(t + 0x38) = 0x52;
    GRef<u64>(t + 0x48) = 0x53;
    GRef<u64>(t + 0x50) = 0x0800000000000010ull;
    GRef<u64>(t + 0x58) = 0;
    GRef<u64>(t + 0x20) = 0;
    GRef<u64>(t + 0x40) = 0;
    const u32 table = s + 0x70;
    for (u32 i = 0; i < 0x100; ++i) {
        const u32 idx = (i & 0xE7) | ((i & 0x10) >> 1) | ((i & 8) << 1);
        GRef<u8>(table + idx) = static_cast<u8>(i < 0x80 ? 0xFFFFFF80u - i : i);
    }
    const u64 fbw = static_cast<u64>(Ctx()->draw_w >> 6);
    GRef<u64>(table + 0x100) = 0x1000000000008001ull;
    t = s + 0x170;
    GRef<u64>(t + 0x18) = 0x3F;  // TEXFLUSH (data unwritten)
    GRef<u64>(t + 0x08) = 0xE;
    GRef<u64>(table + 0x120) = 0x108B400000000005ull;
    t = s + 0x190;
    GRef<u64>(t + 0x10) = (fbw << 16) | 0x00FFFFFF00000000ull;
    GRef<u64>(t + 0x58) = 0x14;
    GRef<u64>(t + 0x18) = 0x4C;
    GRef<u64>(t + 0x20) = 0x1310000A8ull;
    GRef<u64>(t + 0x28) = 0x4E;
    GRef<u64>(t + 0x30) = 0x30000;
    GRef<u64>(t + 0x38) = 0x47;
    GRef<u64>(t + 0x48) = 0x18;
    GRef<u64>(t + 0x08) = 0xE;
    GRef<u64>(t + 0x40) = 0;
    GRef<u64>(t + 0x50) = 0;
    GRef<u64>(table + 0x180) = 0x2400000000000001ull;
    GRef<u64>(s + 0x1F8) = 0x16;
    GRef<u64>(table + 0x190) = (fbw << 14) | 0x2000000000000000ull | (cb << 5) | 0xEA9B00000ull;
    GRef<u64>(s + 0x208) = 0x80808080ull;
    const s32 x0 = GRef<s16>(cam + 0x188), y0 = GRef<s16>(cam + 0x18A);
    const s32 x1 = x0 + GRef<s16>(cam + 0x18C), y1 = y0 + GRef<s16>(cam + 0x18E);
    auto pack = [](s32 lo, s32 hi) {
        return static_cast<u64>(static_cast<u32>(lo)) | (static_cast<u64>(static_cast<u32>(hi)) << 16);
    };
    t = BuildSpriteStripsUv(s + 0x210, pack(x0 * 16, y0 * 16), pack(x1 * 16, y1 * 16),
                            pack(x0 * 16 + 8, y0 * 16 + 9), pack(x1 * 16 + 8, y1 * 16 + 9));
    GRef<u64>(t + 8) = 0xE;
    GRef<u64>(t + 0) = 0x1000000000008004ull;
    const u32 mc = g_pMainContext().addr;
    for (u32 o = 0; o < 16; o += 4) GRef<u32>(t + 0x10 + o) = GRef<u32>(mc + 0xD0 + o);
    for (u32 o = 0; o < 16; o += 4) GRef<u32>(t + 0x20 + o) = GRef<u32>(mc + 0xE0 + o);
    for (u32 o = 0; o < 16; o += 4) GRef<u32>(t + 0x30 + o) = GRef<u32>(mc + 0x110 + o);
    for (u32 o = 0; o < 16; o += 4) GRef<u32>(t + 0x40 + o) = GRef<u32>(mc + 0xF0 + o);
    const u32 end = CloseDmaPacket(s, t + 0x50);
    LinkPacket(list, bucket, s, end);
}

// EE: 0x00113B50
// Copies a work page back into the frame (docs/subsystems/sky.md): 64-pixel column sprites in
// context 2 sampling tex0 (w x 2h, PSMT8H via TEXA); returns the write pointer (p when w < 1).
u32 func_00113B50(u32 p, u32 tex0, u32 fbp, s32 w, s32 h) {
    const s32 n = (w + 63) / 64;
    if (n <= 0) return p;
    const s64 wcol = w >> 6;
    const u64 frame = static_cast<u64>(fbp >> 5) | (static_cast<u64>(wcol) << 16) | 0x0A000000ull;
    const u64 scissor = (static_cast<u64>(static_cast<s64>(w - 1)) << 16) |
                        (static_cast<u64>(static_cast<s64>(2 * h - 1)) << 48);
    const u64 tex = static_cast<u64>(tex0) | (static_cast<u64>(wcol) << 14) | 0xEA8A00000ull;
    GRef<u64>(p + 0x00) = 0x118B400000000008ull;
    GRef<u64>(p + 0x08) = 0xE;
    GRef<u64>(p + 0x18) = 0x3F;  // TEXFLUSH (data unwritten)
    GRef<u64>(p + 0x20) = frame;
    GRef<u64>(p + 0x28) = 0x4D;
    GRef<u64>(p + 0x48) = 0x19;
    GRef<u64>(p + 0x30) = scissor;
    GRef<u64>(p + 0x38) = 0x41;
    GRef<u64>(p + 0x58) = 0x15;
    GRef<u64>(p + 0x60) = 0x8000000000ull;
    GRef<u64>(p + 0x68) = 0x3B;
    GRef<u64>(p + 0x70) = 0x30000;
    GRef<u64>(p + 0x78) = 0x48;
    GRef<u64>(p + 0x80) = 0x1310000A8ull;
    GRef<u64>(p + 0x88) = 0x4F;
    GRef<u64>(p + 0x40) = 0;
    GRef<u64>(p + 0x50) = 0;
    p += 0x90;
    GRef<u64>(p + 0x00) = 0x2400000000000001ull;
    GRef<u64>(p + 0x08) = 0x79;
    GRef<u64>(p + 0x10) = 0x3FF003F7Full;
    GRef<u64>(p + 0x18) = tex;
    p += 0x20;
    GRef<u64>(p + 0) = static_cast<u64>(static_cast<u32>(n) | 0x8000) | 0x4400000000000000ull;
    GRef<u64>(p + 8) = 0x5353;
    u32 q = p + 0x10;
    const u64 h32 = static_cast<u64>(static_cast<s64>(h * 32));
    for (s32 j = 0; j < n; ++j) {
        const u64 x = 0x400ull * static_cast<u64>(j);
        GRef<u64>(q + 8) = x;
        GRef<u64>(q + 0) = (x + 8) | 0x80000;
        GRef<u64>(q + 0x10) = (x + 0x400) | ((h32 + 8) << 16);
        GRef<u64>(q + 0x18) = (x + 0x3F8) | (h32 << 16);
        q += 0x20;
    }
    return q;
}

// EE: 0x001132E0
// Fills the frame's alpha channel with a (clamped to 0..255) for the destination-alpha blends;
// restores draw-env qwords 2-3, 4-5, 10-11, 6-7.
void func_001132E0(u32 list, s32 bucket, s32 a) {
    if (a < 0)
        a = 0;
    else if (a >= 0x100)
        a = 0xFF;
    const u32 s = Ctx()->cur_buf.addr;
    const u32 t = s + 0x10;
    GRef<u64>(t + 0x00) = 0x1003400000000005ull;
    GRef<u64>(t + 0x10) = 0x00FFFFFF00080000ull;
    GRef<u64>(t + 0x18) = 0x4C;
    GRef<u64>(t + 0x20) = 0x1310000A8ull;
    GRef<u64>(t + 0x58) = 1;
    GRef<u64>(t + 0x28) = 0x4E;
    GRef<u64>(t + 0x30) = 0x30000;
    GRef<u64>(t + 0x38) = 0x47;
    GRef<u64>(t + 0x48) = 0x18;
    // sll then sd: the 32-bit value a << 24 is sign-extended into the RGBAQ dword.
    GRef<u64>(t + 0x50) = static_cast<u64>(static_cast<s64>(static_cast<s32>(static_cast<u32>(a) << 24)));
    GRef<u64>(t + 0x08) = 0xE;
    GRef<u64>(t + 0x40) = 0;
    const u32 e = BuildSpriteStrips(s + 0x70, 0, 0x1C002000);
    GRef<u64>(e + 8) = 0xE;
    GRef<u64>(e + 0) = 0x1000000000008004ull;
    const u32 mc = g_pMainContext().addr;
    for (u32 o = 0; o < 16; o += 4) GRef<u32>(e + 0x10 + o) = GRef<u32>(mc + 0xD0 + o);
    for (u32 o = 0; o < 16; o += 4) GRef<u32>(e + 0x20 + o) = GRef<u32>(mc + 0xE0 + o);
    for (u32 o = 0; o < 16; o += 4) GRef<u32>(e + 0x30 + o) = GRef<u32>(mc + 0x110 + o);
    for (u32 o = 0; o < 16; o += 4) GRef<u32>(e + 0x40 + o) = GRef<u32>(mc + 0xF0 + o);
    const u32 end = CloseDmaPacket(s, e + 0x50);
    LinkPacket(list, bucket, s, end);
}


// ---- Sky dome (docs/subsystems/sky.md "Sky dome") -------------------------------------------

namespace {

// Host copy of the VU0 registers the sky passes between functions: the projection matrix vf4-vf7
// and the screen rectangle vf3 = (cx, cy, half-w, half-h), loaded by the ring builders right
// before they call func_0010FFB8 (through func_00110F38 / func_00110FE0).
struct SkyVu0 {
    vu0::Vec4 vf1, vf2, vf3, vf4, vf5, vf6, vf7, vf8, vf14, vf15;
};
SkyVu0 g_SkyVu0;

constexpr u32 kSkyWork = 0x70000000;  // W: scratchpad work area

vu0::Vec4 LoadVec(u32 p) { return {GRef<f32>(p), GRef<f32>(p + 4), GRef<f32>(p + 8), GRef<f32>(p + 12)}; }

void StoreVec(u32 p, const vu0::Vec4& v) {
    GRef<f32>(p) = v.x;
    GRef<f32>(p + 4) = v.y;
    GRef<f32>(p + 8) = v.z;
    GRef<f32>(p + 12) = v.w;
}

vu0::Vec4 ScaleVec(const vu0::Vec4& v, f32 s) {
    return {vu0::Mul(v.x, s), vu0::Mul(v.y, s), vu0::Mul(v.z, s), vu0::Mul(v.w, s)};
}

void Copy16(u32 dst, u32 src) {
    for (u32 o = 0; o < 16; o += 4) GRef<u32>(dst + o) = GRef<u32>(src + o);
}

// vclipw.xyz bits for one test (bit 0 x > |w|, 1 x < -|w|, 2 y > |w|, 3 y < -|w|, 4/5 for z).
u32 ClipFlags(const vu0::Vec4& v, f32 w) {
    const f32 a = std::fabs(w);
    u32 b = 0;
    if (v.x > a) b |= 0x01;
    if (v.x < -a) b |= 0x02;
    if (v.y > a) b |= 0x04;
    if (v.y < -a) b |= 0x08;
    if (v.z > a) b |= 0x10;
    if (v.z < -a) b |= 0x20;
    return b;
}

// Loads the dome matrix W+0x40 into vf4-vf7 with rows scaled by row A[i] = (u, v): vf4 * u,
// vf5 * v, vf6 * u (vaddx.z copies u into vf8.z); vf3 = W+0xC0.
void LoadRingMatrix(u32 w, u32 i) {
    const f32 u = GRef<f32>(w + 0x2E0 + 8 * i), v = GRef<f32>(w + 0x2E4 + 8 * i);
    g_SkyVu0.vf4 = ScaleVec(LoadVec(w + 0x40), u);
    g_SkyVu0.vf5 = ScaleVec(LoadVec(w + 0x50), v);
    g_SkyVu0.vf6 = ScaleVec(LoadVec(w + 0x60), vu0::Add(0.0f, u));
    g_SkyVu0.vf7 = LoadVec(w + 0x70);
    g_SkyVu0.vf3 = LoadVec(w + 0xC0);
}

// Per-byte colour adjust of the 5 colour words at out (VU0, vitof0 / add / clamp 0..255 /
// optional multiply and clamp / vftoi0 / byte repack).
void AdjustColours(u32 out, const vu0::Vec4& add, bool multiply, f32 mul) {
    auto clampv = [](f32 v) {
        if (v < 0.0f) v = 0.0f;  // vmaxx vf0.x
        if (v > 255.0f) v = 255.0f;  // vminibcx 255.0
        return v;
    };
    for (u32 k = 0; k < 5; ++k) {
        const u32 c = GRef<u32>(out + 4 * k);
        f32 lane[4] = {vu0::IntToFloat(static_cast<s32>(c & 0xFF)), vu0::IntToFloat(static_cast<s32>((c >> 8) & 0xFF)),
                       vu0::IntToFloat(static_cast<s32>((c >> 16) & 0xFF)), vu0::IntToFloat(static_cast<s32>(c >> 24))};
        const f32 a[4] = {add.x, add.y, add.z, add.w};
        u32 r = 0;
        for (u32 l = 0; l < 4; ++l) {
            f32 v = clampv(vu0::Add(lane[l], a[l]));
            if (multiply) v = clampv(vu0::Mul(v, mul));
            r |= (static_cast<u32>(static_cast<s32>(v)) & 0xFF) << (8 * l);  // vftoi0, ppach/ppacb
        }
        GRef<u32>(out + 4 * k) = r;
    }
}

vu0::Vec4 ByteVec(u32 c, f32 s) {
    return {vu0::Mul(vu0::IntToFloat(static_cast<s32>(c & 0xFF)), s),
            vu0::Mul(vu0::IntToFloat(static_cast<s32>((c >> 8) & 0xFF)), s),
            vu0::Mul(vu0::IntToFloat(static_cast<s32>((c >> 16) & 0xFF)), s),
            vu0::Mul(vu0::IntToFloat(static_cast<s32>(c >> 24)), s)};
}

u64 SignedWord(u32 addr) { return static_cast<u64>(static_cast<s64>(GRef<s32>(addr))); }

}  // namespace

// EE: 0x0010FFB8
// VU0 projection of p with the matrix in vf4-vf7 and the screen rectangle in vf3: out (8 bytes) =
// u16 x, u16 y, u32 z >> 4 (12.4 fixed point); returns -1 behind the camera or off the 0..4096
// range, else the clip bits & 0xCC (x against half-w, y against half-h).
// MAC read: PCSX2's COP2 GameDB patch makes the cfc2 see the flags of the vmaddaz (ACC.w without
// the fourth row), docs/port/validation.md B-2.
s32 func_0010FFB8(u32 out, u32 p) {
    const vu0::Vec4 v8 = LoadVec(p);
    const SkyVu0& r = g_SkyVu0;
    const f32 accw = vu0::MulAdd(vu0::MulAdd(vu0::Mul(r.vf4.w, v8.x), r.vf5.w, v8.y), r.vf6.w, v8.z);
    vu0::Vec4 v9 = vu0::MulMatrix(r.vf4, r.vf5, r.vf6, r.vf7, v8);
    if (accw <= 0.0f) return -1;  // MAC Zw | Sw
    const f32 q = vu0::Div(1.0f, v9.w);
    v9.x = vu0::Mul(v9.x, q);
    v9.y = vu0::Mul(v9.y, q);
    v9.z = vu0::Mul(v9.z, q);
    const s32 ix = vu0::FloatToInt4(v9.x), iy = vu0::FloatToInt4(v9.y), iz = vu0::FloatToInt4(v9.z);
    // Sticky zero/sign flags of v.xy - 0 and (4096, 4096) - v.xy (status cleared first).
    const bool sticky = vu0::Sub(v9.x, 0.0f) <= 0.0f || vu0::Sub(v9.y, 0.0f) <= 0.0f ||
                        vu0::Sub(4096.0f, v9.x) <= 0.0f || vu0::Sub(4096.0f, v9.y) <= 0.0f;
    GRef<u32>(out) = (static_cast<u32>(ix) & 0xFFFF) | (static_cast<u32>(iy) << 16);
    GRef<u32>(out + 4) = static_cast<u32>(iz) >> 4;
    if (sticky) return -1;
    const vu0::Vec4 d{vu0::Sub(v9.x, r.vf3.x), vu0::Sub(v9.y, r.vf3.y), 0.0f, r.vf3.z};
    const u32 first = ClipFlags(d, r.vf3.z), second = ClipFlags(d, r.vf3.w);
    return static_cast<s32>(((first << 6) | second) & 0xCC);
}

// EE: 0x00110F38
// Projects the 32 ring points pts into out (8 bytes each, the clip result replacing the z word);
// returns the visibility mask (bit i: point i in front) with bit 0 repeated in bit 32 and out[32] =
// out[0] (closed ring). The mask is built in a 64-bit register: bit 31 is sign-extended (sllv),
// so a visible point 31 sets bits 31..63.
u64 func_00110F38(u32 pts, u32 out) {
    u64 mask = 0;
    for (u32 i = 0; i < 32; ++i) {
        const s32 r = func_0010FFB8(out + 8 * i, pts + 16 * i);
        GRef<s32>(out + 8 * i + 4) = r;
        if (r >= 0) mask |= static_cast<u64>(static_cast<s64>(static_cast<s32>(1u << i)));
    }
    mask |= (mask & 1) << 32;
    GRef<u64>(out + 0x100) = GRef<u64>(out);
    return mask;
}

// EE: 0x00110FE0
// Dome cap: one point replicated over the 33 ring slots; 0 when it is behind the camera.
u64 func_00110FE0(u32 p, u32 out) {
    const s32 r = func_0010FFB8(out, p);
    if (r < 0) return 0;
    GRef<s32>(out + 4) = r;
    for (u32 k = 1; k <= 32; ++k) GRef<u64>(out + 8 * k) = GRef<u64>(out);
    return 0x1FFFFFFFFull;
}

// EE: 0x00111050
// Strip between two projected rings A and B (docs/subsystems/sky.md): one 8-register REGLIST loop
// per visible segment; returns the end, or pkt when no segment was written.
u32 func_00111050(u32 pkt, u64 mask, u32 a, u32 b, u32 cols, u64 za, u64 zb) {
    const u64 c1 = GRef<u32>(cols + 4), c0 = GRef<u32>(cols);
    u32 n = 0;
    u32 p = pkt + 0x10;
    for (u32 i = 0; mask != 0;) {
        ++i;
        if ((mask & 3) == 3) {
            const u32 clip = (GRef<u32>(a + 4) & GRef<u32>(b + 0xC)) & (GRef<u32>(b + 4) & GRef<u32>(a + 0xC));
            if (clip == 0) {
                GRef<u64>(p + 0x10) = c0;
                ++n;
                GRef<u64>(p + 0x00) = c0;
                GRef<u64>(p + 0x30) = c1;
                GRef<u64>(p + 0x20) = c1;
                GRef<u64>(p + 0x08) = za | GRef<u32>(a);
                GRef<u64>(p + 0x18) = za | GRef<u32>(a + 8);
                GRef<u64>(p + 0x28) = zb | GRef<u32>(b);
                GRef<u64>(p + 0x38) = zb | GRef<u32>(b + 8);
                p += 0x40;
            }
        }
        a += 8;
        b += 8;
        mask >>= 1;
        if (i >= 32) break;
    }
    if (n == 0) return pkt;
    GRef<u64>(pkt + 8) = 0x5151D1D1;
    GRef<u64>(pkt + 0) = (static_cast<u64>(n) | 0x8000) | 0x8400000000000000ull;
    return p;
}

// EE: 0x00111138
// Dome rings 0..28 into the packet (Z test always); rings 0 and 28 are the poles.
u32 func_00111138(u32 pkt, u32 w, u32 buf, s32 z) {
    GRef<u64>(pkt + 0x00) = 0x1000000000000002ull;
    GRef<u64>(pkt + 0x08) = 0xE;
    GRef<u64>(pkt + 0x10) = 0x310000A8;
    GRef<u64>(pkt + 0x18) = 0x4E;
    GRef<u64>(pkt + 0x20) = 0x30000;
    GRef<u64>(pkt + 0x28) = 0x47;
    u32 p = pkt + 0x30;
    u64 zword = static_cast<u64>(static_cast<s64>(z - 8)) << 32;
    u64 prevZ = zword, prevMask = 0;
    u32 prevBuf = 0;
    g_SkyVu0.vf3 = LoadVec(w + 0xC0);
    const u64 zfull = static_cast<u64>(static_cast<u32>(z)) << 32;
    for (u32 i = 0; i < 29; ++i) {
        if (i == 15) zword = zfull;
        LoadRingMatrix(w, i);
        const u32 ring = buf + (i & 1) * 0x108;
        const u64 mask = (i == 0 || i == 28) ? func_00110FE0(w + 0xD0, ring) : func_00110F38(w + 0xD0, ring);
        const u64 both = prevMask & mask;
        if (both != 0) {
            GRef<u64>(p + 8) = 0;
            GRef<u64>(p + 0) = 0x1400000000000001ull;
            GRef<u64>(p + 0x10) = 0xC;
            p = func_00111050(p + 0x20, both, prevBuf, ring, w + 0x3C4 + 4 * i, prevZ, zword);
        }
        prevBuf = ring;
        prevMask = mask;
        prevZ = zword;
    }
    return p;
}

// EE: 0x00111310
// Horizon fog bands (R+4 bit 4): rings 14..20, 8 alpha bands per visible ring pair at distances
// from R+0xC - 4096 in steps of min(R+0xC - R+8, 4096) / 8.
// fpucheck: 2 sub (span, d0); per band 1 add (z) + 1 add (d)
u32 func_00111310(u32 pkt, u32 w, u32 buf, u32 cam) {
    const u32 rs = GRef<u32>(0x004104D4);
    const f32 far8 = GRef<f32>(rs + 8), farC = GRef<f32>(rs + 0xC);
    f32 span = fpu::Sub(farC, far8);
    const f32 zoff = GRef<f32>(cam + 0x88), zscale = GRef<f32>(cam + 0x98);
    if (4096.0f < span) span = 4096.0f;
    const f32 d0 = fpu::Sub(farC, 4096.0f);
    const f32 step = span * 0.125f;
    GRef<u64>(pkt + 0x00) = 0x1000000000008004ull;
    GRef<u64>(pkt + 0x08) = 0xE;
    GRef<u64>(pkt + 0x10) = 0x1310000A8ull;
    GRef<u64>(pkt + 0x18) = 0x4E;
    GRef<u64>(pkt + 0x20) = 0x70000;
    GRef<u64>(pkt + 0x28) = 0x47;
    GRef<u64>(pkt + 0x30) = 0x44;
    GRef<u64>(pkt + 0x38) = 0x42;
    GRef<u64>(pkt + 0x40) = 0xFF00000000080000ull;
    GRef<u64>(pkt + 0x48) = 0x4C;
    u32 p = pkt + 0x50;
    g_SkyVu0.vf3 = LoadVec(w + 0xC0);
    u32 prevBuf = 0;
    u64 prevMask = 0;
    for (u32 i = 14; i < 21; ++i) {
        LoadRingMatrix(w, i);
        const u32 ring = buf + (i & 1) * 0x108;
        const u64 mask = func_00110F38(w + 0xD0, ring);
        const u64 both = prevMask & mask;
        if (both != 0) {
            f32 d = d0;
            u64 alpha = 0x10;
            for (u32 j = 0; j < 8; ++j) {
                const f32 zf = fpu::Div(zscale, d);
                GRef<u64>(p + 0x00) = 0x1026400000008001ull;
                GRef<u64>(p + 0x10) = (alpha << 32) | 0x64;
                GRef<u64>(p + 0x08) = 0xE;
                GRef<u64>(p + 0x18) = 0x42;
                const f32 zz = fpu::Add(zf, zoff);
                d = fpu::Add(d, step);
                const u64 zw = static_cast<u64>(static_cast<s64>(fpu::FloatToInt(zz))) << 32;
                p = func_00111050(p + 0x20, both, prevBuf, ring, w + 0x3C4 + 4 * i, zw, zw);
                alpha += 8;
            }
        }
        prevBuf = ring;
        prevMask = mask;
    }
    return p;
}

// EE: 0x00110240
// out = third row of RotY(yaw) * RotX(pitch) (unit first), times vf0.w.
void func_00110240(u32 out, f32 pitch, f32 yaw) {
    guest::GuestFrame frame(0x40);
    const u32 m = frame.Address();
    SetUnitMatrixVu0(m);
    sceVu0RotMatrixX(m, m, pitch);
    sceVu0RotMatrixY(m, m, yaw);
    StoreVec(out, ScaleVec(LoadVec(m + 0x20), 1.0f));
}

// EE: 0x001102B0
// Dome frame (docs/subsystems/sky.md): ring directions, view rotation relative to the sun yaw,
// altitude lift, inverse view matrices; W+0xC0 = the screen rectangle K+0x30.
// fpucheck: 1 sub + 1 sub/add (yaw wrap), 1 add (look-up factor)
void func_001102B0(u32 w, u32 unused, u32 cam) {
    (void)unused;
    const f32 k65536 = 65536.0f;
    for (u32 i = 0; i < 32; ++i) {
        const f32 a = GRef<f32>(0x002A1948 + 8 * i), b = GRef<f32>(0x002A194C + 8 * i);
        const vu0::Vec4 v{vu0::Mul(a, k65536), vu0::Sub(0.0f, k65536), vu0::Mul(b, k65536), vu0::Add(0.0f, 1.0f)};
        StoreVec(w + 0xD0 + 0x10 * i, v);
    }
    const u32 m = w + 0x40, ang = w + 0x2D0;
    SetUnitMatrixVu0(m);
    Copy16(ang, cam + 0x10);
    const f32 d = fpu::Sub(GRef<f32>(w + 0x2D4), GRef<f32>(GRef<u32>(0x00410694) + 0x8C));
    f32 yaw = d;
    if (GRef<f32>(0x0040DBE4) < d)
        yaw = fpu::Sub(d, GRef<f32>(0x0040DBE8));
    else if (d < GRef<f32>(0x0040DBEC))
        yaw = fpu::Add(d, GRef<f32>(0x0040DBF0));
    GRef<f32>(w + 0x2D4) = yaw;
    func_00102D28(m, m, ang);
    sceVu0CopyMatrix(w + 0x80, m);
    for (u32 o = 0; o < 16; o += 4) GRef<u32>(ang + o) = 0;
    GRef<f32>(w + 0x2D8) = -1.0f;
    sceVu0ApplyMatrix(ang, m, ang);
    GRef<f32>(w + 0x498) = fpu::Add(GRef<f32>(w + 0x2D8), 1.0f) * 0.5f;
    const f32 h = GRef<f32>(cam + 4);
    const f32 s = fpu::Sqrt(-h * GRef<f32>(0x0040DBF4));
    f32 f = fpu::Div(65536.0f, s);
    if (1.0f < f) f = 1.0f;
    if (65536.0f < s) {
        StoreVec(ang, {0.0f, 0.0f, 0.0f, 1.0f});
        GRef<f32>(w + 0x2D4) = h * f;
        sceVu0TransMatrix(m, m, ang);
    }
    GRef<f32>(w + 0x460) = f;
    GRef<f32>(w + 0x464) = s;
    GRef<f32>(w + 0x468) = h;
    GRef<f32>(w + 0x46C) = GRef<f32>(w + 0x2D4);
    sceVu0InversMatrix(m, m);
    MulMatrixVu0(m, cam + 0x60, m);
    sceVu0InversMatrix(w + 0x80, w + 0x80);
    MulMatrixVu0(w + 0x80, cam + 0x60, w + 0x80);
    Copy16(w + 0xC0, GRef<u32>(0x004104D0) + 0x30);
}

// EE: 0x00110508
// Gradient colour sets for altitude p: below g[0] the set at g+0x10, above g[1] the set at g+0x40,
// else a per-byte blend of the two (three 16-byte lerps).
// fpucheck: 2 sub (blend factor)
void func_00110508(f32 p, u32 out, u32 g) {
    const f32 lo = GRef<f32>(g);
    if (p <= lo) {
        for (u32 o = 0; o < 0x30; o += 8) GRef<u64>(out + o) = GRef<u64>(g + 0x10 + o);
        return;
    }
    const f32 hi = GRef<f32>(g + 4);
    if (hi <= p) {
        for (u32 o = 0; o < 0x30; o += 8) GRef<u64>(out + o) = GRef<u64>(g + 0x40 + o);
        return;
    }
    const f32 span = fpu::Sub(hi, lo);
    const f32 u = fpu::Div(fpu::Sub(p, lo), span);
    func_001050D8(u, out, g + 0x10, g + 0x40);
    func_001050D8(u, out + 0x10, g + 0x20, g + 0x50);
    func_001050D8(u, out + 0x20, g + 0x30, g + 0x60);
}

// EE: 0x00110630
// Ring colours (docs/subsystems/sky.md): gradient and horizon colours, the far-fog blend, the
// altitude colour adjust, the sun glow (one rand() when K+0 bit 0 is set and MC flag 0x100 is
// clear), alpha bytes 0x80.
// fpucheck: blend 2 sub per pass reached; adjust 1-2 sub; glow 1 sub, 1 sub (cos), 1 sub + 1 add (rand)
void func_00110630(f32 t, f32 p, u32 out, u32 g, u32 s) {
    GRef<u32>(out + 0) = GRef<u32>(g + 0x20);
    GRef<u32>(out + 4) = GRef<u32>(g + 0x24);
    func_001050D8(t, g + 0x30, g, g + 0x10);
    GRef<u32>(out + 0x08) = GRef<u32>(g + 0x30);
    GRef<u32>(out + 0x0C) = GRef<u32>(g + 0x34);
    GRef<u32>(out + 0x10) = GRef<u32>(g + 0x38);
    GRef<u32>(out + 0x14) = GRef<u32>(g + 0x3C);
    GRef<u32>(out + 0x18) = static_cast<u32>(LerpColor(t, SignedWord(g + 0x28), SignedWord(g + 0x2C)));
    GRef<u32>(out + 0x1C) = static_cast<u32>(LerpColor(t, SignedWord(s + 8), SignedWord(s + 0xC)));
    const u32 tobj = GRef<u32>(0x00410694);
    const f32 fk = GRef<f32>(0x0040DBF8);
    f32 a = GRef<f32>(tobj + 0xA8) * fk, b = GRef<f32>(tobj + 0xA4) * fk;
    GRef<u32>(out + 0x20) = GRef<u32>(s + 0x10);
    for (u32 k = 8; k >= 7; --k) {
        const u32 slot = out + 4 * k;
        if (b < p) {
            GRef<u32>(slot) = GRef<u32>(out + 0x18);
        } else if (a < p) {
            const f32 inv = fpu::Div(1.0f, fpu::Sub(b, a));
            const f32 f = fpu::Sub(p, a) * inv;
            GRef<u32>(slot) = static_cast<u32>(LerpColor(f, SignedWord(slot), SignedWord(out + 0x18)));
        }
        a = a * 0.5f;
        b = b * 0.5f;
    }
    const u32 ksky = GRef<u32>(0x004104D0);
    if (GRef<f32>(0x0040DBFC) <= p) {
        const f32 f = fpu::Sub(p, GRef<f32>(0x0040DBFC)) * GRef<f32>(0x0040DC00);
        const f32 mul = fpu::Sub(1.0f, f);
        AdjustColours(out, ByteVec(0x00427070, -1.0f), true, mul);
    } else if (GRef<f32>(0x0040DC04) <= p) {
        const f32 f = fpu::Sub(p, GRef<f32>(0x0040DC04)) * GRef<f32>(0x0040DC08);
        AdjustColours(out, ByteVec(0x00427070, f), false, 0.0f);
    }
    f32 glow = fpu::Sub(GRef<f32>(ksky + 0xC), 0.5f);
    if (0.0f < glow)
        glow = fpu::Sub(1.0f, ee::cosf(glow * GRef<f32>(0x0040DC0C)));
    else
        glow = 0.0f;
    if ((GRef<u32>(g_pMainContext().addr) & 0x100) == 0 && (GRef<u32>(ksky) & 1) != 0) {
        const f32 cover = fpu::Sub(1.0f, GRef<f32>(ksky + 0x14));
        const s32 u = (rand() >> 8) & 0xFFF;
        if (u > 0x800) glow = fpu::Add(glow, cover * (fpu::IntToFloat(u) * GRef<f32>(0x0040DC10)));
    }
    if (GRef<f32>(0x0040DC14) < glow) AdjustColours(out, ByteVec(0x00606060, glow), true, 1.0f);
    for (u32 k = 0; k < 9; ++k) GRef<u32>(out + 4 * k) = (GRef<u32>(out + 4 * k) & 0x00FFFFFF) | 0x80000000;
}

// EE: 0x00110AA8
// Ring rows A (u, v) and colours B for the 29 dome rings (docs/subsystems/sky.md).
// fpucheck: rows 0-7 1 add; 8-14 1 sub + 1 add (+1 add step) per row; 15-19 1 sub + 2 sub/add per
// row; row 20 2 sub; 21-28 1 sub (+1-2 sub) per row
void func_00110AA8(u32 w, u32 unused, u32 s, f32 t) {
    (void)unused;
    const u32 rowA = w + 0x2E0, rowB = w + 0x3C8, e = 0x002A1948;
    sceVu0InterVector(w + 0x2D0, s + 0x30, s + 0x20, t);
    auto c = [&](u32 k) { return SignedWord(w + 0x43C + 4 * k); };
    auto setRow = [&](u32 k, f32 u, f32 v, u64 col) {
        GRef<f32>(rowA + 8 * k) = u;
        GRef<f32>(rowA + 8 * k + 4) = v;
        GRef<u32>(rowB + 4 * k) = static_cast<u32>(col);
    };
    const f32 base = GRef<f32>(s + 0x18);
    for (u32 k = 0; k < 8; ++k) {
        GRef<f32>(rowA + 8 * k) = GRef<f32>(e + 8 * k);
        const f32 bb = GRef<f32>(e + 8 * k + 4);
        GRef<f32>(rowA + 8 * k + 4) = fpu::Add(bb * 0.5f, base);
        GRef<u32>(rowB + 4 * k) = static_cast<u32>(LerpColor(bb, c(1), c(0)));
    }
    const f32 h0 = GRef<f32>(w + 0x2D0);
    f32 f = 0.0f;
    f32 dlt = fpu::Sub(h0, base);
    for (u32 k = 8; k < 10; ++k) {
        setRow(k, 1.0f, fpu::Add(base, dlt * f), LerpColor(f, c(1), c(2)));
        f = fpu::Add(f, 0.5f);
    }
    const f32 h1 = GRef<f32>(w + 0x2D4);
    f = 0.0f;
    dlt = fpu::Sub(h1, h0);
    for (u32 k = 10; k < 12; ++k) {
        setRow(k, 1.0f, fpu::Add(h0, dlt * f), LerpColor(f, c(2), c(3)));
        f = fpu::Add(f, 0.5f);
    }
    const f32 h2 = GRef<f32>(w + 0x2D8);
    f = 0.0f;
    dlt = fpu::Sub(h2, h1);
    for (u32 k = 12; k < 15; ++k) {
        setRow(k, 1.0f, fpu::Add(h1, dlt * f), LerpColor(f, c(3), c(4)));
        f = fpu::Add(f, 0.5f);
    }
    const f32 k16 = 1.52587890625e-05f;  // 2^-16
    const f32 e15 = fpu::Sub(GRef<f32>(w + 0x468) * GRef<f32>(0x0040DC18), GRef<f32>(w + 0x46C) * k16);
    const f32 q = GRef<f32>(GRef<u32>(0x00410694) + 0xA4) * k16;
    const f32 v15 = GRef<f32>(s + 0x1C);
    const f32 ee = fpu::Div(e15, q);
    f = 0.0f;
    dlt = fpu::Sub(ee, v15);
    for (u32 k = 15; k < 20; ++k) {
        const f32 fq = (f * q) * 0.5f;
        const f32 vv = fpu::Add(v15, dlt * f);
        setRow(k, fpu::Sub(1.0f, fq), vv, LerpColor(f, c(5), c(6)));
        f = fpu::Add(f, 0.25f);
    }
    const f32 e20n = fpu::Sub((GRef<f32>(w + 0x468) * q) * GRef<f32>(0x0040DC1C), GRef<f32>(w + 0x46C) * k16);
    const f32 h3 = GRef<f32>(w + 0x2DC);
    GRef<u32>(rowB + 4 * 20) = GRef<u32>(w + 0x454);
    GRef<f32>(rowA + 8 * 20) = q * 0.5f;
    const f32 e20 = fpu::Sub(fpu::Div(e20n, q), GRef<f32>(0x0040DC20));
    GRef<f32>(rowA + 8 * 20 + 4) = e20;
    for (u32 k = 20, n = 0; k < 29; ++k, ++n) {
        const f32 ea = GRef<f32>(e + 8 * n), eb = GRef<f32>(e + 8 * n + 4);
        GRef<f32>(rowA + 8 * k) = (eb * 0.5f) * q;
        GRef<f32>(rowA + 8 * k + 4) = fpu::Sub(e20, ea * 0.25f);
        u64 col;
        if (ea <= h3)
            col = LerpColor(fpu::Div(ea, h3), c(6), c(7));
        else
            col = LerpColor(fpu::Div(fpu::Sub(ea, h3), fpu::Sub(1.0f, h3)), c(7), c(8));
        GRef<u32>(rowB + 4 * k) = static_cast<u32>(col);
    }
}

// EE: 0x001115C0
// Sky dome (docs/subsystems/sky.md): frame, colours, rings into bucket 1; the horizon fog bands
// into bucket 5 when R+4 bit 2.
void func_001115C0(u32 cam, u32 list) {
    const u32 w = kSkyWork;
    func_001102B0(w, w + 0x4A0, cam);
    const f32 p = -GRef<f32>(cam + 4);
    u32 s = Ctx()->cur_buf.addr;
    func_00110508(p, w + 0x7A0, GRef<u32>(0x004104D4) + 0x10);
    func_00110630(GRef<f32>(w + 0x498), p, w + 0x43C, w + 0x7A0, GRef<u32>(0x004104D4) + 0x80);
    func_00110AA8(w, w + 0x4A0, GRef<u32>(0x004104D4) + 0x80, GRef<f32>(w + 0x498));
    const u32 ksky = GRef<u32>(0x004104D0);
    GRef<u32>(ksky + 4) = GRef<u32>(w + 0x454) & 0x00FFFFFF;
    u32 e = func_00111138(s + 0x10, w, w + 0x4A0, GRef<s32>(ksky + 8));
    GRef<u64>(e + 8) = 0xE;
    GRef<u64>(e + 0) = 0x1000000000008002ull;
    Copy16(e + 0x10, g_pMainContext().addr + 0xE0);
    Copy16(e + 0x20, g_pMainContext().addr + 0x110);
    LinkPacket(list, 1, s, CloseDmaPacket(s, e + 0x30));
    if ((GRef<u32>(GRef<u32>(0x004104D4) + 4) & 4) == 0) return;
    s = Ctx()->cur_buf.addr;
    e = func_00111310(s + 0x10, w, w + 0x4A0, cam);
    GRef<u64>(e + 8) = 0xE;
    GRef<u64>(e + 0) = 0x1000000000008003ull;
    Copy16(e + 0x10, g_pMainContext().addr + 0xE0);
    Copy16(e + 0x20, g_pMainContext().addr + 0x110);
    Copy16(e + 0x30, g_pMainContext().addr + 0xD0);
    LinkPacket(list, 5, s, CloseDmaPacket(s, e + 0x40));
}

// EE: 0x001119A8
// Per-view sky parameters into K (docs/subsystems/sky.md).
// fpucheck: 1 add (z), unsigned convert, 3 sub (altitude factor)
void func_001119A8(u32 k, u32 cam) {
    const u32 rs = GRef<u32>(0x004104D4);
    const f32 zr = fpu::Div(GRef<f32>(cam + 0x98), GRef<f32>(rs + 0xC));
    const s32 hw = GRef<s16>(cam + 0x18C) / 2;
    GRef<f32>(k + 0x30) = GRef<f32>(cam + 0x178);
    const s32 hh = GRef<s16>(cam + 0x18E) / 2;
    const s32 z = fpu::FloatToInt(fpu::Add(zr, GRef<f32>(cam + 0x88)));
    GRef<f32>(k + 0x38) = fpu::IntToFloat(hw);
    GRef<f32>(k + 0x34) = GRef<f32>(cam + 0x17C);
    GRef<f32>(k + 0x3C) = fpu::IntToFloat(hh);
    GRef<s32>(k + 8) = 7 < z ? z : 8;
    GRef<u32>(k + 0xC) = 0;
    GRef<u32>(k + 0x10) = 0;
    if (GRef<u32>(GRef<u32>(0x00410694) + 4) == 0 && (GRef<u32>(rs + 0xC0) & 8) != 0) {
        const f32 n = fpu::UnsignedToFloat(GRef<u32>(GRef<u32>(0x004104C4) + 0x1660));
        const f32 f = n * GRef<f32>(0x0040DC34);
        GRef<f32>(k + 0x20) = -GRef<f32>(rs + 0xD0) * f;
        GRef<f32>(k + 0x28) = -GRef<f32>(rs + 0xD4) * f;
    } else {
        GRef<u32>(k + 0x20) = 0;
        GRef<u32>(k + 0x28) = 0;
    }
    const f32 lo = GRef<f32>(rs + 0x10);
    const f32 span = fpu::Sub(GRef<f32>(rs + 0x14), lo);
    f32 a = fpu::Div(fpu::Sub(-GRef<f32>(cam + 4), lo), span);
    if (a < 0.0f)
        a = 0.0f;
    else if (1.0f < a)
        a = 1.0f;
    GRef<f32>(k + 0x14) = a;
}

// EE: 0x00111B28
// Sky and background of one view (docs/subsystems/sky.md "Top level").
// fpucheck: 1 sub (glow strength)
void func_00111B28(u32 cam, u32 list) {
    const u32 rs = GRef<u32>(0x004104D4);
    func_001119A8(GRef<u32>(0x004104D0), cam);
    const u32 t = GRef<u32>(0x00410694);
    if ((GRef<u32>(rs + 4) & 0x10) != 0) func_00116D38(GRef<f32>(t + 0xAC), GRef<f32>(t + 0xB0), cam, list);
    if ((GRef<u32>(rs + 4) & 1) != 0)
        func_00117940(GRef<f32>(GRef<u32>(0x00410694) + 0x88), GRef<f32>(GRef<u32>(0x00410694) + 0x8C), cam, list);
    func_001115C0(cam, list);
    if ((GRef<u32>(rs + 4) & 8) != 0) func_00118400(GRef<f32>(rs + 0x84), cam, list, GRef<u16>(rs + 0x82));
    if ((GRef<u32>(rs + 0xC0) & 1) != 0) func_00116918(cam, list);
    if ((GRef<u32>(rs + 0xC0) & 2) != 0) func_00115B10(cam, list);
    if ((GRef<u32>(rs + 0xC0) & 4) != 0) func_00115F28(cam, list);
    const u32 ksky = GRef<u32>(0x004104D0);
    GRef<u32>(ksky) = 0;
    if ((GRef<u32>(rs + 4) & 0x800) == 0) return;
    const f32 st = fpu::Sub(1.0f, GRef<f32>(ksky + 0x14)) * GRef<f32>(ksky + 0x1C);
    func_001120E8(st, list, 0xC, cam, GRef<s16>(ksky + 0x18), 0xC0C0C0);
}


// ---- Full-screen post effects (docs/subsystems/sky.md "Full-screen post effects") -----------

// EE: 0x00111C80
// Downsample level n (context 2, textured FST sprites): for each of the 2^n passes, TEX0_2 =
// tex[k] and two frames, 512-wide source columns halved; n = 0 adds the bottom band.
u32 func_00111C80(u32 p, s32 n, u64 scissor, u32 tex, u32 frames) {
    if (n < 0) return p;
    const s32 w = 0x2000 >> n;
    s32 cnt = w / 512;
    GRef<u64>(p + 8) = 0xE;
    GRef<u64>(p + 0x10) = scissor;
    GRef<u64>(p + 0) = 0x118B400000000001ull;
    GRef<u64>(p + 0x18) = 0x41;
    const s32 passes = 1 << n;
    const s32 c = (0x1C0 << (4 - n)) + 0x40;
    if (n == 0) cnt *= 2;
    p += 0x20;
    for (s32 k = 0; k < passes; ++k) {
        GRef<u64>(p + 0) = 0x1400000000000001ull;
        GRef<u64>(p + 8) = 7;
        const u64 reglist = static_cast<u64>(static_cast<u32>(cnt)) | 0x4400000000000000ull;
        GRef<u64>(p + 0x10) = GRef<u64>(tex);
        tex += 8;
        p += 0x20;
        for (s32 j = 0; j < 2; ++j) {
            GRef<u64>(p + 0) = 0x1000000000000001ull;
            GRef<u64>(p + 8) = 0xE;
            GRef<u64>(p + 0x18) = 0x4D;
            GRef<u64>(p + 0x10) = GRef<u64>(frames);
            frames += 8;
            GRef<u64>(p + 0x20) = reglist;
            GRef<u64>(p + 0x28) = 0x5353;
            u32 q = p + 0x30;
            const u32 o = static_cast<u32>(16 * j);
            for (s32 x = 0; x < w; x += 0x200) {
                const u32 ux = static_cast<u32>(x) + o;
                const u32 hx = static_cast<u32>(x >> 1);
                GRef<u64>(q + 0) = ux | (static_cast<u64>(o) << 16);
                GRef<u64>(q + 0x10) = (ux + 0x1F8) | (static_cast<u64>(static_cast<u32>(c) + o) << 16);
                GRef<u64>(q + 0x18) = (hx + 0xFC) | (static_cast<u64>(static_cast<u32>(c >> 1)) << 16);
                GRef<u64>(q + 8) = hx;
                q += 0x20;
                if (n == 0) {
                    GRef<u64>(q + 0) = ux | 0x1BF80000u;
                    GRef<u64>(q + 8) = hx | (static_cast<u64>(0xE00 - o) << 16);
                    GRef<u64>(q + 0x10) = (ux + 0x1F8) | 0x1BF80000u;
                    GRef<u64>(q + 0x18) = (hx + 0xFC) | 0x0E800000u;
                    q += 0x20;
                }
            }
            p = q;
        }
    }
    return p;
}

// EE: 0x00111EC8
// Upsample / blend level n (context 2, textured alpha-blended FST sprites): per pass one FRAME_2
// and two textures with colours colA / colB, offsets 12 and 4 sixteenths.
u32 func_00111EC8(u32 p, s32 n, u64 scissor, u32 tex, u32 frames, u64 cola, u64 colb) {
    if (n < 0) return p;
    const s32 w = 0x2000 >> n;
    const s32 c = (0x1C0 << (4 - n)) + 0x20;
    GRef<u64>(p + 0) = 0x11AB400000000001ull;
    GRef<u64>(p + 8) = 0xE;
    GRef<u64>(p + 0x10) = scissor;
    GRef<u64>(p + 0x18) = 0x41;
    const s32 cnt = w / 512;
    const s32 passes = 1 << n;
    p += 0x20;
    for (s32 k = 0; k < passes; ++k) {
        GRef<u64>(p + 0) = 0x1000000000000001ull;
        GRef<u64>(p + 8) = 0xE;
        const u64 reglist = static_cast<u64>(static_cast<u32>(cnt)) | 0x4400000000000000ull;
        GRef<u64>(p + 0x18) = 0x4D;
        GRef<u64>(p + 0x10) = GRef<u64>(frames);
        frames += 8;
        p += 0x20;
        for (s32 j = 0; j < 2; ++j) {
            GRef<u64>(p + 0) = 0x2400000000000001ull;
            GRef<u64>(p + 8) = 0x71;
            GRef<u64>(p + 0x10) = j == 0 ? cola : colb;
            GRef<u64>(p + 0x18) = GRef<u64>(tex);
            tex += 8;
            const u32 o = static_cast<u32>(12 - 8 * j);
            GRef<u64>(p + 0x20) = reglist;
            GRef<u64>(p + 0x28) = 0x5353;
            u32 q = p + 0x30;
            for (s32 x = 0; x < w; x += 0x200) {
                const u32 u = static_cast<u32>(x >> 1) + o;
                const u32 v = static_cast<u32>(c >> 1) + o;
                GRef<u64>(q + 8) = static_cast<u32>(x);
                GRef<u64>(q + 0) = u | (static_cast<u64>(o) << 16);
                GRef<u64>(q + 0x10) = (u + 0xFC) | (static_cast<u64>(v) << 16);
                GRef<u64>(q + 0x18) = (static_cast<u32>(x) + 0x1F8) | (static_cast<u64>(static_cast<u32>(c)) << 16);
                q += 0x20;
            }
            p = q;
        }
    }
    return p;
}

namespace {

// Common start of the glow and blur packets: menu draw env, XYOFFSET_2 = 0 + TEXFLUSH,
// (CLAMP_2, RGBAQ) = (5, 0x80808080), the downsample chain and ALPHA_2 = 0x44, the upsample of
// levels 2 and 1. Returns the write pointer.
u32 GlowChain(u32 s, s32 levels) {
    u32 p = BuildMenuDrawEnv(s + 0x10, Ctx()->draw_w, Ctx()->draw_h);
    GRef<u64>(p + 0x00) = 0x1000000000000002ull;
    GRef<u64>(p + 0x18) = 0x19;
    GRef<u64>(p + 0x08) = 0xE;
    GRef<u64>(p + 0x10) = 0;
    GRef<u64>(p + 0x28) = 0x3F;  // TEXFLUSH (data unwritten)
    p += 0x30;
    GRef<u64>(p + 0x08) = 0x19;
    GRef<u64>(p + 0x18) = 0x80808080ull;
    GRef<u64>(p + 0x00) = 0x2400000000000001ull;
    GRef<u64>(p + 0x10) = 5;
    p = func_00111C80(p + 0x20, 0, 0x00FF000000FF0000ull, 0x004104D8, 0x002A1AB8);
    if (levels >= 2) p = func_00111C80(p, 1, 0x007F0000007F0000ull, 0x002A1A48, 0x002A1AC8);
    if (levels >= 3) p = func_00111C80(p, 2, 0x003F0000003F0000ull, 0x002A1A58, 0x002A1AE8);
    GRef<u64>(p + 0x00) = 0x1000000000000001ull;
    GRef<u64>(p + 0x10) = 0x44;
    GRef<u64>(p + 0x18) = 0x43;
    GRef<u64>(p + 0x08) = 0xE;
    p += 0x20;
    if (levels >= 3)
        p = func_00111EC8(p, 2, 0x007F0000007F0000ull, 0x002A1A78, 0x002A1AC8, 0x80808080ull, 0x40808080ull);
    if (levels >= 2)
        p = func_00111EC8(p, 1, 0x00FF000000FF0000ull, 0x002A1A58, 0x002A1AB8, 0x80808080ull, 0x40808080ull);
    return p;
}

}  // namespace

// EE: 0x001120E8
// Glow (t in 0..1): downsample/upsample chain, then the level-0 blend with colour
// LerpColor(t, 0x808080, colour) and alphas q, L (L = t * 64, q = (L << 7) / (128 - L)).
void func_001120E8(f32 t, u32 list, s32 bucket, u32 cam, s32 levels, u32 colour) {
    if (t <= 0.0f || levels <= 0) return;
    if (1.0f < t) t = 1.0f;
    const u32 s = Ctx()->cur_buf.addr;
    u32 p = GlowChain(s, levels);
    const u64 c = LerpColor(t, 0x00808080ull, colour & 0x00FFFFFFu);
    const s64 l = fpu::FloatToInt(t * 64.0f);
    const u64 q = static_cast<u64>(l << 7) / static_cast<u64>(0x80 - l);  // __udivdi3
    const u64 cz = static_cast<u64>(static_cast<u32>(c));
    p = func_00111EC8(p, 0, GRef<u64>(cam + 0x1A0), 0x002A1A48, g_pMainContext().addr + 0xD0, (q << 24) | cz,
                      (static_cast<u64>(l) << 24) | cz);
    GRef<u64>(p + 8) = 0xE;
    GRef<u64>(p + 0x18) = 0x3F;
    GRef<u64>(p + 0) = 0x1000000000008001ull;
    LinkPacket(list, bucket, s, CloseDmaPacket(s, p + 0x20));
}

// EE: 0x00112450
// Blur: the chain, the level-0 blend into the work frame 0x004104E0, then the frame blended back
// by destination alpha (ALPHA_2 = 0x54) in eight 64-pixel columns.
void func_00112450(u32 list, s32 bucket, u32 cam, s32 levels) {
    if (levels <= 0) return;
    const u32 s = Ctx()->cur_buf.addr;
    u32 p = GlowChain(s, levels);
    p = func_00111EC8(p, 0, 0x01BF000001FF0000ull, 0x002A1A48, 0x004104E0, 0x80808080ull, 0x40808080ull);
    GRef<u64>(p + 0x08) = 0xE;
    GRef<u64>(p + 0x00) = 0x11AB400000000003ull;
    GRef<u64>(p + 0x18) = 0x4D;
    GRef<u64>(p + 0x10) = GRef<u64>(g_pMainContext().addr + 0xD0) | 0xFF00000000000000ull;
    GRef<u64>(p + 0x30) = 0x54;
    GRef<u64>(p + 0x20) = GRef<u64>(cam + 0x1A0);
    GRef<u64>(p + 0x28) = 0x41;
    GRef<u64>(p + 0x38) = 0x43;
    p += 0x40;
    GRef<u64>(p + 0x00) = 0x2400000000000001ull;
    GRef<u64>(p + 0x08) = 0x17;
    GRef<u64>(p + 0x10) = 0x264123100ull;
    GRef<u64>(p + 0x18) = 0x80808080ull;
    p += 0x20;
    GRef<u64>(p + 0) = 0x4400000000008008ull;
    GRef<u64>(p + 8) = 0x5353;
    u32 q = p + 0x10;
    for (u32 x = 0; x < 0x2000; x += 0x400) {
        GRef<u64>(q + 8) = x;
        GRef<u64>(q + 0x10) = (x + 0x400) | 0x1C090000u;
        GRef<u64>(q + 0) = (x + 8) | 0x90000u;
        GRef<u64>(q + 0x18) = (x + 0x3F8) | 0x1C000000u;
        q += 0x20;
    }
    LinkPacket(list, bucket, s, CloseDmaPacket(s, q));
}

// EE: 0x00113468
// Context-2 setup (A+D NLOOP 9, EOP); returns p + 0xA0.
u32 func_00113468(u32 p, u64 frame, u64 scissor, u64 xyoffset) {
    GRef<u64>(p + 0x70) = 0x80;
    GRef<u64>(p + 0x68) = 0x15;
    GRef<u64>(p + 0x78) = 0x3B;
    GRef<u64>(p + 0x88) = 9;
    GRef<u64>(p + 0x90) = 1;
    GRef<u64>(p + 0x98) = 0x46;
    GRef<u64>(p + 0x00) = 0x1000000000008009ull;
    GRef<u64>(p + 0x08) = 0xE;
    GRef<u64>(p + 0x10) = frame;
    GRef<u64>(p + 0x18) = 0x4D;
    GRef<u64>(p + 0x20) = scissor;
    GRef<u64>(p + 0x28) = 0x41;
    GRef<u64>(p + 0x30) = xyoffset;
    GRef<u64>(p + 0x38) = 0x19;
    GRef<u64>(p + 0x40) = 0x131000000ull;
    GRef<u64>(p + 0x48) = 0x4F;
    GRef<u64>(p + 0x50) = 0x30000;
    GRef<u64>(p + 0x58) = 0x48;
    GRef<u64>(p + 0x60) = 0x60;
    GRef<u64>(p + 0x80) = 0;
    return p + 0xA0;
}

// EE: 0x00113988
// Clears a w x h work page at fbp (context 1, black, alpha 0) with 64-pixel column sprites.
u32 func_00113988(u32 p, u32 fbp, s32 w, s32 h) {
    const s32 n = (w + 63) / 64;
    if (n <= 0) return p;
    const u32 x0 = static_cast<u32>(0x8000 - 8 * w);
    const u64 frame = static_cast<u64>(fbp >> 5) | (static_cast<u64>(static_cast<s64>(w / 64)) << 16);
    const u64 xyoff = static_cast<u64>(static_cast<s64>(static_cast<s32>(x0))) |
                      (static_cast<u64>(static_cast<u32>(0x8000 - 8 * h)) << 32);
    const u64 scissor = (static_cast<u64>(static_cast<s64>(w - 1)) << 16) |
                        (static_cast<u64>(static_cast<s64>(h - 1)) << 48);
    GRef<u64>(p + 0x00) = 0x1003400000000006ull;
    GRef<u64>(p + 0x08) = 0xE;
    GRef<u64>(p + 0x18) = 0x4C;
    GRef<u64>(p + 0x10) = frame;
    GRef<u64>(p + 0x50) = scissor;
    GRef<u64>(p + 0x20) = 0x1310000A8ull;
    GRef<u64>(p + 0x28) = 0x4E;
    GRef<u64>(p + 0x30) = 0x30000;
    GRef<u64>(p + 0x38) = 0x47;
    GRef<u64>(p + 0x40) = xyoff;
    GRef<u64>(p + 0x58) = 0x40;
    GRef<u64>(p + 0x60) = 0x60;
    GRef<u64>(p + 0x68) = 0x14;
    GRef<u64>(p + 0x48) = 0x18;
    p += 0x70;
    GRef<u64>(p + 0x00) = 0x2400000000000001ull;
    GRef<u64>(p + 0x08) = 0x18;
    GRef<u64>(p + 0x10) = 0;
    GRef<u64>(p + 0x18) = 0;
    p += 0x20;
    GRef<u64>(p + 0) = (static_cast<u64>(static_cast<u32>(n)) | 0x8000) | 0x2400000000000000ull;
    GRef<u64>(p + 8) = 0x55;
    u32 q = p + 0x10;
    u32 x = x0;
    const u64 ylo = static_cast<u64>(static_cast<u32>(0x8000 - 8 * h)) << 16;
    const u64 yhi = static_cast<u64>(static_cast<u32>(8 * h + 0x8000)) << 16;
    for (s32 i = 0; i < n; ++i) {
        const u32 x1 = x + 0x400;
        GRef<u64>(q + 0) = static_cast<u64>(x) | ylo;
        GRef<u64>(q + 8) = static_cast<u64>(x1) | yhi;
        x = x1;
        q += 0x10;
    }
    return q;
}


// ---- Stars (docs/subsystems/sky.md "Stars") ------------------------------------------------

// EE: 0x00116060
// VU0 projection with perspective texture coordinates (matrix vf4-vf7, rectangle vf3, vf1.x = 1.0
// from the caller): xyz = (u16 x, u16 y, z word), q = 1 / w, st = src * q; returns -1 when off the
// 0..4096 range, else clip & 0x303.
// HYPOTHESIS: the MAC read after `vmulq.xyz vf9` (no PCSX2 GameDB patch) sees clear w flags, so
// its "behind the camera" test never fires (docs/port/pcsx2-pending.md).
s32 func_00116060(u32 xyz, u32 st, u32 q, u32 p, u32 src, u32 z) {
    const SkyVu0& r = g_SkyVu0;
    vu0::Vec4 v9 = vu0::MulMatrix(r.vf4, r.vf5, r.vf6, r.vf7, LoadVec(p));
    const f32 qq = vu0::Div(1.0f, v9.w);
    const f32 s = vu0::Mul(r.vf1.x, qq);
    v9.x = vu0::Mul(v9.x, qq);
    v9.y = vu0::Mul(v9.y, qq);
    v9.z = vu0::Mul(v9.z, qq);
    const s32 ix = vu0::FloatToInt4(v9.x), iy = vu0::FloatToInt4(v9.y);
    const bool sticky = vu0::Sub(v9.x, 0.0f) <= 0.0f || vu0::Sub(v9.y, 0.0f) <= 0.0f ||
                        vu0::Sub(4096.0f, v9.x) <= 0.0f || vu0::Sub(4096.0f, v9.y) <= 0.0f;
    GRef<u32>(xyz) = (static_cast<u32>(ix) & 0xFFFF) | (static_cast<u32>(iy) << 16);
    GRef<u32>(xyz + 4) = GRef<u32>(z);
    if (sticky) return -1;
    const vu0::Vec4 d{vu0::Sub(v9.x, r.vf3.x), vu0::Sub(v9.y, r.vf3.y), 0.0f, r.vf3.z};
    const u32 first = ClipFlags(d, r.vf3.w), second = ClipFlags(d, r.vf3.z);
    if (q != 0) GRef<f32>(q) = s;
    const s32 res = static_cast<s32>(((first << 6) | second) & 0x303);
    if (st != 0) {
        GRef<f32>(st) = vu0::Mul(GRef<f32>(src), s);
        GRef<f32>(st + 4) = vu0::Mul(GRef<f32>(src + 4), s);
    }
    return res;
}

// EE: 0x00116130
// Star frame in W (docs/subsystems/sky.md): view matrix around a point 43008 ahead, star drift
// by the frame counter, screen rectangle.
// fpucheck: 3 add (camera offset), unsigned convert, 2 add (drift)
void func_00116130(u32 w, u32 cam, u32 s) {
    sceVu0UnitMatrix(w);
    func_00102D28(w, w, cam + 0x10);
    Copy16(w + 0x40, cam);
    StoreVec(w + 0x50, {0.0f, 0.0f, 0.0f, 1.0f});
    GRef<f32>(w + 0x58) = -43008.0f;
    sceVu0ApplyMatrix(w + 0x50, w, w + 0x50);
    sceVu0TransMatrix(w, w, w + 0x50);
    sceVu0InversMatrix(w, w);
    sceVu0MulMatrix(w, cam + 0x60, w);
    Copy16(w + 0x60, w + 0x50);
    GRef<f32>(w + 0x60) = fpu::Add(GRef<f32>(w + 0x60), -GRef<f32>(cam + 0) * 0.25f);
    GRef<f32>(w + 0x64) = fpu::Add(GRef<f32>(w + 0x64), -GRef<f32>(cam + 4) * 0.25f);
    GRef<f32>(w + 0x68) = fpu::Add(GRef<f32>(w + 0x68), -GRef<f32>(cam + 8) * 0.25f);
    const f32 f = fpu::UnsignedToFloat(GRef<u32>(GRef<u32>(0x004104C4) + 0x1660)) * GRef<f32>(0x0040DC90);
    const u32 ksky = GRef<u32>(0x004104D0);
    GRef<f32>(w + 0x60) = fpu::Add(GRef<f32>(w + 0x60), f * GRef<f32>(s + 0x10));
    GRef<u32>(w + 0x130) = GRef<u32>(ksky + 8);
    GRef<f32>(w + 0x68) = fpu::Add(GRef<f32>(w + 0x68), f * GRef<f32>(s + 0x14));
    GRef<u32>(w + 0x134) = GRef<u32>(ksky + 0x20);
    GRef<u32>(w + 0x138) = GRef<u32>(ksky + 0x28);
    StoreVec(w + 0x90, {0.0f, 0.0f, 0.0f, 1.0f});
    GRef<f32>(w + 0x80) = GRef<f32>(cam + 0x178);
    GRef<f32>(w + 0x84) = GRef<f32>(cam + 0x17C);
    GRef<f32>(w + 0x88) = fpu::IntToFloat(GRef<s16>(cam + 0x18C) / 2);
    GRef<f32>(w + 0x8C) = fpu::IntToFloat(GRef<s16>(cam + 0x18E) / 2);
}

// EE: 0x00116320
// Star quads (docs/subsystems/sky.md): up to 400 stars from the table *0x004104E8, faded by
// distance and cloud cover, each a textured alpha-blended strip of 4 projected corners.
// fpucheck: 1 add (W+0x74); per star 1 sub (y), 1 sub (e), 1 add/sub (fade), 6 add (cloud lookup)
u32 func_00116320(f32 t, f32 c, u32 pkt, u32 w, u32 s) {
    const s32 alpha = GRef<u16>(s + 2);
    s32 count = GRef<u16>(s + 0);
    if (count <= 0 || alpha <= 0) return pkt;
    if (count > 400) count = 400;
    const u32 colA = static_cast<u32>(LerpColor(c, SignedWord(s + 0x10), SignedWord(s + 0x14)));
    const u32 colB = static_cast<u32>(LerpColor(c, SignedWord(s + 0x18), SignedWord(s + 0x1C)));
    GRef<u64>(pkt + 0x00) = 0x1000000000008005ull;
    GRef<u64>(pkt + 0x08) = 0xE;
    GRef<u64>(pkt + 0x10) = 0x44;
    GRef<u64>(pkt + 0x18) = 0x42;
    GRef<u64>(pkt + 0x20) = 0x5000D;
    GRef<u64>(pkt + 0x28) = 0x47;
    GRef<u64>(pkt + 0x30) = 0x1310000A8ull;
    GRef<u64>(pkt + 0x38) = 0x4E;
    GRef<u64>(pkt + 0x40) = 0x61;
    GRef<u64>(pkt + 0x48) = 0x14;
    GRef<u64>(pkt + 0x50) = 0x2007E88599B05FC0ull;
    GRef<u64>(pkt + 0x58) = 6;
    u32 p = pkt + 0x60;
    Copy16(w + 0x70, w + 0x60);
    GRef<f32>(w + 0x74) = fpu::Add(GRef<f32>(w + 0x74), -t * 0.25f);
    guest::GuestFrame frame(0x40);
    const u32 tmpl = frame.Address();
    for (u32 o = 0; o < 0x40; ++o) GRef<u8>(tmpl + o) = GRef<u8>(0x00403E40 + o);
    g_SkyVu0.vf1 = {1.0f, 0.0f, 0.0f, 0.0f};
    g_SkyVu0.vf3 = LoadVec(w + 0x80);
    g_SkyVu0.vf4 = LoadVec(w + 0x00);
    g_SkyVu0.vf5 = LoadVec(w + 0x10);
    g_SkyVu0.vf6 = LoadVec(w + 0x20);
    g_SkyVu0.vf7 = LoadVec(w + 0x30);
    StoreVec(w + 0x90, {0.0f, 0.0f, 0.0f, 1.0f});
    const f32 spread = GRef<f32>(s + 4), size = GRef<f32>(s + 8);
    for (s32 k = 0; k < 4; ++k) {
        const f32 oy = fpu::IntToFloat((k & 2) - 1), ox = fpu::IntToFloat(((k & 1) << 1) - 1);
        GRef<f32>(w + 0xB0 + 8 * static_cast<u32>(k)) = size * ox;
        GRef<f32>(w + 0xB4 + 8 * static_cast<u32>(k)) = size * oy;
    }
    u32 star = GRef<u32>(0x004104E8);
    for (s32 i = 0; i < count; ++i, star += 8) {
        const s32 x = ((fpu::FloatToInt(GRef<f32>(w + 0x70)) + GRef<s16>(star)) & 0xFFFF) - 0x8000;
        const f32 fy = fpu::IntToFloat(static_cast<s32>(GRef<u16>(star + 2) & 0xFFF) - 0x800);
        const s32 zc = fpu::FloatToInt(GRef<f32>(w + 0x78));
        GRef<f32>(w + 0x90) = fpu::IntToFloat(x);
        GRef<f32>(w + 0x94) = fpu::Sub(GRef<f32>(w + 0x74), spread * fy);
        GRef<f32>(w + 0x98) = fpu::IntToFloat(((zc + GRef<s16>(star + 4)) & 0xFFFF) - 0x8000);
        const vu0::Vec4 v9 = vu0::MulMatrix(g_SkyVu0.vf4, g_SkyVu0.vf5, g_SkyVu0.vf6, g_SkyVu0.vf7, LoadVec(w + 0x90));
        const f32 d = vu0::Add(0.0f, v9.w);
        if (GRef<f32>(0x0040DC94) < d || d < GRef<f32>(0x0040DC98)) continue;
        f32 e = fpu::Sub(d, 43008.0f);
        if (e < 0.0f)
            e = fpu::Add(e * GRef<f32>(0x0040DC9C), GRef<f32>(0x0040DCA0));
        else
            e = fpu::Sub(GRef<f32>(0x0040DCA8), e * GRef<f32>(0x0040DCA4));
        if (e <= 0.0f) continue;
        if (1.0f < e) e = 1.0f;
        const f32 dx = fpu::Add(-GRef<f32>(w + 0x50), GRef<f32>(w + 0x90));
        const f32 dz = fpu::Add(-GRef<f32>(w + 0x58), GRef<f32>(w + 0x98));
        f32 cx = fpu::Add(GRef<f32>(w + 0x40), dx * 4.0f);
        f32 cz = fpu::Add(GRef<f32>(w + 0x48), dz * 4.0f);
        cx = fpu::Add(cx, GRef<f32>(w + 0x134));
        cz = fpu::Add(cz, GRef<f32>(w + 0x138));
        e = e * func_00115198(cx, cz);
        if (e <= 0.0f) continue;
        const s32 a = fpu::FloatToInt(fpu::IntToFloat(alpha) * e);
        if (a <= 0) continue;
        const u32 colour = (GRef<f32>(w + 0x54) < GRef<f32>(w + 0x94) ? colA : colB) | (static_cast<u32>(a) << 24);
        GRef<f32>(w + 0xA0) = 8192.0f;
        GRef<f32>(w + 0xA4) = 6144.0f;
        const vu0::Vec4 v22 = LoadVec(w + 0xA0), v21 = LoadVec(w + 0x90);
        const u32 corners = tmpl + 0x20 * static_cast<u32>(i & 1);
        s32 all = -1;
        bool dropped = false;
        u32 rec = w + 0xD0;
        for (u32 k = 0; k < 4; ++k, rec += 0x18) {
            const f32 ox = GRef<f32>(w + 0xB0 + 8 * k), oy = GRef<f32>(w + 0xB4 + 8 * k);
            vu0::Vec4 v10{vu0::Mul(ox, v22.x), vu0::Mul(oy, v22.y), 0.0f, 0.0f};
            v10.z = vu0::Add(0.0f, v10.y);
            v10.y = vu0::Sub(0.0f, 0.0f);
            v10.w = vu0::Sub(1.0f, 1.0f);
            v10 = {vu0::Add(v10.x, v21.x), vu0::Add(v10.y, v21.y), vu0::Add(v10.z, v21.z), vu0::Add(v10.w, v21.w)};
            StoreVec(w + 0xA0, v10);
            const s32 r = func_00116060(rec + 0x10, rec, rec + 0xC, w + 0xA0, corners + 8 * k, w + 0x130);
            if (r < 0) {
                dropped = true;
                break;
            }
            GRef<u32>(rec + 8) = colour;
            all &= r;
        }
        if (dropped || all != 0) continue;
        GRef<u64>(p + 0x00) = 0xE400000000008001ull;
        GRef<u64>(p + 0x08) = 0x00512512512512F0ull;
        GRef<u64>(p + 0x10) = 0x5C;
        u32 q = p + 0x20;
        for (u32 k = 0; k < 4; ++k) {
            const u32 src = w + 0xD0 + 0x18 * k;
            GRef<u64>(q + 0) = GRef<u64>(src + 0);
            GRef<u64>(q + 8) = GRef<u64>(src + 8);
            GRef<u64>(q + 0x10) = GRef<u64>(src + 0x10);
            q += 0x18;
        }
        p = q;
    }
    return p;
}

// EE: 0x00116918
// Stars pass (R+0xC0 bit 1): CLAMP_1 and the view scissor, the star quads, restores; bucket 0xB.
void func_00116918(u32 cam, u32 list) {
    const u32 s = Ctx()->cur_buf.addr;
    GRef<u64>(s + 0x10) = 0x1000000000008002ull;
    GRef<u64>(s + 0x28) = 8;
    GRef<u64>(s + 0x18) = 0xE;
    GRef<u64>(s + 0x20) = 0;
    Copy16(s + 0x30, cam + 0x1A0);
    const u32 rs = GRef<u32>(0x004104D4);
    func_00116130(kSkyWork, cam, rs + 0xC0);
    const f32 c = func_00110170(GRef<f32>(GRef<u32>(0x00410694) + 0x8C), cam + 0x10);
    u32 p = func_00116320(GRef<f32>(rs + 0xC8), c, s + 0x40, kSkyWork, rs + 0x140);
    GRef<u64>(p + 8) = 0xE;
    GRef<u64>(p + 0) = 0x1000000000008003ull;
    Copy16(p + 0x10, g_pMainContext().addr + 0xE0);
    Copy16(p + 0x20, g_pMainContext().addr + 0x110);
    Copy16(p + 0x30, g_pMainContext().addr + 0xF0);
    LinkPacket(list, 0xB, s, CloseDmaPacket(s, p + 0x40));
}


// ---- Clouds and cloud shadow (docs/subsystems/sky.md "Clouds and cloud shadow") ------------

namespace {

// Per-byte blend of the 4 colour bytes of a towards b by the factor f (12-bit fixed point), as the
// MMI sequence of func_001050D8 (psubh, pmulth, >> 12, paddsh, clamp 0..255); low word only.
u32 LerpBytes(s16 f, u32 a, u32 b) {
    u32 out = 0;
    for (u32 i = 0; i < 4; ++i) {
        const s16 ah = static_cast<s16>((a >> (8 * i)) & 0xFF), bh = static_cast<s16>((b >> (8 * i)) & 0xFF);
        const s16 d = static_cast<s16>(bh - ah);
        const s16 step = static_cast<s16>((static_cast<s32>(d) * f) >> 12);
        s32 sum = ah + step;
        if (sum > 0x7FFF) sum = 0x7FFF;
        if (sum < -0x8000) sum = -0x8000;
        if (sum < 0) sum = 0;
        if (sum > 0xFF) sum = 0xFF;
        out |= static_cast<u32>(sum) << (8 * i);
    }
    return out;
}

u64 Xy16(s32 x, s32 y) {
    return (static_cast<u64>(static_cast<u32>(x)) & 0xFFFF) | ((static_cast<u64>(static_cast<u32>(y)) & 0xFFFF) << 16);
}

}  // namespace

// EE: 0x0013EA08
// Word +0x2C of the record pointed to by +0x54 of the current map entry (terrain header T).
u32 func_0013EA08(u32 t) { return GRef<u32>(GRef<u32>(GRef<u32>(t) + 100 * GRef<u32>(t + 4) + 0x54) + 0x2C); }

// EE: 0x00115198
// Cloud density at (x, z): bilinear sample of the 256 x 256 map *0x004104EC (2048 units per cell),
// times *0x0040DC78.
// fpucheck: 2 sub, 3 add
f32 func_00115198(f32 x, f32 z) {
    const s32 ix0 = fpu::FloatToInt(x), iz0 = fpu::FloatToInt(-z);
    const u32 map = GRef<u32>(0x004104EC);
    const f32 fx = fpu::IntToFloat(ix0 & 0x7FF) * (1.0f / 2048.0f);
    const f32 fz = fpu::IntToFloat(iz0 & 0x7FF) * (1.0f / 2048.0f);
    const u32 ix = static_cast<u32>((ix0 + 0x40000) >> 11) & 0xFF, iz = static_cast<u32>((iz0 + 0x40000) >> 11) & 0xFF;
    const u32 ix1 = (ix + 1) & 0xFF, iz1 = (iz + 1) & 0xFF;
    const f32 gz = fpu::Sub(1.0f, fz), gx = fpu::Sub(1.0f, fx);
    auto m = [&](u32 r, u32 c) { return fpu::IntToFloat(GRef<u8>(map + c + r * 0x100)); };
    const f32 a = (gx * gz) * m(iz, ix);
    const f32 b = (fx * gz) * m(iz, ix1);
    const f32 c = (gx * fz) * m(iz1, ix);
    const f32 d = (fx * fz) * m(iz1, ix1);
    return fpu::Add(fpu::Add(fpu::Add(a, b), c), d) * GRef<f32>(0x0040DC78);
}

// EE: 0x001152A8
// Cloud density with the wind offset K+0x20 / K+0x28.
f32 func_001152A8(f32 x, f32 z) {
    const u32 k = GRef<u32>(0x004104D0);
    const f32 zz = fpu::Add(z, GRef<f32>(k + 0x28));
    return func_00115198(fpu::Add(x, GRef<f32>(k + 0x20)), zz);
}

// EE: 0x00115CF8
// Cloud shadow texture quad into the 256 x 256 page 0x128, scrolled by the camera cell and wind.
u32 func_00115CF8(u32 p, u32 cam, u32 wind, u32 colour) {
    GRef<u64>(p + 0x00) = 0x108B400000000006ull;
    GRef<u64>(p + 0x08) = 0xE;
    GRef<u64>(p + 0x18) = 0x4C;
    GRef<u64>(p + 0x10) = 0x40128;
    GRef<u64>(p + 0x20) = 0x1310000A8ull;
    GRef<u64>(p + 0x28) = 0x4E;
    GRef<u64>(p + 0x30) = 0x30000;
    GRef<u64>(p + 0x38) = 0x47;
    GRef<u64>(p + 0x48) = 0x18;
    GRef<u64>(p + 0x50) = 0x00FF000000FF0000ull;
    GRef<u64>(p + 0x58) = 0x40;
    GRef<u64>(p + 0x60) = 0x60;
    GRef<u64>(p + 0x68) = 0x14;
    GRef<u64>(p + 0x40) = 0;
    const f32 cx = GRef<f32>(cam + 0), cz = -GRef<f32>(cam + 8);
    const s32 ix = fpu::FloatToInt(cx) / 2048 + (cx < 0.0f ? 0x7F : 0x80);
    const s32 iz = fpu::FloatToInt(cz) / 2048 + (cz < 0.0f ? 0x7F : 0x80);
    u32 q = p + 0x70;
    GRef<u64>(q + 0x00) = 0x3400000000000001ull;
    GRef<u64>(q + 0x08) = 0x168;
    GRef<u64>(q + 0x10) = 0;
    GRef<u64>(q + 0x20) = colour;
    GRef<u64>(q + 0x18) = 0x2007010621312980ull;
    q += 0x30;
    const s32 wz = fpu::FloatToInt(GRef<f32>(wind + 8)), wx = fpu::FloatToInt(GRef<f32>(wind + 0));
    const u32 v = static_cast<u32>((iz << 4) - wz / 128) & 0xFFF;
    const u32 u = static_cast<u32>((ix << 4) + wx / 128) & 0xFFF;
    GRef<u64>(q + 0) = 0x4400000000000001ull;
    GRef<u64>(q + 8) = 0x53D3;
    GRef<u64>(q + 0x10) = (u + 0xE00) | (static_cast<u64>(v + 0xE00) << 16);
    GRef<u64>(q + 0x28) = 0x1000;
    GRef<u64>(q + 0x18) = 0x10000000;
    GRef<u64>(q + 0x20) = (u + 0x1200) | (static_cast<u64>(v + 0x1200) << 16);
    return q + 0x30;
}

// EE: 0x00115F28
// Cloud shadow plane (R+0xC0 bit 4): the block-26 prebuilt packet, the shadow texture quad,
// restores; bucket 3.
void func_00115F28(u32 cam, u32 list) {
    const u32 s = Ctx()->cur_buf.addr;
    const u32 tag = WriteDmaCallTag(s, GRef<u32>(GRef<u32>(0x004104C4) + 0x16FC));
    const u32 wind = GRef<u32>(0x004104D0) + 0x20;
    const u32 colour = func_0013EA08(GRef<u32>(0x00410694));
    const u32 p = func_00115CF8(tag + 0x10, cam, wind, colour);
    GRef<u64>(p + 8) = 0xE;
    GRef<u64>(p + 0) = 0x1000000000008006ull;
    GRef<u64>(p + 0x18) = 0x3F;
    Copy16(p + 0x20, g_pMainContext().addr + 0xD0);
    Copy16(p + 0x30, g_pMainContext().addr + 0xE0);
    Copy16(p + 0x40, g_pMainContext().addr + 0x110);
    Copy16(p + 0x50, cam + 0x1A0);
    Copy16(p + 0x60, g_pMainContext().addr + 0xF0);
    LinkPacket(list, 3, s, CloseDmaPacket(tag, p + 0x70));
}

// EE: 0x001145E8
// Sun / moon frame in W (docs/subsystems/sky.md): view-screen matrix around a point 147456 ahead,
// the sun direction in view space, the halo colours (one rand() when K+0 bit 0 is set and MC
// flag 0x100 is clear) and the flare screen offsets.
// fpucheck: 1 add (2 far), 1 add + 2 add (colours)
void func_001145E8(u32 w, u32 cam, u32 s) {
    const u32 m = w + 0x10, n = w + 0x50;
    sceVu0UnitMatrix(m);
    func_00102D28(m, m, cam + 0x10);
    StoreVec(w, {0.0f, 0.0f, 0.0f, 1.0f});
    GRef<f32>(w + 8) = -147456.0f;
    sceVu0ApplyMatrix(w, m, w);
    sceVu0TransMatrix(m, m, w);
    sceVu0InversMatrix(m, m);
    sceVu0UnitMatrix(n);
    const f32 farz = fpu::Add(GRef<f32>(cam + 0x170), GRef<f32>(cam + 0x170));
    sceVu0ViewScreenMatrix(n, 80.0f, 1.0f, 1.0f, 2048.0f, 2048.0f, GRef<f32>(cam + 0x190), GRef<f32>(cam + 0x194),
                           GRef<f32>(cam + 0x16C), farz);
    sceVu0MulMatrix(n, n, m);
    const u32 t = GRef<u32>(0x00410694);
    const f32 yaw = GRef<f32>(t + 0x8C), pitch = GRef<f32>(t + 0x88);
    sceVu0UnitMatrix(m);
    sceVu0RotMatrixX(m, m, pitch);
    sceVu0RotMatrixY(m, m, yaw);
    sceVu0MulMatrix(m, cam + 0x20, m);
    StoreVec(w + 0x90, {0.0f, 0.0f, 0.0f, 0.0f});
    GRef<f32>(w + 0x98) = 1.0f;
    sceVu0ApplyMatrix(w + 0x90, m, w + 0x90);
    GRef<u32>(w + 0xCC) = static_cast<u32>(GRef<u16>(s + 0x4C) >> 1) << 8;
    u64 c1 = SignedWord(s + 0x44), c0 = SignedWord(s + 0x48);
    if ((GRef<u32>(g_pMainContext().addr) & 0x100) == 0 && (GRef<u32>(GRef<u32>(0x004104D0)) & 1) != 0) {
        const s32 u = (rand() >> 8) & 0xFFF;
        if (u > 0x800) {
            const f32 f = fpu::IntToFloat(u) * GRef<f32>(0x0040DC64);
            c0 = LerpColor(f, c0, 0x00FFFFFF);
            c1 = LerpColor(f, c1, 0);
        }
    }
    const f32 sz = GRef<f32>(w + 0x98);
    GRef<u32>(w + 0xC0) =
        static_cast<u32>(LerpColor(fpu::Add(sz, GRef<f32>(0x0040DC68)) * GRef<f32>(0x0040DC6C), c1, c0));
    f32 a0 = fpu::Add(-GRef<f32>(w + 0x98), 0.5f) * GRef<f32>(0x0040DC70);
    if (a0 < 0.0f) a0 = 0.0f;
    GRef<u32>(w + 0xC4) = (static_cast<u32>(c0) & 0x00FFFFFF) | (static_cast<u32>(fpu::FloatToInt(a0)) << 24);
    f32 a1 = fpu::Add(GRef<f32>(w + 0x98), GRef<f32>(0x0040DC74)) * 70.0f;
    if (a1 < 0.0f) a1 = 0.0f;
    GRef<u32>(w + 0xC8) = (static_cast<u32>(c1) & 0x00FFFFFF) | (static_cast<u32>(fpu::FloatToInt(a1)) << 24);
    const s32 by = fpu::FloatToInt(GRef<f32>(w + 0x94) * 288.0f), bx = fpu::FloatToInt(GRef<f32>(w + 0x90) * 288.0f);
    const s32 dy = fpu::FloatToInt(GRef<f32>(w + 0x94) * 336.0f), dx = fpu::FloatToInt(GRef<f32>(w + 0x90) * 336.0f);
    auto pack = [](s32 lo, s32 hi) {
        return static_cast<u64>(static_cast<s64>(lo)) | (static_cast<u64>(static_cast<s64>(hi)) << 16);
    };
    GRef<u64>(w + 0xA0) = pack(bx + 0x7B56, by + 0x7B56);
    GRef<u64>(w + 0xB8) = pack(0x8000 - dx + 0x4AA, 0x8000 - dy + 0x4AA);
    GRef<u64>(w + 0xA8) = pack(bx + 0x8000 + 0x4AA, by + 0x8000 + 0x4AA);
    GRef<u64>(w + 0xB0) = pack(0x8000 - dx - 0x4AA, 0x8000 - dy - 0x4AA);
}

// EE: 0x00113790
// 32 cloud puff sprites (docs/subsystems/sky.md): projected centre, half size 9216 * size * w / w',
// colour blended from 0x70FFFFFF to 0x50808080 by the puff's height mix. Returns p + 32 * 0x40.
// fpucheck: 1 add per puff (blend factor)
u32 func_00113790(f32 size, u32 p, u32 m, u32 pts) {
    const vu0::Vec4 c0 = LoadVec(m), c1 = LoadVec(m + 0x10), c2 = LoadVec(m + 0x20), c3 = LoadVec(m + 0x30);
    const f32 k = size * 9216.0f;
    for (u32 i = 0; i < 32; ++i, pts += 8, p += 0x40) {
        const s32 x = GRef<s16>(pts), y = GRef<s16>(pts + 2), z = GRef<s16>(pts + 4), w = GRef<s16>(pts + 6);
        const vu0::Vec4 v10{vu0::IntToFloat(x), vu0::IntToFloat(y), vu0::IntToFloat(z), vu0::IntToFloat(w)};
        const vu0::Vec4 v8{v10.x, v10.y, v10.z, 1.0f};
        vu0::Vec4 v9 = vu0::MulMatrix(c0, c1, c2, c3, v8);
        const f32 q = vu0::Div(1.0f, v9.w);
        f32 h = vu0::Mul(k, v10.w);
        const s32 mix = (y >> 1) + (z >> 4) + w;
        const s32 b = fpu::FloatToInt(fpu::Add(fpu::IntToFloat(mix) * GRef<f32>(0x0040DC38), 0.5f) * 4096.0f);
        v9.x = vu0::Mul(v9.x, q);
        v9.y = vu0::Mul(v9.y, q);
        h = vu0::Mul(h, q);
        const s32 x0 = vu0::FloatToInt4(vu0::Sub(v9.x, vu0::Mul(1.0f, h)));
        const s32 y0 = vu0::FloatToInt4(vu0::Sub(v9.y, vu0::Mul(1.0f, h)));
        const s32 x1 = vu0::FloatToInt4(vu0::Add(v9.x, vu0::Mul(1.0f, h)));
        const s32 y1 = vu0::FloatToInt4(vu0::Add(v9.y, vu0::Mul(1.0f, h)));
        const u64 xy0 = Xy16(x0, y0), xy1 = Xy16(x1, y1);
        const u32 colour = LerpBytes(static_cast<s16>(b), 0x70FFFFFF, 0x50808080);
        GRef<u64>(p + 0x28) = xy0 | (xy0 << 32);
        GRef<u64>(p + 0x38) = xy1 | (xy1 << 32);
        GRef<u64>(p + 0x18) = colour;
        GRef<u64>(p + 0x00) = 0x6400000000008001ull;
        GRef<u64>(p + 0x08) = 0x53D310;
        GRef<u64>(p + 0x10) = 0x156;
        GRef<u64>(p + 0x20) = 0x00080008;
        GRef<u64>(p + 0x30) = 0x03F803F8;
    }
    return p;
}

// EE: 0x00114950
// Composite of a cloud layer's work page: 128 -> 64 reduction into page 0x148, alpha fill, the
// sun halo colour and the two flare sprites with alpha.
u32 func_00114950(u32 p, u32 w, u32 fbp) {
    GRef<u64>(p + 0x00) = 0x108B400000000003ull;
    GRef<u64>(p + 0x10) = 0x20148;
    GRef<u64>(p + 0x08) = 0xE;
    GRef<u64>(p + 0x18) = 0x4C;
    GRef<u64>(p + 0x20) = 0x003F0000003F0000ull;
    GRef<u64>(p + 0x28) = 0x40;
    GRef<u64>(p + 0x30) = 0;
    GRef<u64>(p + 0x38) = 0x18;
    p += 0x40;
    GRef<u64>(p + 0x00) = 0x6400000000000001ull;
    GRef<u64>(p + 0x08) = 0x535316;
    GRef<u64>(p + 0x10) = static_cast<u64>(fbp | 0x28008000u) | 0x680000000ull;
    GRef<u64>(p + 0x18) = 0x80000000ull;
    GRef<u64>(p + 0x20) = 0x00080008;
    GRef<u64>(p + 0x30) = 0x08080808;
    GRef<u64>(p + 0x38) = 0x04000400;
    GRef<u64>(p + 0x28) = 0;
    p += 0x40;
    GRef<u64>(p + 0x00) = 0x108B400000000001ull;
    GRef<u64>(p + 0x10) = 0xFF00000000020148ull;
    GRef<u64>(p + 0x08) = 0xE;
    GRef<u64>(p + 0x18) = 0x4C;
    p += 0x20;
    GRef<u64>(p + 0x00) = 0x4400000000000001ull;
    GRef<u64>(p + 0x08) = 0x5510;
    GRef<u64>(p + 0x10) = 6;
    GRef<u64>(p + 0x18) = 0x00808080;
    GRef<u64>(p + 0x28) = 0x04000400;
    GRef<u64>(p + 0x20) = 0;
    p += 0x30;
    GRef<u64>(p + 0x00) = 0x1000000000000004ull;
    GRef<u64>(p + 0x10) = static_cast<u64>((fbp >> 5) | 0x20000u) | 0xFF00000000000000ull;
    GRef<u64>(p + 0x18) = 0x4C;
    GRef<u64>(p + 0x20) = 0x00007C0000007C00ull;
    GRef<u64>(p + 0x28) = 0x18;
    GRef<u64>(p + 0x08) = 0xE;
    GRef<u64>(p + 0x30) = 0x007F0000007F0000ull;
    GRef<u64>(p + 0x38) = 0x40;
    GRef<u64>(p + 0x48) = 0x3F;  // TEXFLUSH (data unwritten)
    p += 0x50;
    GRef<u64>(p + 0x00) = 0x4400000000000001ull;
    GRef<u64>(p + 0x08) = 0x5510;
    GRef<u64>(p + 0x10) = 6;
    GRef<u64>(p + 0x28) = 0x84008400;
    GRef<u64>(p + 0x18) = GRef<u32>(w + 0xC0);
    GRef<u64>(p + 0x20) = 0x7C007C00;
    p += 0x30;
    GRef<u64>(p + 0x00) = 0x10AB400000000001ull;
    GRef<u64>(p + 0x08) = 0xE;
    GRef<u64>(p + 0x10) = 0x44;
    GRef<u64>(p + 0x18) = 0x42;
    p += 0x20;
    for (u32 i = 1; i < 3; ++i) {
        const u32 c = GRef<u32>(w + 0xC0 + 4 * i);
        if ((c >> 24) == 0) continue;
        GRef<u64>(p + 0x00) = 0x6400000000000001ull;
        GRef<u64>(p + 0x08) = 0x53D316;
        GRef<u64>(p + 0x18) = c;
        GRef<u64>(p + 0x10) = 0x6A800A900ull;
        GRef<u64>(p + 0x20) = 0x00080008;
        GRef<u64>(p + 0x30) = 0x04080408;
        GRef<u64>(p + 0x28) = GRef<u64>(w + 0xA0 + 0x10 * (i - 1));
        GRef<u64>(p + 0x38) = GRef<u64>(w + 0xA8 + 0x10 * (i - 1));
        p += 0x40;
    }
    return p;
}

// EE: 0x00114C60
// One cloud layer (docs/subsystems/sky.md): the 32 puffs rendered into the 128 x 128 work page
// fbp, copied back and composited; returns p + 0x80 after the restore block.
u32 func_00114C60(u32 p, u32 w, u32 fbp, u32 pts, u32 s, u32 cam) {
    p = func_00113988(p, fbp, 0x80, 0x80);
    GRef<u64>(p + 0x00) = 0x1000000000000004ull;
    GRef<u64>(p + 0x10) = 0x44;
    GRef<u64>(p + 0x18) = 0x42;
    GRef<u64>(p + 0x20) = 0x598006800ull;
    GRef<u64>(p + 0x28) = 6;
    GRef<u64>(p + 0x30) = 0x60;
    GRef<u64>(p + 0x38) = 0x14;
    GRef<u64>(p + 0x48) = 8;
    GRef<u64>(p + 0x08) = 0xE;
    GRef<u64>(p + 0x40) = 0;
    const u32 m = w + 0x10;
    sceVu0UnitMatrix(m);
    GRef<f32>(w + 0x10) = GRef<f32>(s + 0);
    GRef<f32>(w + 0x24) = GRef<f32>(s + 4);
    GRef<f32>(w + 0x38) = GRef<f32>(s + 8);
    sceVu0MulMatrix(m, w + 0x50, m);
    p = func_00113790(GRef<f32>(s + 0xC), p + 0x50, m, pts);
    GRef<u64>(p + 0x00) = 0x108B400000000001ull;
    GRef<u64>(p + 0x18) = 0x18;
    GRef<u64>(p + 0x20) = 0x2400000000000001ull;
    GRef<u64>(p + 0x28) = 0x16;
    GRef<u64>(p + 0x30) = static_cast<u64>(fbp | 0x28008000u) | 0x280000000ull;
    GRef<u64>(p + 0x08) = 0xE;
    GRef<u64>(p + 0x10) = 0;
    GRef<u64>(p + 0x38) = GRef<u32>(w + 0xCC);
    p = BuildSpriteStripsUv(p + 0x40, 0, 0x08000800, 0x00080008, 0x08080808);
    p = func_00113B50(p, fbp, fbp, 0x80, 0x80);
    p = func_00114950(p, w, fbp);
    GRef<u64>(p + 0x00) = 0x1000000000008007ull;
    GRef<u64>(p + 0x08) = 0xE;
    GRef<u64>(p + 0x18) = 0x3F;
    Copy16(p + 0x20, g_pMainContext().addr + 0xD0);
    Copy16(p + 0x30, cam + 0x1A0);
    Copy16(p + 0x40, g_pMainContext().addr + 0xF0);
    GRef<u64>(p + 0x60) = 0x30000;
    GRef<u64>(p + 0x58) = 0x3B;
    GRef<u64>(p + 0x68) = 0x47;
    GRef<u64>(p + 0x70) = 0x1310000A8ull;
    GRef<u64>(p + 0x78) = 0x4E;
    GRef<u64>(p + 0x50) = 0;
    return p + 0x80;
}

// EE: 0x00114ED8
// Cloud sprite with clipped texture coordinates (docs/subsystems/sky.md); the caller loads vf1
// (camera position), vf2 (half-size multipliers), vf3 / vf4 (screen rectangle), vf5-vf8 (matrix).
// Returns the view depth v.w, or -1.0 when behind the camera or off screen.
// MAC read after `vmulq.xyz vf10` (no GameDB patch): PCSX2 sees that op's z flags (v.z * Q <= 0).
// fpucheck: per clip step 3 sub + 1 add
f32 func_00114ED8(u32 p, u32 rec, u32 rect, u32 zmin, f32 size) {
    SkyVu0& r = g_SkyVu0;
    const vu0::Vec4 v9 = LoadVec(p);
    const f32 dx = vu0::Sub(v9.x, r.vf1.x), dy = vu0::Sub(v9.y, r.vf1.y), dz = vu0::Sub(v9.z, r.vf1.z);
    f32 d2 = vu0::Mul(dx, dx);
    d2 = vu0::Add(d2, vu0::Mul(dy, dy));
    d2 = vu0::Add(d2, vu0::Mul(dz, dz));
    GRef<f32>(rec) = vu0::Sqrt(d2);
    vu0::Vec4 v10 = vu0::MulMatrix(r.vf5, r.vf6, r.vf7, r.vf8, v9);
    const f32 q = vu0::Div(1.0f, v10.w);
    f32 xmin = GRef<f32>(rect + 0), ymin = GRef<f32>(rect + 4);
    v10.x = vu0::Mul(v10.x, q);
    v10.y = vu0::Mul(v10.y, q);
    v10.z = vu0::Mul(v10.z, q);
    if (v10.z <= 0.0f) return -1.0f;
    const f32 h = vu0::Mul(size, q);
    const vu0::Vec4 v13{vu0::Mul(r.vf2.x, h), vu0::Mul(r.vf2.y, h), vu0::Mul(r.vf2.z, h), vu0::Mul(r.vf2.w, h)};
    vu0::Vec4 v14{vu0::Sub(v10.x, v13.x), vu0::Sub(v10.y, v13.y), vu0::Sub(v10.z, v13.z), vu0::Sub(v10.w, v13.w)};
    vu0::Vec4 v15{vu0::Add(v10.x, v13.x), vu0::Add(v10.y, v13.y), vu0::Add(v10.z, v13.z), vu0::Add(v10.w, v13.w)};
    if (vu0::Sub(v15.x, r.vf3.x) <= 0.0f || vu0::Sub(v15.y, r.vf3.y) <= 0.0f || vu0::Sub(r.vf4.x, v14.x) <= 0.0f ||
        vu0::Sub(r.vf4.y, v14.y) <= 0.0f) {
        r.vf14 = v14;
        r.vf15 = v15;
        return -1.0f;
    }
    f32 c0x = v14.x, c0y = vu0::Add(0.0f, v14.y), c1x = v15.x, c1y = vu0::Add(0.0f, v15.y);
    auto u16f = [&](u32 off) { return fpu::IntToFloat(GRef<u16>(rec + off)); };
    if (c0x < xmin) {
        const f32 a = fpu::Sub(c0x, xmin), b = fpu::Sub(xmin, c1x), den = fpu::Sub(c0x, c1x);
        const f32 s = fpu::Add(b * u16f(8), a * u16f(0xC));
        c0x = xmin;
        GRef<u16>(rec + 8) = static_cast<u16>(fpu::FloatToInt(fpu::Div(s, den)));
        v14.x = vu0::Add(r.vf3.x, 0.0f);
    }
    if (c0y < ymin) {
        const f32 a = fpu::Sub(c0y, ymin), b = fpu::Sub(ymin, c1y), den = fpu::Sub(c0y, c1y);
        const f32 s = fpu::Add(b * u16f(0xA), a * u16f(0xE));
        c0y = ymin;
        GRef<u16>(rec + 0xA) = static_cast<u16>(fpu::FloatToInt(fpu::Div(s, den)));
        v14.y = vu0::Add(r.vf3.y, 0.0f);
    }
    const f32 xmax = GRef<f32>(rect + 8), ymax = GRef<f32>(rect + 0xC);
    if (xmax < c1x) {
        const f32 a = fpu::Sub(c0x, xmax), b = fpu::Sub(xmax, c1x), den = fpu::Sub(c0x, c1x);
        const f32 s = fpu::Add(b * u16f(8), a * u16f(0xC));
        GRef<u16>(rec + 0xC) = static_cast<u16>(fpu::FloatToInt(fpu::Div(s, den)));
        v15.x = vu0::Add(r.vf4.x, 0.0f);
    }
    if (ymax < c1y) {
        const f32 a = fpu::Sub(c0y, ymax), b = fpu::Sub(ymax, c1y), den = fpu::Sub(c0y, c1y);
        const f32 s = fpu::Add(b * u16f(0xA), a * u16f(0xE));
        GRef<u16>(rec + 0xE) = static_cast<u16>(fpu::FloatToInt(fpu::Div(s, den)));
        v15.y = vu0::Add(r.vf4.y, 0.0f);
    }
    r.vf14 = v14;
    r.vf15 = v15;
    const s32 x0 = vu0::FloatToInt4(v14.x), y0 = vu0::FloatToInt4(v14.y);
    const s32 x1 = vu0::FloatToInt4(v15.x), y1 = vu0::FloatToInt4(v15.y), z1 = vu0::FloatToInt4(v15.z);
    GRef<u32>(rec + 0x10) = static_cast<u32>(Xy16(x0, y0));
    u32 z = static_cast<u32>(z1) >> 3;
    if (z < zmin)
        z = zmin;
    else if (0x00FFFFFFu < z)
        z = 0x00FFFFFF;
    GRef<u64>(rec + 0x18) = Xy16(x1, y1) | (static_cast<u64>(z) << 32);
    return vu0::Add(0.0f, v10.w);
}

// EE: 0x001152D8
// Volumetric cloud field (docs/subsystems/sky.md): up to 400 sprites placed around the camera,
// faded by distance and cloud density, comb-sorted far to near, each a sprite strip textured from
// its layer's work page. Returns the write pointer (p when nothing is kept).
// HYPOTHESIS: vf2.zw (from the upper half of the GPR the 64-bit `ld` leaves unchanged) are 0, so the
// sprite depth is the projected centre's (docs/port/pcsx2-pending.md).
// fpucheck: frame 4 sub/add, unsigned convert, 4 add; per sprite 1 sub (y), fade 1-2 sub, 6 sub/add
u32 func_001152D8(u32 p, u32 cam, u32 s, u32 c, s32 layers) {
    s32 n = GRef<u16>(s + 0x50);
    if (n <= 0) return p;
    if (n > 400) n = 400;
    const u32 w = kSkyWork;
    StoreVec(w + 0xA0, {0.0f, 0.0f, 0.0f, 1.0f});
    SetUnitMatrixVu0(w + 0x50);
    func_00102D28(w + 0x10, w + 0x50, cam + 0x10);
    GRef<f32>(w + 0xA8) = -73728.0f;
    sceVu0ApplyMatrix(w + 0xA0, w + 0x10, w + 0xA0);
    sceVu0TransMatrix(w + 0x10, w + 0x10, w + 0xA0);
    sceVu0InversMatrix(w + 0x10, w + 0x10);
    MulMatrixVu0(w + 0x10, cam + 0x60, w + 0x10);
    Copy16(w + 0x90, w + 0xA0);
    GRef<f32>(w + 0x90) = fpu::Sub(GRef<f32>(w + 0x90), GRef<f32>(cam + 0));
    const u32 ksky = GRef<u32>(0x004104D0);
    const f32 lift = fpu::Sub(-GRef<f32>(cam + 4), GRef<f32>(c + 4));
    GRef<f32>(w + 0x94) = fpu::Add(GRef<f32>(w + 0x94), lift);
    GRef<f32>(w + 0x98) = fpu::Sub(GRef<f32>(w + 0x98), GRef<f32>(cam + 8));
    GRef<u32>(w + 0x32F0) = GRef<u32>(ksky + 0x20);
    GRef<u32>(w + 0x32F4) = GRef<u32>(ksky + 0x28);
    const f32 drift = fpu::UnsignedToFloat(GRef<u32>(GRef<u32>(0x004104C4) + 0x1660)) * GRef<f32>(0x0040DC7C);
    const u32 rs = GRef<u32>(0x004104D4);
    GRef<f32>(w + 0x90) = fpu::Add(GRef<f32>(w + 0x90), GRef<f32>(c + 0x10) * drift);
    const f32 amin = fpu::Add(fpu::IntToFloat(GRef<u16>(rs + 0x12C)) * GRef<f32>(0x0040DC80), 12.0f);
    GRef<f32>(w + 0x98) = fpu::Add(GRef<f32>(w + 0x98), GRef<f32>(c + 0x14) * drift);
    f32 farz = GRef<f32>(rs + 0xC);
    const u64 zmin = static_cast<u64>(GRef<u32>(ksky + 8)) + 0x10;
    if (139264.0f < farz) farz = 139264.0f;
    const f32 half = farz * 0.5f;
    const f32 kfar = fpu::Div(256.0f, farz);
    StoreVec(w, {0.0f, 0.0f, 0.0f, 1.0f});
    const f32 sc = GRef<f32>(cam + 0x168) * GRef<f32>(0x0040DC84);
    GRef<f32>(w + 0xB0) = GRef<f32>(cam + 0x180) * sc;
    const f32 hx = fpu::IntToFloat(GRef<s16>(cam + 0x18C) / 2), hy = fpu::IntToFloat(GRef<s16>(cam + 0x18E) / 2);
    GRef<f32>(w + 0xB4) = GRef<f32>(cam + 0x184) * sc;
    GRef<f32>(w + 0xC8) = fpu::Add(GRef<f32>(cam + 0x178), hx);
    GRef<f32>(w + 0xCC) = fpu::Add(GRef<f32>(cam + 0x17C), hy);
    GRef<f32>(w + 0xC0) = fpu::Sub(GRef<f32>(cam + 0x178), hx);
    GRef<f32>(w + 0xC4) = fpu::Sub(GRef<f32>(cam + 0x17C), hy);
    SkyVu0& r = g_SkyVu0;
    r.vf1 = LoadVec(w + 0xA0);
    r.vf2 = {GRef<f32>(w + 0xB0), GRef<f32>(w + 0xB4), 0.0f, 0.0f};
    r.vf3 = {GRef<f32>(w + 0xC0), GRef<f32>(w + 0xC4), 0.0f, 0.0f};
    r.vf4 = {GRef<f32>(w + 0xC8), GRef<f32>(w + 0xCC), 0.0f, 0.0f};
    r.vf5 = LoadVec(w + 0x10);
    r.vf6 = LoadVec(w + 0x20);
    r.vf7 = LoadVec(w + 0x30);
    r.vf8 = LoadVec(w + 0x40);
    const f32 ysp = GRef<f32>(s + 0x58), ssc = GRef<f32>(s + 0x54);
    u32 rec = w + 0xD0;
    s32 kept = 0;
    u32 e = GRef<u32>(0x004104E8);
    for (s32 i = 0; i < n; ++i, e += 8) {
        const f32 fy = fpu::IntToFloat(static_cast<s32>(GRef<u16>(e + 2) & 0xFFF) - 0x800);
        const s32 px = ((fpu::FloatToInt(GRef<f32>(w + 0x90)) + GRef<s16>(e) * 2) & 0x1FFFF) - 0x10000;
        const s32 pz0 = fpu::FloatToInt(GRef<f32>(w + 0x98));
        GRef<f32>(w + 0) = fpu::IntToFloat(px);
        GRef<f32>(w + 4) = fpu::Sub(GRef<f32>(w + 0x94), ysp * fy);
        GRef<f32>(w + 8) = fpu::IntToFloat(((pz0 + GRef<s16>(e + 4) * 2) & 0x1FFFF) - 0x10000);
        GRef<u64>(rec + 8) = 0x07F807F800080008ull;
        const f32 d = func_00114ED8(w, rec, w + 0xC0, static_cast<u32>(zmin),
                                    (fpu::IntToFloat(GRef<s16>(e + 6)) * 2304.0f) * ssc);
        if (d <= 2048.0f || farz < d) continue;
        f32 a = 128.0f;
        if (d < 12288.0f) {
            a = fpu::Sub(d, 2048.0f) * GRef<f32>(0x0040DC88);
            if (a < amin) continue;
        }
        if (half < d) {
            a = fpu::Sub(128.0f, fpu::Sub(d, half) * kfar);
            if (a < amin) continue;
        }
        f32 qx = fpu::Sub(GRef<f32>(cam + 0), GRef<f32>(w + 0xA0));
        f32 qz = fpu::Sub(GRef<f32>(cam + 8), GRef<f32>(w + 0xA8));
        qx = fpu::Add(qx, GRef<f32>(w + 0));
        qz = fpu::Add(qz, GRef<f32>(w + 8));
        qx = fpu::Add(qx, GRef<f32>(w + 0x32F0));
        qz = fpu::Add(qz, GRef<f32>(w + 0x32F4));
        a = a * func_00115198(qx, qz);
        if (a < amin) continue;
        ++kept;
        GRef<u32>(rec + 0x14) = ((static_cast<u32>(i) & static_cast<u32>(layers)) << 8) + 0x2400;
        GRef<s32>(rec + 4) = static_cast<s32>((static_cast<u32>(fpu::FloatToInt(a)) << 24) | 0x808080);
        rec += 0x20;
    }
    if (kept <= 0) return p;
    // Comb sort, farthest first.
    const u32 base = w + 0xD0, tmp = w + 0x32D0;
    s32 gap = kept;
    for (;;) {
        gap = fpu::FloatToInt(fpu::IntToFloat(gap) * GRef<f32>(0x0040DC8C));
        if (gap == 0)
            gap = 1;
        else if (static_cast<u32>(gap - 9) <= 1)
            gap = 11;
        bool swapped = false;
        for (s32 j = 0; j < kept - gap; ++j) {
            const u32 ra = base + 0x20 * static_cast<u32>(j), rb = ra + 0x20 * static_cast<u32>(gap);
            if (!(GRef<f32>(ra) < GRef<f32>(rb))) continue;
            swapped = true;
            for (u32 o = 0; o < 0x20; o += 8) GRef<u64>(tmp + o) = GRef<u64>(ra + o);
            for (u32 o = 0; o < 0x20; o += 8) GRef<u64>(ra + o) = GRef<u64>(rb + o);
            for (u32 o = 0; o < 0x20; o += 8) GRef<u64>(rb + o) = GRef<u64>(tmp + o);
        }
        if (!swapped && gap < 2) break;
    }
    GRef<u64>(p + 0x48) = 0x4E;
    GRef<u64>(p + 0x00) = 0x10AB400000000004ull;
    GRef<u64>(p + 0x08) = 0xE;
    GRef<u64>(p + 0x10) = 0x44;
    GRef<u64>(p + 0x18) = 0x42;
    GRef<u64>(p + 0x20) = 0x60;
    GRef<u64>(p + 0x28) = 0x14;
    GRef<u64>(p + 0x30) = 0x7000D;
    GRef<u64>(p + 0x38) = 0x47;
    GRef<u64>(p + 0x40) = 0x1310000A8ull;
    p += 0x50;
    for (s32 k = 0; k < kept; ++k) {
        const u32 rr = base + 0x20 * static_cast<u32>(k);
        GRef<u64>(p + 0x00) = 0x2400000000008001ull;
        GRef<u64>(p + 0x08) = 0x16;
        GRef<u64>(p + 0x10) = static_cast<u64>(GRef<u32>(rr + 0x14) | 0x28008000u) | 0x680000000ull;
        GRef<u64>(p + 0x18) = GRef<u32>(rr + 4);
        p = BuildSpriteStripsUv(p + 0x20, GRef<u32>(rr + 0x10), GRef<u64>(rr + 0x18), GRef<u32>(rr + 8),
                                GRef<u32>(rr + 0xC));
    }
    return p;
}

// EE: 0x00113D70
// Animated cloud texture (docs/subsystems/sky.md): two scrolling layers rendered into the 64 x 64
// work page 0x2800 and buffer 0x140. The corner dwords take their z / w halves from VU0 vf15,
// which this function does not write (the GS ignores them: ZTST always, Z masked).
// fpucheck: 2 add (t), unsigned convert, layer 0 1 add + wrap, layer 1 1 sub + wrap
u32 func_00113D70(u32 p, u32 cam) {
    const u32 w = kSkyWork;
    GRef<u64>(w + 0xD0) = 0x2007E80599B05F40ull;
    GRef<u64>(w + 0xE0) = 0x2007E80599B05F40ull;
    const f32 a2 = fpu::Add(GRef<f32>(cam + 0x10), GRef<f32>(cam + 0x10));
    const f32 t = fpu::Add(ee::cosf(a2), 1.0f) * 0.5f;
    GRef<u64>(w + 0xD8) = static_cast<u32>(LerpColor(t, 0x00B0B0B0ull, 0xFFFFFFFF80F0F0F0ull));
    GRef<u64>(w + 0xE8) = static_cast<u32>(LerpColor(t, 0xFFFFFFFF80F0F0F0ull, 0x00B0B0B0ull));
    u32 m = (GRef<u64>(w + 0xD8) >> 24) != 0 ? 1u : 0u;
    if ((GRef<u64>(w + 0xE8) >> 24) != 0) m |= 2;
    p = func_00113988(p, 0x2800, 0x40, 0x40);
    GRef<u64>(p + 8) = 0x98;
    GRef<u64>(p + 0) = 0x2400000000008001ull;
    GRef<u64>(p + 0x10) = 0;
    GRef<u64>(p + 0x18) = 0;
    p += 0x20;
    if (m == 0) return p;
    static const u64 kSetup[] = {0x1000000000008008ull, 0xE, 0x44, 0x42, 0x10140, 0x4D, 0x1310000A8ull, 0x4F,
                                 0x30000, 0x48, 0x00007E0000007E00ull, 0x19, 0x003F0000003F0000ull, 0x41, 0x60, 0x15,
                                 0x42, 0x43};
    for (u32 k = 0; k < 18; ++k) GRef<u64>(p + 8 * k) = kSetup[k];
    p += 0x90;
    const u32 frame = GRef<u32>(GRef<u32>(0x004104C4) + 0x1660);
    const f32 f = fpu::UnsignedToFloat(frame & 0x1FFF) * GRef<f32>(0x0040DC3C);
    if ((m & 1) != 0) {
        const f32 a = fpu::Add(-GRef<f32>(cam + 0x18), f);
        f32 ang = a;
        if (GRef<f32>(0x0040DC40) < a)
            ang = fpu::Sub(a, GRef<f32>(0x0040DC44));
        else if (a < GRef<f32>(0x0040DC48))
            ang = fpu::Add(a, GRef<f32>(0x0040DC4C));
        sceVu0UnitMatrix(w + 0x50);
        sceVu0RotMatrixZ(w + 0x50, w + 0x50, ang);
    }
    if ((m & 2) != 0) {
        sceVu0UnitMatrix(w + 0x10);
        func_00102D28(w + 0x10, w + 0x10, cam + 0x10);
        StoreVec(w, {0.0f, 0.0f, 0.0f, 1.0f});
        GRef<f32>(w + 4) = -1.0f;
        sceVu0ApplyMatrix(w, w + 0x10, w);
        f32 y = GRef<f32>(w + 8);
        if (GRef<f32>(cam + 0x10) < 0.0f) y = -y;
        const f32 a = fpu::Sub(ee::atan2f(y, -GRef<f32>(w + 0)), f);
        f32 ang = a;
        if (GRef<f32>(0x0040DC50) < a)
            ang = fpu::Sub(a, GRef<f32>(0x0040DC54));
        else if (a < GRef<f32>(0x0040DC58))
            ang = fpu::Add(a, GRef<f32>(0x0040DC5C));
        sceVu0UnitMatrix(w + 0x10);
        sceVu0RotMatrixZ(w + 0x10, w + 0x10, ang);
    }
    const vu0::Vec4 l0a = LoadVec(w + 0x50), l0b = LoadVec(w + 0x60);
    const vu0::Vec4 l1a = LoadVec(w + 0x10), l1b = LoadVec(w + 0x20);
    const s32 zw = vu0::FloatToInt4(g_SkyVu0.vf15.z), ww = vu0::FloatToInt4(g_SkyVu0.vf15.w);
    const u64 zwHalves = (static_cast<u64>(static_cast<u32>(zw) & 0xFFFF) << 32) |
                         (static_cast<u64>(static_cast<u32>(ww) & 0xFFFF) << 48);
    for (u32 c = 0; c < 4; ++c) {
        const f32 x = GRef<f32>(0x002A1B28 + 8 * c), y = GRef<f32>(0x002A1B2C + 8 * c);
        auto corner = [&](const vu0::Vec4& ra, const vu0::Vec4& rb) {
            const f32 vx = vu0::Add(vu0::MulAdd(vu0::Mul(ra.x, x), rb.x, y), 2048.0f);
            const f32 vy = vu0::Add(vu0::MulAdd(vu0::Mul(ra.y, x), rb.y, y), 2048.0f);
            g_SkyVu0.vf15.x = vx;
            g_SkyVu0.vf15.y = vy;
            return Xy16(vu0::FloatToInt4(vx), vu0::FloatToInt4(vy)) | zwHalves;
        };
        if ((m & 1) != 0) GRef<u64>(w + 0xF0 + 8 * c) = corner(l0a, l0b);
        if ((m & 2) != 0) GRef<u64>(w + 0x110 + 8 * c) = corner(l1a, l1b);
    }
    const u32 n = frame;
    const s32 u1 = static_cast<s32>(((n << 1) & 0x7FF) + 0x1000), u2 = static_cast<s32>(((n << 2) & 0x7FF) + 0x1000);
    const s32 k9 = static_cast<s32>(n & 0x1FF);
    const s32 d1 = fpu::FloatToInt(ee::sinf(fpu::IntToFloat(k9) * GRef<f32>(0x0040DC60)) * 240.0f) + 0x200;
    const s32 d2 = fpu::FloatToInt(ee::sinf(fpu::IntToFloat(k9 + 0x90) * GRef<f32>(0x0040DC60)) * 240.0f) + 0x200;
    auto uv = [](s32 u, s32 v) {
        return static_cast<u64>(static_cast<s64>(u)) | (static_cast<u64>(static_cast<s64>(v)) << 16);
    };
    const u64 uvA = uv(u1 - d1, u2 - d2), uvB = uv(u1 + d1, u2 - d2), uvC = uv(u1 - d1, u2 + d2),
              uvD = uv(u1 + d1, u2 + d2), uvE = uv(u1 - 2 * d2, u2 - 2 * d1), uvF = uv(u1 + 2 * d2, u2 - 2 * d1),
              uvG = uv(u1 - 2 * d2, u2 + 2 * d1), uvH = uv(u1 + 2 * d2, u2 + 2 * d1);
    for (u32 layer = 0; layer < 2; ++layer) {
        if ((m & (1u << layer)) == 0) continue;
        GRef<u64>(p + 0x00) = 0xC400000000000001ull;
        GRef<u64>(p + 0x08) = 0x5353D3D316F0ull;
        GRef<u64>(p + 0x10) = 0x154;
        Copy16(p + 0x20, w + 0xD0 + 0x10 * layer);
        GRef<u64>(p + 0x60) = 0x03F803F8;
        GRef<u64>(p + 0x40) = 0x000803F8;
        GRef<u64>(p + 0x30) = 0x00080008;
        GRef<u64>(p + 0x50) = 0x03F80008;
        GRef<u64>(p + 0x78) = 0x170;
        GRef<u64>(p + 0x70) = 0x3400000000000001ull;
        GRef<u64>(p + 0x80) = 0x354;
        GRef<u64>(p + 0x88) = 0x0007E80599B05F80ull;
        GRef<u64>(p + 0x90) = (GRef<u64>(w + 0xD8 + 0x10 * layer) & 0xFFFFFFFFFF000000ull) | 0x303030;
        GRef<u64>(p + 0xA8) = 0x5353D3D3;
        GRef<u64>(p + 0xB0) = uvA;
        GRef<u64>(p + 0xA0) = 0x8400000000008002ull;
        GRef<u64>(p + 0xF0) = uvE;
        GRef<u64>(p + 0xC0) = uvB;
        GRef<u64>(p + 0xD0) = uvC;
        GRef<u64>(p + 0xE0) = uvD;
        GRef<u64>(p + 0x100) = uvF;
        GRef<u64>(p + 0x110) = uvG;
        GRef<u64>(p + 0x120) = uvH;
        for (u32 c = 0; c < 4; ++c) {
            const u64 cd = GRef<u64>(w + 0xF0 + 0x20 * layer + 8 * c);
            GRef<u64>(p + 0xF8 + 0x10 * c) = cd;
            GRef<u64>(p + 0xB8 + 0x10 * c) = cd;
            GRef<u64>(p + 0x38 + 0x10 * c) = cd;
        }
        p += 0x130;
    }
    GRef<u64>(p + 0x00) = 0x108B400000000002ull;
    GRef<u64>(p + 0x28) = 0x18;
    GRef<u64>(p + 0x08) = 0xE;
    GRef<u64>(p + 0x18) = 0x3F;
    GRef<u64>(p + 0x20) = 0;
    p += 0x30;
    GRef<u64>(p + 0x00) = 0x6400000000000001ull;
    GRef<u64>(p + 0x08) = 0x535316;
    GRef<u64>(p + 0x10) = 0x2A8006800ull;
    GRef<u64>(p + 0x20) = 0x00080008;
    GRef<u64>(p + 0x18) = 0x4000;
    GRef<u64>(p + 0x30) = 0x04080408;
    GRef<u64>(p + 0x38) = 0x04000400;
    GRef<u64>(p + 0x28) = 0;
    p = func_00113B50(p + 0x40, 0x2800, 0x2800, 0x40, 0x40);
    GRef<u64>(p + 0x00) = 0x1000000000008003ull;
    GRef<u64>(p + 0x10) = 0xFF00000000010140ull;
    GRef<u64>(p + 0x08) = 0xE;
    GRef<u64>(p + 0x18) = 0x4C;
    GRef<u64>(p + 0x20) = 0x003F0000003F0000ull;
    GRef<u64>(p + 0x28) = 0x40;
    GRef<u64>(p + 0x38) = 0x3F;
    p += 0x40;
    GRef<u64>(p + 0x00) = 0x4400000000008001ull;
    GRef<u64>(p + 0x08) = 0x5510;
    GRef<u64>(p + 0x10) = 6;
    GRef<u64>(p + 0x28) = 0x04000400;
    GRef<u64>(p + 0x18) = 0x00FFFFFF;
    GRef<u64>(p + 0x20) = 0;
    return p + 0x30;
}

// EE: 0x00115B10
// Cloud pass (R+0xC0 bit 2): view scissor and fog colour, the animated texture, the sun frame, the
// cloud layers and the volumetric field; bucket 0xB (the DMA tag gets a VIF FLUSH).
void func_00115B10(u32 cam, u32 list) {
    const u32 s = Ctx()->cur_buf.addr;
    GRef<u64>(s + 0x10) = 0x1000000000008002ull;
    GRef<u64>(s + 0x28) = 8;
    GRef<u64>(s + 0x18) = 0xE;
    GRef<u64>(s + 0x20) = 0;
    Copy16(s + 0x30, cam + 0x1A0);
    GRef<u64>(s + 0x40) = 0x1000000000008001ull;
    GRef<u64>(s + 0x48) = 0xE;
    const u32 rs = GRef<u32>(0x004104D4);
    GRef<u64>(s + 0x58) = 0x3D;
    GRef<u64>(s + 0x50) = GRef<u32>(GRef<u32>(0x004104D0) + 4);
    u32 p = func_00113D70(s + 0x60, cam);
    func_001145E8(kSkyWork, cam, rs + 0xE0);
    const s32 layers = (GRef<u32>(rs + 0xC0) & 0x100) != 0 ? 1 : 3;
    u32 pts = GRef<u32>(0x004104E8) + 0xC80;
    for (s32 k = 0; k <= layers; ++k) {
        p = func_00114C60(p, kSkyWork, 0x2400 + 0x100 * static_cast<u32>(k), pts, rs + 0xE0 + 0x10 * static_cast<u32>(k),
                          cam);
        pts += 0x100;
    }
    p = func_001152D8(p, cam, rs + 0xE0, rs + 0xC0, layers);
    GRef<u64>(p + 0) = 0x1000000000008002ull;
    GRef<u64>(p + 8) = 0xE;
    Copy16(p + 0x10, g_pMainContext().addr + 0xE0);
    Copy16(p + 0x20, g_pMainContext().addr + 0x110);
    const u32 end = CloseDmaPacket(s, p + 0x30);
    GRef<u32>(s + 8) = 0x11000000;
    LinkPacket(list, 0xB, s, end);
}

// ---- Moon, sun, flare and halo (docs/subsystems/sky.md "Sun and moon") ----------------------

namespace {

// vftoi0: truncation toward zero, saturated to the 32-bit range.
s32 Ftoi0(f32 v) {
    const double d = static_cast<double>(v);
    if (d >= 2147483647.0) return INT32_MAX;
    if (d <= -2147483648.0) return INT32_MIN;
    return static_cast<s32>(d);
}

u64 Sx(s32 v) { return static_cast<u64>(static_cast<s64>(v)); }

s32 Wrap(s64 v) { return static_cast<s32>(static_cast<u32>(static_cast<u64>(v))); }

// pcgtw / pextlw / ppach: four 16-bit lanes (lo.x > x, x > hi.x, lo.y > y, y > hi.y), each 0xFFFF
// when true; lo/hi hold (x, y) as two words.
u64 RectMask(s32 x, s32 y, u64 lo, u64 hi) {
    const s32 lox = static_cast<s32>(static_cast<u32>(lo)), loy = static_cast<s32>(static_cast<u32>(lo >> 32));
    const s32 hix = static_cast<s32>(static_cast<u32>(hi)), hiy = static_cast<s32>(static_cast<u32>(hi >> 32));
    u64 m = 0;
    if (lox > x) m |= 0x000000000000FFFFull;
    if (x > hix) m |= 0x00000000FFFF0000ull;
    if (loy > y) m |= 0x0000FFFF00000000ull;
    if (y > hiy) m |= 0xFFFF000000000000ull;
    return m;
}

bool OffScreen(s32 x, s32 y) { return ((static_cast<u32>(x) >> 16) | (static_cast<u32>(y) >> 16)) != 0; }

}  // namespace

// EE: 0x00116A68
// Billboard frame: unit, RotZ(roll), RotX(pitch), RotY(yaw); out = cam+0x20 x out, translation
// row reset to (0, 0, 0, 1), out = cam+0x60 x out.
void func_00116A68(u32 out, u32 cam, f32 pitch, f32 yaw, f32 roll) {
    SetUnitMatrixVu0(out);
    sceVu0RotMatrixZ(out, out, roll);
    sceVu0RotMatrixX(out, out, pitch);
    sceVu0RotMatrixY(out, out, yaw);
    MulMatrixVu0(out, cam + 0x20, out);
    StoreVec(out + 0x30, {0.0f, 0.0f, 0.0f, 1.0f});
    MulMatrixVu0(out, cam + 0x60, out);
}

// EE: 0x00116B10
// out = in x Scale(sx, sy, 1).
void func_00116B10(u32 out, u32 in, f32 sx, f32 sy) {
    BuildScaleMatrix(sx, sy, 1.0f, out);
    MulMatrixVu0(out, in, out);
}

// EE: 0x00116B58
// View rectangle in 1/16 pixels (words 0-3: x0, y0, x1, y1 inclusive) and the projection scale
// (floats +0x20/+0x24, the rest of that qword zeroed).
void func_00116B58(u32 r, u32 cam) {
    const s32 cx = Wrap(static_cast<s64>(fpu::FloatToInt(GRef<f32>(cam + 0x178))) << 4);
    const s32 cy = Wrap(static_cast<s64>(fpu::FloatToInt(GRef<f32>(cam + 0x17C))) << 4);
    const s32 hw = (GRef<s16>(cam + 0x18C) >> 1) << 4, hh = (GRef<s16>(cam + 0x18E) >> 1) << 4;
    GRef<s32>(r + 0x0) = Wrap(static_cast<s64>(cx) - hw);
    GRef<s32>(r + 0x4) = Wrap(static_cast<s64>(cy) - hh);
    GRef<s32>(r + 0x8) = Wrap(static_cast<s64>(cx) + hw - 0x10);
    GRef<s32>(r + 0xC) = Wrap(static_cast<s64>(cy) + hh - 0x10);
    for (u32 o = 0; o < 16; o += 4) GRef<u32>(r + 0x20 + o) = 0;
    const f32 k = GRef<f32>(0x0040DCAC);
    GRef<f32>(r + 0x20) = GRef<f32>(cam + 0x180) * GRef<f32>(cam + 0x168) * k;
    GRef<f32>(r + 0x24) = GRef<f32>(cam + 0x184) * GRef<f32>(cam + 0x168) * k;
}

// EE: 0x00116BF8
// Projects n points with M (perspective, vftoi4) into XY words at out. Returns -1 when a point
// is behind the camera or outside 0..0xFFFF (earlier words stay written), else the AND of the
// RectMask lanes against rect (rect = 0: lo = 0, hi = 0x7FFFFFFF): non-zero = all outside on
// one side.
u64 func_00116BF8(s32 n, u32 out, u32 pts, u32 m, u32 rect) {
    u64 lo = 0, hi = 0x7FFFFFFF7FFFFFFFull;
    if (rect != 0) {
        hi = GRef<u64>(rect + 8);
        lo = GRef<u64>(rect);
    }
    const vu0::Vec4 c0 = LoadVec(m), c1 = LoadVec(m + 0x10), c2 = LoadVec(m + 0x20), c3 = LoadVec(m + 0x30);
    u64 acc = ~0ull;
    for (s32 k = 0; k < n; ++k) {
        const vu0::Vec4 v = vu0::MulMatrix(c0, c1, c2, c3, LoadVec(pts + 0x10 * k));
        const f32 q = vu0::Div(1.0f, v.w);
        const s32 ix = vu0::FloatToInt4(vu0::Mul(v.x, q)), iy = vu0::FloatToInt4(vu0::Mul(v.y, q));
        if (vu0::FloatToInt4(v.w) < 0 || OffScreen(ix, iy)) return ~0ull;
        acc &= RectMask(ix, iy, lo, hi);
        GRef<u32>(out + 4 * k) = (static_cast<u32>(ix) & 0xFFFF) | (static_cast<u32>(iy) << 16);
    }
    return acc;
}

// EE: 0x00116CD8
// REGLIST tag (NLOOP n, EOP, regs UV, XYZ2) at p, then per k: UV = uv[k], XYZ2 = xy[k] | z << 32.
u32 func_00116CD8(u32 p, s32 n, u32 xy, u32 uv, u64 z) {
    GRef<u64>(p + 8) = 0x53;
    GRef<u64>(p) = (Sx(n) | 0x8000) | 0x2400000000000000ull;
    p += 0x10;
    for (s32 k = 0; k < n; ++k) {
        GRef<u64>(p) = GRef<u32>(uv + 4 * k);
        GRef<u64>(p + 8) = static_cast<u64>(GRef<u32>(xy + 4 * k)) | (z << 32);
        p += 0x10;
    }
    return p;
}

// EE: 0x00116D38
// Moon (R+4 bit 0x10): a textured quad from the 4 corners at 0x002A1B50 into bucket 0xB.
void func_00116D38(f32 pitch, f32 yaw, u32 cam, u32 list) {
    const u32 w = kSkyWork;
    func_00116B58(w, cam);
    func_00116A68(w + 0x30, cam, pitch, yaw, 0.0f);
    const f32 s = GRef<f32>(0x0040DCB0);
    func_00116B10(w + 0x70, w + 0x30, s, s);
    if (func_00116BF8(4, w + 0x140, 0x002A1B50, w + 0x70, w) != 0) return;
    const u32 mc = g_pMainContext().addr;
    const u32 buf = Ctx()->cur_buf.addr;
    u32 p = func_00113468(buf + 0x10, GRef<u64>(mc + 0xD0), GRef<u64>(cam + 0x1A0), GRef<u64>(mc + 0xF0));
    GRef<u64>(p + 0x00) = 0x11AA400000000004ull;
    GRef<u64>(p + 0x08) = 0xE;
    GRef<u64>(p + 0x10) = 0xFF00000000080000ull;
    GRef<u64>(p + 0x18) = 0x4D;
    GRef<u64>(p + 0x20) = 0x1310000A8ull;
    GRef<u64>(p + 0x28) = 0x4F;
    GRef<u64>(p + 0x30) = 0x50000;
    GRef<u64>(p + 0x38) = 0x48;
    GRef<u64>(p + 0x40) = 0x44;
    GRef<u64>(p + 0x48) = 0x43;
    p += 0x50;
    GRef<u64>(p + 0x08) = 0x17;
    GRef<u64>(p + 0x00) = 0x2400000000000001ull;
    GRef<u64>(p + 0x10) = 0x2007EA86A9B0A100ull;
    GRef<u64>(p + 0x18) = 0x80808080ull;
    const u32 e = func_00116CD8(p + 0x20, 4, w + 0x140, 0x002A1B90, GRef<u32>(GRef<u32>(0x004104D0) + 8));
    LinkPacket(list, 0xB, buf, CloseDmaPacket(buf, e));
}

// EE: 0x00116F30
// Flare quad: scale s = (size * 4 + 9) * scale times the projection scale (capped at
// *0x0040DCB4), rotated by roll, centred on (tx, ty); the 4 corners at 0x002A1B50 (no
// perspective, vftoi0) into W+0x140. Returns -1 off the 0..0xFFFF range, else the RectMask AND.
// fpucheck: 1 add
u64 func_00116F30(u32 w, f32 tx, f32 ty, f32 size, f32 scale, f32 roll) {
    const f32 s = fpu::Add(size * 4.0f, 9.0f) * scale;
    const f32 lim = GRef<f32>(0x0040DCB4);
    f32 sx = s * GRef<f32>(w + 0x20);
    const f32 sy0 = s * GRef<f32>(w + 0x24);
    const u64 lo = GRef<u64>(w), hi = GRef<u64>(w + 8);
    if (lim < sx) sx = lim;
    f32 sy = sy0;
    if (lim < sy) sy = lim;
    const u32 m = w + 0x70, sm = w + 0xB0;
    SetUnitMatrixVu0(m);
    sceVu0RotMatrixZ(m, m, roll);
    BuildScaleMatrix(sx, sy, 0.0f, sm);
    MulMatrixVu0(m, sm, m);
    GRef<f32>(w + 0xA0) = tx;
    GRef<f32>(w + 0xA4) = ty;
    const vu0::Vec4 c0 = LoadVec(m), c1 = LoadVec(m + 0x10), c2 = LoadVec(m + 0x20), c3 = LoadVec(m + 0x30);
    u64 acc = ~0ull;
    for (u32 k = 0; k < 4; ++k) {
        const vu0::Vec4 v = vu0::MulMatrix(c0, c1, c2, c3, LoadVec(0x002A1B50 + 0x10 * k));
        const s32 ix = Ftoi0(v.x), iy = Ftoi0(v.y);
        if (OffScreen(ix, iy)) return ~0ull;
        acc &= RectMask(ix, iy, lo, hi);
        GRef<u32>(w + 0x140 + 4 * k) = (static_cast<u32>(ix) & 0xFFFF) | (static_cast<u32>(iy) << 16);
    }
    return acc;
}

// EE: 0x001170F0
// Sun visibility sampling for the flare (docs/subsystems/sky.md): clears the sun rectangle of
// buffer 0x120, draws the depth-tested sun sprite there, samples up to 15 x 15 points of it into
// one pixel of buffer 0x190, reduces and copies the result back as a colour-c sprite. Returns
// the write pointer. TEXFLUSH data dwords are left unwritten as on the PS2.
u32 func_001170F0(u32 p, u32 cam, u32 w, s32 a, s32 b, u64 colour) {
    const s32 sx = GRef<s32>(w + 0x160), sy = GRef<s32>(w + 0x164);
    const u64 lo = GRef<u64>(w), hi = GRef<u64>(w + 8);
    const u64 s1 = Sx(Wrap(static_cast<s64>(sx) + a)) | (Sx(Wrap(static_cast<s64>(sy) + b)) << 16) |
                   (GRef<u64>(w + 0x170) << 32);
    const u64 s0 = Sx(Wrap(static_cast<s64>(sx) - a)) | (Sx(Wrap(static_cast<s64>(sy) - b)) << 16);
    const u32 mc = g_pMainContext().addr;
    u32 t = func_00113468(p, GRef<u64>(mc + 0xD0), GRef<u64>(cam + 0x1A0), GRef<u64>(mc + 0xF0));
    GRef<u64>(t + 0x00) = 0x1000000000008004ull;
    GRef<u64>(t + 0x08) = 0xE;
    GRef<u64>(t + 0x10) = 0x80120;
    GRef<u64>(t + 0x18) = 0x4D;
    GRef<u64>(t + 0x20) = 0x1310000A8ull;
    GRef<u64>(t + 0x28) = 0x4F;
    GRef<u64>(t + 0x30) = 0x30000;
    GRef<u64>(t + 0x38) = 0x48;
    GRef<u64>(t + 0x40) = 0x48;
    GRef<u64>(t + 0x48) = 0x43;
    t += 0x50;
    GRef<u64>(t + 0x00) = 0x4400000000000001ull;
    GRef<u64>(t + 0x08) = 0x5510;
    GRef<u64>(t + 0x10) = 0x206;
    GRef<u64>(t + 0x18) = 0;
    GRef<u64>(t + 0x20) = s0;
    GRef<u64>(t + 0x28) = s1;
    t += 0x30;
    GRef<u64>(t + 0x00) = 0x11AB400000000002ull;
    GRef<u64>(t + 0x08) = 0xE;
    GRef<u64>(t + 0x10) = 0x50000;
    GRef<u64>(t + 0x18) = 0x48;
    GRef<u64>(t + 0x20) = 0x48;
    GRef<u64>(t + 0x28) = 0x43;
    t += 0x30;
    GRef<u64>(t + 0x00) = 0x6400000000000001ull;
    GRef<u64>(t + 0x08) = 0x535317;
    GRef<u64>(t + 0x10) = 0x2007EB02AA40A160ull;
    GRef<u64>(t + 0x18) = 0x80020202ull;
    GRef<u64>(t + 0x20) = 0x80008;
    GRef<u64>(t + 0x28) = s0;
    GRef<u64>(t + 0x30) = 0xF800F8;
    GRef<u64>(t + 0x38) = s1;
    t += 0x40;
    // Sample grid: 15 columns / rows across the sun rectangle, kept when inside the view.
    const s32 x0 = Wrap(static_cast<s64>(GRef<s32>(w + 0x160)) - a);
    const s32 y0 = Wrap(static_cast<s64>(GRef<s32>(w + 0x164)) - b);
    const s32 a2 = Wrap(static_cast<s64>(a) * 2), b2 = Wrap(static_cast<s64>(b) * 2);
    const s32 lox = static_cast<s32>(static_cast<u32>(lo)), loy = static_cast<s32>(static_cast<u32>(lo >> 32));
    const s32 hix = static_cast<s32>(static_cast<u32>(hi)), hiy = static_cast<s32>(static_cast<u32>(hi >> 32));
    u32 mx = 0, my = 0;
    for (s32 k = 0; k < 15; ++k) {
        const s32 dx = Wrap(static_cast<s64>(a2) * (k + 1)) / 16;
        const s32 x = Wrap(static_cast<s64>(x0) + dx);
        if (!(lox > x || x > hix)) {
            GRef<s32>(w + 0x190 + 4 * k) = Wrap(static_cast<s64>(x0) + 8 + dx - 0x7000);
            mx |= 1u << k;
        }
        const s32 dy = Wrap(static_cast<s64>(b2) * (k + 1)) / 16;
        const s32 y = Wrap(static_cast<s64>(y0) + dy);
        if (!(loy > y || y > hiy)) {
            GRef<s32>(w + 0x1D0 + 4 * k) = Wrap(static_cast<s64>(y0) + 8 + dy - 0x7200);
            my |= 1u << k;
        }
    }
    GRef<u64>(t + 0x00) = 0x1000000000000006ull;
    GRef<u64>(t + 0x08) = 0xE;
    GRef<u64>(t + 0x18) = 0x3F;  // TEXFLUSH (data unwritten)
    GRef<u64>(t + 0x20) = 0x10190;
    GRef<u64>(t + 0x28) = 0x4D;
    GRef<u64>(t + 0x30) = 0x30000;
    GRef<u64>(t + 0x38) = 0x48;
    GRef<u64>(t + 0x40) = 0;
    GRef<u64>(t + 0x48) = 0x19;
    GRef<u64>(t + 0x50) = 0x00100000000F0000ull;
    GRef<u64>(t + 0x58) = 0x41;
    GRef<u64>(t + 0x60) = 0;
    GRef<u64>(t + 0x68) = 0x15;
    t += 0x70;
    GRef<u64>(t + 0x00) = 0x7400000000000001ull;
    GRef<u64>(t + 0x08) = 0x01970510;
    GRef<u64>(t + 0x10) = 0x200;
    GRef<u64>(t + 0x18) = 0x80000000ull;
    GRef<u64>(t + 0x20) = 0x01000000;
    GRef<u64>(t + 0x28) = 0x350;
    GRef<u64>(t + 0x30) = 0x2A8022400ull;
    GRef<u64>(t + 0x38) = 5;
    GRef<u64>(t + 0x40) = 0x80808080ull;
    t += 0x50;
    u32 q = t + 0x10;
    s32 n = 0;
    for (u32 k = 0; k < 15; ++k) {
        if ((mx & (1u << k)) == 0) continue;
        for (u32 j = 0; j < 15; ++j) {
            if ((my & (1u << j)) == 0) continue;
            GRef<u64>(q + 8) = 0x01000000;
            GRef<u64>(q) = Sx(GRef<s32>(w + 0x190 + 4 * k)) | (Sx(GRef<s32>(w + 0x1D0 + 4 * j)) << 16);
            q += 0x10;
            ++n;
        }
    }
    if (n > 0) {
        GRef<u64>(t + 8) = 0x53;
        GRef<u64>(t) = Sx(n) | 0x2400000000000000ull;
        t = q;
    }
    GRef<u64>(t + 0x00) = 0x118B400000000002ull;
    GRef<u64>(t + 0x08) = 0xE;
    GRef<u64>(t + 0x18) = 0x3F;  // TEXFLUSH (data unwritten)
    GRef<u64>(t + 0x20) = 0x10190;
    GRef<u64>(t + 0x28) = 0x4D;
    t += 0x30;
    GRef<u64>(t + 0x00) = 0x5400000000000001ull;
    GRef<u64>(t + 0x08) = 0x55317;
    GRef<u64>(t + 0x10) = 0x2A8007200ull;
    GRef<u64>(t + 0x18) = 0x804040FFull;
    GRef<u64>(t + 0x20) = 0x01080008;
    GRef<u64>(t + 0x28) = 0;
    GRef<u64>(t + 0x30) = 0x01000100;
    t = func_00113B50(t + 0x40, 0x3200, 0x3200, 0x40, 0x10);
    GRef<u64>(t + 0x00) = 0x1000000000000007ull;
    GRef<u64>(t + 0x08) = 0xE;
    GRef<u64>(t + 0x18) = 0x3F;  // TEXFLUSH (data unwritten)
    GRef<u64>(t + 0x20) = 0xFF00000000010190ull;
    GRef<u64>(t + 0x28) = 0x4D;
    GRef<u64>(t + 0x30) = 0x1310000A8ull;
    GRef<u64>(t + 0x38) = 0x4F;
    GRef<u64>(t + 0x40) = 0x30000;
    GRef<u64>(t + 0x48) = 0x48;
    GRef<u64>(t + 0x50) = 0;
    GRef<u64>(t + 0x58) = 0x19;
    GRef<u64>(t + 0x60) = 0x000F0000000F0000ull;
    GRef<u64>(t + 0x68) = 0x41;
    GRef<u64>(t + 0x70) = 0;
    GRef<u64>(t + 0x78) = 0x15;
    t += 0x80;
    GRef<u64>(t + 0x00) = 0x8400000000000001ull;
    GRef<u64>(t + 0x08) = 0x53537190;
    GRef<u64>(t + 0x10) = 0x316;
    GRef<u64>(t + 0x18) = 5;
    GRef<u64>(t + 0x20) = colour & 0xFFFFFFFFull;
    GRef<u64>(t + 0x28) = 0x2A8007F48ull;
    GRef<u64>(t + 0x30) = 0x80008;
    GRef<u64>(t + 0x38) = 0;
    GRef<u64>(t + 0x40) = 0x01080108;
    GRef<u64>(t + 0x48) = 0x01000100;
    t += 0x50;
    GRef<u64>(t + 0x08) = 0xE;
    GRef<u64>(t + 0x18) = 0x3F;  // TEXFLUSH (data unwritten)
    GRef<u64>(t + 0x00) = 0x1000000000008001ull;
    return t + 0x20;
}

// EE: 0x001177D8
// Glare overlay: one additive sprite W+0x180..W+0x188 with colour c into list/bucket.
void func_001177D8(u32 list, s32 bucket, u32 cam, u32 w, u64 colour) {
    const u32 mc = g_pMainContext().addr;
    const u32 buf = Ctx()->cur_buf.addr;
    u32 p = func_00113468(buf + 0x10, GRef<u64>(mc + 0xD0), GRef<u64>(cam + 0x1A0), GRef<u64>(mc + 0xF0));
    GRef<u64>(p + 0x00) = 0x1000000000008005ull;
    GRef<u64>(p + 0x08) = 0xE;
    GRef<u64>(p + 0x10) = 0xFF00000000080000ull;
    GRef<u64>(p + 0x18) = 0x4D;
    GRef<u64>(p + 0x20) = 0x1310000A8ull;
    GRef<u64>(p + 0x28) = 0x4F;
    GRef<u64>(p + 0x30) = 0x50000;
    GRef<u64>(p + 0x38) = 0x48;
    GRef<u64>(p + 0x40) = 0x48;
    GRef<u64>(p + 0x48) = 0x43;
    GRef<u64>(p + 0x50) = 0x2007E902A9B0A000ull;
    GRef<u64>(p + 0x58) = 7;
    p += 0x60;
    GRef<u64>(p + 0x00) = 0x2400000000008001ull;
    GRef<u64>(p + 0x18) = colour & 0xFFFFFFFFull;
    GRef<u64>(p + 0x08) = 0x10;
    GRef<u64>(p + 0x10) = 0x356;
    const u32 e = BuildSpriteStripsUv(p + 0x20, GRef<u64>(w + 0x180), GRef<u64>(w + 0x188), 0x00080008, 0x07F807F8);
    LinkPacket(list, bucket, buf, CloseDmaPacket(buf, e));
}

// EE: 0x00117940
// Sun, flare and glare (R+4 bit 1; docs/subsystems/sky.md): projects the sun, sets the glare
// level K+0xC (unless R+4 bit 0x100), and when the sun is fully inside the view draws the glare
// (bucket 0xB) and the visibility-sampled flare quad (bucket 0xF).
// fpucheck: 2 add + 1 sub (scale), 1 add (glare gain), 2 sub + 1 add (distance), 1 sub (level),
// 3 sub (roll)
void func_00117940(f32 pitch, f32 yaw, u32 cam, u32 list) {
    const u32 w = kSkyWork;
    func_00116B58(w, cam);
    const f32 f = fpu::Add(fpu::Sub(GRef<f32>(cam + 0x168), GRef<f32>(0x0040DCB8)) * GRef<f32>(0x0040DCBC) *
                               GRef<f32>(0x0040DCC0),
                           1.0f);
    const s32 r2 = GRef<s32>(w + 8), r0 = GRef<s32>(w), r1 = GRef<s32>(w + 4), r3 = GRef<s32>(w + 0xC);
    const f32 fx = GRef<f32>(cam + 0x180) * f;
    GRef<f32>(w + 0x20) = fx;
    const s32 a = fpu::FloatToInt(fx * GRef<f32>(0x0040DCC4));
    const f32 fy = GRef<f32>(cam + 0x184) * f;
    const s32 b = fpu::FloatToInt(fy * GRef<f32>(0x0040DCC4));
    GRef<f32>(w + 0x24) = fy;
    GRef<s32>(w + 0x18) = Wrap(static_cast<s64>(r2) + a);
    GRef<s32>(w + 0x10) = Wrap(static_cast<s64>(r0) - a);
    GRef<s32>(w + 0x14) = Wrap(static_cast<s64>(r1) - b);
    GRef<s32>(w + 0x1C) = Wrap(static_cast<s64>(r3) + b);
    func_00116A68(w + 0x30, cam, pitch, yaw, 0.0f);
    StoreVec(w + 0xF0, {0.0f, 0.0f, 0.0f, 1.0f});
    GRef<f32>(w + 0xF8) = 65536.0f;
    const u64 r = func_00116BF8(1, w + 0x140, w + 0xF0, w + 0x30, w + 0x10);
    if (r == ~0ull) return;
    const u32 ksky = GRef<u32>(0x004104D0);
    const s32 sy = GRef<u16>(w + 0x142);
    const s32 z = GRef<s32>(ksky + 8);
    GRef<s32>(w + 0x16C) = b;
    const s32 sx = GRef<u16>(w + 0x140);
    GRef<s32>(w + 0x168) = a;
    GRef<s32>(w + 0x160) = sx;
    GRef<s32>(w + 0x164) = sy;
    const s32 a4 = Wrap(static_cast<s64>(a) * 4), b4 = Wrap(static_cast<s64>(b) * 4);
    const u32 flags = GRef<u32>(GRef<u32>(0x004104D4) + 4);
    GRef<u64>(w + 0x188) = Sx(Wrap(static_cast<s64>(sx) + a4)) | (Sx(Wrap(static_cast<s64>(sy) + b4)) << 16) |
                           (static_cast<u64>(static_cast<u32>(z)) << 32);
    GRef<u64>(w + 0x180) = Sx(Wrap(static_cast<s64>(sx) - a4)) | (Sx(Wrap(static_cast<s64>(sy) - b4)) << 16);
    GRef<u64>(w + 0x170) = Sx(z);
    u64 c = 0xFFFFFFFF80808080ull;
    f32 gain = 1.0f;
    if ((flags & 0x600) != 0) {
        f32 t = GRef<f32>(ksky + 0x14) * 1.25f;
        if (1.0f < t) t = 1.0f;
        f32 k0, k1;
        if ((flags & 0x200) != 0) {
            c = LerpColor(t, 0xFFFFFFFF80202020ull, 0xFFFFFFFF80808080ull);
            k0 = GRef<f32>(0x0040DCC8);
            k1 = GRef<f32>(0x0040DCCC);
        } else {
            c = LerpColor(t, 0xFFFFFFFF80383838ull, 0xFFFFFFFF80808080ull);
            k0 = GRef<f32>(0x0040DCD0);
            k1 = GRef<f32>(0x0040DCD4);
        }
        gain = fpu::Add(t * k0, k1);
    }
    if (r == 0) func_001177D8(list, 0xB, cam, w, c);
    const f32 fsx = fpu::IntToFloat(sx), fsy = fpu::IntToFloat(sy);
    const f32 dx = std::fabs(fpu::Sub(fsx, GRef<f32>(cam + 0x178) * 16.0f));
    const f32 dy = std::fabs(fpu::Sub(fsy, GRef<f32>(cam + 0x17C) * 16.0f));
    const f32 d = fpu::Add(dx, dy);
    f32 v = fpu::Sub(1.0f, fpu::Div(d * GRef<f32>(0x0040DCD8), GRef<f32>(cam + 0x168))) * gain;
    if (v <= 0.0f) return;
    if (1.0f < v) v = 1.0f;
    if ((GRef<u32>(GRef<u32>(0x004104D4) + 4) & 0x100) == 0) GRef<f32>(GRef<u32>(0x004104D0) + 0xC) = v;
    if (r != 0) return;
    const f32 u = fpu::IntToFloat(Wrap(static_cast<s64>(sx) + 2 * sy)) * GRef<f32>(0x0040DCDC);
    const f32 roll = fpu::Sub(fpu::Sub(fpu::Sub(u, ee::floorf(u)) * GRef<f32>(0x0040DCE0), GRef<f32>(0x0040DCE4)),
                              GRef<f32>(0x0040DCE8));
    if (func_00116F30(w, fsx, fsy, v, GRef<f32>(0x0040DCEC), roll) != 0) return;
    const u32 buf = Ctx()->cur_buf.addr;
    const s32 ha = GRef<s32>(w + 0x168), hb = GRef<s32>(w + 0x16C);
    u32 p = func_001170F0(buf + 0x10, cam, w, ha - (ha >> 2), hb - (hb >> 2), c);
    const u32 mc = g_pMainContext().addr;
    p = func_00113468(p, GRef<u64>(mc + 0xD0), GRef<u64>(cam + 0x1A0), GRef<u64>(mc + 0xF0));
    GRef<u64>(p + 0x00) = 0x11AA400000000004ull;
    GRef<u64>(p + 0x08) = 0xE;
    GRef<u64>(p + 0x10) = 0xFF00000000080000ull;
    GRef<u64>(p + 0x18) = 0x4D;
    GRef<u64>(p + 0x20) = 0x1310000A8ull;
    GRef<u64>(p + 0x28) = 0x4F;
    GRef<u64>(p + 0x30) = 0x30000;
    GRef<u64>(p + 0x38) = 0x48;
    GRef<u64>(p + 0x40) = 0x48;
    GRef<u64>(p + 0x48) = 0x43;
    p += 0x50;
    GRef<u64>(p + 0x00) = 0x2400000000008001ull;
    GRef<u64>(p + 0x08) = 0x17;
    GRef<u64>(p + 0x10) = 0x20064006A9B0A000ull;
    GRef<u64>(p + 0x18) = LerpColor(v, 0xFFFFFFFF80303030ull, 0xFFFFFFFF80404040ull) & 0xFFFFFFFFull;
    const u32 e = func_00116CD8(p + 0x20, 4, w + 0x140, 0x002A1BA0, GRef<u64>(w + 0x170));
    LinkPacket(list, 0xF, buf, CloseDmaPacket(buf, e));
}

// EE: 0x00117E80
// Sun halo: one large textured sprite centred on the projected sun, clipped to the screen and the
// view rectangle; returns p unchanged when the sun is behind or far off screen.
u32 func_00117E80(u32 p, u32 cam) {
    const u32 w = kSkyWork;
    SetUnitMatrixVu0(w);
    func_00102D28(w + 0x80, w, cam + 0x10);
    sceVu0InversMatrix(w + 0x80, w + 0x80);
    MulMatrixVu0(w + 0x80, cam + 0x60, w + 0x80);
    sceVu0RotMatrixX(w, w, -GRef<f32>(GRef<u32>(0x00410694) + 0x88));
    sceVu0RotMatrixY(w, w, GRef<f32>(GRef<u32>(0x00410694) + 0x8C));
    MulMatrixVu0(w, w + 0x80, w);
    StoreVec(w + 0xC0, {0.0f, 0.0f, 0.0f, 1.0f});
    GRef<f32>(w + 0xC8) = GRef<f32>(0x0040DCF0);
    sceVu0RotTransPers(w + 0xD0, w, w + 0xC0, 1);
    if (GRef<s32>(w + 0xDC) < 0x1F5) return p;
    const s32 sx = GRef<s32>(w + 0xD0);
    if (0xF000 < sx || sx < 0x1000) return p;
    const s32 sy = GRef<s32>(w + 0xD4);
    if (0xF000 < sy || sy < 0x1000) return p;
    const f32 f = GRef<f32>(cam + 0x168) * GRef<f32>(0x0040DCF4);
    const s32 hy = fpu::FloatToInt(f * 4096.0f);
    const s32 hx = fpu::FloatToInt(f * 3280.0f);
    const s32 cy = fpu::FloatToInt(GRef<f32>(cam + 0x17C));
    const s32 cx = fpu::FloatToInt(GRef<f32>(cam + 0x178));
    const s32 hh = GRef<s16>(cam + 0x18E) / 2, hw = GRef<s16>(cam + 0x18C) / 2;
    const u64 clip1 = Sx(Wrap(static_cast<s64>(Wrap(static_cast<s64>(cx) + hw)) << 4)) |
                      (Sx(Wrap(static_cast<s64>(Wrap(static_cast<s64>(cy) + hh)) << 4)) << 16);
    const u64 clip0 = Sx(Wrap(static_cast<s64>(Wrap(static_cast<s64>(cx) - hw)) << 4)) |
                      (Sx(Wrap(static_cast<s64>(Wrap(static_cast<s64>(cy) - hh)) << 4)) << 16);
    GRef<u64>(p + 0x00) = 0x1000000000000005ull;
    GRef<u64>(p + 0x10) = 0xFF00000000080000ull;
    GRef<u64>(p + 0x18) = 0x4C;
    GRef<u64>(p + 0x20) = 0x3400D;
    GRef<u64>(p + 0x28) = 0x47;
    GRef<u64>(p + 0x30) = 0x1310000A8ull;
    GRef<u64>(p + 0x38) = 0x4E;
    GRef<u64>(p + 0x40) = 0x60;
    GRef<u64>(p + 0x48) = 0x14;
    GRef<u64>(p + 0x50) = 0x58;
    GRef<u64>(p + 0x08) = 0xE;
    GRef<u64>(p + 0x58) = 0x42;
    p += 0x60;
    GRef<u64>(p + 0x00) = 0x4400000000000001ull;
    GRef<u64>(p + 0x08) = 0x1068;
    GRef<u64>(p + 0x10) = 5;
    GRef<u64>(p + 0x20) = 0x156;
    GRef<u64>(p + 0x28) = 0x60808080;
    GRef<u64>(p + 0x18) = 0x2007E982A9B0A100ull;
    p += 0x30;
    s32 x1 = Wrap(static_cast<s64>(sx) + hx), y1 = Wrap(static_cast<s64>(sy) + hy);
    s32 x0 = Wrap(static_cast<s64>(sx) - hx), y0 = Wrap(static_cast<s64>(sy) - hy);
    s32 u0 = 8, u1 = 0x3F8, v0 = 8, v1 = 0x3F8;
    const f32 wx = fpu::IntToFloat(Wrap(static_cast<s64>(hx) * 2)), wy = fpu::IntToFloat(Wrap(static_cast<s64>(hy) * 2));
    if (x0 < 0) {
        u0 = fpu::FloatToInt(fpu::Div(fpu::IntToFloat(-x0) * 1008.0f, wx)) + 8;
        x0 = 0;
    }
    if (0xFFFF < x1) {
        u1 = 0x3F8 - fpu::FloatToInt(fpu::Div(fpu::IntToFloat(x1 - 0xFFFF) * 1008.0f, wx));
        x1 = 0xFFFF;
    }
    if (y0 < 0) {
        v0 = fpu::FloatToInt(fpu::Div(fpu::IntToFloat(-y0) * 1008.0f, wy)) + 8;
        y0 = 0;
    }
    if (0xFFFF < y1) {
        v1 = 0x3F8 - fpu::FloatToInt(fpu::Div(fpu::IntToFloat(y1 - 0xFFFF) * 1008.0f, wy));
        y1 = 0xFFFF;
    }
    p = BuildClippedSpriteStripsUv(p, Sx(x0) | (Sx(y0) << 16), Sx(x1) | (Sx(y1) << 16), Sx(u0) | (Sx(v0) << 16),
                                   Sx(u1) | (Sx(v1) << 16), clip0, clip1);
    GRef<u64>(p + 8) = 0xE;
    GRef<u64>(p + 0) = 0x1000000000008003ull;
    const u32 mc = g_pMainContext().addr;
    Copy16(p + 0x10, mc + 0xD0);
    Copy16(p + 0x20, mc + 0xE0);
    Copy16(p + 0x30, mc + 0x110);
    return p + 0x40;
}

// EE: 0x00118330
// Sun halo pass (R+4 bit 2): func_00117E80 plus the draw-env restore, linked into list/bucket.
void func_00118330(u32 cam, u32 list, s32 bucket) {
    if ((GRef<u32>(GRef<u32>(0x004104D4) + 4) & 2) == 0) return;
    const u32 buf = Ctx()->cur_buf.addr;
    const u32 e = func_00117E80(buf + 0x10, cam);
    const u32 mc = g_pMainContext().addr;
    GRef<u64>(e + 0) = 0x1000000000008002ull;
    GRef<u64>(e + 8) = 0xE;
    Copy16(e + 0x10, mc + 0xE0);
    Copy16(e + 0x20, mc + 0x110);
    LinkPacket(list, bucket, buf, CloseDmaPacket(buf, e + 0x30));
}

// ---- Weather particles (docs/subsystems/sky.md "Weather particles") -------------------------

// EE: 0x00118400
// Rain / snow streaks (R+4 bit 8): up to 400 particles of the table *0x004104E8 wrapped around
// the camera, each a textured sprite faded by distance; bucket 0xB. Nothing is linked when no
// particle is kept.
// fpucheck: 1 sub (dy), 2 sub (fade), 1 sub (size)
void func_00118400(f32 intensity, u32 cam, u32 list, s32 n) {
    if (intensity <= 0.0f) return;
    if (n <= 0) return;
    if (1.0f < intensity) intensity = 1.0f;
    if (!(n < 0x191)) n = 0x190;
    const u32 mc = g_pMainContext().addr;
    const u32 buf = Ctx()->cur_buf.addr;
    u32 p = func_00113468(buf + 0x10, GRef<u64>(mc + 0xD0), GRef<u64>(cam + 0x1A0), GRef<u64>(mc + 0xF0));
    const s32 ia = fpu::FloatToInt(intensity * 128.0f);
    GRef<u64>(p + 0x00) = 0x1000000000000003ull;
    GRef<u64>(p + 0x08) = 0xE;
    GRef<u64>(p + 0x10) = 0x5000C;
    GRef<u64>(p + 0x18) = 0x48;
    GRef<u64>(p + 0x20) = 0x1310000A8ull;
    GRef<u64>(p + 0x30) = (static_cast<u64>(static_cast<u32>(ia)) << 32) | 0x68;
    GRef<u64>(p + 0x28) = 0x4F;
    GRef<u64>(p + 0x38) = 0x43;
    p += 0x40;
    GRef<u64>(p + 0x00) = 0x2400000000008001ull;
    GRef<u64>(p + 0x08) = 0x97;
    GRef<u64>(p + 0x10) = 0x2007EA02A9B0A100ull;
    GRef<u64>(p + 0x18) = 0;
    p += 0x20;  // reserved slot for the particle REGLIST tag
    const u64 zw = static_cast<u64>(GRef<u32>(GRef<u32>(0x004104D0) + 8)) << 32;
    const u32 w = kSkyWork;
    sceVu0UnitMatrix(w + 0x20);
    func_00102D28(w + 0x20, w + 0x20, cam + 0x10);
    StoreVec(w, {0.0f, 0.0f, 0.0f, 1.0f});
    GRef<f32>(w + 8) = -57344.0f;
    sceVu0ApplyMatrix(w, w + 0x20, w);
    sceVu0TransMatrix(w + 0x20, w + 0x20, w);
    sceVu0InversMatrix(w + 0x20, w + 0x20);
    sceVu0MulMatrix(w + 0x20, cam + 0x60, w + 0x20);
    for (u32 i = 0; i < 4; ++i) Copy16(w + 0xA0 + 0x10 * i, 0x002A1BB0 + 0x10 * i);
    for (u32 o = 0; o < 16; o += 4) GRef<u32>(w + 0x10 + o) = 0;
    GRef<f32>(w + 0x10) = GRef<f32>(cam + 0x180);
    GRef<f32>(w + 0x14) = GRef<f32>(cam + 0x184);
    const vu0::Vec4 vf1 = LoadVec(w + 0x10);
    const vu0::Vec4 c0 = LoadVec(w + 0x20), c1 = LoadVec(w + 0x30), c2 = LoadVec(w + 0x40), c3 = LoadVec(w + 0x50);
    const vu0::Vec4 org = LoadVec(w);
    GRef<s32>(w + 0x60) = Ftoi0(org.x);
    GRef<s32>(w + 0x64) = Ftoi0(org.y);
    GRef<s32>(w + 0x68) = Ftoi0(org.z);
    GRef<s32>(w + 0x6C) = Ftoi0(org.w);
    StoreVec(w + 0x10, {0.0f, 0.0f, 0.0f, 1.0f});
    u32 tbl = GRef<u32>(0x004104E8);
    s32 kept = 0;
    u32 q = p + 0x10;
    const s32 ox = GRef<s32>(w + 0x60), oy = GRef<s32>(w + 0x64), oz = GRef<s32>(w + 0x68);
    const f32 eye = GRef<f32>(w + 4);
    for (s32 i = 0; i < n; ++i, tbl += 8) {
        const s32 py = static_cast<s32>((static_cast<u32>(oy) + GRef<u16>(tbl + 2)) & 0xFFFF) - 0x8000;
        const s32 px = static_cast<s32>((static_cast<u32>(ox) + GRef<u16>(tbl + 0)) & 0xFFFF) - 0x8000;
        const s32 pz = static_cast<s32>((static_cast<u32>(oz) + GRef<u16>(tbl + 4)) & 0xFFFF) - 0x8000;
        const f32 fy = fpu::IntToFloat(py);
        const f32 dy = fpu::Sub(fy, eye);
        GRef<f32>(w + 0x14) = fy;
        GRef<f32>(w + 0x10) = fpu::IntToFloat(px);
        GRef<f32>(w + 0x18) = fpu::IntToFloat(pz);
        if (-4096.0f < dy) continue;
        const f32 g = -16384.0f < dy ? dy * GRef<f32>(0x0040DCF8) : 1.0f;
        vu0::Vec4 v = vu0::MulMatrix(c0, c1, c2, c3, LoadVec(w + 0x10));
        const f32 qd = vu0::Div(1.0f, v.w);
        v.x = vu0::Mul(v.x, qd);
        v.y = vu0::Mul(v.y, qd);
        v.z = vu0::Mul(v.z, qd);
        const s32 ix = vu0::FloatToInt4(v.x), iy = vu0::FloatToInt4(v.y);
        if (vu0::FloatToInt4(v.w) < 0 || OffScreen(ix, iy)) continue;
        const f32 dist = std::fabs(fpu::Sub(vu0::Add(0.0f, v.w), 57344.0f));
        const f32 e = fpu::Sub(1.0f, dist * GRef<f32>(0x0040DCFC)) * g;
        if (e <= 0.0f) continue;
        const f32 s = fpu::Sub(8.0f, fpu::IntToFloat(i & 7) * GRef<f32>(0x0040DD00)) * g;
        const vu0::Vec4 h{vu0::Mul(vf1.x, s), vu0::Mul(vf1.y, s), vu0::Mul(vf1.z, s), vu0::Mul(vf1.w, s)};
        const s32 ax = vu0::FloatToInt4(vu0::Sub(v.x, h.x)), ay = vu0::FloatToInt4(vu0::Sub(v.y, h.y));
        const s32 bx = vu0::FloatToInt4(vu0::Add(v.x, h.x)), by = vu0::FloatToInt4(vu0::Add(v.y, h.y));
        if (OffScreen(ax, ay)) continue;
        if (OffScreen(bx, by)) continue;
        u32 tab;
        if (i < 5)
            tab = w + 0xA0;
        else if (i < 0x13)
            tab = w + 0xB0;
        else
            tab = 0x46 < i ? w + 0xD0 : w + 0xC0;
        const u32 c = (i & 4) != 0 ? GRef<u32>(tab) : GRef<u32>(tab + 4 * (i & 3));
        const s16 fade = static_cast<s16>(fpu::FloatToInt(e * 4096.0f));
        GRef<u64>(q + 0x00) = 0x356;
        GRef<u64>(q + 0x10) = 0x80408;
        GRef<u64>(q + 0x08) = LerpBytes(fade, 0x00080808, c);
        GRef<u64>(q + 0x18) = Xy16(ax, ay);
        GRef<u64>(q + 0x20) = 0x01F805F8;
        GRef<u64>(q + 0x28) = Xy16(bx, by) | zw;
        ++kept;
        q += 0x30;
    }
    if (kept <= 0) return;
    GRef<u64>(p + 8) = 0x535310;
    GRef<u64>(p) = (Sx(kept) | 0x8000) | 0x6400000000000000ull;
    LinkPacket(list, 0xB, buf, CloseDmaPacket(buf, q));
}

}  // namespace ac04::game
