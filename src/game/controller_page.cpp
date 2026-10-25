// Controller page "CONTROLLER SETUP" (scene 4 subs 0x2D-0x30; docs/subsystems/frontend.md,
// page 18): per player (both pads are read) CONTROL TYPE, REVERSE, VIBRATION and PILOT VIEW, with
// the button mapping and the controller picture.
//
// Page object (0x360 bytes, *0x0041183C): +0x10 data load queue, +0x220 DATA 0xB5 {font block,
// controller picture packet}, +0x240 fade object, +0x24C fade level, +0x250 crosshair, +0x28C
// panel (+0x29C/+0x2A8 outline times, +0x2D8/+0x2DC/+0x2E0 timers a/b/c, +0x2E4 crosshair start,
// +0x2EC crosshair), +0x2F0 message box, +0x314 u16 settings[2] (copy of 0x002A15D0), +0x318
// state, +0x31C/+0x31E MenuGoTo target, +0x320 row (0 player, 1-4 settings), +0x324 value of the
// row, +0x328 s32 values[5] (player, then the settings of that player: bits 1, 2, 8, 4),
// +0x33C row animation mask, +0x340 f32 row animation timers[6].
//
// Port baseline (User approval 2026-10-20, docs/decisions/port-baseline.md): CONTROL TYPE has four
// choices, NORMAL (MODERN), EASY (MODERN), NORMAL (CLASSIC), EASY (CLASSIC). The Classic ones are
// the game's two types on their original squares; the Modern ones (two squares to their left) are
// the same types with L1/L2 and R1/R2 swapped by the port's input layer. Moving through the four
// squares changes the game's type bit exactly as the original two squares do, so guest RAM and the
// save data hold only the Classic type; the per-player Modern flag is kept by the port
// (common/port_hooks.h, settings input.modern_controls) and handed over on ACCEPT.
#include "game/game.h"

#include <array>
#include <cstddef>

#include "common/debug_hooks.h"
#include "common/ee_math.h"
#include "common/guest_stack.h"
#include "common/port_hooks.h"
#include "game/port_menu.h"
#include "sce/sce.h"

