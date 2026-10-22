// Mission select (scene 4 subs 0x42-0x49; docs/port/slices.md §A.4): free mission, extra
// missions and VS mission lists with their records, and the settings menu (aircraft, save/load,
// VS weather) before a free mission.
//
// Page object (0x770 bytes, *0x0041182C): +0 bucket list, +0x10 data load queue, +0x220 DATA 0x110
// (font block, +0x224 image), +0x240 fade (+0x24C level), +0x250 panel tab, +0x290 animated frame
// {u16 x, y, w, h; f32 sx, sy}, +0x2A0 panel (5 rows, 0x003AF070), +0x2F8/+0x2FA crosshair target,
// +0x300 crosshair, +0x304 menu tree (4 columns 0x003AEFD0, connections 0x003AF030, parents
// 0x00411190), +0x70C help line, +0x730 kind (0 free mission, 1 extra, 2 VS, 3 VS team), +0x734
// state (0-5 mission list: 0 wait, 1 browse, 2 confirm, 3 to the settings, 4 leave to the mission,
// 5 back; 6-10 settings menu: 6 wait, 7 menu, 8 leave to a sub-page, 9 leave the page, 10 back to
// the list), +0x738/+0x73A MenuGoTo target, +0x73C mission index, +0x740 previewed index, +0x744
// available count, +0x748 preview level, +0x74C row count, +0x74E row x, +0x750 thumbnail
// positions, +0x754 frame animation step, +0x758 frame time, +0x75C VS flag, +0x760/+0x764 text
// times.
#include "game/game.h"

#include "common/debug_hooks.h"
#include "common/ee_math.h"
#include "common/guest_stack.h"
#include "game/main_context.h"
#include "sce/sce.h"

