// Mission sound (slice S5h): the voice API entry points (PlaySound / SetSoundVolume /
// SetSoundPitch), the engine mixer and ambient sounds, positional event sounds, the 3D listener
// with the engine voices of other aircraft, missiles and fly-bys, and the sequence sounds
// (docs/subsystems/sound.md "Voice API", "Engine and ambient sounds", "Positional and 3D sounds").
#include "game/game.h"

#include <cmath>
#include <cstring>

#include "common/ee_math.h"
#include "common/guest_stack.h"
#include "game/flight_common.h"
#include "sce/sce.h"

namespace ac04::game {

using guest::GRef;
using namespace fl;

namespace {

constexpr u32 kLayers = 0x00423880;   // engine layer voice records, 2 x 0x318 (11 x 0x48)
constexpr u32 kThrottle = 0x00423EB0;  // per-player engine input, 2 x 0x50

u32 Layer(s32 p, s32 n) { return kLayers + static_cast<u32>(p) * 0x318 + static_cast<u32>(n) * 0x48; }

// libgcc __cmpdf2 (0x0028BD28, fp-bit) of two doubles: -1 / 0 / 1. The engine values compared
// here are never NaN.
s32 CmpDf(double a, double b) { return a < b ? -1 : (a > b ? 1 : 0); }

}  // namespace

// ---- Voice API ------------------------------------------------------------------------------------

// EE: 0x0021D328
// Core of PlaySound: key-on request for sound `sound` of bank `bank` into rec with the left /
// right volumes scaled by the category volume (+0x28) and the record's volume +0x2A (x 129)
// through the pan law +0x20, pan mode +0x14 from the pair; a linked sound (+0x1E >= 0) is played
// the same way into a stack record whose voice is kept in rec +8. -1 when no voice was free.
s32 func_0021D328(s16 bank, s16 sound, s16 left, s16 right, u32 rec, s8 priority) {
    GRef<s16>(rec + 0x18) = sound;
    GRef<s16>(rec + 0xC) = bank;
    GRef<s8>(rec + 0x1A) = priority;
    const f32 fl = fpu::IntToFloat(left), fr = fpu::IntToFloat(right);
    GRef<u32>(rec + 8) = 0;
    GRef<s8>(rec + 0xE) = -1;
    if (func_002174D0(1, rec) == -1) return -1;
    const s8 dv = static_cast<s8>(func_0021AC28(rec + 0x18));
    GRef<u16>(rec + 0x2A) = static_cast<u16>(func_0021CDD0(GRef<u8>(rec + 0x2A), dv));
    const f32 cat = func_0021CD08(GRef<u8>(rec + 0x28));
    const s16 v = static_cast<s16>(GRef<s16>(rec + 0x2A) * 129);
    GRef<s16>(rec + 0x2C) = func_0021A800(GRef<u8>(rec + 0x20), v, static_cast<s16>(fpu::FloatToInt(fl * cat)));
    const s16 r = func_0021A800(GRef<u8>(rec + 0x20), v, static_cast<s16>(fpu::FloatToInt(fr * cat)));
    GRef<s16>(rec + 0x2E) = r;
    GRef<s32>(rec + 0x14) = func_0021AD48(GRef<s16>(rec + 0x2C), r);
    func_0021D0C8(rec);
    if (GRef<s16>(rec + 0x1E) >= 0) {
        guest::GuestFrame linked(0x50);
        func_0021D328(bank, GRef<s16>(rec + 0x1E), left, right, linked.Address(), priority);
        GRef<u32>(rec + 8) = GRef<u32>(linked.Address() + 4);
    }
    return 0;
}

// EE: 0x0021D2E0
// Plays sound `sound` of bank `bank` at volumes (left, right) into the voice record rec. 0, or -1
// when no voice was free.
s32 PlaySound(s16 bank, s16 sound, s16 left, s16 right, u32 rec) {
    return func_0021D328(bank, sound, left, right, rec, -1) != -1 ? 0 : -1;
}

// EE: 0x0021D830
// New volumes (left, right) for the voice of rec (category and record volume as in PlaySound).
// When the pan mode changes on a voice flagged +0x1C bit 0, the voice is restarted (0x0021D138)
// and a copy of the old record stopped; else a volume request (mode 1) is queued. The linked voice
// (+8) gets the same volumes.
s32 SetSoundVolume(s16 left, s16 right, u32 rec) {
    const f32 fr = fpu::IntToFloat(right), fl = fpu::IntToFloat(left);
    const f32 cat = func_0021CD08(GRef<u8>(rec + 0x28));
    const s16 v = static_cast<s16>(GRef<s16>(rec + 0x2A) * 129);
    GRef<s16>(rec + 0x2C) = func_0021A800(GRef<u8>(rec + 0x20), v, static_cast<s16>(fpu::FloatToInt(fl * cat)));
    const s16 r = func_0021A800(GRef<u8>(rec + 0x20), v, static_cast<s16>(fpu::FloatToInt(fr * cat)));
    GRef<s16>(rec + 0x2E) = r;
    const s32 pm = func_0021AD48(GRef<s16>(rec + 0x2C), r);
    if (GRef<s32>(rec + 0x14) == pm || (GRef<u8>(rec + 0x1C) & 1) == 0) {
        GRef<s32>(rec + 0x14) = pm;
        GRef<u32>(rec + 0x10) = 1;
        if (func_002174D0(2, rec) == -1) return -1;
        QueueSoundVoiceCmd(rec);
    } else {
        GRef<s32>(rec + 0x14) = pm;
        guest::GuestFrame old(0x50);
        for (u32 o = 0; o < 0x48; o += 8) GRef<u64>(old.Address() + o) = GRef<u64>(rec + o);
        func_0021D138(rec);
        StopSound(old.Address());
    }
    const u32 linked = W(rec + 8);
    if (linked != 0) {
        guest::GuestFrame lr(0x50);
        if (func_00218008(linked, lr.Address()) != -1) SetSoundVolume(left, right, lr.Address());
    }
    return 0;
}

// EE: 0x0021DE30
// New pitch (note, fine) for the voice of rec (request mode 4), also for its linked voice.
s32 SetSoundPitch(u8 note, s8 fine, u32 rec) {
    GRef<s8>(rec + 0x33) = fine;
    GRef<u8>(rec + 0x32) = note;
    GRef<u32>(rec + 0x10) = 4;
    if (func_002174D0(2, rec) == -1) return -1;
    QueueSoundVoiceCmd(rec);
    const u32 linked = W(rec + 8);
    if (linked != 0) {
        guest::GuestFrame lr(0x50);
        if (func_00218008(linked, lr.Address()) != -1) SetSoundPitch(note, fine, lr.Address());
    }
    return 0;
}

// EE: 0x0021DFC8
// Doppler pitch for rec: its base pitch (+0x30/+0x31 with offsets +0x34/+0x35) shifted by
// 0x0021A8E8 for relative speed k.
void func_0021DFC8(f32 k, u32 rec) {
    const u32 pair = ((GRef<u8>(rec + 0x30) + static_cast<u32>(GRef<s8>(rec + 0x34))) << 8) & 0xFF00;
    const s16 off = static_cast<s16>(GRef<u8>(rec + 0x31) + GRef<s8>(rec + 0x35));
    const u16 v = func_0021A8E8(k, static_cast<s16>(func_0021CD70(pair, off)));
    SetSoundPitch(static_cast<u8>((v & 0x7F00u) >> 8), static_cast<s8>(v & 0x7Fu), rec);
}

// ---- Engine sound ---------------------------------------------------------------------------------

// EE: 0x002156B8
// Starts engine layer n of player p at volume pct (0..100 -> 0..255): layers 5 and 6 from bank 10,
// the others from the player's engine bank (0x00410CD0 [p]); sound ids 0x0033D4C0 [n]. Any other
// layer holding the same channel loses it. Returns the layer's channel.
u8 func_002156B8(s32 p, s32 n, u32 pct) {
    const s16 v = static_cast<s16>(((pct & 0xFF) * 0xFF) / 100);
    const s16 id = GRef<s16>(0x0033D4C0 + 2u * static_cast<u32>(n));
    if (static_cast<u32>(n - 5) < 2)
        PlaySound(10, id, v, v, Layer(p, n));
    else
        PlaySound(GRef<s16>(0x00410CD0 + 4u * static_cast<u32>(p)), id, v, v, Layer(p, n));
    const u32 rec = Layer(p, n);
    for (s32 i = 0; i < 0xB; ++i) {
        if (i == n) continue;
        const u32 o = Layer(p, i);
        if (GRef<s8>(rec + 0xE) == GRef<s8>(o + 0xE) && GRef<s8>(rec + 0xE) != -1) GRef<u8>(o + 0xE) = 0xFF;
    }
    return GRef<u8>(Layer(p, n) + 0xE);
}

// EE: 0x00215870
// Volume pct (0..100) for engine layer n of player p.
void func_00215870(s32 p, s32 n, u32 pct) {
    const s16 v = static_cast<s16>((((pct & 0xFF) * 0xFF) / 100) & 0xFF);
    SetSoundVolume(v, v, Layer(p, n));
}

// EE: 0x002158D8
// Pitch of engine layer n of player p: its base pitch (+0x30/+0x31, note offset +0x34) shifted by
// off + the fine offset +0x35 (0x0021CD70).
void func_002158D8(s32 p, s32 n, s16 off) {
    const u32 rec = Layer(p, n);
    const u32 pair = (GRef<u8>(rec + 0x31) | ((GRef<u8>(rec + 0x30) + static_cast<u32>(GRef<s8>(rec + 0x34))) << 8)) & 0xFFFF;
    const u16 v = func_0021CD70(pair, static_cast<s16>(off + GRef<s8>(rec + 0x35)));
    SetSoundPitch(static_cast<u8>((v & 0x7F00u) >> 8), static_cast<s8>(v & 0x7Fu), rec);
}

// EE: 0x002159E0
// Volume curve `curve` (< 5; else flat 100) of player p's curve set at throttle t: bytes at
// throttle x 10, linearly interpolated.
// fpucheck: verified 1
u32 func_002159E0(f32 t, s32 p, s64 curve) {
    const f32 x = t * 10.0f;
    const s32 i = fpu::FloatToInt(x);
    const f32 frac = fpu::Sub(x, fpu::IntToFloat(i));
    u32 lo, hi;
    if (curve < 5) {
        const u32 base = 0x00423F50 + static_cast<u32>(p) * 0x10E + static_cast<u32>(curve) * 0x12 + static_cast<u32>(i);
        lo = GRef<u8>(base);
        hi = GRef<u8>(base + 1);
    } else {
        lo = hi = 100;
    }
    const s32 d = static_cast<s8>(hi - lo);
    if (d == 0) return lo;
    return (lo + static_cast<u32>(fpu::FloatToInt(fpu::IntToFloat(d) * frac))) & 0xFF;
}

// EE: 0x00215A90
// Pitch curve `curve` (s16 entries at +0x5A, 0x24 per curve) of player p's curve set at throttle t.
// fpucheck: verified 1
s16 func_00215A90(f32 t, s32 p, s32 curve) {
    const f32 x = t * 10.0f;
    const s32 i = fpu::FloatToInt(x);
    const u32 base = 0x00423F50 + 0x5A + static_cast<u32>(p) * 0x10E + static_cast<u32>(curve) * 0x24;
    const s16 lo = GRef<s16>(base + 2u * static_cast<u32>(i));
    const s16 hi = GRef<s16>(base + 2u * static_cast<u32>(i + 1));
    const f32 frac = fpu::Sub(x, fpu::IntToFloat(i));
    const s16 d = static_cast<s16>(hi - lo);
    if (d == 0) return lo;
    return static_cast<s16>(lo + fpu::FloatToInt(fpu::IntToFloat(d) * frac));
}

// EE: 0x00215B38
// Speed volume factor (percent): 100 from the speed 0x00410CF0 on, else speed x 0x00410CF2 /
// 0x00410CF6.
u32 func_00215B38(f32 speed) {
    if (!(speed < fpu::IntToFloat(GRef<s16>(0x00410CF0)))) return 100;
    return static_cast<u32>(fpu::FloatToInt(fpu::IntToFloat(GRef<s16>(0x00410CF2)) * speed / fpu::IntToFloat(GRef<s16>(0x00410CF6)))) &
           0xFF;
}

// EE: 0x00215B90
// Speed pitch offset: speed x 0x00410CF4 / 0x00410CF6.
s16 func_00215B90(f32 speed) {
    return static_cast<s16>(fpu::FloatToInt(fpu::IntToFloat(GRef<s16>(0x00410CF4)) * speed / fpu::IntToFloat(GRef<s16>(0x00410CF6))));
}

// EE: 0x00215D20
// Engine mixer (per frame while enabled; mode 0x00410CDC: 0 one player, 1..3 two): for each player
// and each of the 9 layers enabled in 0x004117F8 [p], the volume from the layer table by mode
// (0x0033D448, or 0x0033D470 / 0x0033D498 for players 0 / 1; mode 2 mutes player 1, mode 3 player
// 0) x the throttle curve (player 0's curve set) and for layer 6 the speed factor; silent layers
// are stopped, others started or re-levelled. Layer 7 is only pitched; layer 8 (afterburner) is
// switched by the throttle crossing the double constants 0x004068A8 / B0 / B8 / C0 with 300-frame
// hold counters (0x00410C90 / C98) and a 480-frame re-trigger lock (0x00410C88). Pitch: -5 for
// layers 3, 5, 6 of engine type 2, the pitch curve for layers 0 and 4, the speed pitch for layer 6.
// Copies +0x1C / +0x20 of the engine input to 0x00410CE0 / 0x00410CE8. -1 when disabled.
s32 func_00215D20() {
    if (W(0x00410CD8) == 0) return -1;
    const u32 mode = W(0x00410CDC);
    s32 np;
    if (mode == 0)
        np = 1;
    else
        np = 2 < mode - 1 ? 0 : 2;
    for (s32 p = 0; p < np; ++p) {
        const u32 pp = 4u * static_cast<u32>(p);
        const u32 A = 0x00410C88 + pp, B = 0x00410C90 + pp, C = 0x00410C98 + pp, D = 0x00410CA0 + pp, E = 0x00410CA8 + pp;
        if (0 < GRef<s32>(A)) GRef<s32>(A) = GRef<s32>(A) - 1;
        const u32 in = kThrottle + static_cast<u32>(p) * 0x50;
        const u32 rec8 = Layer(p, 8);
        for (s32 L = 0; L < 9; ++L) {
            const u32 rec = Layer(p, L);
            if ((((static_cast<s32>(GRef<s16>(0x004117F8 + 2u * static_cast<u32>(p))) >> L) ^ 1) & 1) != 0) continue;
            const u32 row = 3u * static_cast<u32>(L);
            u32 base;
            if (W(0x00410CDC) == 0) {
                base = GRef<u8>(0x0033D448 + GRef<u8>(in) + row);
            } else if (W(0x00410CDC) == 2) {
                base = p != 0 ? 0u : GRef<u8>(0x0033D470 + GRef<u8>(kThrottle) + row);
            } else if (W(0x00410CDC) == 3) {
                base = p == 0 ? 0u : GRef<u8>(0x0033D498 + GRef<u8>(in) + row);
            } else {
                base = p == 0 ? GRef<u8>(0x0033D470 + GRef<u8>(kThrottle) + row) : GRef<u8>(0x0033D498 + GRef<u8>(in) + row);
            }
            s32 v = static_cast<s32>(base * func_002159E0(F(in + 4), 0, L)) / 100;
            u32 vol = static_cast<u32>(v) & 0xFF;
            if (L == 6) {
                v = static_cast<s32>(static_cast<u32>(v) * func_00215B38(F(in + 8))) / 100;
                vol = static_cast<u32>(v) & 0xFF;
            }
            if (vol == 0) {
                if (GRef<s8>(rec + 0xE) == -1) continue;
                func_00215838(p, L);
                GRef<s8>(rec + 0xE) = -1;
            } else if (L == 7) {
            } else if (L != 8) {
                if (GRef<s8>(rec + 0xE) == -1)
                    GRef<u8>(rec + 0xE) = func_002156B8(p, L, vol);
                else
                    func_00215870(p, L, vol);
            } else {
                const double t = static_cast<double>(F(in + 4));
                if (W(D) != 0) {
                    if (0 < CmpDf(t, GRef<double>(0x004068A8))) {
                        GRef<u8>(rec8 + 0xE) = func_002156B8(p, 8, vol);
                        GRef<s32>(A) = 0x1E0;
                        GRef<u32>(D) = 0;
                    }
                } else if (W(E) != 0) {
                    if (!(CmpDf(t, GRef<double>(0x004068B0)) >= 0)) {
                        GRef<u8>(rec8 + 0xE) = func_002156B8(p, 8, vol);
                        GRef<s32>(A) = 0x1E0;
                        GRef<u32>(E) = 0;
                    }
                } else if (!(0 < CmpDf(t, GRef<double>(0x004068B8)))) {
                    GRef<u32>(C) = 0;
                    GRef<s32>(B) = GRef<s32>(B) + 1;
                    if (!(GRef<s32>(B) < 300) && !(0 < GRef<s32>(A))) {
                        GRef<u32>(B) = 0;
                        GRef<u32>(D) = 1;
                    }
                } else if (CmpDf(t, GRef<double>(0x004068C0)) < 0) {
                    GRef<u32>(B) = 0;
                    GRef<u32>(C) = 0;
                } else {
                    GRef<u32>(B) = 0;
                    GRef<s32>(C) = GRef<s32>(C) + 1;
                    if (!(GRef<s32>(C) < 300) && !(0 < GRef<s32>(A))) {
                        GRef<u32>(C) = 0;
                        GRef<u32>(E) = 1;
                    }
                }
                if (GRef<s8>(rec + 0xE) == -1) continue;
                guest::GuestFrame tmp(0x50);
                GRef<u8>(tmp.Address() + 0xE) = GRef<u8>(rec + 0xE);
                func_00217B40(tmp.Address());
                if (GRef<s8>(tmp.Address() + 0xE) != GRef<s8>(rec + 0xE)) {
                    GRef<s8>(rec + 0xE) = -1;
                } else {
                    func_00215870(p, 8, vol);
                }
            }
            if (GRef<s8>(rec + 0xE) == -1) continue;
            s16 off = 0;
            if (GRef<u8>(in) == 2 && (L == 3 || L == 5 || L == 6)) off = -5;
            if (L == 0 || L == 4) off = static_cast<s16>(off + func_00215A90(F(in + 4), 0, L));
            if (L == 6) off = static_cast<s16>(off + func_00215B90(F(in + 8)));
            if (off != 0) func_002158D8(p, L, off);
        }
        GRef<u32>(0x00410CE8 + pp) = W(in + 0x20);
        GRef<u32>(0x00410CE0 + pp) = W(in + 0x1C);
    }
    return 0;
}

// EE: 0x00216470
// Per-frame engine input of player p (0x50 bytes from src: type, throttle, speed, ...); a throttle
// above the double 0x004068C8 is replaced by C 0x0040FFA4.
void func_00216470(s32 p, u32 src) {
    const u32 dst = kThrottle + static_cast<u32>(p) * 0x50;
    for (u32 o = 0; o < 0x50; o += 8) GRef<u64>(dst + o) = GRef<u64>(src + o);
    if (0 < CmpDf(static_cast<double>(F(dst + 4)), GRef<double>(0x004068C8))) GRef<u32>(dst + 4) = W(0x0040FFA4);
}

// EE: 0x002166A8
// Engine mixer step while the engine sound is on (0x0033D858).
void func_002166A8() {
    if (W(0x0033D858) != 0) func_00215D20();
}

// EE: 0x002167B0
// Wind / airflow voice of ambient record rec (bank 6 sound 0x9B): level from the speed in the
// aircraft frame (|xy| x 255 / 40 + z / 2) panned by the yaw change since the last frame (x 300,
// wrapped by C 0x0040FFA8..0x0040FFB4), both channels doubled and clamped to 0..255. The first
// frame only records the yaw (+0xB4 bit 0), the voice starts on the second (bit 1).
// fpucheck: verified 9
void func_002167B0(u32 rec, u32 vel, u32 rot) {
    if ((W(rec + 0xB4) & 3u) == 0) {
        GRef<u32>(rec + 0xB4) = W(rec + 0xB4) | 1u;
        GRef<u32>(rec + 0xB8) = W(rot + 8);
        return;
    }
    guest::GuestFrame fr(0x90);
    const u32 m0 = fr.Address(0), m = fr.Address(0x40), v = fr.Address(0x80);
    sceVu0UnitMatrix(m0);
    func_00102D28(m, m0, rot);
    sceVu0InversMatrix(m, m);
    sceVu0ApplyMatrix(v, m, vel);
    f32 lvl = fpu::Sqrt(fpu::Add(F(v) * F(v), F(v + 4) * F(v + 4))) * 255.0f / 40.0f;
    f32 dr = fpu::Sub(F(rot + 8), F(rec + 0xB8));
    lvl = fpu::Add(lvl, F(v + 8) * 0.5f);
    if (F(0x0040FFA8) < dr)
        dr = fpu::Sub(dr, F(0x0040FFAC));
    else if (dr < F(0x0040FFB0))
        dr = fpu::Add(dr, F(0x0040FFB4));
    const f32 t = dr * 300.0f;
    f32 l = fpu::Sub(lvl, t), r = fpu::Add(lvl, t);
    l = fpu::Add(l, l);
    r = fpu::Add(r, r);
    if (255.0f < l) l = 255.0f;
    if (l < 0.0f) l = 0.0f;
    if (255.0f < r) r = 255.0f;
    if (r < 0.0f) r = 0.0f;
    if ((W(rec + 0xB4) & 2u) != 0) {
        SetSoundVolume(static_cast<s16>(fpu::FloatToInt(l)), static_cast<s16>(fpu::FloatToInt(r)), rec);
    } else {
        PlaySound(6, 0x9B, static_cast<s16>(fpu::FloatToInt(l)), static_cast<s16>(fpu::FloatToInt(r)), rec);
        GRef<u32>(rec + 0xB4) = W(rec + 0xB4) | 2u;
    }
    GRef<u32>(rec + 0xB8) = W(rot + 8);
}

// EE: 0x002169B8
// SPU core 0 input volume from k with the curve idx of table 0x004068D0 ({s32 scale, f32 offset}):
// (k + offset) x scale / offset, clamped to 0..0x7FFF.
void func_002169B8(f32 k, u32 /*rec*/, s32 idx) {
    const u32 e = 0x004068D0 + 8u * static_cast<u32>(idx);
    const f32 off = F(e + 4);
    s32 v = fpu::FloatToInt(fpu::Add(k, off) * fpu::IntToFloat(GRef<s32>(e)) / off);
    if (v < 0)
        v = 0;
    else if (0x7FFF < v)
        v = 0x7FFF;
    func_0021CFA0(0, static_cast<s16>(v), static_cast<s16>(v));
}

// EE: 0x00216A80
// Extra ambient voice (missions 6, 9, 10; bank 10 sound 0xA3 on rec +0x50): level +0xBC rises by 8
// / falls by 1 per frame toward n / 64, clamped to 0..255.
// fpucheck: verified 2
void func_00216A80(u32 rec, s32 n) {
    const f32 target = fpu::IntToFloat(n) * 0.015625f;
    f32 v = F(rec + 0xBC);
    if (v < target)
        v = fpu::Add(v, 8.0f);
    else if (target < v)
        v = fpu::Sub(v, 1.0f);
    if (v < 0.0f)
        v = 0.0f;
    else if (255.0f < v)
        v = 255.0f;
    GRef<f32>(rec + 0xBC) = v;
    const s16 s = static_cast<s16>(fpu::FloatToInt(v));
    if ((W(rec + 0xB4) & 8u) == 0) {
        PlaySound(10, 0xA3, s, s, rec + 0x50);
        GRef<u32>(rec + 0xB4) = W(rec + 0xB4) | 8u;
    } else {
        SetSoundVolume(s, s, rec + 0x50);
    }
}

// EE: 0x00216B80
// Per-frame engine and ambient sound of a player (info: {player, input curve, extra level, ...,
// +0x10 engine input (0x50 bytes), +0x40 velocity, +0x50 rotation; +0x1C input volume}).
void func_00216B80(u32 info) {
    const s32 p = GRef<s32>(info);
    const s32 idx = GRef<s32>(info + 4);
    const f32 k = F(info + 0x1C);
    func_00216470(p, info + 0x10);
    const u32 amb = 0x0033D7C0 + static_cast<u32>(p) * 0xC0;
    func_002167B0(amb, info + 0x40, info + 0x50);
    func_002169B8(k, amb, idx);
    const u8 m = Game(0x27);
    if (m == 6 || m == 9 || m == 10) func_00216A80(amb, GRef<s32>(info + 8));
}

// EE: 0x00216CA0
// Clears the 3-slot positional table (count 0x004241C0, used flags 0x004241C4..CC).
void func_00216CA0() {
    GRef<u32>(0x004241C0) = 0;
    for (s32 i = 2; i >= 0; --i) GRef<u32>(0x004241C4 + 4u * static_cast<u32>(i)) = 0;
}

// EE: 0x00216CE0
// Registers rec in the positional table (slot = count, at most 2; the first one also sets the
// listener position 0x004241D0 from rec +0x10); rec +4 = slot.
void func_00216CE0(u32 rec) {
    s32 n = GRef<s32>(0x004241C0);
    if (n == 0)
        sceVu0CopyVector(0x004241D0, rec + 0x10);
    else if (!(n < 3))
        n = 2;
    const u32 used = 0x004241C4 + 4u * static_cast<u32>(n);
    if (W(used) == 0) GRef<u32>(used) = 1;
    GRef<s32>(rec + 4) = n;
    GRef<u32>(0x004241E0 + 4u * static_cast<u32>(n)) = rec;
    GRef<s32>(0x004241C0) = GRef<s32>(0x004241C0) + 1;
}

// EE: 0x002170C8
// Selects the sequence engine sound (index into 0x00406900) and resets its state.
void func_002170C8(s32 on) {
    GRef<s32>(0x00424238) = on;
    GRef<u32>(0x00424240) = 0;
    GRef<u32>(0x0042423C) = 0;
}

// EE: 0x002170E0
// Sequence engine sound (voice 0x004241F0; {bank, sound} pairs 0x00406900, view factors
// 0x00406910): state 0 records u +4 as the start, then (once `fast`) the sound starts at level
// (u +4 - start + 255) clamped to 0..255 x the view factor; state 2 re-levels it every frame.
// fpucheck: verified 4
void func_002170E0(u32 u, s32 fast, s32 view) {
    const u32 st = 0x0042423C;
    const auto level = [&] {
        f32 v = fpu::Add(fpu::Sub(F(u + 4), F(0x00424240)), 255.0f);
        if (255.0f < v)
            v = 255.0f;
        else if (v < 0.0f)
            v = 0.0f;
        return static_cast<s16>(fpu::FloatToInt(v * F(0x00406910 + 4u * static_cast<u32>(view))));
    };
    const s32 s = GRef<s32>(st);
    if (s == 0) {
        GRef<u32>(0x00424240) = W(u + 4);
        GRef<s32>(st) = 1;
    } else if (s == 2) {
        const s16 v = level();
        SetSoundVolume(v, v, 0x004241F0);
        return;
    } else if (s != 1) {
        return;
    }
    if (fast == 0) return;
    const u32 e = 0x00406900 + 8u * W(0x00424238);
    const s16 v = level();
    PlaySound(static_cast<s16>(GRef<s32>(e)), static_cast<s16>(GRef<s32>(e + 4)), v, v, 0x004241F0);
    GRef<s32>(st) = 2;
}

// EE: 0x002172A8
// Stops the sequence engine sound.
void func_002172A8() { StopSound(0x004241F0); }

// EE: 0x00217B40
// SOUND.IRX status byte (0x00424FD0 [channel]) of the voice whose handle is rec +4 (found in the
// 48-entry voice table 0x00424250); -1 without a handle or when not found.
s32 func_00217B40(u32 rec) {
    const u32 h = W(rec + 4);
    if (h == 0) return -1;
    s32 ch = -1;
    for (u32 i = 0; i < 0x30; ++i) {
        const u32 e = 0x00424250 + i * 0x48;
        if (W(e + 4) == h) {
            ch = GRef<s8>(e + 0xE);
            break;
        }
    }
    if (ch == -1) return -1;
    return GRef<u8>(0x00424FD0 + static_cast<u32>(ch));
}

// ---- Event and positional sounds -------------------------------------------------------------------

// EE: 0x00219188
// Queues one-shot event sound `kind` at pos: kinds 3..5 in pool 0x0034EB90 (2 entries), 0xF..0x12
// in pool 0x0034EFB0, the others up to 0x12 in pool 0x0034E770 (8 entries each); entries
// {+0x50 position, +0x60 kind, +0x68 state 0, +0x6C delay -99}. Returns 0 for unknown kinds.
s32 PlayEventSe(s32 kind, u32 pos) {
    const u32 k = static_cast<u32>(kind) & 0xFF;
    u32 pool;
    s32 size;
    if (k - 3 < 3) {
        pool = 0x0034EB90;
        size = 2;
    } else if (k - 0xF < 4) {
        pool = 0x0034EFB0;
        size = 8;
    } else if (k < 0x13) {
        pool = 0x0034E770;
        size = 8;
    } else {
        return 0;
    }
    const u32 e = pool + 0x20 + (W(pool) << 7);
    GRef<u32>(e + 0x60) = k;
    GRef<s32>(e + 0x6C) = -99;
    GRef<u32>(e + 0x68) = 0;
    sceVu0CopyVector(e + 0x50, pos);
    const s32 n = GRef<s32>(pool) + 1;
    GRef<s32>(pool) = n;
    if (!(n < size)) GRef<u32>(pool) = 0;
    return 1;
}

// EE: 0x00219330
// Event sounds per frame for the listener cam (3 pools: counts 0x0034E620, pointers 0x0034E630;
// pool {+4 band widths x 3, +0x10 sound speed}): a new event measures its distance (dropped above
// 30000; kind 0xB is immediate) and waits distance / speed frames; then it plays the sound of its
// distance band (table 0x0034E640 + 16 x kind: banks +0..2, sounds +3..5, band limits +8 / +0xC;
// law 0x41, middle band 0x50) with gain 255 - excess / band width, panned by the angle to the
// camera (0x00219850 / 0x00219900 / 0x00219998), pitched down by distance / 10.
void func_00219330(u32 cam) {
    guest::GuestFrame fr(0x10);
    const u32 lr = fr.Address(0);
    for (u32 k = 0; k < 3; ++k) {
        const s32 n = GRef<s32>(0x0034E620 + 4 * k);
        const u32 pool = W(0x0034E630 + 4 * k);
        for (s32 i = 0; i < n; ++i) {
            const u32 e = pool + 0x20 + (static_cast<u32>(i) << 7);
            s32 state = GRef<s32>(e + 0x68);
            const s32 kind = GRef<s32>(e + 0x60);
            if (state == 0) {
                const s32 d = fpu::FloatToInt(func_00104F10(e + 0x50, cam));
                const s32 spd = GRef<s32>(pool + 0x10);
                const s32 delay = spd == 0 ? 0 : d / spd;
                if (kind == 0xB) {
                    GRef<u32>(e + 0x6C) = 0;
                    state = 2;
                    GRef<s32>(e + 0x68) = 2;
                } else if (d < 0x7531) {
                    GRef<s32>(e + 0x70) = d;
                    GRef<s32>(e + 0x6C) = delay;
                    state = 2;
                    GRef<s32>(e + 0x68) = 2;
                } else {
                    state = 1;
                    GRef<s32>(e + 0x68) = 1;
                }
            }
            if (state != 2) continue;
            const s32 left = GRef<s32>(e + 0x6C) - 1;
            GRef<s32>(e + 0x6C) = left;
            if (left >= 0) continue;
            const u32 tab = 0x0034E640 + static_cast<u32>(kind) * 16;
            const s32 d = GRef<s32>(e + 0x70);
            s32 band = 1, law = 0x41, gain;
            u32 bank, se;
            if (kind == 0xB) {
                se = GRef<u8>(tab + 4);
                gain = 0xFF;
                bank = GRef<u8>(tab + 1);
            } else {
                s32 x;
                if (d < GRef<s32>(tab + 8)) {
                    x = d;
                    band = 0;
                } else {
                    x = d - GRef<s32>(tab + 8);
                    if (d < GRef<s32>(tab + 0xC)) {
                        law = 0x50;
                    } else {
                        x = d - GRef<s32>(tab + 0xC);
                        band = 2;
                    }
                }
                bank = GRef<u8>(tab + static_cast<u32>(band));
                se = GRef<u8>(tab + static_cast<u32>(band) + 3);
                gain = 0xFF - x / GRef<s32>(pool + 4 + 4u * static_cast<u32>(band));
            }
            func_00219900(func_00219850(e + 0x50, cam, cam + 0x10), lr, lr + 4);
            f32 r = func_00219998(F(lr + 4), gain, law);
            GRef<f32>(lr + 4) = r;
            if (r < 0.0f) {
                r = -r;
                GRef<f32>(lr + 4) = r;
            }
            f32 l = func_00219998(F(lr), gain, law);
            GRef<f32>(lr) = l;
            if (l < 0.0f) {
                l = -l;
                GRef<f32>(lr) = l;
            }
            PlaySound(static_cast<s16>(bank), static_cast<s16>(se), static_cast<s16>(fpu::FloatToInt(l)),
                      static_cast<s16>(fpu::FloatToInt(F(lr + 4))), e);
            func_0021A0F0(e, -d / 10, 0);
            GRef<s32>(e + 0x68) = 1;
        }
    }
}

// EE: 0x00219668
// Plays the fixed sound n (bank / sound pairs 0x00410D18) at full volume on a temporary record.
void func_00219668(s32 n) {
    guest::GuestFrame rec(0x50);
    const u32 e = 0x00410D18 + 2u * static_cast<u32>(n);
    PlaySound(GRef<u8>(e), GRef<u8>(e + 1), 0xFF, 0xFF, rec.Address());
}

// EE: 0x002196E0
// PlaySound on the fixed record 0x0042A450; 1 instead of 0 on success.
s32 func_002196E0(s16 bank, s16 se, s16 left, s16 right) {
    const s32 r = PlaySound(bank, se, left, right, 0x0042A450);
    return r == 0 ? 1 : r;
}

// EE: 0x00219728
// func_0021D188 (full volume) into rec; 1 instead of 0 on success.
s32 func_00219728(s32 bank, s32 se, u32 rec) {
    const s32 r = func_0021D188(static_cast<s16>(bank), static_cast<s16>(se), rec);
    return r == 0 ? 1 : r;
}

// EE: 0x00219998
// Channel volume from a pan gain f (-1..1) and a distance gain g with law b: positive pans give
// f g + b clamped to 0..255, others f g - b clamped to -255..0; 0 when g + b <= 0.
// fpucheck: verified 2
f32 func_00219998(f32 f, s32 g, s32 b) {
    if (g + b <= 0) return 0.0f;
    if (0.0f < f) {
        const f32 v = fpu::Add(f * fpu::IntToFloat(g), fpu::IntToFloat(b));
        if (255.0f < v) return 255.0f;
        if (v < 0.0f) return 0.0f;
        return v;
    }
    const f32 v = fpu::Sub(f * fpu::IntToFloat(g), fpu::IntToFloat(b));
    if (v < -255.0f) return -255.0f;
    if (0.0f < v) return 0.0f;
    return v;
}

// EE: 0x00219A40
// Left / right volumes of a sound at `to` heard from `from` (angles) at distance dist: gain 255 -
// dist / div with law `law` (0x00219998), both made positive.
// fpucheck: verified 1
void func_00219A40(f32 dist, u32 out_l, u32 out_r, s32 div, s32 law, u32 to, u32 from, u32 angles) {
    const f32 a = func_00219850(to, from, angles);
    const s32 g = fpu::FloatToInt(fpu::Sub(255.0f, dist / fpu::IntToFloat(div)));
    guest::GuestFrame fr(0x10);
    const u32 lr = fr.Address(0);
    func_00219900(a, lr, lr + 4);
    f32 l = func_00219998(F(lr), g, law);
    GRef<f32>(lr) = l;
    f32 r = func_00219998(F(lr + 4), g, law);
    GRef<f32>(lr + 4) = r;
    if (l < 0.0f) {
        l = -l;
        GRef<f32>(lr) = l;
    }
    if (r < 0.0f) {
        r = -r;
        GRef<f32>(lr + 4) = r;
    }
    GRef<f32>(out_l) = l;
    GRef<f32>(out_r) = r;
}

// EE: 0x00219F28
// Forward direction (0, 0, 1, 0) turned by the angles.
void func_00219F28(u32 out, u32 angles) {
    guest::GuestFrame fr(0x90);
    const u32 m0 = fr.Address(0), m = fr.Address(0x40), v = fr.Address(0x80);
    for (u32 o = 0; o < 0x10; o += 4) GRef<u32>(v + o) = 0;
    GRef<f32>(v + 8) = 1.0f;
    sceVu0UnitMatrix(m0);
    func_00102D28(m, m0, angles);
    sceVu0ApplyMatrix(out, m, v);
}

// EE: 0x00219FB0
// Length of the xyz part of v (libm double sqrt 0x002849A8 of the float sum, then __truncdfsf2;
// both correctly rounded, as on the host).
// fpucheck: verified 2
f32 func_00219FB0(u32 v) {
    const f32 s = fpu::Add(fpu::Add(F(v) * F(v), F(v + 4) * F(v + 4)), F(v + 8) * F(v + 8));
    return static_cast<f32>(std::sqrt(static_cast<double>(s)));
}

// EE: 0x00219FF8
// Angle between the facing of `angles` and the direction from a to c (acosf of the normalised dot
// product).
f32 func_00219FF8(u32 a, u32 /*unused*/, u32 c, u32 angles) {
    guest::GuestFrame fr(0x20);
    const u32 dir = fr.Address(0), d = fr.Address(0x10);
    func_00219F28(dir, angles);
    sceVu0SubVector(d, c, a);
    const f32 dot = sceVu0InnerProduct(d, dir);
    return ee::acosf(dot / func_00219FB0(d));
}

// EE: 0x0021A0F0
// Shifts the pitch of rec (base +0x30/+0x31, or the current +0x32/+0x33 when `current`) by d fine
// steps (128 per note, note clamped to 0..0x7F) and sets it.
void func_0021A0F0(u32 rec, s32 d, s32 current) {
    s32 fine, note;
    if (current != 0) {
        fine = GRef<s8>(rec + 0x33);
        note = GRef<u8>(rec + 0x32);
    } else {
        fine = GRef<u8>(rec + 0x31);
        note = GRef<u8>(rec + 0x30);
    }
    fine += d;
    if (!(fine < 0x80)) {
        const s32 q = (fine < 0 ? fine + 0x7F : fine) >> 7;
        note += q;
        fine -= q << 7;
    } else if (fine < -0x80) {
        const s32 a = -fine;
        const s32 q1 = (a < 0 ? a + 0x7F : a) >> 7;
        const s32 q2 = (fine < 0 ? fine + 0x7F : fine) >> 7;
        note += q2;
        fine = (q1 << 7) - a;
    }
    if (!(note < 0x80))
        note = 0x7F;
    else if (note < 0)
        note = 0;
    SetSoundPitch(static_cast<u8>(note), static_cast<s8>(fine), rec);
}

// EE: 0x0021A8E8
// Doppler pitch: the pitch pair (note in the high byte, fine 0..0x7F) as one value scaled by
// 1 + k x 60 / (0x00410D24 x 0x00410D28 x 0x154), k capped at 0x00410D20.
// fpucheck: verified 1
u16 func_0021A8E8(f32 k, s16 pitch) {
    const f32 lim = fpu::IntToFloat(GRef<s32>(0x00410D20));
    if (lim < k) k = lim;
    const s32 pv = ((static_cast<s32>(pitch) & 0xFF00) >> 1) | (static_cast<s32>(pitch) & 0x7F);
    const f32 fp = fpu::IntToFloat(pv);
    const s32 den = GRef<s32>(0x00410D24) * (GRef<s32>(0x00410D28) * 0x154);
    const s16 v = static_cast<s16>(fpu::FloatToInt(fpu::Add(fp, fp * k * 60.0f / fpu::IntToFloat(den))));
    return static_cast<u16>(static_cast<s16>(((v & 0x7F80) << 1) | (v & 0x7F)));
}

// EE: 0x0021AE30
// Queues a positional sound event ev {+0 list, +4 group, +8 variant, +0x10 position} in list
// ev&1 (0x00351E50 + 0x110 x list, 8 entries {u16 bank, u16 sound, ..., +0x10 position}; ids from
// 0x00351D80 + 12 x group + 4 x variant). 0 when the list is full.
s32 func_0021AE30(u32 ev) {
    const u32 list = 0x00351E50 + (W(ev) & 1u) * 0x110;
    const s32 n = GRef<s32>(list);
    if (!(n < 8)) return 0;
    const u32 e = list + 0x10 + (static_cast<u32>(n) << 5);
    const u32 id = W(ev + 4) * 12 + W(ev + 8) * 4 + 0x00351D80;
    GRef<u16>(e + 2) = GRef<u16>(id + 2);
    GRef<u16>(e) = GRef<u16>(id);
    sceVu0CopyVector(e + 0x10, ev + 0x10);
    GRef<s32>(list) = GRef<s32>(list) + 1;
    return 1;
}

// EE: 0x0021AF18
// Plays and clears positional list k for the listener cam (gain over distance / 50, law 0x32, on
// the shared record 0x0042A930).
void func_0021AF18(s32 k, u32 cam) {
    const u32 list = 0x00351E50 + static_cast<u32>(k) * 0x110;
    const s32 n = GRef<s32>(list);
    guest::GuestFrame fr(0x10);
    const u32 lr = fr.Address(0);
    for (s32 i = 0; i < n; ++i) {
        const u32 e = list + 0x10 + (static_cast<u32>(i) << 5);
        const f32 d = func_00104F10(e + 0x10, cam);
        func_00219A40(d, lr, lr + 4, 0x32, 0x32, e + 0x10, cam, cam + 0x10);
        PlaySound(GRef<s16>(e), GRef<s16>(e + 2), static_cast<s16>(fpu::FloatToInt(F(lr))), static_cast<s16>(fpu::FloatToInt(F(lr + 4))),
                  0x0042A930);
    }
    GRef<u32>(list) = 0;
}

// ---- 3D listener -----------------------------------------------------------------------------------

// EE: 0x0021B128
// Assigns the emitters (pointers to {id, kind, vel, rot, pos}) to listener L's voice slots (4
// aircraft slots at +0x130 for kind 0, 5 missile slots at +0x5F0 for kind 1; 0x130 bytes each:
// +0xF0 vel, +0x100 pos, +0x110 rot, +0x124 id, +0x128 kind, +0x12C new): an emitter keeps the slot
// already holding its id (positions refreshed); the others take free slots, marked new.
s32 func_0021B128(u32 L, u32 ptrs, s32 kind) {
    u32 base;
    s32 n;
    if (kind == 0) {
        base = L + 0x130;
        n = 4;
    } else {
        base = L + 0x5F0;
        n = kind == 1 ? 5 : 0;
    }
    const u32 slot_of = 0x0042A980, src_of = 0x0042A9A0;
    for (s32 i = 0; i < n; ++i) {
        GRef<s32>(src_of + 4u * static_cast<u32>(i)) = -1;
        GRef<s32>(slot_of + 4u * static_cast<u32>(i)) = -1;
    }
    for (s32 i = 0; i < n; ++i) {
        const s32 id = GRef<s32>(W(ptrs + 4u * static_cast<u32>(i)));
        if (id == -1) continue;
        for (s32 j = 0; j < n; ++j) {
            const u32 s = base + static_cast<u32>(j) * 0x130;
            if (id != GRef<s32>(s + 0x124)) continue;
            GRef<s32>(src_of + 4u * static_cast<u32>(j)) = i;
            GRef<s32>(slot_of + 4u * static_cast<u32>(i)) = j;
            const u32 r = W(ptrs + 4u * static_cast<u32>(i));
            GRef<u8>(s + 0x12C) = 0;
            sceVu0CopyVector(s + 0xF0, W(r + 8));
            sceVu0CopyVector(s + 0x110, W(r + 0xC));
            sceVu0CopyVector(s + 0x100, W(r + 0x10));
            break;
        }
    }
    for (s32 i = 0; i < n; ++i) {
        if (GRef<s32>(slot_of + 4u * static_cast<u32>(i)) != -1) continue;
        for (s32 j = 0; j < n; ++j) {
            if (GRef<s32>(src_of + 4u * static_cast<u32>(j)) != -1) continue;
            GRef<s32>(slot_of + 4u * static_cast<u32>(i)) = j;
            GRef<s32>(src_of + 4u * static_cast<u32>(j)) = i;
            const u32 r = W(ptrs + 4u * static_cast<u32>(i));
            const u32 s = base + static_cast<u32>(j) * 0x130;
            GRef<u8>(s + 0x12C) = 1;
            const s32 id = GRef<s32>(r);
            GRef<s32>(s + 0x124) = id;
            GRef<u32>(s + 0x128) = W(r + 4);
            if (id != -1) {
                sceVu0CopyVector(s + 0xF0, W(r + 8));
                sceVu0CopyVector(s + 0x110, W(r + 0xC));
                sceVu0CopyVector(s + 0x100, W(r + 0x10));
            }
            break;
        }
    }
    return 1;
}

// EE: 0x0021B3A8
// Aircraft emitters of player p's listener (0x0042AA10 + 0x1020 p).
void func_0021B3A8(s32 p, u32 ptrs) { func_0021B128(0x0042AA10 + static_cast<u32>(p) * 0x1020, ptrs, 0); }

// EE: 0x0021B3D8
// Missile emitters of player p's listener.
void func_0021B3D8(s32 p, u32 ptrs) { func_0021B128(0x0042AA10 + static_cast<u32>(p) * 0x1020, ptrs, 1); }


// EE: 0x0021B408
// Aircraft engine voices of listener info->L (info: {player, code, listener pos, ..., +0x10 / +0x14
// camera / angles, +0x18 L}). For each of the 4 aircraft slots (L +0x130 + 0x130 n: voice at +0,
// vel +0xF0, pos +0x100, rot +0x110, prev distance +0x120, id +0x124, kind +0x128, new +0x12C):
// the sound set (0x00352130 + 0x140 x kind; in sub-state 0x11 the players use set 1, or set 10 with
// their own engine bank 0x0033D7C0 [id] +0xAC while flagged 0x01000000) gives volume (distance band
// +4, doubled in sub-state 0x11, gain +0xC..+0x10 by the angle off the nose 0x00219FF8) and law +8.
// A new emitter or a code change restarts the voice (and in sub-state 0x11 the player's cockpit
// voices L +0x50 / +0xE0); else it is re-levelled with Doppler (0x0021DFC8) and, for the players in
// sub-state 0x11, scaled by the altitude over the terrain and the forward speed (status flag
// 0x10000000), with the sonic-boom / touch-down effects and the SPU input volume from the
// altitude; then the fly-by sounds 0x0021CA80. Per active slot an entry at L +0xBE0 + 0x110 n keeps
// {+0xE8 id, +0xEC pan angle, +0xF0 angle, +0xF4 distance, +0xF8 previous, +0xFC speed, +0x100 set};
// the two nearest get the wind voice (bank 10 sound 0x7D) and the two layered engine voices
// (+0x50 / +0xA0) of their set.
// fpucheck: verified 25
void func_0021B408(u32 info) {
    const u32 L = W(info + 0x18);
    const u32 lpos = W(info + 8), cam_a = W(info + 0x10), cam_b = W(info + 0x14);
    const bool sub11 = Ctx()->sub == 0x11;
    const u32 code = W(info + 4) & 0x7Fu;
    s32 restart = 0;
    if (GRef<s16>(L) != static_cast<s32>(code))
        restart = 1;
    else if (sub11)
        restart = (W(info + 4) & 0x80u) != 0 ? 1 : 0;
    s32 n_active = 0, next = 1, spu_set = 0, player_seen = 0;
    guest::GuestFrame fr(0x40);
    const u32 lr = fr.Address(0x10);  // sp+0x10 / +0x14
    const u32 order = fr.Address(0), w1 = fr.Address(0x18), e1 = fr.Address(0x20), e2 = fr.Address(0x28);
    const auto amb_bank = [](s32 id) { return GRef<s16>(0x0033D7C0 + static_cast<u32>(id) * 0xC0 + 0xAC); };
    for (s32 slot = 0; slot < 4; ++slot) {
        const u32 s = L + 0x130 + static_cast<u32>(slot) * 0x130;
        const s32 id = GRef<s32>(s + 0x124);
        const bool player = static_cast<u32>(id) < 2;
        if (sub11) player_seen = player ? 1 : player_seen;
        if (id == -1) {
            StopSound(s);
            continue;
        }
        const u32 pos = s + 0x100, rot = s + 0x110;
        const f32 prev_speed = player ? F(L + 4 + 4u * static_cast<u32>(id)) : 0.0f;
        const u32 ent = L + 0xBE0 + static_cast<u32>(n_active) * 0x110;
        u32 set;
        if (sub11 && player) {
            const u32 u = Unit(id);
            if ((W(u + 0x4C) & 0x01000000u) != 0) {
                GRef<u16>(L + 2) = static_cast<u16>(GRef<u16>(L + 2) & ~(1u << (id & 0x1F)));
                GRef<u32>(s + 0x128) = 10;
                set = 0x00352DB0;
                const u16 b = GRef<u16>(0x0033D7C0 + static_cast<u32>(id) * 0xC0 + 0xAC);
                GRef<u16>(0x00352DB0) = b;
                GRef<u16>(set + 0x14) = b;
            } else {
                u16 v;
                if ((1u << (id & 0x1F)) == 0) {  // never true for ids 0 / 1
                    if ((GRef<s16>(L + 2) & 1) != 0) GRef<u8>(s + 0x12C) = 1;
                    v = GRef<u16>(L + 2);
                } else {
                    v = GRef<u16>(L + 2);
                }
                GRef<u32>(s + 0x128) = 1;
                GRef<u16>(L + 2) = static_cast<u16>(v | (1u << (id & 0x1F)));
                set = 0x00352270;
            }
        } else {
            set = 0x00352130 + GRef<u32>(s + 0x128) * 0x140;
        }
        const s32 law = GRef<s32>(set + 8);
        f32 band = F(set + 4);
        if (sub11) band = fpu::Add(band, band);
        const f32 dist = func_00104F10(pos, lpos);
        const f32 angle = func_00219FF8(cam_a, cam_b, pos, rot);
        const f32 pan = func_00219850(pos, cam_a, cam_b);
        f32 gain = fpu::Sub(255.0f, dist / band);
        f32 speed;
        if (player) {
            sceVu0UnitMatrix(0x0042A9C0);
            func_00102D28(0x0042A9C0, 0x0042A9C0, rot);
            sceVu0InversMatrix(0x0042A9C0, 0x0042A9C0);
            sceVu0ApplyMatrix(0x0042AA00, 0x0042A9C0, s + 0xF0);
            speed = F(0x0042AA08);
        } else {
            speed = 0.0f;
        }
        GRef<f32>(ent + 0xEC) = pan;
        GRef<f32>(ent + 0xF0) = angle;
        GRef<f32>(ent + 0xF4) = dist;
        GRef<s32>(ent + 0xE8) = id;
        GRef<f32>(ent + 0xFC) = speed;
        GRef<u32>(ent + 0xF8) = W(s + 0x120);
        GRef<u32>(ent + 0x100) = set;
        const f32 g0 = F(set + 0xC), g1 = F(set + 0x10);
        const f32 gd = fpu::Sub(g0, g1);
        if (gd == 0.0f)
            gain = gain * g0;
        else
            gain = gain * fpu::Add(fpu::Sub(F(0x0040FFC0), angle) * gd / F(0x0040FFC0), g1);
        const s32 g = fpu::FloatToInt(gain);
        func_00219900(pan, lr, lr + 4);
        GRef<f32>(lr) = func_00219998(F(lr), g, law);
        GRef<f32>(lr + 4) = func_00219998(F(lr + 4), g, law);
        const auto lvol = [&] { return static_cast<s16>(fpu::FloatToInt(F(lr))); };
        const auto rvol = [&] { return static_cast<s16>(fpu::FloatToInt(F(lr + 4))); };
        if (restart != 0 || GRef<s8>(s + 0x12C) != 0) {
            StopSound(s);
            PlaySound(GRef<u8>(set), GRef<u8>(set + 2), lvol(), rvol(), s);
            if (sub11 && player) {
                if ((StatusFlags() & 0x10000000u) != 0) {
                    StopSound(L + 0x50);
                    PlaySound(amb_bank(id), 0, 0, 0, L + 0x50);
                    s32 v = fpu::FloatToInt(fpu::Sub(1536.0f, speed * 64.0f));
                    v = v < 0 ? 0 : (v < 0x601 ? v : 0x600);
                    func_0021A0F0(s, -v, 0);
                }
                StopSound(L + 0xE0);
                PlaySound(amb_bank(id), 8, 0, 0, L + 0xE0);
                GRef<f32>(L + 4 + 4u * (static_cast<u32>(id) & 1)) = speed;
            }
            GRef<u8>(s + 0x12D) = 0;
            GRef<u8>(s + 0x12E) = 0;
            GRef<u16>(ent + 0x106) = 0;
            GRef<u16>(ent + 0x104) = 0;
        } else if (sub11 && player) {
            f32 h = func_0013E338(Terrain(), pos);
            const f32 y = F(s + 0x104);
            const u32 st = StatusFlags();
            if ((st & 0x01000000u) != 0) {
                if (-960.0f < h) h = -960.0f;
            } else if ((st & 0x02000000u) != 0) {
                h = -96.0f;
            }
            const f32 alt = fpu::Sub(y, h);
            if ((StatusFlags() & 0x10000000u) != 0) {
                f32 a = fpu::Sub(1.0f, fpu::Sub(-alt, 1000.0f) / 100.0f);
                f32 b = fpu::Sub(1.0f, fpu::Sub(speed, 4.0f) * 0.125f);
                if (a < 0.0f)
                    a = 0.0f;
                else if (1.0f < a)
                    a = 1.0f;
                if (b < 0.0f) b = 0.0f;
                f32 l = F(lr) * b * a;
                if (255.0f < l)
                    l = 255.0f;
                else if (l < 0.0f)
                    l = 0.0f;
                f32 r = F(lr + 4) * b * a;
                if (255.0f < r)
                    r = 255.0f;
                else if (r < 0.0f)
                    r = 0.0f;
                SetSoundVolume(static_cast<s16>(fpu::FloatToInt(l)), static_cast<s16>(fpu::FloatToInt(r)), L + 0x50);
                s32 v = fpu::FloatToInt(fpu::Sub(1536.0f, speed * 64.0f));
                v = v < 0 ? 0 : (v < 0x601 ? v : 0x600);
                func_0021A0F0(L + 0x50, 0x400 - v, 0);
                if (speed < F(0x0040FFC4) && F(0x0040FFC4) <= prev_speed && -30.0f < alt)
                    func_002196E0(amb_bank(id), 7, lvol(), rvol());
                s16 se;
                s32 fine, lim;
                if ((StatusFlags() & 0x01000000u) != 0) {
                    fine = 0;
                    se = 0x94;
                    lim = 11;
                } else {
                    se = 0x93;
                    fine = 300;
                    lim = 10;
                }
                const f32 fl = fpu::IntToFloat(lim);
                if (fl <= speed)
                    GRef<u32>(0x00410D2C) = 0;
                else if (fl <= prev_speed)
                    GRef<u32>(0x00410D2C) = 1;
                if (-30.0f < alt && W(0x00410D2C) == 1) {
                    StopSound(L + 0x98);
                    PlaySound(10, se, lvol(), rvol(), L + 0x98);
                    func_0021A0F0(L + 0x98, fine, 0);
                    GRef<u32>(0x00410D2C) = 0;
                }
                if (speed < 10.0f && -30.0f < alt) {
                    const f32 k = fpu::Add(speed * 0.5f / 10.0f, 0.5f);
                    GRef<f32>(lr + 4) = F(lr + 4) * k;
                    GRef<f32>(lr) = F(lr) * k;
                }
            }
            if (code == 0x13 || code == 1)
                SetSoundVolume(200, 200, L + 0xE0);
            else
                SetSoundVolume(0, 0, L + 0xE0);
            if (SetSoundVolume(lvol(), rvol(), s) != -1) {
                func_0021DFC8(fpu::Sub(F(s + 0x120), dist), s);
                if (spu_set == 0) {
                    s32 v = fpu::FloatToInt(fpu::Add(alt, F(0x0040FFC8)) * F(0x0040FFCC) / F(0x0040FFC8));
                    if (v < 0)
                        v = 0;
                    else if (0x7FFF < v)
                        v = 0x7FFF;
                    spu_set = 1;
                    func_0021CFA0(0, static_cast<s16>(v), static_cast<s16>(v));
                }
            }
            GRef<f32>(L + 4 + 4u * (static_cast<u32>(id) & 1)) = speed;
            func_0021CA80(dist, angle, info, s);
        } else {
            if (SetSoundVolume(lvol(), rvol(), s) != -1) func_0021DFC8(fpu::Sub(F(s + 0x120), dist), s);
            func_0021CA80(dist, angle, info, s);
        }
        n_active = next;
        GRef<f32>(s + 0x120) = dist;
        ++next;
    }
    if (player_seen == 0) StopSound(L + 0xE0);
    for (s32 i = 3; i >= 0; --i) GRef<s32>(order + 4u * static_cast<u32>(i)) = i;
    const auto ent_of = [L](s32 k) { return L + 0xBE0 + static_cast<u32>(k) * 0x110; };
    for (s32 i = 0; i < n_active; ++i) {
        for (s32 j = i; j < n_active; ++j) {
            const u32 oi = order + 4u * static_cast<u32>(i), oj = order + 4u * static_cast<u32>(j);
            const s32 a = GRef<s32>(oi), b = GRef<s32>(oj);
            if (F(ent_of(b) + 0xF4) < F(ent_of(a) + 0xF4)) {
                GRef<s32>(oi) = b;
                GRef<s32>(oj) = a;
            }
        }
    }
    for (s32 k = 0; k < 4; ++k) {
        const s32 o = GRef<s32>(order + 4u * static_cast<u32>(k));
        const u32 e = ent_of(o);
        const u32 set = W(e + 0x100);
        if (k < 2 && o < n_active) {
            const s32 law = GRef<s32>(set + 0x44);
            const s32 g = fpu::FloatToInt(fpu::Sub(255.0f, F(e + 0xF4) / F(set + 0x40)));
            func_00219900(F(e + 0xEC), w1, w1 + 4);
            GRef<f32>(w1) = func_00219998(F(w1), g, law);
            GRef<f32>(w1 + 4) = func_00219998(F(w1 + 4), g, law);
            if (F(w1) < 0.0f) GRef<f32>(w1) = -F(w1);
            if (F(w1 + 4) < 0.0f) GRef<f32>(w1 + 4) = -F(w1 + 4);
            const s16 l = static_cast<s16>(fpu::FloatToInt(F(w1))), r = static_cast<s16>(fpu::FloatToInt(F(w1 + 4)));
            if (GRef<s16>(e + 0x104) != 0) {
                SetSoundVolume(l, r, e);
            } else {
                StopSound(e);
                PlaySound(10, 0x7D, l, r, e);
                GRef<s16>(e + 0x104) = 1;
            }
            func_0021A0F0(e, fpu::FloatToInt(-F(e + 0xF4) / 1000.0f * 128.0f), 0);
        } else {
            StopSound(e);
            GRef<s16>(e + 0x104) = 0;
        }
    }
    for (s32 k = 0; k < 4; ++k) {
        const s32 o = GRef<s32>(order + 4u * static_cast<u32>(k));
        const u32 e = ent_of(o);
        const u32 set = W(e + 0x100);
        const u32 v1 = e + 0x50, v2 = e + 0xA0;
        if (k < 2 && o < n_active) {
            const f32 C = F(0x0040FFD0);
            const auto layer = [&](u32 t, bool rising, u32 out) {
                const f32 d = F(e + 0xF4) / F(t + 4);
                const f32 a = F(t + 0xC), b = F(t + 0x10);
                const f32 gd = fpu::Sub(a, b);
                f32 gain = fpu::Sub(255.0f, d);
                if (gd == 0.0f) {
                    gain = gain * a;
                } else {
                    const f32 w = rising ? F(e + 0xF0) : fpu::Sub(C, F(e + 0xF0));
                    gain = gain * fpu::Add(w * gd / C, b);
                }
                const s32 g = fpu::FloatToInt(gain);
                func_00219900(F(e + 0xEC), out, out + 4);
                const s32 law = GRef<s32>(t + 8);
                GRef<f32>(out) = func_00219998(F(out), g, law);
                GRef<f32>(out + 4) = func_00219998(F(out + 4), g, law);
            };
            layer(set + 0x14, true, e1);
            layer(set + 0x28, false, e2);
            const auto vol = [](u32 a) { return static_cast<s16>(fpu::FloatToInt(F(a))); };
            if (GRef<s16>(e + 0x106) == 0) {
                const s16 se1 = GRef<s16>(set + 0x16), bank1 = GRef<s16>(set + 0x14);
                StopSound(v1);
                PlaySound(bank1, se1, vol(e1), vol(e1 + 4), v1);
                StopSound(v2);
                PlaySound(GRef<s16>(set + 0x28), GRef<s16>(set + 0x2A), vol(e2), vol(e2 + 4), v2);
                GRef<s16>(e + 0x106) = 1;
            } else {
                if (SetSoundVolume(vol(e1), vol(e1 + 4), v1) != -1) {
                    func_0021DFC8(fpu::Sub(F(e + 0xF8), F(e + 0xF4)), v1);
                    func_0021A0F0(v1, -fpu::FloatToInt(F(e + 0xF4) / 10.0f), 1);
                }
                SetSoundVolume(vol(e2), vol(e2 + 4), v2);
                func_0021A0F0(v1, -fpu::FloatToInt(F(e + 0xF4) / 10.0f), 1);
            }
        } else {
            StopSound(v1);
            StopSound(v2);
            GRef<s16>(e + 0x106) = 0;
        }
    }
}

// EE: 0x0021C4F8
// Missile whoosh voices of listener info->L (5 slots at L +0x5F0, bank 10 sound 0x19 on slot
// +0x50): starts once a missile closes inside C 0x0040FFD4 (gain over distance / 30, law 0x50),
// re-levels while it stays, stops when it was inside C 0x0040FFD8 and leaves; a new missile with
// the code's bit 0x80 restarts the state.
void func_0021C4F8(u32 info) {
    const u32 L = W(info + 0x18);
    const u32 flag = (W(info + 4) >> 7) & 1u;
    const u32 lpos = W(info + 8), a = W(info + 0x10), b = W(info + 0x14);
    guest::GuestFrame fr(0x10);
    const u32 lr = fr.Address(0);
    for (u32 j = 0; j < 5; ++j) {
        const u32 s = L + 0x5F0 + j * 0x130;
        if (GRef<s32>(s + 0x124) == -1) continue;
        const f32 d = func_00104F10(s + 0x100, lpos);
        if (GRef<s8>(s + 0x12C) != 0 && flag != 0) {
            GRef<u8>(s + 0x12D) = 0;
            GRef<f32>(s + 0x120) = d;
            continue;
        }
        const f32 R = F(0x0040FFD4);
        if (d < R && R < F(s + 0x120)) {
            func_00219A40(d, lr, lr + 4, 0x1E, 0x50, s + 0x100, a, b);
            const s8 st = GRef<s8>(s + 0x12D);
            if (st == 0) {
                if (R <= F(s + 0x120)) {
                    PlaySound(10, 0x19, static_cast<s16>(fpu::FloatToInt(F(lr))), static_cast<s16>(fpu::FloatToInt(F(lr + 4))), s + 0x50);
                    GRef<u8>(s + 0x12D) = 1;
                }
            } else if (st == 1) {
                SetSoundVolume(static_cast<s16>(fpu::FloatToInt(F(lr))), static_cast<s16>(fpu::FloatToInt(F(lr + 4))), s + 0x50);
            }
        } else if (F(s + 0x120) < F(0x0040FFD8)) {
            StopSound(s + 0x50);
            GRef<u8>(s + 0x12D) = 2;
        }
        GRef<f32>(s + 0x120) = d;
    }
}

// EE: 0x0021C700
// 3D sound step of a listener: info +0x18 = listener 0x0042AA10 + 0x1020 x player; aircraft voices,
// missile voices; the listener keeps the code (info +4 & 0x7F).
void func_0021C700(u32 info) {
    const u32 L = 0x0042AA10 + W(info) * 0x1020;
    GRef<u32>(info + 0x18) = L;
    func_0021B408(info);
    func_0021C4F8(info);
    GRef<u16>(L) = static_cast<u16>(GRef<u16>(info + 4) & 0x7F);
}

// EE: 0x0021C760
// One fly-by layer of aircraft slot s heard by listener info (params P: {f32 trigger distance,
// voice, state byte, toggle, band entry T}): the band (3 = none) from the aircraft speed
// (0x00104F80 GetVectorLength; kinds 11/12 below 6 only none) and the distance change thresholds T +0x1C..;
// state 0 starts the band's sound (T +0 bank, +2 + 2 band sound, alternating variant by the toggle)
// once the aircraft is within the trigger distance (or for codes 0x11 / 0x15), state 1 re-levels it
// (gain over distance / T +8, law T +0x18) and pitches it by the distance change; state 2 = done.
// fpucheck: verified 3
void func_0021C760(f32 a, u32 info, u32 s, u32 P, s32 /*band*/) {
    const f32 spd = GetVectorLength(s + 0xF0);
    const u32 T = W(P + 0x10);
    const u32 th = T + 0x1C;
    const f32 delta = fpu::Sub(F(s + 0x120), a);
    const f32 trig = F(P);
    const u32 rec = W(P + 4), state = W(P + 8);
    const u32 code = W(info + 4) & 0x7Fu;
    const s32 div = GRef<s32>(T + 8), law = GRef<s32>(T + 0x18);
    s32 lv;
    if (static_cast<u32>(GRef<s32>(s + 0x128) - 0xB) < 2 && spd < 6.0f) {
        lv = 3;
    } else if (delta < F(th)) {
        lv = 3;
    } else if (!(spd < F(0x0040FFDC))) {
        if (delta < F(th + 4))
            lv = 0;
        else
            lv = delta < F(th + 8) ? 1 : 2;
    } else {
        lv = 3;
    }
    guest::GuestFrame fr(0x10);
    const u32 lr = fr.Address(0);
    const s8 st = GRef<s8>(state);
    if (st == 1) {
        const f32 k = F(T + 0xC + 4u * static_cast<u32>(lv));
        func_00219A40(a, lr, lr + 4, div, law, s + 0x100, W(info + 0x10), W(info + 0x14));
        func_0021A0F0(rec, fpu::FloatToInt(fpu::Sub(delta, F(th + 4u * static_cast<u32>(lv))) * (k * 0.0078125f)), 0);
        if (SetSoundVolume(static_cast<s16>(fpu::FloatToInt(F(lr))), static_cast<s16>(fpu::FloatToInt(F(lr + 4))), rec) != -1) return;
        GRef<u8>(state) = 2;
        return;
    }
    if (st != 0) return;
    bool go = false;
    if (a <= trig) go = trig < F(s + 0x120) || code == 0x11 || code == 0x15;
    if (!go) return;
    if (!(lv < 3)) {
        GRef<u8>(state) = 2;
        return;
    }
    const u32 tog = W(P + 0xC);
    const s16 t0 = GRef<s16>(tog);
    const f32 k = F(T + 0xC + 4u * static_cast<u32>(lv));
    s16 se = GRef<s16>(T + 2 + 2u * static_cast<u32>(lv));
    const s16 bank = GRef<s16>(T);
    if (t0 >= 0) {
        se = static_cast<s16>(se + t0);
        GRef<s16>(tog) = static_cast<s16>(1 - t0);
    }
    func_00219A40(a, lr, lr + 4, div, law, s + 0x100, W(info + 0x10), W(info + 0x14));
    StopSound(rec);
    PlaySound(bank, se, static_cast<s16>(fpu::FloatToInt(F(lr))), static_cast<s16>(fpu::FloatToInt(F(lr + 4))), rec);
    const s32 v = fpu::FloatToInt(fpu::Sub(delta, F(th + 4u * static_cast<u32>(lv))) * (k * 0.0078125f));
    func_0021A0F0(rec, fpu::FloatToInt(fpu::IntToFloat(v)), 0);
    GRef<u8>(state) = 1;
}

// EE: 0x0021CA80
// Fly-by sounds of aircraft slot s (band 0 below C 0x0040FFE0 of `angle`, 1 above C 0x0040FFE4,
// else 2): two layers of the kind's set (0x00352130 + 0x140 x kind: triggers +0x48 / +0xC4,
// toggles +0x3C / +0x3E, band entries +0x4C / +0xC8 + 0x28 x band) on voices s +0x50 / +0xA0
// with states s +0x12D / +0x12E.
void func_0021CA80(f32 dist, f32 angle, u32 info, u32 s) {
    s32 band;
    if (angle < F(0x0040FFE0))
        band = 0;
    else
        band = F(0x0040FFE4) < angle ? 1 : 2;
    const u32 T = 0x00352130 + GRef<u32>(s + 0x128) * 0x140;
    guest::GuestFrame fr(0x20);
    const u32 P = fr.Address(0);
    GRef<f32>(P) = F(T + 0x48);
    GRef<u32>(P + 4) = s + 0x50;
    GRef<u32>(P + 8) = s + 0x12D;
    GRef<u32>(P + 0xC) = T + 0x3C;
    GRef<u32>(P + 0x10) = T + 0x4C + static_cast<u32>(band) * 0x28;
    func_0021C760(dist, info, s, P, band);
    GRef<f32>(P) = F(T + 0xC4);
    GRef<u32>(P + 0xC) = T + 0x3E;
    GRef<u32>(P + 0x10) = T + 0xC8 + static_cast<u32>(band) * 0x28;
    GRef<u32>(P + 4) = s + 0xA0;
    GRef<u32>(P + 8) = s + 0x12E;
    func_0021C760(dist, info, s, P, band);
}

// EE: 0x0021CBB8
// Stops every voice of both listeners (aircraft slots and their two layers and fly-bys, the nearest
// entries' voices, the missile slots, the cockpit voices) and frees the aircraft slots.
void func_0021CBB8() {
    for (u32 p = 0; p < 2; ++p) {
        const u32 L = 0x0042AA10 + p * 0x1020;
        u32 s1 = L + 0x130, s2 = L + 0x180, s4 = L + 0xBE0, s3 = L + 0xC30;
        for (s32 i = 3; i >= 0; --i) {
            StopSound(s1);
            StopSound(s2);
            StopSound(s2 + 0x50);
            s2 += 0x130;
            StopSound(s4);
            s4 += 0x110;
            StopSound(s3);
            StopSound(s3 + 0x50);
            s3 += 0x110;
            GRef<s32>(s1 + 0x124) = -1;
            s1 += 0x130;
        }
        u32 m = L + 0x640;
        for (s32 i = 4; i >= 0; --i) {
            StopSound(m);
            m += 0x130;
        }
        StopSound(L + 0x50);
        StopSound(L + 0x98);
        StopSound(L + 0xE0);
    }
}

// EE: 0x0014D4D0
// Per-frame missile emitters for every player's listener: listener position / velocity from the
// scene camera (sub-state 0x11; velocity = change since the last frame, kept in unit list +0x60 +
// 16 p; no owner excluded) or the player unit; the 5 nearest live missiles (projectile pool
// +0x4000, not owned by the listener) become the records unit list +0x24 + 0x14 p {index, ...,
// +8 relative velocity (listener + missile, on this frame's stack), +0xC rotation, +0x10 missile},
// then 0x0021B3D8.
void func_0014D4D0() {
    for (u32 p = 0; p < Players(); ++p) {
        guest::GuestFrame fr(0xB0);
        const u32 dist = fr.Address(0), idx = fr.Address(0x20), rel = fr.Address(0x40), pos = fr.Address(0x90), vel = fr.Address(0xA0);
        u32 owner;
        if (Ctx()->sub == 0x11) {
            CopyQ(pos, Scene() + 0xB0);
            sceVu0SubVector(vel, pos, UnitList() + 0x60 + 16 * p);
            CopyQ(UnitList() + 16 * p + 0x60, pos);
            owner = 0xFF;
        } else {
            const u32 u = Unit(static_cast<s32>(p));
            CopyQ(pos, u);
            CopyQ(vel, u + 0x20);
            owner = p & 0xFF;
        }
        for (s32 k = 4; k >= 0; --k) {
            GRef<f32>(dist + 4u * static_cast<u32>(k)) = 262144.0f;
            GRef<s32>(idx + 4u * static_cast<u32>(k)) = -1;
        }
        for (u32 i = 0; i < 0x20; ++i) {
            const u32 pool = W(0x004106AC);
            if (((~W(pool + 0x8000 + 0xA28) >> i) & 1u) != 0) continue;
            const u32 m = pool + 0x4000 + i * 0x60;
            if ((W(m + 0x5C) & 1u) != 0) continue;
            if (GRef<u8>(m + 0x58) == owner) continue;
            const f32 d = func_00104F10(m, pos);
            for (s32 k = 4; k >= 0; --k) {
                const u32 dk = dist + 4u * static_cast<u32>(k), ik = idx + 4u * static_cast<u32>(k);
                const f32 cur = F(dk);
                if (!(d < cur)) break;
                if (k < 4) {
                    GRef<f32>(dk + 4) = cur;
                    GRef<u32>(ik + 4) = W(ik);
                }
                GRef<f32>(dk) = d;
                GRef<u32>(ik) = i;
            }
        }
        for (u32 k = 0; k < 5; ++k) {
            const u32 em = W(UnitList() + p * 0x14 + 4 * k + 0x24);
            const s32 n = GRef<s32>(idx + 4 * k);
            GRef<s32>(em) = n;
            if (n == -1) continue;
            const u32 m = W(0x004106AC) + 0x4000 + static_cast<u32>(n) * 0x60;
            GRef<u32>(em + 0x10) = m;
            sceVu0AddVector(rel + 0x10 * k, vel, m + 0x20);
            GRef<u32>(em + 8) = rel + 0x10 * k;
            GRef<u32>(em + 0xC) = m + 0x10;
        }
        func_0021B3D8(static_cast<s32>(p), UnitList() + p * 0x14 + 0x24);
    }
}

namespace {

double RodataDouble(u32 a) {
    const u64 bits = GRef<u64>(a);
    double d;
    std::memcpy(&d, &bits, 8);
    return d;
}

}  // namespace

// EE: 0x00216D88
// Positional sounds of the 3 slots 0x004241C4[k] in state 1 (then 2), source records
// 0x004241E0[k] (+0 sound def index, +4 variant, +0x14 note base, +0x20 position), heard from
// camera `cam` (position +0, angles +0x10) as listener `listener`: gain (s32)(255 - d / 20), the
// angle and the left / right levels (0x00219850, 0x00219900) shaped by the def's curve (0x00219998,
// made positive). Sound def 0x0033D790 + 0x14 x index: +0 bank (s8), +1 / +2 sounds, +4 curve, +8 +
// 4 x listener note offset (<< 7). Variant 0: sound 1 at the levels with note base + offset
// (0x0021A0F0); other variants: sound 1 at the levels x 0.7 (double, the .rodata constants
// 0x004068F0 / 0x004068F8). Then sound 2 at the levels with note (-12 - variant) << 7 + offset.
// fpucheck: 1 sub, 1 add
void func_00216D88(u32 cam, s32 listener) {
    guest::GuestFrame frame(0x30);
    const u32 sp = frame.Address(), ang = sp + 0x10, lv = sp + 0x20, rv = sp + 0x24;
    for (u32 k = 0; k < 0x10; k += 4) GRef<f32>(sp + k) = F(cam + k);
    for (u32 k = 0; k < 0x10; k += 4) GRef<f32>(ang + k) = F(cam + 0x10 + k);
    const u32 rec = 0x00424170;
    for (u32 k = 0; k < 3; ++k) {
        const u32 st = 0x004241C4 + k * 4;
        if (W(st) != 1) continue;
        const u32 src = W(st + 0x1C);
        const u32 def = 0x0033D790 + W(src) * 0x14;
        const f32 base = F(src + 0x14);
        const f32 d = func_00104F10(src + 0x20, sp);
        const s32 gain = fpu::FloatToInt(fpu::Sub(255.0f, d / 20.0f));
        func_00219900(func_00219850(src + 0x20, sp, ang), lv, rv);
        const s32 curve = GRef<s32>(def + 4);
        f32 l = func_00219998(F(lv), gain, curve);
        GRef<f32>(lv) = l;
        if (l < 0.0f) GRef<f32>(lv) = -l;
        f32 r = func_00219998(F(rv), gain, curve);
        GRef<f32>(rv) = r;
        if (r < 0.0f) GRef<f32>(rv) = -r;
        const s32 note = static_cast<s32>(W(def + 8 + static_cast<u32>(listener) * 4) << 7);
        const s16 bank = GRef<s8>(def);
        if (W(src + 4) == 0) {
            PlaySound(bank, GRef<u8>(def + 1), static_cast<s16>(fpu::FloatToInt(F(lv))),
                      static_cast<s16>(fpu::FloatToInt(F(rv))), rec);
            func_0021A0F0(rec, fpu::FloatToInt(fpu::Add(base, fpu::IntToFloat(note))), 0);
        } else {
            const s16 l7 = static_cast<s16>(static_cast<s32>(static_cast<double>(F(lv)) * RodataDouble(0x004068F0)));
            const s16 r7 = static_cast<s16>(static_cast<s32>(static_cast<double>(F(rv)) * RodataDouble(0x004068F8)));
            PlaySound(bank, GRef<u8>(def + 1), l7, r7, rec);
        }
        PlaySound(GRef<s8>(def), GRef<u8>(def + 2), static_cast<s16>(fpu::FloatToInt(F(lv))),
                  static_cast<s16>(fpu::FloatToInt(F(rv))), rec);
        func_0021A0F0(rec, static_cast<s32>(static_cast<u32>(-12 - GRef<s32>(src + 4)) << 7) + note, 0);
        GRef<u32>(st) = 2;
    }
}

// EE: 0x00216C58
// Clears the words 0x004241C4..0x004241CC, then the positional SE table (0x00216CA0).
void func_00216C58() {
    for (s32 k = 2; k >= 0; --k) GRef<u32>(0x004241C4 + static_cast<u32>(k) * 4) = 0;
    func_00216CA0();
}

}  // namespace ac04::game
