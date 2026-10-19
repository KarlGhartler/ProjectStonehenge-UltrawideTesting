// Replay recording (slice S5c): RecordReplayFrame 0x0016E8B8 writes one frame of player and object
// snapshots into the replay ring (recorder gp 0x00410AF8: +4 buffer start, +8 write pointer,
// +0x14 frame count, +0x1C buffer size). Docs: docs/subsystems/flight.md "Replay".
#include "game/game.h"

#include "common/ee_math.h"
#include "game/flight_common.h"

namespace ac04::game {

using guest::GRef;
using namespace fl;

namespace {

u32 Rec() { return W(0x00410AF8); }

// Wraps an angle once into [lo, hi] with the given correction constants (6 adds per record type).
f32 WrapOnce(u32 a, f32 hi, f32 hi_sub, f32 lo, f32 lo_add) {
    f32 v = F(a);
    if (hi < v) {
        v = fpu::Sub(v, hi_sub);
        GRef<f32>(a) = v;
    }
    if (v < lo) {
        v = fpu::Add(v, lo_add);
        GRef<f32>(a) = v;
    }
    return v;
}

u8 Byte255(f32 v) { return static_cast<u8>(fpu::FloatToInt(v * 255.0f)); }

}  // namespace

// EE: 0x0016DF88
// Space for `size` bytes at the write pointer; when it does not fit (before the last word) the ring
// restarts: marker 0x76543210 at the old write pointer, the rest of the buffer cleared.
u32 func_0016DF88(s32 size) {
    const u32 r = Rec();
    u32 w = W(r + 8);
    const u32 start = W(r + 4);
    if (W(r + 0x1C) - 4u <= w + static_cast<u32>(size - static_cast<s32>(start))) {
        GRef<u32>(r + 8) = start;
        GRef<u32>(w) = 0x76543210u;
        const u32 end = start + W(r + 0x1C);
        for (w += 4; w < end; w += 4) GRef<u32>(w) = 0;
    }
    return W(r + 8);
}

// EE: 0x0016E000
void func_0016E000(u32 w) { GRef<u32>(Rec() + 8) = w; }

// EE: 0x0016E8B8
// One replay frame: the object snapshots, then the frame marker 0x01234567 0x76544567.
void RecordReplayFrame() {
    func_0016F688();
    const u32 w = func_0016DF88(8);
    GRef<u32>(w) = 0x01234567u;
    GRef<u32>(w + 4) = 0x76544567u;
    func_0016E000(w + 4);
    GRef<s32>(Rec() + 0x14) += 1;
}

// EE: 0x0016F688
// Snapshots of this frame (when the scene's replay flag bit 0 is set): one 0x30-byte record per
// active player (position, angles as s16 * C, view, lights, throttle/stick/gear bytes, flags) and
// one per moving object (0x30 bytes for vehicles kinds 8-9 with four animation tracks as floats,
// else 0x1C bytes with tracks as bytes).
// fpucheck: verified 18 (three record types x 3 angles x 2 wrap ops, through WrapOnce)
void func_0016F688() {
    if ((~W(Scene() + 0x1668) & 1u) != 0) return;
    for (s32 i = 0; i < static_cast<s32>(Players()); ++i) {
        const u32 u = Unit(i);
        if ((~W(u + 0x4C) & 0x20000000u) != 0) continue;
        const u32 r = func_0016DF88(0x30);
        const u32 P = W(u + 0x44);
        GRef<u8>(r) = 0;
        GRef<u8>(r + 1) = static_cast<u8>(i);
        const f32 hi = F(0x0040E98C), lo = F(0x0040E994);
        const f32 x = WrapOnce(u + 0x10, hi, F(0x0040E990), lo, F(0x0040E998));
        const f32 y = WrapOnce(u + 0x14, hi, F(0x0040E99C), lo, F(0x0040E9A0));
        const f32 z = WrapOnce(u + 0x18, hi, F(0x0040E9A4), lo, F(0x0040E9A8));
        const f32 k = F(0x0040E9AC);
        GRef<u32>(r + 8) = W(u);
        GRef<u32>(r + 0xC) = W(u + 4);
        GRef<s16>(r + 6) = static_cast<s16>(fpu::FloatToInt(z * k));
        GRef<u32>(r + 0x10) = W(u + 8);
        GRef<s16>(r + 4) = static_cast<s16>(fpu::FloatToInt(y * k));
        GRef<s16>(r + 2) = static_cast<s16>(fpu::FloatToInt(x * k));
        GRef<u8>(r + 0x14) = GRef<u8>(u + 0x62);
        GRef<u8>(r + 0x15) = GRef<u8>(P + 0x2F0);
        GRef<u8>(r + 0x16) = GRef<u8>(P + 0x2F1);
        GRef<u8>(r + 0x17) = GRef<u8>(P + 0x2F2);
        GRef<u32>(r + 0x18) = W(P + 0x25C);
        GRef<u8>(r + 0x1C) = Byte255(F(P + 0x240));
        GRef<u8>(r + 0x1D) = Byte255(F(P + 0x250));
        GRef<u8>(r + 0x1E) = Byte255(F(P + 0x258));
        GRef<u8>(r + 0x1F) = Byte255(F(P + 0x254));
        GRef<u8>(r + 0x20) = Byte255(F(P + 0x248));
        GRef<u8>(r + 0x21) = Byte255(F(P + 0x24C));
        GRef<u8>(r + 0x22) = Byte255(F(P + 0x244));
        GRef<u8>(r + 0x23) = GRef<u8>(P + 0x286);
        GRef<u8>(r + 0x24) = GRef<u8>(P + 0x287);
        GRef<u8>(r + 0x25) = GRef<u8>(P + 0x288);
        GRef<u8>(r + 0x28) = GRef<u8>(P + 0x280);
        GRef<u8>(r + 0x29) = Byte255(F(P + 0x260));
        GRef<u8>(r + 0x2A) = Byte255(F(P + 0x264));
        GRef<u8>(r + 0x2B) = Byte255(F(P + 0x268));
        GRef<u16>(r + 0x2C) = GRef<u16>(u + 0x56);
        const u8 v = GRef<u8>(P + 0x186);
        GRef<u8>(r + 0x2F) = 0;
        GRef<u8>(r + 0x2E) = v;
        if ((W(u + 0x4C) & 0x1000000u) != 0) GRef<u8>(r + 0x2F) = 1;
        func_0016E000(r + 0x30);
    }
    for (s32 i = 2; i < UnitCount(); ++i) {
        const u32 u = Unit(i);
        u32 f = W(u + 0x4C);
        if ((~f & 0x10000000u) != 0) continue;
        if ((~W(u + 0x48) & 0x200000u) != 0 && (~f & 0x20000000u) != 0) continue;
        if ((f & 0x1000u) != 0) {
            f |= 0x100000u;
            GRef<u32>(u + 0x4C) = f;
        }
        if ((W(u + 0x50) & 0x73000000u) != 0) {
            f |= 0x100000u;
            GRef<u32>(u + 0x4C) = f;
        }
        if ((~f & 0x100000u) != 0) continue;
        const bool vehicle = static_cast<u32>(GRef<u8>(u + 0x6A) - 8) < 2;
        u32 r, end;
        f32 hi, sx, lo, ax, sy, ay, sz, az, k;
        if (vehicle) {
            r = func_0016DF88(0x30);
            hi = F(0x0040E9B0);
            sx = F(0x0040E9B4);
            lo = F(0x0040E9B8);
            ax = F(0x0040E9BC);
            sy = F(0x0040E9C0);
            ay = F(0x0040E9C4);
            sz = F(0x0040E9C8);
            az = F(0x0040E9CC);
            k = F(0x0040E9D0);
            end = r + 0x30;
        } else {
            r = func_0016DF88(0x1C);
            hi = F(0x0040E9D4);
            sx = F(0x0040E9D8);
            lo = F(0x0040E9DC);
            ax = F(0x0040E9E0);
            sy = F(0x0040E9E4);
            ay = F(0x0040E9E8);
            sz = F(0x0040E9EC);
            az = F(0x0040E9F0);
            k = F(0x0040E9F4);
            end = r + 0x1C;
        }
        GRef<u8>(r) = 0;
        GRef<u8>(r + 1) = static_cast<u8>(i);
        const f32 x = WrapOnce(u + 0x10, hi, sx, lo, ax);
        const f32 y = WrapOnce(u + 0x14, hi, sy, lo, ay);
        const f32 z = WrapOnce(u + 0x18, hi, sz, lo, az);
        GRef<u32>(r + 8) = W(u);
        GRef<u32>(r + 0xC) = W(u + 4);
        GRef<s16>(r + 6) = static_cast<s16>(fpu::FloatToInt(z * k));
        GRef<s16>(r + 2) = static_cast<s16>(fpu::FloatToInt(x * k));
        GRef<s16>(r + 4) = static_cast<s16>(fpu::FloatToInt(y * k));
        GRef<u32>(r + 0x10) = W(u + 8);
        GRef<u8>(r + 0x14) = GRef<u8>(u + 0x5A);
        GRef<u16>(r + 0x16) = GRef<u16>(u + 0x56);
        for (s32 t = 0; t < 4; ++t) {
            f32 a = 0.0f;
            if ((W(u + 0x4C) & 0x20000000u) != 0) a = func_00145DA0(UnitModels(), GRef<u8>(u + 0x55), t);
            if (1.0f < a) a = 1.0f;
            if (vehicle)
                GRef<f32>(r + 0x18 + 4u * static_cast<u32>(t)) = a < 0.0f ? 0.0f : a;
            else
                GRef<u8>(r + 0x18 + static_cast<u32>(t)) = static_cast<u8>(fpu::FloatToInt(a < 0.0f ? 255.0f * 0.0f : a * 255.0f));
        }
        GRef<u8>(r + 0x15) = 0;
        f = W(u + 0x4C);
        if ((f & 0x1000000u) != 0) {
            GRef<u8>(r + 0x15) = 1;
            f = W(u + 0x4C);
        }
        if ((~f & 0x20000000u) != 0) GRef<u8>(r + 0x15) = static_cast<u8>(GRef<u8>(r + 0x15) | 0x80);
        if (vehicle) {
            GRef<u32>(r + 0x28) = W(u + 0x70);
            GRef<u32>(r + 0x2C) = W(u + 0x74);
        }
        func_0016E000(end);
    }
}

}  // namespace ac04::game