namespace ac04::game {

using guest::GRef;

namespace {

AC04_GUEST_VAR(u32, g_MissionSelect, 0x0041182C)  // gp-0x41C4
AC04_GUEST_VAR(u32, g_pMenuFade, 0x00410E88)

constexpr u32 kLastIndex = 0x00411198;  // u16 per kind: the last mission index chosen

s32& State(u32 p) { return GRef<s32>(p + 0x734); }

u32 GameState() { return GRef<u32>(0x004104A4); }

}  // namespace

// EE: 0x00239C58
// Help line by state (jump table 0x004090E0): browsing/leaving the list the mission's help
// (per-kind table 0x003AEEB8), confirming in VS message 0x2C, in the settings menu the item's help
// (0x003AEEC8); otherwise hidden.
void UpdateMissionSelectHelp(u32 p) {
    s16 msg = -1;
    switch (State(p)) {
        case 1:
        case 3:
            msg = GRef<s16>(GRef<u32>(0x003AEEB8 + GRef<u32>(p + 0x730) * 4) + static_cast<u32>(GRef<s32>(p + 0x73C)) * 2);
            break;
        case 2:
        case 4:
            if (GRef<s32>(p + 0x75C) == 1) msg = 0x2C;
            break;
        case 7:
            msg = GRef<s16>(0x003AEEC8 + static_cast<u32>(GetMenuTreeSelection(p + 0x304)) * 2);
            break;
        default:
            if (static_cast<u32>(State(p)) < 0xB) {
                GRef<u32>(p + 0x714) = 0;
                return;
            }
            break;
    }
    if (GRef<s16>(p + 0x71A) != msg) {
        SetMessageBoxText(p + 0x70C, msg);
        GRef<f32>(p + 0x714) = K(0x004102E8);
    }
    UpdateMessageBox(K(0x004102EC), p + 0x70C, static_cast<s32>(Ctx()->frame_count));
}

// EE: 0x00239D50
// Rows and available missions of `kind` (table 0x00410E80): extra missions count the unlock bits
// of 0x002A031C (up to 5); VS shows 5 rows until bit 0 of 0x002A0318; the last index chosen for the
// kind if still available.
void GetMissionCounts(u32 p, s32 kind) {
    const u16 rows = GRef<u16>(0x00410E80 + static_cast<u32>(kind) * 2);
    GRef<u16>(p + 0x74C) = rows;
    GRef<u32>(p + 0x744) = rows;
    if (kind == 1) {
        u32 bits = GRef<u32>(0x002A031C);
        s32 n = 0;
        if (bits != 0) {
            n = 1;
            for (;;) {
                bits >>= 1;
                if (n >= 5 || bits == 0) break;
                ++n;
            }
        }
        GRef<s32>(p + 0x744) = n;
    } else if (kind == 3 && (GRef<u32>(0x002A0318) & 1) == 0) {
        GRef<u16>(p + 0x74C) = 5;
        GRef<u32>(p + 0x744) = 5;
    }
    const u32 last = GRef<u16>(kLastIndex + static_cast<u32>(kind) * 2);
    GRef<u32>(p + 0x73C) = last < GRef<u32>(p + 0x744) ? last : 0;
}

// EE: 0x00239E10
// Mission index `n` (0 when not available) and the crosshair to its row.
void SelectMissionRow(u32 p, u32 n) {
    GRef<u16>(p + 0x2FA) = 0xC50;
    if (!(n < GRef<u32>(p + 0x744))) n = 0;
    GRef<u32>(p + 0x73C) = n;
    const s32 x = static_cast<s32>(GRef<u16>(p + 0x74E) + n * 0x140 + 0x80);
    GRef<s16>(p + 0x2F8) = static_cast<s16>(x);
    MoveCrosshairAuto(GRef<u32>(p + 0x300), x, 0xC50);
}

// EE: 0x00239E58
// Mission list: fade, counts, row layout (centred: 0x1420 - 160 x rows), thumbnail positions
// (0x003AF150 by kind), frame, panel (5 rows, 0x003AF070), crosshair.
void InitMissionList(u32 p) {
    const u32 fade = g_pMenuFade();
    State(p) = 0;
    GRef<u16>(fade + 0x30) = 1;
    GRef<u16>(fade + 0x32) = 0;
    InitFade(p + 0x240, 1);
    u32 kind = GRef<u32>(p + 0x730);
    if (kind >= 4) {
        GRef<u32>(p + 0x730) = 0;
        kind = 0;
    }
    GetMissionCounts(p, static_cast<s32>(kind));
    const u32 rows = GRef<u16>(p + 0x74C);
    const u32 index = GRef<u32>(p + 0x73C);
    GRef<u32>(p + 0x740) = index;
    GRef<u32>(p + 0x748) = 0;
    GRef<u32>(p + 0x754) = 0;
    GRef<u16>(p + 0x74E) = static_cast<u16>(0x1400 - ((rows * 16 + (rows - 1) * 4) << 3));
    GRef<u32>(p + 0x750) = GRef<u32>(0x003AF150 + kind * 4);
    GRef<f32>(p + 0x758) = K(0x004102F0);
    ResetMissionFrame(p + 0x290);
    InitPanel(p + 0x2A0, 5, 0x003AF070, p + 0x250);
    GRef<u16>(p + 0x2A2) |= 0xC;
    SelectMissionRow(p, index);
    GRef<u32>(p + 0x714) = 0;
    GRef<u32>(p + 0x75C) = 0;
}

// EE: 0x00239F58
// Settings menu: fade, the menu tree with the items hidden by kind (free 0xFE0, extra 0xF1F, VS
// team 0xFF), counts; VS team: weather by mission (0x003AF180, 0-2, else 1) into the tree and the
// game state (+0x2E from 0x003AEEF0), and the three weather/time strings into 0x0042D1E0.
void InitMissionSettings(u32 p) {
    guest::GuestFrame frame(0x10);
    const u32 hidden = frame.Address();
    ee::memset(hidden, 0, 0x10);
    const u32 fade = g_pMenuFade();
    GRef<u16>(fade + 0x30) = 1;
    GRef<u16>(fade + 0x32) = 0;
    State(p) = 6;
    InitFade(p + 0x240, 1);
    u32 kind = GRef<u32>(p + 0x730);
    if (kind >= 4) {
        GRef<u32>(p + 0x730) = 0;
        kind = 0;
    }
    if (kind == 1) GRef<u32>(hidden) = 0xF1F;
    else if (kind == 0) GRef<u32>(hidden) = 0xFE0;
    else if (kind == 3) GRef<u32>(hidden) = 0xFF;
    InitMenuTree(p + 0x304, 4, 0x003AEFD0, 0x003AF030, hidden, 0x00411190);
    GetMissionCounts(p, static_cast<s32>(kind));
    if (kind == 3) {
        u32 n = GRef<u32>(p + 0x73C);
        if (n >= 8) n = 0;
        u32 weather = GRef<u16>(0x003AF180 + n * 2);
        if (weather >= 3) weather = 1;
        GRef<u16>(p + 0x35A) = static_cast<u16>(weather);
        GRef<u16>(GameState() + 0x2E) = GRef<u16>(0x003AEEF0 + weather * 2 + n * 6);
        u32 dst = 0x0042D1E0;
        u32 src = 0x003AEF20 + n * 12;
        for (s32 i = 2; i >= 0; --i, src += 4, dst += 0x40) {
            u64 args[1] = {GRef<u32>(src)};
            ee::sprintf(dst, 0x004111A0, args, 1);
        }
    }
    GRef<u32>(p + 0x764) = 0;
    GRef<u32>(p + 0x714) = 0;
    GRef<u32>(p + 0x760) = 0;
}

// EE: 0x0023A118
// Page object (0x770) by sub (jump table 0x00409110): 0x42-0x44 the extra, VS and VS team lists
// (VS flag for 0x43), 0x45-0x47 the settings menus of free, extra and VS team; else the free
// mission list.
u32 CreateMissionSelectPage() {
    const u32 p = WorkHeapAlloc(0x770, 0x10);
    switch (Ctx()->sub) {
        case 0x42:
            GRef<u32>(p + 0x730) = 1;
            InitMissionList(p);
            break;
        case 0x43:
            GRef<u32>(p + 0x730) = 2;
            InitMissionList(p);
            GRef<u32>(p + 0x75C) = 1;
            break;
        case 0x44:
            GRef<u32>(p + 0x730) = 3;
            InitMissionList(p);
            break;
        case 0x45:
            GRef<u32>(p + 0x730) = 0;
            InitMissionSettings(p);
            break;
        case 0x46:
            GRef<u32>(p + 0x730) = 1;
            InitMissionSettings(p);
            break;
        case 0x47:
            GRef<u32>(p + 0x730) = 3;
            InitMissionSettings(p);
            break;
        default:
            GRef<u32>(p + 0x730) = 0;
            InitMissionList(p);
            break;
    }
    return p;
}

// EE: 0x0023A208
// Frame `out` between rectangles `a` and `b` ({u16 x, y, w, h; f32 sx, sy}) at `t`: positions
// linear (truncated), scales eased (EvalHermite with k, clamped to 0..1).
void LerpFrame(f32 t, u32 out, u32 a, u32 b) {
    f32 e = EvalHermite(t, K(0x004102F4), K(0x004102F8));
    if (e < 0.0f) {
        e = 0.0f;
    } else if (1.0f < e) {
        e = 1.0f;
    }
    auto pos = [&](u32 o) {
        const u32 from = GRef<u16>(a + o);
        const s32 d = static_cast<s32>(GRef<u16>(b + o) - from);
        GRef<u16>(out + o) = static_cast<u16>(from + static_cast<u32>(fpu::FloatToInt(fpu::Mul(t, fpu::IntToFloat(d)))));
    };
    // The original's order: x and sx, y and sy, w, h.
    {
        const u32 from = GRef<u16>(a);
        const f32 fs = GRef<f32>(a + 8), ts = GRef<f32>(b + 8);
        const s32 d = static_cast<s32>(GRef<u16>(b) - from);
        const f32 ds = fpu::Mul(fpu::Sub(ts, fs), e);
        GRef<u16>(out) = static_cast<u16>(from + static_cast<u32>(fpu::FloatToInt(fpu::Mul(t, fpu::IntToFloat(d)))));
        GRef<f32>(out + 8) = fpu::Add(fs, ds);
    }
    {
        const u32 from = GRef<u16>(a + 2);
        const f32 fs = GRef<f32>(a + 0xC), ts = GRef<f32>(b + 0xC);
        const s32 d = static_cast<s32>(GRef<u16>(b + 2) - from);
        const f32 ds = fpu::Mul(fpu::Sub(ts, fs), e);
        GRef<u16>(out + 2) = static_cast<u16>(from + static_cast<u32>(fpu::FloatToInt(fpu::Mul(t, fpu::IntToFloat(d)))));
        GRef<f32>(out + 0xC) = fpu::Add(fs, ds);
    }
    pos(4);
    pos(6);
}

// EE: 0x0023A360
// The frame's first key rectangle (0x003AF190).
void ResetMissionFrame(u32 frame) {
    GRef<u64>(frame) = GRef<u64>(0x003AF190);
    GRef<u64>(frame + 8) = GRef<u64>(0x003AF198);
}

// EE: 0x0023A380
// Animated frame at time `t` (key times 0x003AF1D0, 8): steps 0-2 between the key rectangles
// (0x003AF190 + 16 x step), 3-4 the rectangle 0x003AF1C0, 5 towards the selected row's thumbnail
// rectangle, 6 that rectangle growing from scale 1 to 2, 7 held at scale 2 (jump table 0x00409130).
// Returns the step.
s32 AnimateMissionFrame(f32 t, u32 out, u32 p, s32 index) {
    const u32 keys = 0x003AF1D0;
    s32 step = 0;
    f32 local = 0.0f, cur = 0.0f, span = 0.0f;
    bool have_local = true;
    if (t < GRef<f32>(keys + 4)) {
        cur = GRef<f32>(keys);
        span = fpu::Sub(GRef<f32>(keys + 4), cur);
    } else {
        step = 1;
        for (;;) {
            if (step >= 7) {
                have_local = false;
                break;
            }
            const f32 next = GRef<f32>(keys + static_cast<u32>(step + 1) * 4);
            if (t < next) {
                cur = GRef<f32>(keys + static_cast<u32>(step) * 4);
                span = fpu::Sub(next, cur);
                break;
            }
            ++step;
        }
    }
    if (have_local) local = fpu::Div(fpu::Sub(t, cur), span);
    guest::GuestFrame frame(0x10);
    const u32 row = frame.Address();
    GRef<u32>(row) = GRef<u32>(GRef<u32>(p + 0x750) + static_cast<u32>(index) * 4);
    GRef<u16>(row + 6) = 0xC50;
    GRef<f32>(row + 0xC) = 1.0f;
    GRef<f32>(row + 8) = 1.0f;
    GRef<u16>(row + 4) = static_cast<u16>(GRef<u16>(p + 0x74E) + index * 0x140 + 0x80);
    switch (step) {
        case 0:
        case 1:
        case 2:
            LerpFrame(local, out, 0x003AF190 + static_cast<u32>(step) * 16, 0x003AF1A0 + static_cast<u32>(step) * 16);
            break;
        case 3:
        case 4:
            CopyQword(out, 0x003AF1C0);
            break;
        case 5:
            LerpFrame(local, out, 0x003AF1C0, row);
            break;
        case 6:
            CopyQword(out, row);
            GRef<f32>(out + 0xC) = 2.0f;
            GRef<f32>(out + 8) = 2.0f;
            LerpFrame(local, out, row, out);
            break;
        case 7:
            CopyQword(out, row);
            GRef<f32>(out + 0xC) = 2.0f;
            GRef<f32>(out + 8) = 2.0f;
            break;
        default:
            break;
    }
    return step;
}

// EE: 0x0023A550
// Accept a mission: VS asks to confirm first (state 2), then sets up the VS game (game[0x25] = 1,
// [0x29] = 2, mission id, player 1 aircraft 2 colour 0 slot 0) and leaves with the menu fade
// (state 4, target 2/0); other kinds go to the settings menu (state 3). SE 2; panel closes.
void AcceptMission(u32 p) {
    if (GRef<s32>(p + 0x730) == 2) {
        if (State(p) == 1) {
            State(p) = 2;
            PlayMenuSe(0, 2);
            return;
        }
        StartFade(0.5f, g_pMenuFade(), 1);
        State(p) = 4;
        GRef<u16>(p + 0x738) = 2;
        GRef<u16>(p + 0x73A) = 0;
        GRef<u8>(GameState() + 0x25) = 1;
        GRef<u8>(GameState() + 0x29) = 2;
        const s16 id = GetMissionId(2, GRef<s32>(p + 0x73C) + 1);
        GRef<u8>(GameState() + 0x27) = static_cast<u8>(id);
        GRef<u8>(GameState()) = 2;
        GRef<u8>(GameState() + 1) = 0;
        GRef<u8>(GameState() + 3) = 0;
    } else {
        State(p) = 3;
        StartFade(0.5f, p + 0x240, 1);
    }
    PlayMenuSe(0, 2);
    SetPanelState(p + 0x2A0, 1);
}

// EE: 0x0023A668
// List input (player 2 too in VS team): X/Circle accept; with more than one mission, the tab
// arrows and left/right through the available rows (SE 4).
void MissionListInput(u32 p) {
    MainContext* ctx = Ctx();
    u32 repeat = ctx->pad_buttons[0][2];
    u32 pressed = ctx->pad_buttons[0][1];
    if (GRef<s32>(p + 0x730) == 3) {
        pressed |= ctx->pad_buttons[1][1];
        repeat |= ctx->pad_buttons[1][2];
    }
    const s32 count = GRef<s32>(p + 0x744);
    const bool single = count < 2;
    if (single) {
        pressed &= 0xFFFFFFF3u;
        repeat &= 0xFFFFFFF3u;
    }
    const u32 fade = g_pMenuFade();
    const u16 tabs = static_cast<u16>(GRef<u16>(fade + 0x32) | 0x10);
    GRef<u16>(fade + 0x32) = tabs;
    if ((pressed & 0x50) != 0) {
        AcceptMission(p);
        return;
    }
    if (single) return;
    GRef<u16>(fade + 0x32) = static_cast<u16>(tabs | 0xC);
    if ((repeat & 0xC) == 0) return;
    s32 n = GRef<s32>(p + 0x73C);
    if ((repeat & 8) != 0) ++n;
    if ((repeat & 4) != 0) --n;
    n = (n + count) % count;
    GRef<u16>(p + 0x2A0) |= 0x200;
    GRef<u32>(p + 0x2F0) = 0;
    GRef<s32>(p + 0x73C) = n;
    SelectMissionRow(p, static_cast<u32>(n));
    PlayMenuSe(0, 4);
}

// EE: 0x0023A770
// Mission preview: once the cursor rests on the previewed mission and the frame has settled (step
// 6+), the preview fades in (SE 5 at its start); otherwise it fades out and, at 0 while resting,
// switches to the current mission. Left/right held restart the frame time.
void UpdateMissionPreview(u32 p) {
    if ((GRef<u16>(p + 0x2A2) & 8) == 0) return;
    MainContext* ctx = Ctx();
    u32 held = ctx->pad_buttons[0][0];
    if (GRef<s32>(p + 0x730) == 3) held |= ctx->pad_buttons[1][0];
    if (GRef<s32>(p + 0x744) < 2) held &= 0xFFFFFFF3u;
    if (State(p) != 1) held = 0;
    held &= 0xC;
    if (held != 0) GRef<u32>(p + 0x758) = 0;
    const bool resting = held == 0 && GRef<u16>(p + 0x250) == 0;
    if (resting && GRef<s32>(p + 0x73C) == GRef<s32>(p + 0x740) && GRef<s32>(p + 0x754) >= 6) {
        const f32 cur = GRef<f32>(p + 0x748);
        f32 level = fpu::Add(cur, K(0x004102FC));
        if (1.0f < level) level = 1.0f;
        if (cur <= 0.0f) PlayMenuSe(0, 5);
        GRef<f32>(p + 0x748) = level;
        return;
    }
    f32 level = fpu::Sub(GRef<f32>(p + 0x748), K(0x00410300));
    if (level < 0.0f) level = 0.0f;
    GRef<f32>(p + 0x748) = level;
    if (resting && level <= 0.0f) GRef<s32>(p + 0x740) = GRef<s32>(p + 0x73C);
}

// EE: 0x0023A8B8
// List page update: frame time and animation (SE 0xE when it starts moving), the panel and preview,
// then by state (jump table 0x00409150): 0 wait for the menu fade and the frame, 1 browse (Triangle
// back: SE 3, state 5), 2 VS confirm (Triangle back to 1, X/Circle accept), 3 to the settings menu
// (index saved), 4 MenuGoTo, 5 back to the campaign menu (MenuGoTo 1/3).
void UpdateMissionList(u32 p) {
    const u32 fade = g_pMenuFade();
    MainContext* ctx = Ctx();
    u32 pressed = ctx->pad_buttons[0][1];
    if (GRef<s32>(p + 0x730) == 3) pressed |= ctx->pad_buttons[1][1];
    if (GRef<s32>(p + 0x744) < 2) pressed &= 0xFFFFFFF3u;
    const f32 t = fpu::Add(GRef<f32>(p + 0x758), K(0x00410304));
    const s32 old_step = GRef<s32>(p + 0x754);
    GRef<f32>(p + 0x758) = t;
    const s32 step = AnimateMissionFrame(t, p + 0x290, p, GRef<s32>(p + 0x73C));
    GRef<s32>(p + 0x754) = step;
    if (old_step == 0 && step != 0) PlayMenuSe(0, 0xE);
    if (State(p) != 0) {
        UpdatePanel(p + 0x2A0);
        if ((GRef<u16>(p + 0x2A0) & 0x800) != 0) UpdateMissionPreview(p);
    }
    auto save_index = [&]() {
        GRef<u16>(kLastIndex + GRef<u32>(p + 0x730) * 2) = GRef<u16>(p + 0x73C);
    };
    switch (static_cast<u32>(State(p))) {
        case 0:
            if (K(0x00410308) < GRef<f32>(fade + 0xC) && GRef<s32>(p + 0x754) >= 3) State(p) = 1;
            break;
        case 1:
            GRef<u16>(fade + 0x32) |= 0x20;
            if ((pressed & 0x20) != 0) {
                PlayMenuSe(0, 3);
                StartFade(0.5f, p + 0x240, 1);
                SetPanelState(p + 0x2A0, 1);
                State(p) = 5;
            } else if ((GRef<u16>(p + 0x2A0) & 0x800) != 0) {
                MissionListInput(p);
            }
            break;
        case 2:
            GRef<u16>(fade + 0x32) |= 0x30;
            if ((pressed & 0x20) != 0) {
                PlayMenuSe(0, 3);
                State(p) = 1;
            } else if ((pressed & 0x50) != 0) {
                AcceptMission(p);
            }
            break;
        case 3:
            if ((GRef<u32>(p + 0x240) & 8) != 0) {
                State(p) = 0;
                save_index();
                InitMissionSettings(p);
            }
            break;
        case 4:
            if ((GRef<u32>(fade) & 8) != 0) {
                save_index();
                MenuGoTo(GRef<u16>(p + 0x738), GRef<u16>(p + 0x73A));
            }
            break;
        case 5:
            if ((GRef<u32>(p + 0x240) & 8) != 0) {
                save_index();
                MenuGoTo(1, 3);
            }
            break;
        default:
            break;
    }
}

// EE: 0x0023AB48
// Leave to sub-page `target` of the menu (state 8, page fade out, MenuGoTo kind 1).
void LeaveMissionSelect(u32 p, u16 target) {
    State(p) = 8;
    StartFade(0.5f, p + 0x240, 1);
    GRef<u16>(p + 0x73A) = target;
    GRef<u16>(p + 0x738) = 1;
}

// EE: 0x0023ABA0
// Leave the page with the menu fade (state 9) to MenuGoTo(kind, arg).
void LeaveMissionSelectMenu(u32 p, u16 kind, u16 arg) {
    State(p) = 9;
    StartFade(0.5f, g_pMenuFade(), 1);
    GRef<u16>(p + 0x73A) = arg;
    GRef<u16>(p + 0x738) = kind;
}

// EE: 0x0023AC00
// Triangle in the settings menu: closes the open column, or goes back to the list (state 10). SE 3.
void MissionSettingsCancel(u32 p, u32 tree) {
    const s32 column = GRef<s32>(tree);
    if (column == 0) {
        StartFade(0.5f, p + 0x240, 1);
        State(p) = 10;
    } else {
        GRef<s32>(tree) = 0;
        SetListWidgetState(tree + 8 + static_cast<u32>(column) * 0x18, 3);
    }
    PlayMenuSe(0, 3);
}

// EE: 0x0023AC70
// Settings menu item (jump table 0x00409170): 0/3/5/8 start the mission (game[0x25]/[0x29] and the
// mission id by kind 0/0/1/3; MenuGoTo 3 or 2), 1/6/9/10 hangar and records pages (MenuGoTo 0/7..),
// 2 the briefing of the mission (id kind 0), 4/7 save/load (with the HDD flag the column opens,
// else sub-pages 0x4D/0x4E), 0xB the VS weather column, 0xC-0xF save/load sub-pages, 0x10-0x12 VS
// weather choice. SE 2.
void MissionSelectAction(u32 p, u32 tree) {
    const s16 item = GetMenuTreeSelection(tree);
    auto start = [&](u16 kind, u8 g25, u8 g29, u32 id_kind) {
        LeaveMissionSelectMenu(p, kind, 0);
        GRef<u8>(GameState() + 0x25) = g25;
        GRef<u8>(GameState() + 0x29) = g29;
        const s16 id = GetMissionId(id_kind, GRef<s32>(p + 0x73C) + 1);
        GRef<u8>(GameState() + 0x27) = static_cast<u8>(id);
    };
    switch (static_cast<u16>(item)) {
        case 0: start(3, 1, 2, 0); break;
        case 1: LeaveMissionSelectMenu(p, 0, 7); break;
        case 2: {
            const s16 id = GetMissionId(0, GRef<s32>(p + 0x73C) + 1);
            GRef<u8>(GameState() + 0x27) = static_cast<u8>(id);
            LeaveMissionSelectMenu(p, 0, 4);
            break;
        }
        case 3: start(2, 1, 3, 0); break;
        case 4:
            if ((Ctx()->flags & 0x80000) != 0) {
                GRef<s32>(tree) = 1;
                SetListWidgetState(tree + 0x20, 1);
            } else {
                LeaveMissionSelect(p, 0x4D);
            }
            break;
        case 5: start(2, 1, 2, 1); break;
        case 6: LeaveMissionSelectMenu(p, 0, 8); break;
        case 7:
            if ((Ctx()->flags & 0x80000) != 0) {
                GRef<s32>(tree) = 2;
                SetListWidgetState(tree + 0x38, 1);
            } else {
                LeaveMissionSelect(p, 0x4E);
            }
            break;
        case 8: start(2, 2, 2, 3); break;
        case 9: LeaveMissionSelectMenu(p, 0, 9); break;
        case 10: LeaveMissionSelectMenu(p, 0, 10); break;
        case 0xB:
            GRef<s32>(tree) = 3;
            SetListWidgetState(tree + 0x50, 1);
            GRef<u16>(tree + 0x56) = GRef<u16>(0x003AF180 + static_cast<u32>(GRef<s32>(p + 0x73C)) * 2);
            break;
        case 0xC: LeaveMissionSelect(p, 0x54); break;
        case 0xD: LeaveMissionSelect(p, 0x4D); break;
        case 0xE: LeaveMissionSelect(p, 0x55); break;
        case 0xF: LeaveMissionSelect(p, 0x4E); break;
        case 0x10:
        case 0x11:
        case 0x12: {
            GRef<s32>(tree) = 0;
            SetListWidgetState(tree + 0x50, 3);
            const u32 n = static_cast<u32>(GRef<s32>(p + 0x73C));
            const u32 weather = static_cast<u32>(item) - 0x10;
            GRef<u16>(0x003AF180 + n * 2) = static_cast<u16>(weather);
            GRef<u32>(p + 0x764) = 0;
            GRef<u16>(GameState() + 0x2E) = GRef<u16>(0x003AEEF0 + weather * 2 + n * 6);
            break;
        }
        default:
            break;
    }
    PlayMenuSe(0, 2);
}

// EE: 0x0023AF78
// Settings menu states (jump table 0x004091C0): 6 wait for the menu fade, 7 the menu (tabs 0x33;
// Triangle cancel, X/Circle action), 8/9 MenuGoTo once the page or menu fade is out, 10 back to
// the list.
void MissionSelectUpdate(u32 p) {
    MainContext* ctx = Ctx();
    u32 repeat = ctx->pad_buttons[0][2];
    u32 pressed = ctx->pad_buttons[0][1];
    if (GRef<s32>(p + 0x730) == 3) {
        pressed |= ctx->pad_buttons[1][1];
        repeat |= ctx->pad_buttons[1][2];
    }
    const u32 fade = g_pMenuFade();
    switch (State(p)) {
        case 6:
            if (K(0x0041030C) < GRef<f32>(fade + 0xC)) State(p) = 7;
            break;
        case 7: {
            const u32 tree = p + 0x304;
            GRef<u16>(fade + 0x32) = 0x33;
            UpdateMenuTree(tree, repeat);
            if ((pressed & 0x20) != 0) {
                MissionSettingsCancel(p, tree);
            } else if ((pressed & 0x50) != 0) {
                MissionSelectAction(p, tree);
            }
            break;
        }
        case 8:
            if ((GRef<u32>(p + 0x240) & 8) != 0) MenuGoTo(GRef<u16>(p + 0x738), GRef<u16>(p + 0x73A));
            break;
        case 9:
            if ((GRef<u32>(fade) & 8) != 0) MenuGoTo(GRef<u16>(p + 0x738), GRef<u16>(p + 0x73A));
            break;
        case 10:
            if ((GRef<u32>(p + 0x240) & 8) != 0) {
                State(p) = 0;
                InitMissionList(p);
            }
            break;
        default:
            break;
    }
}

namespace {

// The "finished" bits of the panel texts (+0x2A0): below 0 the text is still sliding in (0x8000);
// past `settle` the panel flag `bit` (set when the text started) is cleared.
void FinishPanelText(u32 p, f32 rest, f32 settle_value, f32 settle, u32 saved, u16 bit, u16 extra) {
    if (rest < 0.0f) {
        GRef<u16>(p + 0x2A0) |= 0x8000;
    } else if (settle < settle_value && (saved & bit) != 0) {
        GRef<u16>(p + 0x2A0) = static_cast<u16>((GRef<u16>(p + 0x2A0) & ~bit) | extra);
    }
}

}  // namespace

// EE: 0x0023B108
// Records panel (text layer): the mission heading (kind name and number, column titles, three
// ranks), the mission name and its difficulty/rank line (free mission: records 0x002A045C +
// difficulty x 0x168 + mission x 0x14; extra missions: 0x002A0CCC + mission x 0x14), the three best
// times (h'm"s) and points, and the row tabs (available / locked) with their numbers; each part
// slides in with its time (+0x2EC, +0x2F0, +0x2F4) and marks the panel when settled. Then the
// selected row's pulse box.
void DrawMissionRecords(u32 list, s32 index, u32 p) {
    const u32 saved = GRef<u16>(p + 0x2A0);
    GRef<u16>(p + 0x2A0) = static_cast<u16>(saved & 0x7FFF);
    const s32 n = GRef<s32>(p + 0x73C);
    const s32 kind = GRef<s32>(p + 0x730);
    BeginMenuTextLayer(list, index);
    guest::GuestFrame frame(0x100);
    const u32 buf = frame.Address();
    u64 args[3];
    const f32 t0 = GRef<f32>(p + 0x2EC);
    if (0.0f < t0) {
        f32 x = fpu::Mul(t0, 90.0f);
        DrawMenuText(x, 0x240, 0x520, 4, 0x004111D0);
        DrawMenuText(fpu::Sub(x, 8.0f), 0x300, 0x520, 4, 0x00409220);
        DrawMenuText(fpu::Sub(x, 16.0f), 0x300, 0x6F0, 4, 0x00409238);
        if (kind == 0) {
            DrawMenuText(fpu::Sub(x, 24.0f), 0x300, 0x8B0, 4, 0x004111D8);
        } else {
            DrawMenuText(fpu::Sub(x, 24.0f), 0x300, 0x8B0, 4, 0x00409248);
        }
        args[0] = GRef<u32>(0x003AF1F0 + static_cast<u32>(kind) * 4);
        args[1] = static_cast<u64>(static_cast<s64>(n + 1));
        ee::sprintf(buf, 0x004111E0, args, 2);
        DrawMenuText(fpu::Sub(x, 32.0f), 0x300, 0x5E0, 3, buf);
        x = fpu::Sub(x, 16.0f);
        const f32 x2 = fpu::Sub(x, 8.0f);
        DrawMenuText(x, 0x1400, 0x520, 4, 0x004111E8);
        DrawMenuText(x2, 0x1D00, 0x520, 4, 0x004111F0);
        s32 y = 0x5E0;
        for (s32 i = 1; i < 4; ++i, y += 0xD0) {
            args[0] = static_cast<u64>(i);
            ee::sprintf(buf, 0x004111F8, args, 1);
            const f32 d = fpu::Mul(fpu::IntToFloat(i), 8.0f);
            DrawMenuText(fpu::Sub(x, d), 0x1400, y, 3, buf);
            DrawMenuText(fpu::Sub(x2, d), 0x1D00, y, 3, buf);
        }
        const f32 rest = fpu::Sub(x, 52.0f);
        FinishPanelText(p, rest, rest, 4.0f, saved, 0x100, 0);
    }
    const f32 t1 = fpu::Sub(GRef<f32>(p + 0x2F0), K(0x00410310));
    if (0.0f < t1) {
        f32 x = fpu::Mul(t1, 90.0f);
        u32 rec = 0;
        args[0] = GRef<u32>(GRef<u32>(0x003AF160 + static_cast<u32>(kind) * 4) + static_cast<u32>(n) * 4);
        ee::sprintf(buf, 0x004111A0, args, 1);
        DrawMenuText(x, 0x300, 0x7A0, 3, buf);
        if (kind == 1) {
            rec = 0x002A0CCC + static_cast<u32>(n) * 0x14;
            u32 rank = GRef<u16>(rec);
            if (rank >= 5) rank = 0;
            args[0] = GRef<u32>(GRef<u32>(0x003AF170) + static_cast<u32>(n) * 4);
            args[1] = GRef<u32>(0x003AF200 + rank * 4);
            ee::sprintf(buf, 0x00411200, args, 2);
        } else if (kind == 0) {
            const u32 difficulty = GRef<u8>(GameState() + 0x28);
            rec = 0x002A045C + difficulty * 0x168 + static_cast<u32>(n) * 0x14;
            const u32 d = difficulty < 6 ? difficulty : 0;
            u32 rank = GRef<u16>(rec);
            if (rank >= 5) rank = 0;
            args[0] = GRef<u32>(0x003845E0 + d * 4);
            args[1] = GRef<u32>(0x003AF200 + rank * 4);
            ee::sprintf(buf, 0x00411200, args, 2);
        } else if (kind >= 2 && kind < 4) {
            args[0] = GRef<u32>(GRef<u32>(0x003AF170 + static_cast<u32>(kind - 1) * 4) + static_cast<u32>(n) * 4);
            ee::sprintf(buf, 0x004111A0, args, 1);
        }
        DrawMenuText(fpu::Sub(x, 8.0f), 0x300, 0x960, 3, buf);
        const bool early = n < 3;
        x = fpu::Sub(x, 14.0f);
        const f32 x2 = fpu::Sub(x, 8.0f);
        s32 y = 0x5E0;
        for (u32 i = 0; i < 3; ++i, y += 0xD0) {
            if (rec == 0 || (kind == 1 && early)) {
                ee::sprintf(buf, 0x00409258, nullptr, 0);  // --'--"--
            } else {
                const s32 t = GRef<s32>(rec + 8 + i * 4);
                args[0] = static_cast<u64>(static_cast<s64>((t / 3600) % 100));
                args[1] = static_cast<u64>(static_cast<s64>((t / 60) % 60));
                args[2] = static_cast<u64>(static_cast<s64>((t % 60) * 100 / 60));
                ee::sprintf(buf, 0x00409268, args, 3);
            }
            const f32 fi = fpu::IntToFloat(static_cast<s32>(i));
            DrawMenuText(fpu::Sub(x, fpu::Mul(fi, 8.0f)), 0x1500, y, 3, buf);
            if (rec == 0 || (kind == 1 && !early)) {
                ee::sprintf(buf, 0x00409278, nullptr, 0);  // ----- PTS
            } else {
                u32 pts = GRef<u16>(rec + 2 + i * 2);
                if (pts > 99999) pts = 99999;
                args[0] = pts;
                ee::sprintf(buf, 0x00409288, args, 1);
            }
            DrawMenuText(fpu::Sub(x2, fpu::Mul(fi, 8.0f)), 0x1E00, y, 3, buf);
        }
        const f32 rest = fpu::Sub(x, 40.0f);
        FinishPanelText(p, rest, x, 6.0f, saved, 0x200, 0);
    }
    const f32 t2 = GRef<f32>(p + 0x2F4);
    if (0.0f < t2) {
        const f32 x = fpu::Mul(t2, 90.0f);
        const u32 rows = GRef<u16>(p + 0x74C);
        const s32 count = GRef<s32>(p + 0x744);
        u32 rx = GRef<u16>(p + 0x74E);
        f32 xi = x;
        for (s32 i = 0; i < static_cast<s32>(rows);) {
            DrawMenuText(xi, static_cast<s32>(rx), 0xBE0, 8, i < count ? 0x00411208u : 0x00411210u);
            ++i;
            args[0] = static_cast<u64>(i);
            ee::sprintf(buf, 0x00411218, args, 1);
            DrawMenuText(xi, static_cast<s32>(rx + 0x20), 0xCF0, 5, buf);
            rx += 0x140;
            xi = fpu::Sub(xi, 1.0f);
        }
        if (5.0f < x && (GRef<u16>(p + 0x2A2) & 2) == 0) GRef<u16>(p + 0x2A2) |= 2;
        const f32 rest = fpu::Sub(x, fpu::Add(fpu::IntToFloat(static_cast<s32>(rows)), 1.0f));
        FinishPanelText(p, rest, rest, 5.0f, saved, 0x400, 0x800);
    }
    EndMenuTextLayer();
    if ((GRef<u16>(p + 0x2A0) & 0x800) != 0 && GRef<u16>(p + 0x250) == 0)
        DrawPulseBox(list, index, 0x1067, static_cast<s32>(GRef<u16>(p + 0x74E) + GRef<u32>(p + 0x73C) * 0x140), 0xBE0, 0x100,
                     static_cast<s32>(Ctx()->frame_count));
}

// EE: 0x0023B930
// Mission thumbnail from the page image (+0x224 packet): kind x 0x480 + n x 64 in the image (TEX0),
// swinging with sinf(level x k) around the fade position (+0x264/+0x266).
void DrawMissionThumbnail(f32 level, u32 list, s32 index, u32 p, u32 kind, u32 n) {
    if ((GRef<u16>(p + 0x2A2) & 8) == 0 || !(0.0f < level)) return;
    if (1.0f < level) level = 1.0f;
    if (kind >= 4) kind = 0;
    if (!(n < GRef<u32>(p + 0x744))) n = 0;
    const u32 start = Ctx()->cur_buf.addr;
    const u32 pkt = WriteDmaCallTag(start, GRef<u32>(p + 0x224));
    u32 q = BuildMenuDrawEnv(pkt + 0x10, 0x280, 0x1C0);
    const f32 a = fpu::Mul(level, K(0x00410314));
    GRef<u64>(q) = 0x11AB400000000001ull;
    GRef<u64>(q + 8) = 0xE;
    GRef<u64>(q + 0x10) = 0x7000000064ull;
    GRef<u64>(q + 0x18) = 0x43;
    const f32 s = ee::sinf(a);
    q += 0x20;
    const u64 tbp = static_cast<u64>(static_cast<s64>(static_cast<s32>(kind * 0x480 + 0x1C00 + n * 0x40)));
    const u64 cbp = static_cast<u64>(static_cast<s64>(static_cast<s32>(kind * 0x80 + n * 4 + 0x2C00))) << 37;
    const u64 tex0 = (tbp | 0x29308000u) | 0x2000000000000000ull | cbp | 0x280000000ull;
    const s32 dy = fpu::FloatToInt(fpu::Mul(s, 1488.0f));
    const s32 dx = fpu::FloatToInt(fpu::Mul(s, 2048.0f));
    const u32 fy = GRef<u16>(p + 0x266), fx = GRef<u16>(p + 0x264);
    auto sext = [](u32 v) { return static_cast<u64>(static_cast<s64>(static_cast<s32>(v))); };
    GRef<u64>(q) = 0x6400000000008001ull;  // REGLIST {TEX0_2? (reg 7), RGBAQ, UV, XYZ3, UV, XYZ2}
    GRef<u64>(q + 0x10) = tex0;
    GRef<u64>(q + 8) = 0x53D317;
    GRef<u64>(q + 0x20) = 0x80008;
    GRef<u64>(q + 0x38) = sext(fx + static_cast<u32>(dx)) | sext(fy + static_cast<u32>(dy)) << 16;
    GRef<u64>(q + 0x18) = 0x80808080;
    GRef<u64>(q + 0x28) = static_cast<u64>(fx) | static_cast<u64>(fy) << 16;
    GRef<u64>(q + 0x30) = 0x5D80808;
    const u32 end = CloseDmaPacket(pkt, q + 0x40);
    LinkPacket(list, index, start, end);
}

// EE: 0x0023BBA0
// The animated frame sprite (image +0x224) from the frame rectangle {x, y, w, h, sx, sy}, scaled
// about (w + 0x6C00, h + 0x7200).
void DrawMissionFrame(u32 list, s32 index, u32 p, u32 rect) {
    const u32 start = Ctx()->cur_buf.addr;
    const u32 pkt = WriteDmaCallTag(start, GRef<u32>(p + 0x224));
    u32 q = BuildMenuDrawEnv(pkt + 0x10, 0x280, 0x1C0);
    const u32 y = GRef<u16>(rect + 2), x = GRef<u16>(rect);
    const f32 sy = GRef<f32>(rect + 0xC), sx = GRef<f32>(rect + 8);
    const s32 ty = static_cast<s32>(GRef<u16>(rect + 6) + 0x7200), tx = static_cast<s32>(GRef<u16>(rect + 4) + 0x6C00);
    const s32 y1 = fpu::FloatToInt(fpu::Mul(sy, fpu::IntToFloat(static_cast<s32>(0x1610 - y))));
    const s32 y0 = fpu::FloatToInt(fpu::Mul(sy, fpu::IntToFloat(-static_cast<s32>(y))));
    const s32 x1 = fpu::FloatToInt(fpu::Mul(sx, fpu::IntToFloat(static_cast<s32>(0x1FF0 - x))));
    const s32 x0 = fpu::FloatToInt(fpu::Mul(sx, fpu::IntToFloat(-static_cast<s32>(x))));
    auto sext = [](s32 v) { return static_cast<u64>(static_cast<s64>(v)); };
    GRef<u64>(q + 8) = 0xE;
    GRef<u64>(q + 0x10) = 0x8000000068ull;
    GRef<u64>(q) = 0x11AB400000000001ull;
    GRef<u64>(q + 0x18) = 0x43;
    q += 0x20;
    GRef<u64>(q) = 0x6400000000008001ull;
    GRef<u64>(q + 8) = 0x53D317;
    GRef<u64>(q + 0x10) = 0x2006200265322E00ull;
    GRef<u64>(q + 0x38) = sext(tx + x1) | sext(ty + y1) << 16;
    GRef<u64>(q + 0x18) = 0x808080;
    GRef<u64>(q + 0x20) = 0x90008;
    GRef<u64>(q + 0x28) = sext(tx + x0) | sext(ty + y0) << 16;
    GRef<u64>(q + 0x30) = 0x16191FF8;
    const u32 end = CloseDmaPacket(pkt, q + 0x40);
    LinkPacket(list, index, start, end);
}

// EE: 0x0023BD78
// Mission list page: buckets (2), frame, menu fade (bucket by the panel level), panel, thumbnail and
// records once the panel is up, help line, black fade.
void DrawMissionList(u32 p) {
    InitBucketList(p, 2);
    SetPacketListBuckets(kMainContextAddress + 0x60, p);
    DrawMissionFrame(p, 0, p, p + 0x290);
    DrawMenuFade(p, K(0x00410318) < GRef<f32>(p + 0x2AC) ? 0 : 1, g_pMenuFade());
    DrawPanel(p, 0, p + 0x2A0);
    if (K(0x0041031C) < GRef<f32>(p + 0x2AC)) {
        DrawMissionThumbnail(GRef<f32>(p + 0x748), p, 0, p, GRef<u32>(p + 0x730), GRef<u32>(p + 0x740));
        DrawMissionRecords(p, 0, p);
    }
    UploadMessageBoxGlyphs(p, 0, p + 0x70C);
    DrawMessageBox(p, 0, p + 0x70C);
    DrawBlackFade(GRef<f32>(p + 0x24C), p, 0);
}

// EE: 0x0023BE88
// Settings menu texts at row `y` (text layer): per row (labels 0x003AF218) the player 1 value
// (name 0x003AF224, aircraft code, special weapon code of its slot) and in VS team player 2's; VS
// team also the weather line (+0x2E of the game state); tick SE 9 every other frame while a text
// types in.
void DrawMissionSettingsTexts(u32 list, s32 index, u32 p, s32 y) {
    guest::GuestFrame frame(0x140);
    const u32 buf = frame.Address(), values = buf + 0x100;
    const s32 kind = GRef<s32>(p + 0x730);
    bool typing = false;
    for (u32 i = 0; i < 2; ++i) {
        const u32 slot = GameState() + i * 16;
        GRef<u32>(values + i * 12) = GRef<u32>(0x003AF224 + i * 4);
        u32 special = GRef<u8>(slot + 3);
        const u32 aircraft = GRef<u8>(slot);
        if (special >= 3) special = 0;
        GRef<u32>(values + i * 12 + 4) = GetAircraftCode(aircraft);
        const u32 entry = GetAircraftLoadoutEntry(aircraft, static_cast<s32>(special));
        GRef<u32>(values + i * 12 + 8) = GetSpecialWeaponCode(static_cast<u32>(static_cast<s32>(GRef<s8>(entry))));
    }
    BeginMenuTextLayer(list, index);
    const f32 base = fpu::Mul(GRef<f32>(p + 0x760), 90.0f);
    s32 row = y + 0x1C0;
    u64 args[1];
    for (s32 i = 0; i < 3; ++i, row += 0xF0) {
        const f32 x = fpu::Sub(base, fpu::Mul(fpu::IntToFloat(i + 1), 8.0f));
        DrawMenuText(x, 0x480, row, 3, GRef<u32>(0x003AF218 + static_cast<u32>(i) * 4));
        args[0] = GRef<u32>(values + static_cast<u32>(i) * 4);
        ee::sprintf(buf, 0x00411240, args, 1);
        DrawMenuText(fpu::Sub(x, 9.0f), 0xA20, row, 3, buf);
        if (0.0f < x && fpu::Sub(x, 15.0f) < 0.0f) typing = true;
        if (kind == 3) {
            args[0] = GRef<u32>(values + 0xC + static_cast<u32>(i) * 4);
            ee::sprintf(buf, 0x00411240, args, 1);
            DrawMenuText(fpu::Sub(x, 16.0f), 0xF00, row, 3, buf);
            if (0.0f < x && fpu::Sub(x, 25.0f) < 0.0f) typing = true;
        }
    }
    if (kind == 3) {
        f32 x = fpu::Sub(fpu::Mul(GRef<f32>(p + 0x760), 90.0f), 32.0f);
        DrawMenuText(x, 0x480, row, 3, 0x00411248);
        DrawMenuText(fpu::Sub(x, 9.0f), 0xA40, row, 3, 0x00411250);
        const f32 w = fpu::Sub(fpu::Mul(GRef<f32>(p + 0x764), 90.0f), 4.0f);
        if (w < x) x = w;
        args[0] = GRef<u16>(GameState() + 0x2E);
        ee::sprintf(buf, 0x00411258, args, 1);
        DrawMenuText(fpu::Sub(x, 10.0f), 0xAA0, row, 3, buf);
        if (0.0f < x && fpu::Sub(x, 16.0f) < 0.0f) typing = true;
    }
    EndMenuTextLayer();
    if (typing && (Ctx()->frame_count & 1) == 0) PlayMenuSe(0, 9);
}

// EE: 0x0023C230
// Title bar sliding in by `t` from (x, y) by (dx, dy) (the trailing edge by at most 0x500): a
// two-colour strip (LerpColor black -> `color` by t, then the pulse sinf(t x k) of 0x808080).
void DrawTitleBar(f32 t, u32 list, s32 index, s32 x, s32 y, s32 dx, s32 dy, u32 color) {
    if (t <= 0.0f) return;
    if (1.0f < t) t = 1.0f;
    const f32 fdx = fpu::IntToFloat(dx), fdy = fpu::IntToFloat(dy);
    const s32 cdx = dx > 0x500 ? 0x500 : dx, cdy = dy > 0x500 ? 0x500 : dy;
    const f32 fcx = fpu::IntToFloat(cdx), fcy = fpu::IntToFloat(cdy);
    const u32 start = Ctx()->cur_buf.addr;
    const s32 x1 = x + fpu::FloatToInt(fpu::Mul(t, fdx));
    const s32 y1 = y + fpu::FloatToInt(fpu::Mul(t, fdy));
    const s32 x0 = x1 - fpu::FloatToInt(fpu::Mul(t, fcx));
    const s32 y0 = y1 - fpu::FloatToInt(fpu::Mul(t, fcy));
    u32 q = BuildMenuDrawEnv(start + 0x10, 0x280, 0x1C0);
    auto sext = [](s32 v) { return static_cast<u64>(static_cast<s64>(v)); };
    GRef<u64>(q) = 0x1125400000000002ull;
    GRef<u64>(q + 8) = 0xE;
    GRef<u64>(q + 0x10) = 0x800000008000ull;
    GRef<u64>(q + 0x20) = 0x8000000068ull;
    GRef<u64>(q + 0x18) = 0x19;
    GRef<u64>(q + 0x28) = 0x43;
    q += 0x30;
    GRef<u64>(q) = 0x4400000000008002ull;  // REGLIST x2 {RGBAQ, XYZ3, RGBAQ, XYZ2}
    GRef<u64>(q + 8) = 0x51D1;
    GRef<u64>(q + 0x10) = LerpColor(t, 0, color) & 0xFFFFFFFFull;
    const u64 head = sext(x1 + 0x8000) | sext(y1 + 0x8000) << 16;
    const f32 a = fpu::Mul(t, K(0x00410320));
    GRef<u64>(q + 0x30) = 0;
    GRef<u64>(q + 0x28) = head;
    GRef<u64>(q + 0x38) = sext(x0 + 0x8000) | sext(y0 + 0x8000) << 16;
    GRef<u64>(q + 0x48) = head;
    GRef<u64>(q + 0x18) = sext(x + 0x8000) | sext(y + 0x8000) << 16;
    GRef<u64>(q + 0x20) = color;
    GRef<u64>(q + 0x40) = LerpColor(ee::sinf(a), 0x808080, 0) & 0xFFFFFFFFull;
    const u32 end = CloseDmaPacket(start, q + 0x50);
    LinkPacket(list, index, start, end);
}

// EE: 0x0023C4C0
// Settings menu page: the title bar row by kind (extra 0xBE0, VS team 0xD60, else 0xEE0), its level
// (sinf ease of +0x760 x 5, 1 once past), buckets (1), menu tree, texts, title bar, help line,
// fades.
void DrawMissionSettings(u32 p) {
    const s32 kind = GRef<s32>(p + 0x730);
    s32 y = 0xEE0;
    if (kind == 1) y = 0xBE0;
    else if (kind == 3) y = 0xD60;
    f32 level = fpu::Mul(GRef<f32>(p + 0x760), 5.0f);
    if (1.0f < level) {
        level = 1.0f;
    } else {
        level = ee::sinf(fpu::Mul(level, K(0x00410324)));
    }
    InitBucketList(p, 1);
    SetPacketListBuckets(kMainContextAddress + 0x60, p);
    DrawMenuTree(p, 0, p + 0x304);
    DrawMissionSettingsTexts(p, 0, p, y);
    DrawTitleBar(level, p, 0, 0x400, y, 0x2000, 0, 0xA0825B);
    UploadMessageBoxGlyphs(p, 0, p + 0x70C);
    DrawMessageBox(p, 0, p + 0x70C);
    DrawBlackFade(GRef<f32>(p + 0x24C), p, 0);
    DrawMenuFade(p, 0, g_pMenuFade());
}

// EE: 0x0023C600
// Subs 0x42-0x47: work heap mark, menu draw mode with Z test, the page, DATA 0x110 (2 items), BGM
// 4, menu fade frame; sub 0x48.
void EnterMissionSelect() {
    PopWorkHeapMark();
    PushWorkHeapMark();
    Ctx()->draw_mode = 2;
    Ctx()->drawenv_pkt[4] = 0x1310000DCull;
    Ctx()->drawenv_pkt[0x14] = 0x1310000DCull;
    const u32 p = CreateMissionSelectPage();
    g_MissionSelect() = p;
    ResetMenuDataLoad(p + 0x10);
    QueueMenuDataLoad(p + 0x10, 0x110, 2, p + 0x220);
    PlayMenuBgm(4);
    DrawMenuFadeFrame(p, g_pMenuFade());
    RequestSubState(0x48);
}

// EE: 0x0023C6A8
// Sub 0x48: once loaded, the font block and the help line box; sub 0x49.
void LoadMissionSelect() {
    const u32 p = g_MissionSelect();
    DrawMenuFadeFrame(p, g_pMenuFade());
    if (UpdateMenuDataLoad(p + 0x10) < 0) {
        LoadFontBlock(GRef<u32>(p + 0x220));
        InitMessageBox(p + 0x70C);
        RequestSubState(0x49);
    }
}

// EE: 0x0023C708
// Sub 0x49: fade, help line; the mission list (states < 6) or the settings menu (text times
// advance); then its draw.
void MissionSelectRun() {
    debug::OnGuestCheckpoint(0x0023C708);
    const u32 p = g_MissionSelect();
    const u32 fade = g_pMenuFade();
    UpdateFade(p + 0x240);
    UpdateMissionSelectHelp(p);
    GRef<u16>(fade + 0x32) = 0;
    if (static_cast<u32>(State(p)) < 6) {
        UpdateMissionList(p);
    } else {
        const f32 step = K(0x00410328);
        const f32 a = fpu::Add(GRef<f32>(p + 0x760), step);
        const f32 b = fpu::Add(GRef<f32>(p + 0x764), step);
        GRef<f32>(p + 0x760) = a;
        GRef<f32>(p + 0x764) = b;
        MissionSelectUpdate(p);
    }
    if (static_cast<u32>(State(p)) < 6) {
        DrawMissionList(p);
    } else {
        DrawMissionSettings(p);
    }
}

AC04_GUEST_FUNCTION(0x0023C600, EnterMissionSelect);
AC04_GUEST_FUNCTION(0x0023C6A8, LoadMissionSelect);
AC04_GUEST_FUNCTION(0x0023C708, MissionSelectRun);

}  // namespace ac04::game
