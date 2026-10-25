// Scene 2 (SceneFlight): the frame dispatcher and the mission-load sub-states 0-8 and 0x1E, with
// the loading screen and the system creation/reset helpers they call. Exact creation order and
// allocation sizes: docs/subsystems/mission-flow.md (the work heap is unchecked and 98 % full in
// mission 1, so every size and order matters). Sub-state flow: docs/subsystems/mission-flow.md.
//
// S = flight scene object (gp 0x004104C4, 0x1810 bytes). Sub 9 (cameras and drawing) is here; the
// sub-state handlers 0xA-0x1D (gameplay, cutscenes, replay, menus) belong to block B/C and are reached through the guest table 0x002A1620;
// until they are translated CallGuest logs them once and returns.
#include "game/game.h"

#include "common/debug_hooks.h"
#include "common/ee_math.h"
#include "common/guest_stack.h"
#include "game/main_context.h"
#include "platform/log.h"
#include "sce/sce.h"

namespace ac04::game {

using guest::GRef;

AC04_GUEST_VAR(u32, g_FlightDataEntry, 0x004117A8)

namespace {

u8 Game(u32 i) { return GRef<u8>(g_GameState().addr + i); }
void SetGame(u32 i, u8 v) { GRef<u8>(g_GameState().addr + i) = v; }
u32 S() { return g_FlightScene(); }

void Put64(u32 p, u64 v) { GRef<u64>(p) = v; }

}  // namespace

// ---- Frame dispatcher -------------------------------------------------------------------------

// EE: 0x0010BA98
// Sub 9 handler: view cameras and draw lists only.
void func_0010BA98() {
    UpdateViewCameras();
    BuildFlightDrawLists();
}
AC04_GUEST_FUNCTION(0x0010BA98, func_0010BA98);

namespace {

// --freeze-sim (debug layer, S5 block A; docs/port/command-line.md): replaces FlightPlayFrame
// 0x0010B150 (subs 0xA-0xC) and the special-sequence handler 0x0010BA38 (sub 0x13, the opening of
// mission id 0x25) with their block A parts only: random index (FlightPlayFrame only), terrain/world
// update, cameras, draw lists. FlightSimStep, the sequence step 0x0010C748, the player,
// sound-listener and skip passes are not run.
void FreezeSimFrame(bool play_frame) {
    if (play_frame) SetRandomIndex(GRef<u32>(S() + 0x165C));
    func_00139FE8(GRef<u32>(0x00410694));
    func_00144CC0(GRef<u32>(0x0041069C));
    func_0013EC20(GRef<u32>(0x00410698));
    const u32 t = GRef<u32>(0x00410694);
    const u32 e = GRef<u32>(t) + GRef<u32>(t + 4) * 100;
    func_0013EC38(GRef<u32>(0x00410698), GRef<u32>(e + 0x44), GRef<u32>(e + 0x50));
    UpdateViewCameras();
    BuildFlightDrawLists();
}

// --freeze-sim, sub 0x13: the skip pass of UpdateInEngineSequence 0x00177058 (EE 0x00178C30-
// 0x00178DA4) without the keyframe player, so a frozen opening can still be left the original
// way: a Start press (game button 6, +0x2B4 bit 0x40) starts a 32-frame countdown, then the
// sequence ends as at its last frame (mission id 0x25 -> mission 1, 0x24 -> mission 0xF, 0x23 ->
// menu, sub 0xE -> sub 0xF). The end on reaching the sequence length is not taken: the frame
// counter +0x114 does not advance.
void FreezeSimSequenceSkip() {
    const u32 seq = GRef<u32>(0x00410B18);
    bool end = false;
    if ((Ctx()->pad_buttons[0][1] & 0x40) != 0 && (GRef<u8>(seq + 0x119) & 8) == 0) {
        GRef<u8>(seq + 0x119) = static_cast<u8>(GRef<u8>(seq + 0x119) | 8);
        GRef<u8>(seq + 0x11B) = 0;
    }
    if ((GRef<u8>(seq + 0x119) & 8) != 0) {
        const u8 n = GRef<u8>(seq + 0x11B);
        if (n < 0x1F)
            GRef<u8>(seq + 0x11B) = static_cast<u8>(n + 1);
        else
            end = true;
    }
    if (!end) return;
    if (Ctx()->sub == 0xE) {
        RequestSubState(0xF);
        func_0021F190(0.5f, 0, 0, 0, 2);
    } else {
        ps2ptr<u8> game = g_GameState();
        const u8 id = game[0x27];
        if (id == 0x23) {
            EnterMenu(0);
        } else if (id == 0x24) {
            game[0x27] = 0xF;
            game[0x2A] = 0;
            RequestScene(2);
            GRef<u32>(GRef<u32>(0x00410E90)) = 0;
            func_0021F190(1.0f, 0, 0, 0, 2);
        } else if (id == 0x25) {
            game[0] = 2;
            game[0x27] = 1;
            game[0x2A] = 0;
            RequestScene(2);
            GRef<u32>(GRef<u32>(0x00410E90)) = 0;
            func_0021F190(1.0f, 0, 0, 0, 2);
        }
    }
    GRef<u32>(GRef<u32>(0x00410B18) + 0x110) = 0;
    GRef<u8>(GRef<u32>(0x004106B4) + 0x62451) = 1;
}

}  // namespace


// EE: 0x0010A130
// Fade of player p: target and time; returns the current level (f0).
f32 func_0010A130(f32 target, f32 time, s32 p) {
    const u32 r = S() + static_cast<u32>(p) * 4;
    GRef<f32>(r + 0x1680) = target;
    GRef<f32>(r + 0x1678) = time;
    return GRef<f32>(r + 0x1670);
}

// EE: 0x0010A158
// Ticks the two per-player screen fade levels (S+0x1670) towards their targets (+0x1680).
// fpucheck: 3 adds (time - k, target - cur, cur + step)
void func_0010A158() {
    for (u32 i = 0; i < 2; ++i) {
        const u32 cur = S() + 0x1670 + 4 * i;
        const u32 time = S() + 0x1678 + 4 * i;
        if (GRef<f32>(cur) == GRef<f32>(cur + 0x10)) {
            GRef<u32>(time) = 0;
            continue;
        }
        const f32 k = GRef<f32>(0x0040DA84);
        const f32 t = fpu::Sub(GRef<f32>(time), k);
        GRef<f32>(time) = t;
        if (t <= 0.0f) {
            GRef<u32>(time) = 0;
            GRef<f32>(cur) = GRef<f32>(cur + 0x10);
        } else {
            const f32 c = GRef<f32>(cur);
            GRef<f32>(cur) = fpu::Add(c, fpu::Sub(GRef<f32>(cur + 0x10), c) * k / t);
        }
        if (1.0f < GRef<f32>(cur)) GRef<f32>(cur) = 1.0f;
        if (GRef<f32>(cur) < 0.0f) GRef<u32>(cur) = 0;
    }
}

// EE: 0x0010A228
void func_0010A228() {}

// EE: 0x0010A230
// Sub 0xA init (first gameplay frame): view cameras, cutscene-mode flag, fade in.
void func_0010A230() {
    debug::OnGuestCheckpoint(0x0010A230);
    func_0010C9D0();
    const u32 rules = GRef<u32>(0x004104F4);
    GRef<u32>(S() + 0x165C) = 0;
    GRef<u32>(S() + 0x1668) |= 0x100;
    GRef<u32>(rules + 0x34) = 0;
    GRef<u32>(rules + 0x24) = 0;
    for (s32 p = 0; p < static_cast<s32>(Game(0x25)); ++p) {
        GRef<f32>(S() + 0x1670 + 4 * static_cast<u32>(p)) = 1.0f;
        func_0010A130(0.0f, 0.5f, p);
    }
}

// EE: 0x0010AF10
// Subs 2-5 init: queues the DATA entry of the mission (alternate set by the player aircraft),
// the player / second aircraft (variant by game[1] / game[0x11]) or the demo data.
void LoadFlightDataEntry() {
    MainContext* ctx = Ctx();
    s32 entry = 0;
    ctx->flags |= 0x40000;
    const u16 sub = ctx->sub;
    if (sub == 2) {
        const u8 id = Game(0x27);
        if (id < 0x13 && ((GRef<s32>(0x004104C8) >> (Game(0) & 0x1F)) & 1) != 0) {  // g_AltMissionAircraftMask
            entry = id + 0x52;
        } else {
            entry = id + 1;
        }
    } else if (sub == 3 || sub == 4) {
        const u32 base = sub == 3 ? 0 : 0x10;
        const u8 variant = Game(base + 1);
        entry = Game(base) + (variant == 1 ? 0x7D : variant == 2 ? 0x95 : 0x65);
    } else if (sub == 5) {
        entry = Game(0x2B) + 0xAD;
    }
    g_FlightDataEntry() = AllocAndLoadDataEntry(entry);
}

// EE: 0x0010FDD8
// Sub 6 init: the engine-sound bank loader state.
void func_0010FDD8() {
    GRef<u32>(0x002A1938) = 0;
}

// EE: 0x0010A4E0
// Runs the init of a newly entered sub-state (request bit 0x20 without 0x10).
void EnterFlightSubState() {
    MainContext* ctx = Ctx();
    const u32 req = ctx->req;
    if ((req & 0x30) == 0 || (req & 0x10) != 0 || (req & 0x20) == 0) return;
    const u16 sub = ctx->sub;
    GRef<u32>(S() + 0x1658) = 0;
    GRef<u32>(S() + 0x1654) = 0;
    ctx->flags &= 0xFFFBFFFFu;
    switch (sub) {
        case 2: case 3: case 4: case 5: LoadFlightDataEntry(); break;
        case 6: func_0010FDD8(); break;
        case 0xA: func_0010A230(); break;
        case 0xB: func_0010A2D8(); break;
        case 0xC: thunk_func_00118C88(); break;
        case 0xD: func_0010A388(); break;
        case 0x10: func_0010A3C0(); break;
        case 0x13: func_0010A3E0(); break;
        case 0x14: func_0010A400(); break;
        case 0x19: thunk_func_0017ABD8(); break;
        case 0x1C: func_0010A440(); break;
        case 0x1D: thunk_func_0017A7C8(); break;
        default: break;
    }
}

// EE: 0x0010A4A0
// Sub-state 0xC exit hook: empty (`jr ra`; not a function in the Ghidra database before S5e).
void func_0010A4A0() {}

// EE: 0x0010A5D0
// Sub-state exit hooks (scene change or sub change) of subs 0xC, 0xF, 0x12, 0x1B, 0x1D.
void func_0010A5D0() {
    MainContext* ctx = Ctx();
    if ((ctx->req & 1) != 0) {
        if (ctx->sub == 0x12) thunk_func_0016E1D0();
        return;
    }
    if ((ctx->req & 2) == 0) return;
    switch (ctx->sub) {
        case 0xC: func_0010A4A0(); break;
        case 0xF: func_0010A4A8(); break;
        case 0x12: thunk_func_0016E1D0(); break;
        case 0x1B: func_0010A478(); break;
        case 0x1D: thunk_func_0017A8D0(); break;
        default: break;
    }
}

// EE: 0x0010A030
// Scene 2 handler: sub-state init, the pause-allowed flag (0x800) by sub-state, the sub handler
// from table 0x002A1620, fades, frame timers (+0x1654/+0x1658, mission timer +0x1660 unless paused
// in subs 0xB/0x1C), exit hooks.
void SceneFlight() {
    EnterFlightSubState();
    MainContext* ctx = Ctx();
    if (ctx->sub == 0xB) {
        ctx->flags &= 0xFFFFF7FFu;
    } else if (ctx->sub == 0x1C) {
        ctx->flags = (ctx->flags & 0xFFFFF7FFu) | 0x200;
    } else {
        ctx->flags |= 0x800;
    }
    const u16 cur = Ctx()->sub;
    if (debug::FreezeSim() && ((cur >= 0xA && cur <= 0xC) || cur == 0x13)) {
        FreezeSimFrame(cur != 0x13);
        if (cur == 0x13) FreezeSimSequenceSkip();
    } else
        guest::CallGuest<void>(GRef<u32>(0x002A1620 + 4u * cur));
    func_0010A158();
    const u32 s = S();
    ctx = Ctx();
    GRef<u32>(s + 0x1654) += 1;
    GRef<u32>(s + 0x1658) += 1;
    const u16 sub = ctx->sub;
    if ((sub != 0xB && sub != 0x1C) || (ctx->flags & 0x100) == 0) GRef<u32>(s + 0x1660) += 1;
    func_0010A228();
    func_0010A5D0();
}
AC04_GUEST_FUNCTION(0x0010A030, SceneFlight);

// ---- Sub 0/1: scene creation --------------------------------------------------------------------

// EE: 0x0010A668
void CreateFlightScene() {
    if (Game(0x24) == 0) {
        if ((GRef<u64>(g_GameState().addr + 0x20) & 0x00FFFF0000000000ull) == 0x0000020000000000ull) SetGame(0x26, 1);
        if (Game(0x25) == 1 && Game(0x26) != 0) SetGame(0x26, 0);
    }
    const u32 s = WorkHeapAlloc(0x1810, 0x10);
    g_FlightScene() = s;
    for (u32 o = 0x1750; o >= 0x1694; o -= 4) GRef<u32>(s + o) = 0;
    for (u32 n = 0; n < 2; ++n)
        for (s32 i = 15; i >= 0; --i) GRef<u32>(s + 0x1754 + 0x40 * n + 4 * static_cast<u32>(i)) = 0;
    for (s32 i = 3; i >= 0; --i) GRef<u32>(s + 0x17D4 + 4 * static_cast<u32>(i)) = 0;
    GRef<u8>(s + 0x1650) = 0;
    for (u32 n = 0; n < 5; ++n) func_0010D6A0(S() + n * 0x440);
    const u32 t = S();
    GRef<u8>(t + 0x1652) = 0;
    GRef<u32>(t + 0x1660) = 0;
    GRef<u32>(t + 0x1654) = 0;
    GRef<u32>(t + 0x1658) = 0;
    GRef<u32>(t + 0x166C) = 0;
    GRef<u32>(t + 0x1688) = 0;
    GRef<u64>(t + 0x17E8) = 5;
    GRef<u32>(t + 0x1668) = 0;
    MainContext* ctx = Ctx();
    ctx->clear_rgba[0] = 0;
    ctx->draw_h = 0x1C0;
    ctx->disp_w = 0x200;
    ctx->disp_h = 0xE0;
    ctx->draw_w = 0x200;
    ctx->clear_rgba[1] = 0;
    ctx->clear_rgba[2] = 0;
    ctx->clear_rgba[3] = 0;
    ctx->frame_psm = 0;
    ctx->zbuf_psm = 0x31;
    ctx->ztest = 3;
    ctx->draw_mode = 0;
    ctx->disp_flags |= 0x8000;
    BuildDrawEnvPackets();
    SetGame(0x2D, 0);
    GRef<f32>(S() + 0x1690) = 1.0f;
    GRef<u32>(S() + 0x168C) = 0;
    if (Game(0x2A) != 1) return;
    GRef<u32>(S() + 0x168C) = 1;
    func_00239640();
}

// EE: 0x0010A898
// Sub 0/1 handler: creates the scene, sets the mission kind S+0x1653 (0 campaign, 1 trial, 2 VS,
// 3 tutorial, 4 sub 1 / demo ids) and the packet double buffer, then goes to sub 2.
void FlightSceneEnter() {
    CreateFlightScene();
    MainContext* ctx = Ctx();
    ctx->flags &= 0xFFFDFFFFu;
    u8 kind = 0;
    if (ctx->sub == 1) {
        kind = 4;
    } else {
        const u8 id = Game(0x27);
        if (id > 0x13) {
            if (static_cast<u8>(id - 0x15) < 5) kind = 1;
            else if (static_cast<u8>(id - 0x23) < 3) kind = 4;
            else if (static_cast<u8>(id - 0x20) < 8) kind = 3;
            else if (static_cast<u8>(id - 0x14) < 0x15) kind = 2;
        }
    }
    GRef<u8>(S() + 0x1653) = kind;
    AllocDoubleBuffer(kMainContextAddress + offsetof(MainContext, dbuf), 0x10000);
    Ctx()->cur_buf = guest::ps2ptr<void>(SwapDoubleBuffer(kMainContextAddress + offsetof(MainContext, dbuf)));
    RequestSubState(2);
}
AC04_GUEST_FUNCTION(0x0010A898, FlightSceneEnter);

// ---- Loading screen ----------------------------------------------------------------------------

// EE: 0x0010AD80
// Frame draw lists of the loading sub-states: 16 buckets at S+0x1540, then the loading screen.
void func_0010AD80() {
    const bool wait_sub = Ctx()->sub == 0x1E;
    GRef<u8>(S() + 0x1651) = 0;
    const u32 list = S() + static_cast<u32>(static_cast<s32>(GRef<s8>(S() + 0x1651))) * 0x10 + 0x1540;
    InitBucketList(list, 0x10);
    SetPacketListBuckets(kMainContextAddress + offsetof(MainContext, vif1_list), list);
    const f32 alpha = GRef<f32>(S() + 0x1690);
    const s32 counter = GRef<s32>(S() + 0x166C);
    if ((GRef<u32>(S() + 0x168C) & 1) == 0) {
        func_00239310(alpha, list, 0, counter, wait_sub ? 1 : 0);
    } else {
        func_00239B60(alpha, list, 0, counter, wait_sub ? 1 : 0);
    }
    GRef<s32>(S() + 0x166C) += 1;
}

// EE: 0x00239640
// Loading-screen variant: briefing picture index 0x00411100 from the mission id (campaign number
// for ids 1-18, 0x12 for 0x24, 1 for 0x25); the blink counter 0x00411828 = 0.
void func_00239640() {
    const u8 id = Game(0x27);
    s32 n;
    if (id == 0x24) {
        n = 0x12;
    } else if (id == 0x25) {
        n = 1;
    } else {
        if (static_cast<u32>(id - 1) >= 0x12) return;
        n = GetCampaignMissionNumber(id);
    }
    if (static_cast<u32>(n - 1) < 0x12) GRef<u32>(0x00411100) = static_cast<u32>(n - 1);
    GRef<u32>(0x00411828) = 0;
}

// EE: 0x00239310
// Default loading screen: the frame (DMA call of 0x003A60A0), a fading picture quad with alpha
// `alpha` (0..1 -> 0..128) and a colour lerp by the frame counter, plus (outside sub 0x1E, after
// 8 * 1/k frames) a pulsing second quad.
// fpucheck: 3 adds (f - 8, x - floor(x), 1 - cos)
void func_00239310(f32 alpha, u32 list, s32 index, s32 counter, s32 wait_sub) {
    if (alpha <= 0.0f) return;
    if (1.0f < alpha) alpha = 1.0f;
    const u32 start = Ctx()->cur_buf.addr;
    const u32 call = WriteDmaCallTag(start, 0x003A60A0);
    u32 p = BuildFrameDrawEnv(call + 0x10);
    const f32 f = fpu::IntToFloat(counter) * GRef<f32>(0x004102D4);
    Put64(p + 0x00, 0x11AB400000008002ull);
    Put64(p + 0x08, 0xE);
    Put64(p + 0x18, 0x43);
    Put64(p + 0x28, 0x19);
    Put64(p + 0x10, (static_cast<u64>(static_cast<s64>(fpu::FloatToInt(alpha * 128.0f))) << 32) | 0x68);
    Put64(p + 0x20, 0);
    p += 0x30;
    f32 t;
    if (1.0f < f) t = 1.0f;
    else t = f < 0.0f ? 0.0f : f;
    Put64(p + 0x00, 0x6400000000008001ull);
    Put64(p + 0x08, 0x53D317);
    Put64(p + 0x10, 0x20060002A9322D00ull);
    const u64 c = LerpColor(t, 0xFFFFFFFF80000000ull, 0xFFFFFFFF80808080ull);
    Put64(p + 0x18, c & 0xFFFFFFFFull);
    Put64(p + 0x38, 0xF201C80);
    Put64(p + 0x28, 0xAA00380);
    Put64(p + 0x30, 0x4881908);
    Put64(p + 0x20, 0x80008);
    p += 0x40;
    if (wait_sub == 0 && 8.0f < f) {
        const f32 x = fpu::Sub(f, 8.0f) * 0.5f;
        const f32 fr = fpu::Sub(x, ee::floorf(x));
        const f32 v = fpu::Sub(1.0f, ee::cosf(fr * GRef<f32>(0x004102D8))) * 0.5f;
        if (0.0f < v) {
            Put64(p + 0x00, 0x6400000000008001ull);
            Put64(p + 0x08, 0x53D317);
            Put64(p + 0x10, 0x20060082A9322E00ull);
            const u64 c2 = LerpColor(v, 0, 0xFFFFFFFF80808080ull);
            Put64(p + 0x18, c2 & 0xFFFFFFFFull);
            Put64(p + 0x38, 0x4B00700);
            Put64(p + 0x20, 0x80008);
            Put64(p + 0x28, 0x1300100);
            Put64(p + 0x30, 0x3880608);
            p += 0x40;
        }
    }
    LinkPacket(list, index, start, CloseDmaPacket(call, p));
}

// EE: 0x002396B0
// Mission-briefing loading screen text: "MISSION n", the title, the objective lines (difficulty
// set by game[0x28]) typed in by `t`, then the "PRESS START" line and its dots, or (blink counter
// non-zero) the blinking "LOADING" line. Typing SE every third frame while lines are incomplete.
// fpucheck: 9 adds (t - 1, three x - n, strlen + 24, x - (strlen + 24), t - 3, t - k twice as x + x, frac)
void func_002396B0(f32 t, u32 list, s32 index, s32 counter, s32 blink) {
    u32 n = GRef<u32>(0x00411100);
    if (n >= 0x12) n = 0;
    bool typing = false;
    BeginMenuTextLayer(list, index);
    const u32 lines = Game(0x28) < 2 ? 0x003AEE28 : 0x003AEDE0;
    guest::GuestFrame frame(0x100);
    const u32 buf = frame.Address();
    const f32 a = fpu::Sub(t, 1.0f);
    if (0.0f < a) {
        const f32 x = a * 60.0f;
        u64 arg = n + 1;
        ee::sprintf(buf, 0x00411108, &arg, 1);
        DrawMenuText(x, 0x380, 0x380, 1, buf);
        arg = GRef<u32>(0x003844A0 + 4 * n);
        ee::sprintf(buf, 0x00411110, &arg, 1);
        DrawMenuText(fpu::Sub(x, 8.0f), 0x380, 0x5C0, 3, buf);
        DrawMenuText(fpu::Sub(x, 16.0f), 0x380, 0x760, 3, 0x00409000);
        const u32 obj = GRef<u32>(lines + 4 * n);
        DrawMenuText(fpu::Sub(x, 24.0f), 0x380, 0x8E0, 3, obj);
        const f32 len = fpu::Add(fpu::IntToFloat(static_cast<s32>(ee::strlen(GRef<u32>(lines + 4 * n)))), 24.0f);
        if (fpu::Sub(x, len) < 0.0f) typing = true;
        const f32 b = fpu::Sub(t, 3.0f);
        if (blink == 0) {
            if (0.0f < b) {
                const f32 y = b * 60.0f;
                DrawMenuText(y, 0x380, 0xC80, 3, 0x00409018);
                if (y < 12.0f) typing = true;
                const f32 d = fpu::Sub(b, GRef<f32>(0x004102DC));
                s32 dots = fpu::FloatToInt(fpu::Add(d, d));
                if (dots >= 0x51) dots = 0x50;
                s32 xp = 0xAE0;
                for (s32 i = 0; i < dots; ++i, xp += 0x40) DrawMenuText(8.0f, xp, 0xC80, 3, 0x00411118);
            }
        } else {
            const f32 bx = fpu::IntToFloat(blink) * GRef<f32>(0x004102E0);
            const f32 fr = fpu::Sub(bx, ee::floorf(bx));
            if (0.25f < fr) {
                DrawMenuText(100.0f, 0x380, 0xC80, 3, 0x00409028);
            } else {
                DrawMenuTextColored(100.0f, 0x380, 0xC80, 3, 0x00409028, 0x0060584C);  // t1 0x606060 unused
            }
        }
    }
    EndMenuTextLayer();
    if (typing && counter % 3 == 1) PlayMenuSe(0, 9);
}

// EE: 0x002399E0
// The briefing picture behind the text: the frame (as a PSMT8H texture, TEX0 from the drawing
// environment's FRAME) drawn as a sprite.
void func_002399E0(u32 list, s32 index) {
    MainContext* ctx = Ctx();
    const u64 frame = ctx->drawenv_pkt[2];
    const u32 start = ctx->cur_buf.addr;
    const u64 w = ctx->draw_w;
    u32 p = BuildFrameDrawEnv(start + 0x10);
    const u64 tex0 = ((frame & 0x1FF) << 5) | (((w + 0x3F) >> 6) << 14) | 0x2A8000000ull;
    Put64(p + 0x00, 0x1000000000000001ull);
    Put64(p + 0x08, 0xE);
    Put64(p + 0x18, 0x19);
    Put64(p + 0x10, 0);
    p += 0x20;
    Put64(p + 0x00, 0xC400000000008001ull);
    Put64(p + 0x10, 0x316);
    Put64(p + 0x08, 0x5D1053D31790ull);
    Put64(p + 0x18, 5);
    Put64(p + 0x20, tex0);
    Put64(p + 0x68, 0x1A402000);
    Put64(p + 0x28, 0x80808080);
    Put64(p + 0x30, 0x1880008);
    Put64(p + 0x38, 0x1400000);
    Put64(p + 0x40, 0x1A882008);
    Put64(p + 0x48, 0x1A401999);
    Put64(p + 0x50, 0x206);
    Put64(p + 0x58, 0x80000000);
    Put64(p + 0x60, 0x1401999);
    LinkPacket(list, index, start, CloseDmaPacket(start, p + 0x70));
}

// EE: 0x00239B60
// Briefing loading screen (game[0x2A] == 1): text, picture, black fade and frame filter.
void func_00239B60(f32 alpha, u32 list, s32 index, s32 counter, s32 wait_sub) {
    const f32 t = fpu::IntToFloat(counter) * GRef<f32>(0x004102E4);
    f32 level = alpha;
    if (t < 1.0f && t < level) level = t;
    if (wait_sub == 1) {
        if (GRef<u32>(0x00411828) == 0) PlayMenuSe(0, 0xD);
        GRef<u32>(0x00411828) += 1;
    }
    func_002396B0(t, list, index, counter, GRef<s32>(0x00411828));
    func_002399E0(list, index);
    DrawBlackFade(level, list, index);
    DrawMenuFrameFilter(level, list, index, 1);
}

// EE: 0x0010A998
// Sub 0x1E (loading screen wait): after 60 frames (briefing variant: 120 frames and START/X), the
// screen fades out by *0x0040DA88 per frame, then sub 7.
// fpucheck: 1 add (alpha - k)
void func_0010A998() {
    const u32 s = S();
    const u32 flags = GRef<u32>(s + 0x168C);
    if ((flags & 2) == 0) {
        if ((flags & 1) == 0) {
            if (GRef<u32>(s + 0x166C) > 0x3C) GRef<u32>(s + 0x168C) = flags | 2;
        } else {
            u32 pressed = Ctx()->pad_buttons[0][1];
            if (GRef<u8>(s + 0x1653) == 2) pressed |= Ctx()->pad_buttons[1][1];
            if (GRef<u32>(s + 0x166C) > 0x78 && (pressed & 0x50) != 0) GRef<u32>(s + 0x168C) = flags | 2;
        }
    } else {
        const f32 a = fpu::Sub(GRef<f32>(s + 0x1690), GRef<f32>(0x0040DA88));
        GRef<f32>(s + 0x1690) = a;
        if (a < 0.0f) {
            GRef<u32>(s + 0x1690) = 0;
            RequestSubState(7);
        }
    }
    func_0010AD80();
}
AC04_GUEST_FUNCTION(0x0010A998, func_0010A998);

// ---- Subs 2-6: data loads and engine sounds -------------------------------------------------------

// EE: 0x0010B038
// Subs 2-5: when the queued DATA entry has arrived, stores its item pointers (mission blocks at
// S+0x1694, player aircraft +0x1754, second aircraft +0x1794, demo data +0x17D4) and moves on.
void StoreFlightDataPointers() {
    func_0010AD80();
    if (IsDataLoadDone() == 0) return;
    const u32 entry = g_FlightDataEntry();
    const u16 sub = Ctx()->sub;
    u32 dst = 0;
    u16 next = 0;
    if (sub == 2) {
        dst = S() + 0x1694;
        next = 3;
    } else if (sub == 3) {
        dst = S() + 0x1754;
        next = Game(0x25) == 2 ? 4 : (Game(0x29) == 4 ? 5 : 6);
    } else if (sub == 4) {
        dst = S() + 0x1794;
        next = 6;
    } else if (sub == 5) {
        dst = S() + 0x17D4;
        next = 6;
    }
    const s32 count = GRef<s32>(entry);
    for (s32 i = 0; i < count; ++i)
        GRef<u32>(dst + 4 * static_cast<u32>(i)) = entry + GRef<u32>(entry + 4 * static_cast<u32>(i + 1));
    RequestSubState(next);
}
AC04_GUEST_FUNCTION(0x0010B038, StoreFlightDataPointers);

// EE: 0x0010FDE8
// Sub 6: loads the engine-sound bank and the per-player aircraft sound entries (state 0x002A1938,
// player index 0x002A193C), then waits three frames and goes to sub 0x1E.
void func_0010FDE8() {
    const u8 players = Game(0x25);
    u32& state = GRef<u32>(0x002A1938);
    s32& player = GRef<s32>(0x002A193C);
    const u32 sel = (~GRef<u32>(GRef<u32>(0x00410E90)) & 1) * 0x94;
    switch (state) {
        case 0: {
            const u32 v = GRef<u32>(0x00353BF8 + sel);
            func_00218B28(2);
            func_00217778(v);
            func_00219B48();
            state = 1;
            player = 0;
            break;
        }
        case 1:
            if (GRef<s32>(0x00353C2C + sel) == 1) state = 2;
            break;
        case 2: {
            const u32 r = S() + static_cast<u32>(player) * 0x40;
            func_00216570(player, GRef<u32>(r + 0x1774), GRef<u32>(r + 0x1770));
            state = 3;
            break;
        }
        case 3: {
            const u32 id = Game(static_cast<u32>(player) * 0x10);
            const u32 bank = GRef<u32>(0x002A187C + id * 8);
            if (StreamBdEntryToIop(static_cast<s32>(bank), GRef<s32>(0x002A1878 + id * 8)) != 1) break;
            const u32 slot = static_cast<u32>(player) * 0xC0;
            player += 1;
            GRef<u32>(0x0033D86C + slot) = bank;
            if (static_cast<s32>(players) <= player) {
                player = 0;
                state = 6;
                break;
            }
            state = 2;
            break;
        }
        case 6: state = 7; break;
        case 7: state = 8; break;
        case 8: RequestSubState(0x1E); break;
        default: break;
    }
    func_0010AD80();
}
AC04_GUEST_FUNCTION(0x0010FDE8, func_0010FDE8);

// ---- Sub 7: system creation -----------------------------------------------------------------------

// EE: 0x0010AA50
void CreateFlightSystems() {
    func_001118C8();
    CreateWorldRenderObjects();
    func_0011B948();
    CreateProjectilePools();
    AllocEffectBlock();
    func_00136518();
    func_00176908();
    func_0015E7B0();
    func_0016DA60();
    func_00155AE0();
    func_0016DA78();
    func_0016DE20();
    if (GRef<u8>(S() + 0x1653) == 1) func_0017BB38();
    func_0017D8B0();
    func_0017DC98();
    func_0017B2C8();
    func_0017B708();
    if (Game(0x27) == 1) func_0017B868();
    if (Game(0x27) == 8) func_0017B978();
    GRef<u32>(S() + 0x1664) = WorkHeapAlloc(0, 0);
    func_0010AD80();
    RequestSubState(8);
}
AC04_GUEST_FUNCTION(0x0010AA50, CreateFlightSystems);

// ---- Sub 8: system reset (also on every retry) ------------------------------------------------------

// EE: 0x0010AB58
void StartFlightSystems() {
    RewindWorkHeapTo(GRef<u32>(S() + 0x1664));
    GRef<u32>(S() + 0x1660) = 0;
    func_00219260();
    func_0021AEE0();
    StopAllVoices();
    func_0021FDF8(1);
    func_0021FDF8(2);
    func_0015E818();
    func_0011BA90();
    InitEffects(GRef<u32>(S() + 0x170C));
    ResetProjectiles();
    func_0017B2F0();
    func_0017B730();
    if (Game(0x27) == 1) func_0017B890();
    if (Game(0x27) == 8) func_0017B9A0();
    func_00118A18();
    func_0010FA70();
    func_00135D60();
    func_0010C840();
    func_00136570(0);
    func_00136570(1);
    func_0016DAE0();
    func_00111950();
    func_00257630(Game(0x25));
    if (Game(0x27) == 8) func_002594F0();
    if (GRef<u8>(S() + 0x1653) == 1) func_0017BC70();
    if (GRef<u8>(S() + 0x1653) == 3) func_0017BF90();
    func_00250C30(GRef<u32>(S() + 0x171C), GRef<u32>(S() + 0x1720));
    func_00219110();
    func_0021B030();
    func_0016DE48();
    func_0016DEF8();
    func_0010AD80();
    if (GRef<u8>(S() + 0x1653) == 4) {
        RequestSubState(0x13);
        return;
    }
    if (Game(0x29) == 4) {
        GRef<u32>(S() + 0x1668) &= 0xFFFEFFFFu;
        RequestSubState(0x10);
        return;
    }
    if (Game(0x27) == 10 && Game(0x29) != 3) {
        SetGame(0x2C, 0);
        RequestSubState(0xD);
        return;
    }
    RequestSubState(10);
}
AC04_GUEST_FUNCTION(0x0010AB58, StartFlightSystems);

// EE: 0x0010C840
void func_0010C840() {}

// EE: 0x0010FA70
// Mission setup: stream slots stopped, units placed (block 22), radar unit summary, mission rules
// (block 28, part 2 = HUD map bits when the block holds two parts).
void func_0010FA70() {
    for (s32 i = 0; i < 6; ++i) func_0021F190(0.0f, i, 0xFF, 0xFF, 1);
    func_0021E798(0, 0);
    func_0021F190(0.0f, 0, 0, 0, 1);
    SetupMissionUnits();
    func_0024CBE0();
    const u32 block = GRef<u32>(S() + 0x1704);
    if (GRef<s32>(block) != 2) {
        func_0011A3D8(GRef<u32>(0x004104F8), block);
        return;
    }
    func_0011A3D8(GRef<u32>(0x004104F8), block + GRef<u32>(block + 4));
    func_0015E8C0(block + GRef<u32>(block + 8));
}

// ---- Mission rules records -------------------------------------------------------------------------

// EE: 0x00118A18
// The three rules records (gp 0x004104F0 / F4 / F8). The status record starts with VU0 vf0 (0, 0,
// 0, 1.0) and a zero qword.
void func_00118A18() {
    GRef<u32>(0x004104F0) = WorkHeapAlloc(0x70, 0x10);
    GRef<u32>(0x004104F4) = WorkHeapAlloc(0x90, 0x10);
    GRef<u32>(0x004104F8) = WorkHeapAlloc(0xA8, 0x10);
    const u32 a = GRef<u32>(0x004104F0);
    GRef<u8>(a + 0x28) = 0xFA;
    GRef<u32>(a + 0x10) = 0;
    GRef<u32>(a + 0x14) = 0x1A5E0;
    GRef<u32>(a + 0x18) = 9999;
    GRef<u8>(a + 0x2A) = 2;
    GRef<f32>(a + 0x20) = 262144.0f;
    GRef<f32>(a + 0x1C) = -262144.0f;
    GRef<u16>(a + 0x24) = 100;
    GRef<u16>(a + 0x26) = 200;
    GRef<u8>(a + 0x2B) = 1;
    GRef<u8>(a + 0x34) = 0xFF;
    GRef<f32>(a + 0x30) = 131072.0f;
    GRef<u32>(a + 0x2C) = 0;
    GRef<u8>(a + 0x35) = 0xFF;
    GRef<u32>(a + 0x60) = 60;
    GRef<u32>(a + 0x44) = 9999;
    GRef<u32>(a + 0x48) = 0xE10;
    GRef<u32>(a + 0x4C) = 0x1C20;
    GRef<u32>(a + 0x50) = 0x2A30;
    GRef<u32>(a + 0x58) = 600;
    GRef<u32>(a + 0x5C) = 0x78;
    GRef<u32>(a + 0x38) = 9999;
    GRef<u32>(a + 0x3C) = 9999;
    GRef<u32>(a + 0x40) = 9999;
    GRef<u32>(a + 0x54) = 600;
    const u32 b = GRef<u32>(0x004104F4);
    GRef<u32>(b + 0x0) = 0;
    GRef<u32>(b + 0x4) = 0;
    GRef<u32>(b + 0x8) = 0;
    GRef<u32>(b + 0xC) = 0x3F800000;  // sqc2 vf0
    for (u32 o = 0x10; o < 0x20; o += 4) GRef<u32>(b + o) = 0;
    GRef<u8>(b + 0x7D) = 0;
    GRef<u32>(b + 0x20) = 0;
    GRef<u8>(b + 0x7C) = 0;
    GRef<u32>(b + 0x24) = 0;
    for (u32 o : {0x3Cu, 0x28u, 0x2Cu, 0x30u, 0x34u, 0x40u, 0x44u, 0x48u}) GRef<u32>(b + o) = 0;
    for (u32 i = 0; i < 8; ++i) {
        GRef<u8>(b + 0x4C + i) = 0;
        GRef<u8>(b + 0x54 + i) = 0;
        GRef<u8>(b + 0x5C + i) = 0;
        GRef<u8>(b + 0x64 + i) = 0;
        GRef<u8>(b + 0x6C + i) = 0;
        GRef<u8>(b + 0x74 + i) = 0;
    }
    GRef<u8>(b + 0x7E) = 0;
    GRef<u8>(b + 0x7F) = 0;
    GRef<s32>(b + 0x80) = -1;
    if (Game(0x29) == 3) {
        const u8 id = Game(0x27);
        const bool hard = id == 3 || id == 6 || id == 9 || id == 0xE || id == 0x11;
        GRef<f32>(GRef<u32>(0x004104F0) + 0x20) = hard ? 196608.0f : 262144.0f;
    }
    GRef<u16>(b + 0x86) = 0;
    GRef<u16>(b + 0x84) = 0;
}

// EE: 0x0011A3D8
// Event script tables of mission block 28 (rules record R = gp 0x004104F8): 0x13 section offsets,
// three level-sorted lists (4-byte entries, first byte the level, 0xFF end) and three unit link
// lists (u16 unit, u16 offset, 0xFFFF end) into the units' +0x8C/+0x90/+0x94.
void func_0011A3D8(u32 r, u32 block) {
    auto at = [block](u32 o) { return block + GRef<u32>(block + o); };
    const u32 l0 = at(0x28), l1 = at(0x30), l2 = at(0x38);
    GRef<u32>(r + 0x00) = at(0x04);
    GRef<u32>(r + 0x3C) = l0;
    GRef<u32>(r + 0x04) = at(0x08);
    GRef<u32>(r + 0x40) = l1;
    GRef<u32>(r + 0x08) = at(0x0C);
    GRef<u32>(r + 0x0C) = at(0x14);
    GRef<u32>(r + 0x10) = at(0x1C);
    GRef<u32>(r + 0x14) = at(0x24);
    GRef<u32>(r + 0x18) = at(0x2C);
    GRef<u32>(r + 0x1C) = at(0x34);
    GRef<u32>(r + 0x28) = at(0x4C);
    GRef<u32>(r + 0x2C) = at(0x54);
    GRef<u32>(r + 0x30) = at(0x10);
    GRef<u32>(r + 0x20) = at(0x3C);
    GRef<u32>(r + 0x24) = at(0x44);
    GRef<u32>(r + 0x34) = at(0x18);
    GRef<u32>(r + 0x38) = at(0x20);
    GRef<u32>(r + 0x44) = l2;
    const u32 s0 = at(0x40), s1 = at(0x48), s2 = at(0x50);
    for (u32 i = 0; i < 8; ++i) {
        GRef<u32>(r + 0x48 + 4 * i) = 0;
        GRef<u32>(r + 0x68 + 4 * i) = 0;
        GRef<u32>(r + 0x88 + 4 * i) = 0;
    }
    auto sorted = [](u32 list, u32 out) {
        u32 e = list;
        for (u32 level = 0; level < 8; ++level) {
            while (static_cast<u32>(GRef<u8>(e)) < level) e += 4;
            if (GRef<u8>(e) == 0xFF) break;
            GRef<u32>(out) = e;
            out += 4;
        }
    };
    sorted(s0, r + 0x48);
    sorted(s1, r + 0x68);
    sorted(s2, r + 0x88);
    auto links = [](u32 list, u32 base, u32 field) {
        for (u32 e = list; GRef<u16>(e) != 0xFFFF; e += 4) {
            const u32 unit = GRef<u32>(0x002A1F70 + 4u * GRef<u16>(e));
            if ((~GRef<u32>(unit + 0x48) & 0x10000000) == 0) GRef<u32>(unit + field) = base + GRef<u16>(e + 2);
        }
    };
    links(l0, GRef<u32>(r + 0x18), 0x8C);
    links(l1, GRef<u32>(r + 0x1C), 0x90);
    links(l2, GRef<u32>(r + 0x20), 0x94);
}

}  // namespace ac04::game