namespace ac04::game {

using guest::GRef;

namespace {
AC04_GUEST_VAR(u32, g_ControllerPage, 0x0041183C)  // gp-0x41B4
AC04_GUEST_VAR(u32, g_pMenuFade, 0x00410E88)
AC04_GUEST_VAR(u16, g_MissionNo, 0x002A15B8)

constexpr u32 kSettingBits = 0x00411328;  // u16[4]: 1, 2, 8, 4

s32& State(u32 obj) { return GRef<s32>(obj + 0x318); }
s32& Value(u32 obj, u32 i) { return GRef<s32>(obj + 0x328 + i * 4); }

void ClearRowTimers(u32 obj) {
    for (u32 off = 0x354; off >= 0x340; off -= 4) GRef<u32>(obj + off) = 0;
}

// Port: the Modern flags while the page is open (port_hooks.h), per player.
std::array<bool, 2> g_Modern{};

bool Modern(u32 obj) { return g_Modern[Value(obj, 0) == 0 ? 0 : 1]; }

// Square of the CONTROL TYPE row: 0 NORMAL (MODERN), 1 EASY (MODERN), 2 NORMAL (CLASSIC), 3 EASY
// (CLASSIC). The game's value (row 1) is 0 NORMAL, 1 EASY.
s32 TypeChoice(u32 obj) { return (Modern(obj) ? 0 : 2) + Value(obj, 1); }

// Square column of a row relative to the original first square (CONTROL TYPE: -2..1).
s32 Column(u32 obj, s32 row) { return row == 1 ? TypeChoice(obj) - 2 : Value(obj, static_cast<u32>(row)); }

u32 TypeText(s32 choice) {
    static const char* const kNames[4] = {"NORMAL (MODERN)", "EASY (MODERN)", "NORMAL (CLASSIC)", "EASY (CLASSIC)"};
    return port_menu::String(static_cast<u32>(choice), kNames[choice & 3]);
}

// Mapping-table row whose action a row shows: Modern swaps L1 / L2 (rows 4 / 5) and R1 / R2 (6 / 7).
s32 ActionRow(u32 obj, s32 row) { return Modern(obj) && 4 <= row && row <= 7 ? (row ^ 1) : row; }

}  // namespace

// EE: 0x002423D8
// Help line of the controller page (message per row, table 0x003AF540).
void UpdateControllerHelp(u32 obj) {
    s32 state = State(obj);
    if (state == 1) {
        u32 row = GRef<u32>(obj + 0x320);
        if (4 < row) row = 0;
        const s16 id = GRef<s16>(0x003AF540 + row * 2);
        if (GRef<s16>(obj + 0x2FE) != id) {
            SetMessageBoxText(obj + 0x2F0, id);
            state = State(obj);
            GRef<f32>(obj + 0x2F8) = K(0x00410378);
        }
    }
    if (state != 0) UpdateMessageBox(K(0x0041037C), obj + 0x2F0, static_cast<s32>(Ctx()->frame_count));
}

// EE: 0x00242468
// Cursor on row `row`, column `col`; the setting values from the selected player's word.
void SetControllerCursor(u32 obj, s32 row, s32 col) {
    const s32 x = col * 0x140 + 0x1100;
    const s32 y = row * 0x1C0 + 0x5A0;
    GRef<s16>(obj + 0x2E4) = static_cast<s16>(x);
    GRef<s16>(obj + 0x2E6) = static_cast<s16>(y);
    MoveCrosshairAuto(GRef<u32>(obj + 0x2EC), x, y);
    const u16 w = Value(obj, 0) == 0 ? GRef<u16>(obj + 0x314) : GRef<u16>(obj + 0x316);
    Value(obj, 1) = (w & 1) != 0 ? 1 : 0;
    Value(obj, 2) = (w & 2) != 0 ? 1 : 0;
    Value(obj, 3) = (w & 8) != 0 ? 1 : 0;
    Value(obj, 4) = (w & 4) != 0 ? 1 : 0;
}

// EE: 0x00242528
// Writes the setting values back into the selected player's word; switching vibration on
// vibrates that pad (K 0x00410380 seconds).
void StoreControllerSettings(u32 obj, u32 words) {
    const u32 p = Value(obj, 0) == 0 ? words : words + 2;
    u16 w = GRef<u16>(p);
    for (u32 i = 1; i < 5; ++i) {
        const u16 bit = GRef<u16>(kSettingBits + (i - 1) * 2);
        if (Value(obj, i) == 0) {
            w = static_cast<u16>(w & ~bit);
        } else {
            w = static_cast<u16>(bit | w);
        }
    }
    if ((GRef<u16>(p) & 8) == 0 && (w & 8) != 0) {
        StartPadVibration(K(0x00410380), kMainContextAddress + offsetof(MainContext, pad_state) + static_cast<u32>(Value(obj, 0)) * 0x50);
    }
    GRef<u16>(p) = w;
}

// EE: 0x002425F0
// Input from either pad: cancel / accept (accept writes both players' words to 0x002A15D0 and
// applies vibration), up/down choose the row, left/right change the value (the changed rows
// animate).
void ControllerInput(u32 obj) {
    const MainContext* ctx = Ctx();
    u32 repeat = ctx->pad_buttons[0][2];
    const u32 repeat2 = ctx->pad_buttons[1][2];
    const u32 edge = ctx->pad_buttons[0][1] | ctx->pad_buttons[1][1];
    GRef<u16>(g_pMenuFade() + 0x32) |= 0x3F;
    repeat |= repeat2;
    if ((edge & 0x20) != 0) {
        StartFade(K(0x00410384), obj + 0x240, 1);
        SetPanelState(obj + 0x28C, 1);
        GRef<u32>(obj + 0x33C) = 0;
        ClearRowTimers(obj);
        State(obj) = 4;
        PlayMenuSe(0, 3);
        return;
    }
    if ((edge & 0x50) != 0) {
        StartFade(K(0x00410388), obj + 0x240, 1);
        SetPanelState(obj + 0x28C, 1);
        GRef<u32>(obj + 0x33C) = 0;
        ClearRowTimers(obj);
        GRef<u32>(0x002A15D0) = GRef<u32>(obj + 0x314);
        State(obj) = 2;
        ApplyVibrationOptions(0x002A15D0);
        port::Menu().set_modern_controls = g_Modern;  // port: Modern control types
        PlayMenuSe(0, 2);
        return;
    }
    if ((repeat & 3) != 0) {
        s32 row = GRef<s32>(obj + 0x320) - static_cast<s32>(repeat & 1);
        if ((repeat & 2) != 0) row += 1;
        row = (row + 5) % 5;
        GRef<s32>(obj + 0x320) = row;
        const s32 v = Value(obj, static_cast<u32>(row));
        GRef<s32>(obj + 0x324) = v;
        SetControllerCursor(obj, row, Column(obj, row));
        PlayMenuSe(0, 4);
        return;
    }
    if ((repeat & 0xC) == 0) return;
    s32 v = GRef<s32>(obj + 0x324);
    const bool modern_before = Modern(obj);
    if (GRef<s32>(obj + 0x320) == 1) {
        // Port: four CONTROL TYPE squares; the type bit steps as over the original two.
        s32 c = TypeChoice(obj);
        if ((repeat & 8) != 0) c += 1;
        if ((repeat & 4) != 0) c -= 1;
        c = (c + 4) % 4;
        g_Modern[Value(obj, 0) == 0 ? 0 : 1] = c < 2;
        v = c & 1;
    } else {
        if ((repeat & 8) != 0) v += 1;
        if ((repeat & 4) != 0) v -= 1;
        v = (v + 2) % 2;
    }
    auto mask = [obj]() {
        u32 m = 0;
        for (u32 i = 1; i < 5; ++i) {
            if (Value(obj, i) != 0) m |= 1u << (i - 1);
        }
        return m;
    };
    const u32 before = mask();
    GRef<s32>(obj + 0x324) = v;
    Value(obj, GRef<u32>(obj + 0x320)) = v;
    if (0 < GRef<s32>(obj + 0x320)) StoreControllerSettings(obj, obj + 0x314);
    SetControllerCursor(obj, GRef<s32>(obj + 0x320), Column(obj, GRef<s32>(obj + 0x320)));
    u32 changed = before ^ mask();
    if (Modern(obj) != modern_before && (changed & 1) == 0) {
        // Port: only the Modern flag changed: the CONTROL TYPE text is typed out again.
        GRef<u32>(obj + 0x340) = 0;
        GRef<u32>(obj + 0x33C) |= 1;
    }
    for (s32 k = 0; k < 4; ++k, changed >>= 1) {
        if ((changed & 1) == 0) continue;
        if (k == 0) {
            GRef<u32>(obj + 0x340) = 0;
            GRef<u32>(obj + 0x354) = 0;
            GRef<u32>(obj + 0x33C) |= 0x21;
        } else if (k == 1) {
            GRef<u32>(obj + 0x344) = 0;
            GRef<u32>(obj + 0x350) = 0;
            GRef<u32>(obj + 0x33C) |= 0x12;
        } else if (k == 2) {
            GRef<u32>(obj + 0x348) = 0;
            GRef<u32>(obj + 0x33C) |= 4;
        } else {
            GRef<u32>(obj + 0x34C) = 0;
            GRef<u32>(obj + 0x33C) |= 8;
        }
    }
    PlayMenuSe(0, 4);
}

// EE: 0x00242978
// Row animation timers advance (K 0x0041038C per frame) while their mask bit is set.
void AnimateControllerRows(u32 obj) {
    const f32 step = K(0x0041038C);
    u32 mask = GRef<u32>(obj + 0x33C);
    for (u32 i = 0; i < 6 && mask != 0; ++i, mask >>= 1) {
        if ((mask & 1) != 0) GRef<f32>(obj + 0x340 + i * 4) = fpu::Add(GRef<f32>(obj + 0x340 + i * 4), step);
    }
}

// EE: 0x002429C8
// From the main menu (sub 0x2D) back to page 4, otherwise to sub-page 0x18.
u32 CreateControllerPage() {
    const u32 obj = WorkHeapAlloc(0x360, 0x10);
    const u32 fade = g_pMenuFade();
    State(obj) = 0;
    GRef<u16>(fade + 0x32) = 0;
    GRef<u16>(fade + 0x30) = 1;
    InitFade(obj + 0x240, 1);
    GRef<u16>(obj + 0x31C) = 1;
    if (Ctx()->sub == 0x2D) {
        GRef<u16>(obj + 0x31E) = 4;
        PlayMenuBgm(4);
    } else {
        GRef<u16>(obj + 0x31E) = 0x18;
        PlayMenuBgm(static_cast<s16>(GetProgressBgmTrack(1, g_MissionNo())));
    }
    InitPanel(obj + 0x28C, 6, 0x003AF4E0, obj + 0x250);
    GRef<u16>(obj + 0x28E) |= 0xE;
    const u32 settings = GRef<u32>(0x002A15D0);
    GRef<s32>(obj + 0x320) = 0;
    GRef<s32>(obj + 0x324) = 0;
    Value(obj, 0) = 0;
    GRef<u32>(obj + 0x314) = settings;
    g_Modern = port::Menu().modern_controls;  // port: Modern control types
    SetControllerCursor(obj, 0, 0);
    GRef<u32>(obj + 0x33C) = 0x3F;
    GRef<f32>(obj + 0x348) = K(0x00410398);
    GRef<f32>(obj + 0x34C) = K(0x0041039C);
    GRef<f32>(obj + 0x350) = K(0x00410390);
    GRef<f32>(obj + 0x354) = K(0x00410394);
    GRef<f32>(obj + 0x340) = K(0x00410390);
    GRef<f32>(obj + 0x344) = K(0x00410394);
    return obj;
}

// EE: 0x00242AE8
void ControllerUpdate(u32 obj) {
    const u32 fade = g_pMenuFade();
    GRef<u16>(fade + 0x32) = 0;
    UpdateControllerHelp(obj);
    s32 state = 0;
    if (State(obj) != 0) {
        UpdatePanel(obj + 0x28C);
        AnimateControllerRows(obj);
        state = State(obj);
    }
    if (state == 1) {
        if ((GRef<u16>(obj + 0x28C) & 0x800) != 0) ControllerInput(obj);
    } else if (state == 0) {
        if (K(0x004103A0) < GRef<f32>(fade + 0xC)) State(obj) = 1;
    } else if (state == 2 || state == 4) {
        if ((GRef<u32>(obj + 0x240) & 8) == 0 && (GRef<u16>(obj + 0x28C) & 8) == 0) return;
        MenuGoTo(GRef<s16>(obj + 0x31C), GRef<u16>(obj + 0x31E));
    }
}

// EE: 0x00242BE8
// Texts: row labels, player number, the button mapping (timer a), the changed setting values
// (row timers), the type/reverse panels, the choice marks (timer c) and the pulsing boxes on the
// chosen values.
void DrawControllerTexts(u32 list, s32 index, u32 obj) {
    guest::GuestFrame frame(0x100);
    const u32 buf = frame.Address();
    const u32 parts = GRef<u16>(obj + 0x28C);
    GRef<u16>(obj + 0x28C) = static_cast<u16>(parts & 0x7FFF);
    BeginMenuTextLayer(list, index);
    const f32 a = GRef<f32>(obj + 0x2D8);
    if (0.0f < a) {
        const f32 t = a * 90.0f;
        DrawMenuText(t, 0x1C0, 0x4A0, 4, 0x00411400);  // "|"
        f32 r = fpu::Sub(t, 1.0f);
        s32 y = 0x4A0;
        for (u32 k = 0; k < 5; ++k, y += 0x1C0, r = fpu::Sub(r, 8.0f)) {
            DrawMenuText(r, 0x280, y, 4, GRef<u32>(0x003AF550 + k * 4));
        }
        const u64 pargs[1] = {Sx(Value(obj, 0) + 1)};
        ee::sprintf(buf, 0x004098A0, pargs, 1);  // "PLAYER %02d"
        DrawMenuText(fpu::Sub(t, 4.0f), 0x280, 0x560, 3, buf);
        DrawMenuText(fpu::Sub(t, 7.0f), 0x1C0, 0xF70, 4, 0x00411400);
        r = fpu::Sub(t, 8.0f);
        y = 0xF70;
        for (u32 k = 0; k < 2; ++k, y += 0x1C0) {
            const f32 next = fpu::Sub(r, 8.0f);
            DrawMenuText(r, 0x280, y, 4, GRef<u32>(0x003AF588 + k * 4));
            r = next;
            DrawMenuText(next, 0xB80, y, 4, GRef<u32>(0x003AF5A0 + k * 4));
        }
        r = t;
        y = 0x490;
        for (s32 i = 0; i < 0xD; ++i, y += 0x140, r = fpu::Sub(r, 4.0f)) {
            const u32 label = GRef<u32>(0x003AF5B8 + static_cast<u32>(i) * 4);
            if (GRef<s8>(label) == 'm') {
                const u32 color = GRef<u32>(0x003AF628 + static_cast<u32>(GRef<s8>(label + 1) - '0') * 4);
                DrawMenuTextColored(r, 0x1500, y, 0xB, label + 1, color);
            } else {
                DrawMenuText(r, 0x1500, y, 3, label);
            }
            const s32 shown = ActionRow(obj, i);  // port: Modern swaps the shoulder rows
            const u32 action = GRef<u32>(0x003AF5F0 + static_cast<u32>(shown) * 4);
            if (shown == 5 || shown == 7) {
                const f32 back = fpu::Sub(r, 6.0f);
                f32 v = fpu::Sub(fpu::Sub(GRef<f32>(obj + 0x354), K(0x004103A4)) * 90.0f, static_cast<f32>(shown - 5) * 4.0f);
                if (back < v) v = back;
                if (Value(obj, 1) == 0) {
                    const u64 aargs[1] = {action};
                    ee::sprintf(buf, 0x00411408, aargs, 1);  // "%s"
                } else {
                    ee::sprintf(buf, 0x004098B0, nullptr, 0);  // "------------"
                }
                DrawMenuText(v, 0x1AC0, y, 3, buf);
            } else {
                DrawMenuText(fpu::Sub(r, 6.0f), 0x1AC0, y, 3, action);
            }
        }
        const f32 left = fpu::Sub(t, 69.0f);
        if (left < 0.0f) {
            GRef<u16>(obj + 0x28C) |= 0x8000;
        } else if (4.0f < left && (parts & 0x100) != 0) {
            GRef<u16>(obj + 0x28C) &= 0xFEFF;
        }
    }
    {
        const f32 k = K(0x004103A8);
        s32 y = 0x720;
        for (u32 i = 0; i < 4; ++i, y += 0x1C0) {
            const f32 v = fpu::Sub(GRef<f32>(obj + 0x340 + i * 4), k);
            if (!(0.0f < v)) continue;
            f32 s = v * 60.0f;
            const u32 text = i == 0 ? TypeText(TypeChoice(obj))  // port: the four CONTROL TYPE names
                                    : GRef<u32>(0x003AF568 + (i * 2 + static_cast<u32>(Value(obj, i + 1))) * 4);
            DrawMenuText(s, 0x280, y, 3, text);
            s = fpu::Sub(s, 6.0f);
            // The animation ends after 12 characters, past the glow of the last typed ones (the
            // original names have at most 7); the port's CONTROL TYPE names get the same margin.
            const u32 len = i == 0 ? ee::strlen(text) : 0;
            const f32 end = len > 7 ? static_cast<f32>(len - 1) : 6.0f;
            if (s < 0.0f) {
                GRef<u16>(obj + 0x28C) |= 0x8000;
            } else if (end < s) {
                const u32 m = GRef<u32>(obj + 0x33C);
                if (((static_cast<s32>(m) >> i) & 1) != 0) GRef<u32>(obj + 0x33C) = m & ~(1u << i);
            }
        }
    }
    {
        const f32 k = K(0x004103AC);
        s32 x = 0x280;
        for (u32 j = 0; j < 2; ++j, x += 0x900) {
            const f32 v = fpu::Sub(GRef<f32>(obj + 0x350 + j * 4), k);
            if (v <= 0.0f) continue;
            f32 s = v * 90.0f;
            const u32 e = j == 0 ? 0x003AF590 + static_cast<u32>(Value(obj, 2)) * 8 : 0x003AF5A8 + static_cast<u32>(Value(obj, 1)) * 8;
            DrawMenuText(s, x, 0x1020, 3, GRef<u32>(e));
            DrawMenuText(fpu::Sub(s, 8.0f), x, 0x11E0, 3, GRef<u32>(e + 4));
            s = fpu::Sub(s, 20.0f);
            if (s < 0.0f) {
                GRef<u16>(obj + 0x28C) |= 0x8000;
            } else if (4.0f < s) {
                const u32 bit = 4 + j;
                const u32 m = GRef<u32>(obj + 0x33C);
                if (((static_cast<s32>(m) >> bit) & 1) != 0) GRef<u32>(obj + 0x33C) = m & ~(1u << bit);
            }
        }
    }
    const f32 c = GRef<f32>(obj + 0x2E0);
    if (0.0f < c) {
        f32 t = c * 90.0f;
        f32 r = fpu::Sub(t, 8.0f);
        s32 y = 0x530;
        for (s32 row = 0; row < 5; ++row, y += 0x1C0, r = fpu::Sub(r, 8.0f)) {
            f32 m = r;
            // Port: CONTROL TYPE has two more squares, left of the original two.
            s32 x = row == 1 ? 0x1080 - 2 * 0x140 : 0x1080;
            for (s32 k = row == 1 ? 3 : 1; k >= 0; --k, x += 0x140) {
                DrawMenuText(m, x, y, 8, 0x00411410);
                m = fpu::Sub(m, 2.0f);
            }
        }
        t = fpu::Sub(t, 44.0f);
        if (t < 0.0f) {
            GRef<u16>(obj + 0x28C) |= 0x8000;
        } else if (4.0f < t && (parts & 0x400) != 0) {
            GRef<u16>(obj + 0x28C) = static_cast<u16>((GRef<u16>(obj + 0x28C) & 0xFBFF) | 0x800);
        }
    }
    EndMenuTextLayer();
    if ((GRef<u16>(obj + 0x28C) & 0x800) == 0) return;
    s32 y = 0x530;
    for (u32 row = 0; row < 5; ++row, y += 0x1C0) {
        const s32 frame_no = static_cast<s32>(row) == GRef<s32>(obj + 0x320) ? static_cast<s32>(Ctx()->frame_count) : 0;
        DrawPulseBox(list, index, 0x1067, Column(obj, static_cast<s32>(row)) * 0x140 + 0x1080, y, 0x100, frame_no);
    }
}

// EE: 0x00243330
// The controller picture (sprite from the DATA 0xB5 texture packet).
void DrawControllerImage(u32 list, s32 index, u32 obj) {
    if (GRef<f32>(obj + 0x29C) < K(0x004103B0)) return;
    const u32 start = Ctx()->cur_buf.addr;
    const u32 tag = WriteDmaCallTag(start, GRef<u32>(obj + 0x224));
    const u32 p = BuildMenuDrawEnv(tag + 0x10, 0x280, 0x1C0);
    Q(p, 0) = 0x11AB400000000002ull;
    Q(p, 1) = 0xE;
    Q(p, 2) = 0x8000000068ull;
    Q(p, 3) = 0x43;
    Q(p, 5) = 0x19;
    Q(p, 4) = 0;
    Q(p, 8) = 0x20060002A9312B00ull;
    Q(p, 6) = 0x6400000000008001ull;
    Q(p, 7) = 0x53D317;
    Q(p, 0xD) = 0x117012D0;
    Q(p, 9) = 0x808080;
    Q(p, 0xA) = 0x80008;
    Q(p, 0xB) = 0x7B003D0;
    Q(p, 0xC) = 0x9C80F08;
    LinkPacket(list, index, start, CloseDmaPacket(tag, p + 0x70));
}

// EE: 0x002434A0
// The mapping table lines (12 rows of line pairs and a vertical line).
void DrawControllerGrid(u32 list, s32 index, u32 obj) {
    if (GRef<f32>(obj + 0x2A8) < K(0x004103B4)) return;
    const u32 start = Ctx()->cur_buf.addr;
    const u32 p = BuildMenuDrawEnv(start + 0x10, 0x280, 0x1C0);
    Q(p, 0) = 0x1125400000000003ull;
    Q(p, 1) = 0xE;
    Q(p, 3) = 0x43;
    Q(p, 2) = 0x8000000068ull;
    Q(p, 5) = 0x19;
    Q(p, 6) = 0xD1C1A9;
    Q(p, 7) = 1;
    Q(p, 4) = 0;
    Q(p, 8) = 0x2400000000000019ull;
    Q(p, 9) = 0x5D;
    Q(p, 0xA) = 0x4401A40;
    Q(p, 0xB) = 0x14701A40;
    u32 q = p + 0x60;
    s32 y = 0x570;
    for (s32 i = 11; i >= 0; --i, y += 0x140, q += 0x20) {
        const u64 yy = Sx(y) << 0x10;
        GRef<u64>(q) = yy | 0x1490;
        GRef<u64>(q + 8) = yy | 0x1A40;
        GRef<u64>(q + 0x10) = yy | 0x1A50;
        GRef<u64>(q + 0x18) = yy | 0x2570;
    }
    LinkPacket(list, index, start, CloseDmaPacket(start, q));
}

// EE: 0x00243618
void DrawControllerPage(u32 obj) {
    InitBucketList(obj, 1);
    SetPacketListBuckets(kMainContextAddress + offsetof(MainContext, vif1_list), obj);
    DrawPanel(obj, 0, obj + 0x28C);
    DrawControllerTexts(obj, 0, obj);
    DrawControllerImage(obj, 0, obj);
    DrawControllerGrid(obj, 0, obj);
    UploadMessageBoxGlyphs(obj, 0, obj + 0x2F0);
    DrawMessageBox(obj, 0, obj + 0x2F0);
    DrawBlackFade(GRef<f32>(obj + 0x24C), obj, 0);
    DrawMenuFade(obj, 0, g_pMenuFade());
}

// EE: 0x002436D8
// Subs 0x2D/0x2E: creates the page and queues DATA 0xB5.
void EnterControllerPage() {
    PopWorkHeapMark();
    PushWorkHeapMark();
    MainContext* ctx = Ctx();
    ctx->draw_mode = 2;
    ctx->drawenv_pkt[4] = 0x1310000DCull;
    ctx->drawenv_pkt[20] = 0x1310000DCull;
    const u32 obj = CreateControllerPage();
    g_ControllerPage() = obj;
    ResetMenuDataLoad(obj + 0x10);
    QueueMenuDataLoad(obj + 0x10, 0xB5, 2, obj + 0x220);
    DrawMenuFadeFrame(obj, g_pMenuFade());
    RequestSubState(0x2F);
}

// EE: 0x00243778
void LoadControllerPage() {
    const u32 obj = g_ControllerPage();
    if (UpdateMenuDataLoad(obj + 0x10) < 0) {
        LoadFontBlock(GRef<u32>(obj + 0x220));
        InitMessageBox(obj + 0x2F0);
        RequestSubState(0x30);
    }
    DrawMenuFadeFrame(obj, g_pMenuFade());
}

// EE: 0x002437C8
void RunControllerPage() {
    const u32 obj = g_ControllerPage();
    debug::OnGuestCheckpoint(0x002437C8);
    UpdateFade(obj + 0x240);
    ControllerUpdate(obj);
    DrawControllerPage(obj);
}

AC04_GUEST_FUNCTION(0x002436D8, EnterControllerPage);
AC04_GUEST_FUNCTION(0x00243778, LoadControllerPage);
AC04_GUEST_FUNCTION(0x002437C8, RunControllerPage);

}  // namespace ac04::game
