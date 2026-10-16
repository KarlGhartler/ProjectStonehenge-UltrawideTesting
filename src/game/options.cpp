// Option data defaults and the "apply options" helpers run at boot (docs/port/skeleton-readiness.md
// §10). The campaign/save block is 0x002A0318 (0x12C0 bytes); the option bytes are at 0x002A15C4
// (sound), 0x002A15CC (display), 0x002A15D0 (vibration).
#include "game/game.h"

#include "common/ee_math.h"
#include "sce/sce.h"

namespace ac04::game {

using guest::GRef;

// EE: 0x00105340
// HYPOTHESIS: ApplyOptionSettings.
void ApplyOptionSettings() {
    ApplySoundOptions(0x002A15C4);
    ApplyDisplayOffset(0x002A15CC);
    ApplyBrightness(0x002A15CC);
    ApplyVibrationOptions(0x002A15D0);
    if ((GRef<u8>(0x002A15CC) & 1) == 0) {
        Ctx()->flags &= 0xFFFEFFFFu;
    } else {
        Ctx()->flags |= 0x10000;  // SCREEN RATIO 16:9
    }
}

// EE: 0x001053C8
void func_001053C8(u32 block) {
    func_001056D8(block);
    func_00105728(block + 0x12A0);
    func_00105748(block + 0x12AC);
}

// EE: 0x001053F8
// HYPOTHESIS: InitOptionData. Campaign block defaults, then the save-page state.
void InitOptionData() {
    func_001053C8(0x002A0318);
    func_00246FE0();
}

// EE: 0x00105528
// Copies the 0x30-byte default header 0x002A0280 ("UUUU"...) into the block.
void func_00105528(u32 dst) {
    for (u32 i = 0; i < 6; ++i) GRef<u64>(dst + 8 * i) = GRef<u64>(0x002A0280 + 8 * i);
}

// EE: 0x00105568
u32 func_00105568(u32 dst) {
    return ee::memset(dst, 0, 0x10C);
}

// EE: 0x00105578
// Clears 0x8D4 bytes, fills 6 x 18 x 3 words with 0x34BC0 and copies the 0x64-byte table
// 0x002A02B0 to +0x870.
void func_00105578(u32 dst) {
    ee::memset(dst, 0, 0x8D4);
    for (u32 i = 0; i < 6; ++i) {
        for (u32 j = 0; j < 0x12; ++j) {
            for (s32 k = 2; k >= 0; --k) {
                GRef<u32>(dst + i * 0x168 + j * 0x14 + 0x10 - 4u * static_cast<u32>(2 - k)) = 0x34BC0;
            }
        }
    }
    for (u32 off = 0; off < 0x64; off += 0x14) {
        GRef<u64>(dst + 0x870 + off) = GRef<u64>(0x002A02B0 + off);
        GRef<u64>(dst + 0x878 + off) = GRef<u64>(0x002A02B8 + off);
        GRef<u32>(dst + 0x880 + off) = GRef<u32>(0x002A02C0 + off);
    }
}

// EE: 0x00105670
void func_00105670(u32 p) {
    GRef<u32>(p) = 0;
    GRef<u8>(p + 7) = 0xF;
    GRef<u8>(p + 4) = 0xF;
    GRef<u8>(p + 5) = 0xF;
    GRef<u8>(p + 6) = 0xF;
}

// EE: 0x00105690
void func_00105690(u32 p) {
    GRef<u8>(p + 3) = 0;
    GRef<u8>(p + 1) = 3;
    GRef<u8>(p) = 0;
    GRef<u8>(p + 2) = 0;
}

// EE: 0x001056A8
// Screen position offsets of the screen-option block (+2 dx, +3 dy) back to 0.
void ResetScreenPosition(u32 p) {
    GRef<u8>(p + 3) = 0;
    GRef<u8>(p + 2) = 0;
}

// EE: 0x001056B8
// Brightness of the screen-option block back to the neutral level 3.
void ResetBrightness(u32 p) { GRef<u8>(p + 1) = 3; }

// EE: 0x00105448
// Clamps the records block (0x002A0318) before saving (frontend.md "Save / load"): credits
// 0..999,999,999 (signed), the totals +0x38 to 0x80BEFBC4 and +0x3C/+0x40 to 999,999, the
// per-aircraft sortie counts to 999,999 and the sortie count +0x44 to 0x3C14D81 (unsigned).
void ClampStatistics(u32 p) {
    s32& credits = GRef<s32>(p + 0xA18);
    if (!(-1 < credits)) {
        credits = 0;
    } else if (999999999 < credits) {
        credits = 999999999;
    }
    if (0x80BEFBC4u < GRef<u32>(p + 0x38)) GRef<u32>(p + 0x38) = 0x80BEFBC4u;
    if (999999u < GRef<u32>(p + 0x3C)) GRef<u32>(p + 0x3C) = 999999u;
    if (999999u < GRef<u32>(p + 0x40)) GRef<u32>(p + 0x40) = 999999u;
    for (u32 a = 0; a < 0x15; ++a) {
        for (u32 c = 0; c < 3; ++c) {
            u32& n = GRef<u32>(p + 0x48 + a * 0xC + c * 4);
            if (999999u < n) n = 999999u;
        }
    }
    if (0x3C14D81u < GRef<u32>(p + 0x44)) GRef<u32>(p + 0x44) = 0x3C14D81u;
}

// EE: 0x001056C8
void func_001056C8(u32 p) {
    GRef<u16>(p + 2) = 8;
    GRef<u16>(p) = 8;
}

// EE: 0x00105418
// New campaign: resets the campaign selection block (0x002A15B8) and the progress/save block
// (0x002A0318). Called by the main menu's NEW GAME items 9-14 (frontend.md).
void ResetCampaignData() {
    func_00105728(0x002A15B8);
    func_001056D8(0x002A0318);
}

// EE: 0x001056D8
void func_001056D8(u32 p) {
    GRef<u32>(p) = 0;
    GRef<u32>(p + 4) = 1;
    func_00105528(p + 8);
    func_00105568(p + 0x38);
    func_00105578(p + 0x144);
    GRef<u32>(p + 0xA18) = 0;
    func_0017BB58(p + 0xA1C);
}

// EE: 0x00105728
void func_00105728(u32 p) {
    GRef<u16>(p + 10) = 0;
    GRef<u16>(p + 6) = 2;
    GRef<u16>(p) = 0;
    GRef<u16>(p + 2) = 2;
    GRef<u16>(p + 4) = 0;
    GRef<u16>(p + 8) = 0;
}

// EE: 0x00105748
void func_00105748(u32 p) {
    func_00105670(p);
    func_00105690(p + 8);
    func_001056C8(p + 0xC);
}

// EE: 0x00105778
void ApplyVibrationOptions(u32 options) {
    for (u32 port = 0; port < 2; ++port) {
        const u32 pad = g_pMainContext().addr + 0x210 + port * 0x50;
        SetPadVibrationEnabled(pad, (GRef<u16>(options + 2 * port) & 8) != 0 ? 1 : 0, 0);
    }
}

// EE: 0x00105800
// Output mode (bit 0: 2 = stereo? 1 = mono, H) and the four category volumes (signed bytes 4..7,
// level / 15).
void ApplySoundOptions(u32 options) {
    SetSoundOutputMode((GRef<u32>(options) & 1) == 0 ? 1 : 2);
    for (u32 i = 0; i < 4; ++i) {
        const s8 level = GRef<s8>(options + 4 + i);
        func_0021A328(fpu::Div(fpu::IntToFloat(level), 15.0f), static_cast<s32>(i));
    }
}

// EE: 0x001058D8
// Screen position option (bytes 2 and 3, clamped to +-12) applied to DISPLAY (dispenv[3]).
void ApplyDisplayOffset(u32 options) {
    MainContext* ctx = Ctx();
    s64 dx = GRef<s8>(options + 2);
    s64 dy = GRef<s8>(options + 3);
    if (dx < -0xC) dx = -0xC;
    else if (dx > 0xC) dx = 0xC;
    if (dy < -0xC) dy = -0xC;
    else if (dy > 0xC) dy = 0xC;
    const u64 display = ctx->dispenv[3];
    const u64 x = static_cast<u64>(static_cast<s64>(static_cast<s32>(dx) * 8 + 0x27C)) & 0xFFC;
    ctx->dispenv[3] = (display & 0xFFFFFFFFFFFFF000ull) | x;
    u64 y;
    if ((ctx->dispenv[1] & 1) == 0) {  // SMODE2.INT
        y = static_cast<u64>(static_cast<s64>(static_cast<s32>(dy) + 0x19)) & 0x7FF;
    } else {
        y = static_cast<u64>(static_cast<s64>(static_cast<s32>(dy) * 2 + 0x32)) & 0x7FE;
    }
    ctx->dispenv[3] = (display & 0xFFFFFFFFFF800000ull) | x | y << 0xC;
}

// EE: 0x00105988
// Brightness level (byte 1, 0..6; 3 = neutral) -> colour scale of the field copy.
void ApplyBrightness(u32 options) {
    const u32 level = GRef<u8>(options + 1);
    f32 scale;
    if (level == 3) {
        scale = 1.0f;
    } else if (level < 3) {
        scale = fpu::Sub(1.0f, fpu::Mul(fpu::IntToFloat(static_cast<s32>(3 - level)), GRef<f32>(0x0040DA78)));
    } else {
        scale = fpu::Add(fpu::Mul(fpu::IntToFloat(static_cast<s32>(level - 3)), GRef<f32>(0x0040DA7C)), 1.0f);
    }
    MainContext* ctx = Ctx();
    ctx->brightness_b = scale;
    ctx->brightness_r = scale;
    ctx->brightness_g = scale;
}

// EE: 0x0017BB58
// Fills 0x440 halfwords with 0xFFFF (runs of 256, 256, 256, 256 and 64 in the original).
void func_0017BB58(u32 p) {
    for (u32 i = 0; i < 0x440; ++i) GRef<u16>(p + 2 * i) = 0xFFFF;
}

// EE: 0x00246FE0
void func_00246FE0() {
    GRef<u16>(GRef<u32>(0x004114D8)) = 0;
}

}  // namespace ac04::game
