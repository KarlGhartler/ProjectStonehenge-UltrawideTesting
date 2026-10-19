// Scene 2 gameplay frame (slice S5e): FlightPlayFrame (subs 0xA-0xC), FlightSimStep, the sub-state
// init/exit hooks, mission rules (objectives, end of mission, phases, timed events) and the event
// script, the per-frame unit pass and the falling-unit emitters. Flow: docs/subsystems/
// mission-flow.md; per-function notes: docs/port/s5-blockB-inventory.csv.
#include "game/game.h"

#include "common/debug_hooks.h"
#include "common/ee_math.h"
#include "common/guest_stack.h"
#include "game/flight_common.h"
#include "platform/log.h"
#include "sce/sce.h"

namespace ac04::game {

using guest::GRef;
using namespace fl;

namespace {

bool StartPressed(s32 p) { return (Ctx()->pad_buttons[p][1] & 0x40) != 0; }
bool PadGone(s32 p) { return GRef<s32>(PadState(p)) == 0x80; }

// The pause entry of FlightPlayFrame.
void EnterPause(u32 set) {
    MainContext* ctx = Ctx();
    ctx->flags = set;
    func_0021E798(0, 1);
    func_0021CBB8();
    func_00219260();
    func_0021AEE0();
    func_0017DCC0();
}

bool Alive(u32 u) { return (W(u + 0x4C) & 0x1000000u) != 0; }

// Status +0x7D result bits and the end-of-gameplay request shared by the failure paths.
void Fail(u8 bit) {
    const u32 st = Status();
    GRef<u8>(st + 0x7D) = static_cast<u8>(GRef<u8>(st + 0x7D) | bit);
    GRef<u8>(Status() + 0x7D) = static_cast<u8>(GRef<u8>(Status() + 0x7D) & 0xFE);
    GRef<u32>(Status() + 0x20) &= 0xFFFFFFFEu;
    RequestSubState(0xC);
}

}  // namespace

// ---- Sub-state init and exit hooks (table 0x00403C60, exits 0x0010A5D0) ------------------------

// EE: 0x0010A2D8
// Sub 0xB init (gameplay starts): mission timer running, cutscene flag, fade in of every player.
void func_0010A2D8() {
    func_00118C60();
    GRef<u32>(Scene() + 0x1668) |= 0x100u;
    for (s32 p = 0; p < static_cast<s32>(Players()); ++p)
        if (F(Scene() + 0x1670 + 4u * static_cast<u32>(p)) != 0.0f) func_0010A130(0.0f, 0.5f, p);
}

// EE: 0x0010A388
// Sub 0xD init (mid-mission sequence): heap mark, sequence setup, faded out.
void func_0010A388() {
    PushWorkHeapMark();
    func_00135E10();
    GRef<f32>(Scene() + 0x1670) = 1.0f;
}

// EE: 0x0010A3C0
// Sub 0x10 init (replay/demo playback).
void func_0010A3C0() {
    func_0010D470();
    func_0016E010();
}

// EE: 0x0010A3E0
// Sub 0x13 init (in-engine special sequence).
void func_0010A3E0() {
    GRef<f32>(Scene() + 0x1670) = 1.0f;
    SetGame(0x2C, 0);
    SetupInEngineSequence();
}

// EE: 0x0010A400
// Sub 0x14 init (carrier landing).
void func_0010A400() {
    GRef<f32>(Scene() + 0x1670) = 1.0f;
    func_0010A130(0.0f, 0.5f, 0);
    func_00179BC0();
}

// EE: 0x0010A440
// Sub 0x1C init (game-over menu).
void func_0010A440() {
    func_0010A130(0.0f, 0.0f, 0);
    func_0017E1D0();
    func_0021CBB8();
}

// EE: 0x0010A478
// Sub 0x1B exit: back to gameplay after the carrier takeoff.
void func_0010A478() {
    if (Ctx()->next_sub == 0xB) func_0017B0B0();
}

// EE: 0x0010A4A8
// Sub 0xF exit: end of a mid-mission sequence.
void func_0010A4A8() {
    func_00135EF8();
    PopWorkHeapMark();
    func_0010B820();
}

// EE: 0x0010B820
// Gameplay resumes after a scripted sequence: the player is put level (missions 4 stage 0,
// 10 and 15 stage 1: rot.x = rot.z = 0, y = C 0x0040DA8C, velocity C 0x0040DA90 forward), effects,
// projectiles and unit sounds reset, all voices stopped.
void func_0010B820() {
    GRef<u32>(Scene() + 0x1688) = 0;
    const u32 u0 = Unit(0);
    const u8 id = Game(0x27), st = Game(0x2C);
    if ((id == 4 && st == 0) || (id == 10 && st == 1) || (id == 0xF && st == 1)) {
        GRef<u32>(u0 + 0x10) = 0;
        GRef<u32>(u0 + 0x18) = 0;
        GRef<u32>(u0 + 4) = W(0x0040DA8C);
        guest::GuestFrame f(0x50);
        const u32 v = f.Address(0), m = f.Address(0x10);
        ZeroQ(v);
        GRef<u32>(v + 8) = W(0x0040DA90);
        sceVu0UnitMatrix(m);
        func_00102D28(m, m, u0 + 0x10);
        sceVu0ApplyMatrix(u0 + 0x20, m, v);
    }
    InitEffects(W(Scene() + 0x170C));
    ResetProjectiles();
    func_0011D888();
    func_0016E910();
    func_00219260();
    func_0021AEE0();
    StopAllVoices();
}

// ---- Gameplay frame ---------------------------------------------------------------------------

// EE: 0x0010B150
// Subs 0xA-0xC: random index, world update, simulation step, cameras, player sounds or the
// cutscene stops, sound listeners, draw lists, then the cutscene skip or pause handling.
void FlightPlayFrame() {
    SetRandomIndex(W(Scene() + 0x165C));
    func_00139FE8(Terrain());
    func_00144CC0(SeaObj());
    func_0013EC20(WorldObjs());
    const u32 t = Terrain();
    const u32 e = W(t) + W(t + 4) * 100;
    func_0013EC38(WorldObjs(), W(e + 0x44), W(e + 0x50));
    FlightSimStep();
    UpdateViewCameras();
    if ((~Ctx()->flags & 0x100u) == 0) {
        for (s32 p = 0; p < static_cast<s32>(Players()); ++p) {
            StopPlayerLoopSounds(p);
            func_001279B0(p, 0);
        }
    } else {
        for (s32 p = 0; p < static_cast<s32>(Players()); ++p) UpdatePlayerSound(p);
        func_002166A8();
        func_0011DC20();
        func_0014D4D0();
        for (s32 v = 0; v < static_cast<s32>(GRef<s8>(Scene() + 0x1650)); ++v) {
            const u32 cam = Scene() + 0x440u * static_cast<u32>(v) + 0xB0;
            func_00219330(cam);
            func_0021AF18(v, cam);
            const u32 obj = W(UnitList() + 4u * static_cast<u32>(v) + 0x4C);
            GRef<u32>(obj + 0x10) = cam;
            GRef<u32>(obj + 0x14) = cam + 0x10;
            func_0021C700(obj);
        }
    }
    BuildFlightDrawLists();
    MainContext* ctx = Ctx();
    if ((ctx->flags & 0x100u) != 0) {
        func_0017DFF8();
        return;
    }
    if ((~ctx->flags & 0x800u) == 0) return;
    if (Players() == 1 && Alive(Unit(0)) && (~StatusFlags() & 0x40000u) != 0 && (StartPressed(0) || PadGone(0)))
        EnterPause((Ctx()->flags & 0xFFFFDFFFu) | 0x200u);
    if (Players() == 2 && Alive(Unit(0))) {
        if (!Alive(Unit(1))) {
            if ((Ctx()->flags & 0x200u) != 0) func_002559F8();
            return;
        }
        if (StartPressed(0) || (PadGone(0) && GRef<s32>(PadState(0) + 0x50) == 0x80))
            EnterPause((Ctx()->flags & 0xFFFFDFFFu) | 0x200u);
        if ((~Ctx()->flags & 0x200u) != 0 && (Ctx()->pad_buttons[1][1] & 0x40) != 0)
            EnterPause(Ctx()->flags | 0x2200u);
    }
    if ((Ctx()->flags & 0x200u) != 0) func_002559F8();
}
AC04_GUEST_FUNCTION(0x0010B150, FlightPlayFrame);

// EE: 0x0010C320
// Simulation step (docs/subsystems/mission-flow.md "Frame order"): replay-record flag, unit
// states, projectiles, falling units, AI and effect passes, HUD, replay, rules, radio, vibration,
// tutorial, frame counter.
void FlightSimStep() {
    MainContext* ctx = Ctx();
    if ((~ctx->flags & 0x100u) == 0) return;
    const u16 sub = ctx->sub;
    bool rec = static_cast<u32>(sub - 10) < 2;
    if (!rec && sub == 0xC && W(Scene() + 0x1654) < 0xB4 && (~StatusFlags() & 0x8000u) != 0) rec = true;
    if (rec)
        GRef<u32>(Scene() + 0x1668) |= 1u;
    else
        GRef<u32>(Scene() + 0x1668) &= 0xFFFFFFFEu;
    if (Game(0x27) == 8) UpdateChatterTimers();
    UpdateUnitStates();
    AgeProjectiles();
    func_0011D300();
    UpdateProjectiles();
    func_0011D988();
    UpdateEffects();
    for (s32 p = 0; p < static_cast<s32>(Players()); ++p) UpdateHud(p);
    if ((W(Scene() + 0x1668) & 1u) != 0) RecordReplayFrame();
    if (static_cast<u32>(Ctx()->sub - 10) < 2) func_0024CDD0();
    if ((MissionKind() == 0 || MissionKind() == 3) && Game(0x29) != 3) func_00251050();
    func_00257890();
    if (MissionKind() == 3) func_0017BFB0();
    func_00155AE8();
    func_0016DB40();
    GRef<s32>(Scene() + 0x165C) += 1;
    func_001365A0();
    UpdateMissionRules();
}

// ---- Phases -----------------------------------------------------------------------------------

// EE: 0x0010FB30
// Phase change (docs/port/validation.md V-5): units of the next phase (+0x48 bit 0x100; with
// 0x8000000 only at difficulty >= 2 when 0x002A0318 bit 0) become active (+0x4C 0x10000000, and
// 0x20000000 when lockable); the remaining ones are retired (0x4000000; wrecks 0x40000000 cleared;
// alive enemy units of the old phase lose their target when rules +0x10 bit 4: ground units also
// get 0x2000000).
void func_0010FB30() {
    for (s32 i = 2; i < UnitCount(); ++i) {
        const u32 u = Unit(i);
        const u32 f48 = W(u + 0x48);
        if ((f48 & 0x100u) != 0) {
            if ((f48 & 0x8000000u) != 0 && (Game(0x28) < 2 || (W(0x002A0318) & 1u) == 0)) continue;
            const u32 v = W(u + 0x4C) | 0x10000000u;
            GRef<u32>(u + 0x4C) = v;
            if ((f48 & 0x20000000u) != 0) GRef<u32>(u + 0x4C) = v | 0x20000000u;
            continue;
        }
        if (GRef<u8>(u + 0x69) == 0x47) continue;
        const u32 f4c = W(u + 0x4C);
        if ((f4c & 0x40000000u) != 0) {
            GRef<u32>(u + 0x4C) = (f4c | 0x4000000u) & 0xBFFFFFFFu;
            continue;
        }
        if ((~f4c & 0x10000000u) != 0) continue;
        const u32 a = f4c | 0x4000000u;
        GRef<u32>(u + 0x4C) = a;
        if ((~a & 0x1000000u) != 0 || (f48 & 3u) != 1 || (W(Rules() + 0x10) & 4u) == 0) continue;
        const u8 cls = GRef<u8>(u + 0x54);
        if (cls == 1) {
            GRef<u8>(u + 0x64) = 0;
            GRef<u16>(u + 0x60) = 0;
        } else if (cls == 2) {
            GRef<u8>(u + 0x64) = 0;
            GRef<u16>(u + 0x60) = 0;
            GRef<u32>(u + 0x4C) = a | 0x2000000u;
        }
    }
}

// EE: 0x0010FCA8
// End of gameplay (first frame of sub 0xC): enemy units (VS: side 1; otherwise every side != 0)
// drop their targets, ground units stop firing (0x2000000); players' lock-on state cleared.
void func_0010FCA8() {
    for (s32 i = 2; i < UnitCount(); ++i) {
        const u32 u = Unit(i);
        const u32 side = W(u + 0x48) & 3u;
        if (MissionKind() == 2 ? side != 1 : side == 0) continue;
        const u8 cls = GRef<u8>(u + 0x54);
        if (cls == 1) {
            GRef<u8>(u + 0x64) = 0;
        } else if (cls == 2) {
            GRef<u8>(u + 0x64) = 0;
            GRef<u32>(u + 0x4C) |= 0x2000000u;
        }
    }
    for (s32 p = 0; p < static_cast<s32>(Players()); ++p) {
        const u32 u = Unit(p);
        GRef<u8>(u + 0x62) = 0xFF;
        const u32 P = W(u + 0x44);
        GRef<u8>(P + 0x154) = 0xFF;
        GRef<u8>(P + 0x157) = 0xFF;
        GRef<u8>(P + 0x156) = 0xFF;
        GRef<u8>(P + 0x155) = 0xFF;
        GRef<u32>(P + 0x188) &= 0xFFFF00FFu;
    }
}

// ---- Mission rules ----------------------------------------------------------------------------

// EE: 0x00118C60
void func_00118C60() {
    if (Game(0x29) != 3) GRef<u32>(Status() + 0x20) |= 0x100u;
}

// EE: 0x00118C88
// Sub 0xC init: radio queue end, mission timer stopped.
void func_00118C88() {
    func_00254FD8();
    GRef<u32>(Status() + 0x20) &= 0xFFFFFEFFu;
}

// EE: 0x001191B8
// Objectives and failure conditions of sub 0xB (docs/subsystems/mission-flow.md "Mission rules").
// fpucheck: 3 adds (z - limit, z + vz, - limit)
void CheckMissionObjectives() {
    u32 st = Status();
    const u32 u0 = Unit(0);
    const u32 type = W(Rules() + 0x10) & 3u;
    u32 flags;
    if (type == 1) {
        if (GRef<s8>(Rules() + 0x28) <= GRef<s8>(Status() + 0x7C)) {
            if ((W(Rules() + 0x10) & 0x200u) == 0) {
                GRef<u32>(Status() + 0x20) |= 1u;
                GRef<u8>(st + 0x7D) = static_cast<u8>(GRef<u8>(Status() + 0x7D) | 1);
                RequestSubState(0xC);
                st = Status();
            } else {
                GRef<u32>(Status() + 0x20) |= 8u;
            }
        }
        flags = W(st + 0x20);
    } else {
        if (type == 0) {
            if (W(Rules() + 0x18) <= GRef<u16>(u0 + 0x5C)) GRef<u32>(Status() + 0x20) |= 2u;
        } else if (type == 2) {
            if (F(u0 + 8) < F(Rules() + 0x1C)) {
                GRef<u8>(Status() + 0x7D) = static_cast<u8>(GRef<u8>(Status() + 0x7D) | 1);
                GRef<u32>(st + 0x20) = W(Status() + 0x20) | 1u;
                u16 next = 0xC;
                if (Game(0x27) == 3) {
                    next = 0x14;
                    GRef<u32>(Status() + 0x20) |= 0x20000000u;
                }
                RequestSubState(next);
                st = Status();
            }
        }
        flags = W(st + 0x20);
    }
    bool hp_check = true;
    if ((~flags & 1u) != 0) {
        f32 lim;
        if ((W(Rules() + 0x10) & 3u) == 2) {
            const u32 P = W(u0 + 0x44);
            s32 t = GRef<s32>(st + 0x24);
            if (t % 600 == 0 && F(u0 + 0x14) < F(0x0040DD04) && F(0x0040DD08) < F(u0 + 0x14)) {
                const u8 id = Game(0x27);
                if (id == 3)
                    func_00252FB8(0x16, 1, 0);
                else if (id == 4)
                    func_00252FB8(0xF, 5, 0);
                else if (id == 6)
                    func_00252FB8(0x18, 1, 0);
                st = Status();
            }
            t = GRef<s32>(st + 0x24);
            u8 id = Game(0x27);
            if (t % 900 == 0 && id == 6 && F(u0 + 4) < F(0x0040DD0C)) {
                func_00252FB8(0x19, 1, 0);
                id = Game(0x27);
            }
            if ((id == 3 || id == 6) && (~GRef<u8>(P + 0x282) & 8) != 0 &&
                65536.0f < fpu::Sub(F(u0 + 8), F(Rules() + 0x1C)) &&
                fpu::Sub(fpu::Add(F(u0 + 8), F(u0 + 0x28)), F(Rules() + 0x1C)) <= 65536.0f) {
                if (id == 3)
                    func_00252FB8(0x15, 1, 0);
                else if (id == 6)
                    func_00252FB8(0x1B, 1, 0);
                GRef<u8>(P + 0x282) = static_cast<u8>(GRef<u8>(P + 0x282) | 8);
            }
            lim = F(Rules() + 0x20);
            st = Status();
        } else if (MissionKind() == 3) {
            lim = F(Rules() + 0x20);
        } else {
            if (F(u0 + 8) < F(Rules() + 0x1C)) RequestSubState(0x14);
            lim = F(Rules() + 0x20);
            st = Status();
        }
        const f32 x = F(u0), z = F(u0 + 8);
        bool out;
        if (x < -lim || lim < x || lim < z)
            out = true;
        else if (Game(0x27) < 0x13 && Game(0x29) != 3)
            out = false;
        else
            out = z < -lim;
        if (out) {
            GRef<u8>(st + 0x7D) = static_cast<u8>(GRef<u8>(st + 0x7D) | 8);
            RequestSubState(0xC);
            st = Status();
        }
        flags = W(st + 0x20);
        if ((~flags & 1u) != 0 && W(Rules() + 0x14) <= W(st + 0x24)) {
            if (Game(0x27) == 0x11) GRef<u32>(st + 0x20) = flags | 2u;
            flags = W(st + 0x20);
            if ((flags & 2u) == 0) {
                GRef<u8>(st + 0x7D) = static_cast<u8>(GRef<u8>(st + 0x7D) | 2);
            } else {
                flags &= 0xFFFFFFFDu;
                GRef<u32>(st + 0x20) = flags;
                if ((W(Rules() + 0x10) & 0x200u) != 0) {
                    GRef<u32>(st + 0x20) = flags | 8u;
                    hp_check = true;
                    goto hp;
                }
                GRef<u32>(st + 0x20) = flags | 1u;
                GRef<u8>(st + 0x7D) = static_cast<u8>(GRef<u8>(st + 0x7D) | 1);
            }
            RequestSubState(0xC);
            st = Status();
        }
    }
hp:
    (void)hp_check;
    if (GRef<s16>(u0 + 0x5E) == 0) {
        GRef<u8>(st + 0x7D) = static_cast<u8>(GRef<u8>(st + 0x7D) | 4);
        GRef<u8>(Status() + 0x7D) = static_cast<u8>(GRef<u8>(Status() + 0x7D) & 0xFE);
        GRef<u32>(Status() + 0x20) &= 0xFFFFFFFEu;
        RequestSubState(0xC);
        st = Status();
    }
    if ((W(st + 0x20) & 0x40u) != 0) {
        Fail(0x20);
        st = Status();
    }
    if (Game(0x27) == 7 && (W(st + 0x20) & 4u) != 0 && GRef<s32>(st + 0x24) == 0x477C) {
        s32 n = 0;
        for (s32 i = 0; i < UnitCount(); ++i) {
            const u32 nf = ~W(Unit(i) + 0x4C);
            if ((nf & 0x10000000u) == 0 && (nf & 0x1000000u) == 0 && GRef<u8>(Unit(i) + 0x69) == 0x35) ++n;
        }
        if (n != 0) {
            GRef<u8>(st + 0x7D) = static_cast<u8>(GRef<u8>(st + 0x7D) | 0x20);
            GRef<u8>(Status() + 0x7D) = static_cast<u8>(GRef<u8>(Status() + 0x7D) & 0xFE);
            GRef<u32>(Status() + 0x20) = (W(Status() + 0x20) & 0xFFFFFFFEu) | 0x100000u;
            RequestSubState(0xC);
            func_0010C850(0.0f, 0.0f, 0.0f, Scene(), 0);
            st = Status();
        }
    }
    if (Game(0x27) == 0xF && (W(st + 0x20) & 0x40000u) != 0) {
        const u32 c = W(st + 0x3C) + 1;
        GRef<u32>(st + 0x3C) = c;
        if (GRef<s8>(st + 0x7C) == 1) {
            if (F(Scene() + 0x1670) == 0.0f) {
                func_0010A130(1.0f, 1.0f, 0);
                st = Status();
            } else if (F(Scene() + 0x1670) == 1.0f) {
                GRef<u32>(st + 0x20) |= 1u;
                GRef<u8>(st + 0x7D) = static_cast<u8>(GRef<u8>(st + 0x7D) | 1);
                SetGame(0x2C, 0);
                RequestSubState(0xD);
                st = Status();
            }
        } else if (c >= 0x79) {
            GRef<u8>(st + 0x7D) = static_cast<u8>(GRef<u8>(st + 0x7D) | 0x20);
            GRef<u8>(Status() + 0x7D) = static_cast<u8>(GRef<u8>(Status() + 0x7D) & 0xFE);
            GRef<u32>(Status() + 0x20) &= 0xFFFFFFFEu;
            SetGame(0x2C, 1);
            RequestSubState(0xD);
            st = Status();
        }
    }
    const s16 cd = GRef<s16>(st + 0x86);
    if (cd != 0) GRef<s16>(st + 0x86) = static_cast<s16>(cd - 1);
    if ((W(st + 0x20) & 0x200u) != 0) {
        GRef<u32>(st + 0x20) &= 0xFFFFFDFFu;
        QueueRadioMessage(GRef<u16>(st + 0x84));
    }
}

// EE: 0x00119910
// Sub 0xC: end-of-mission messages, radio and the exit to replay (0x10), mission sequences (0xD),
// results or the game-over menu (0x1C) (docs/subsystems/mission-flow.md).
void MissionEndRules() {
    const u32 u0 = Unit(0);
    const u32 fr = W(Scene() + 0x1654);
    if (fr == 0) func_0010FCA8();
    u32 st;
    u32 flags;
    bool check_tail = false;
    if (Game(0x29) == 3) {
        const u8 r = GRef<u8>(Status() + 0x7D);
        if ((r & 2) == 0) {
            if ((r & 8) != 0 && W(Scene() + 0x1654) == 0) ShowCenterMessage(0, 0x10, 0);
        } else if (W(Scene() + 0x1654) == 0) {
            ShowCenterMessage(0, 0xF, 0);
        }
        GRef<u32>(Rules() + 0x54) = 300;
        st = Status();
        flags = W(st + 0x20);
    } else if ((GRef<u8>(Status() + 0x7D) & 1) != 0) {
        if (W(Scene() + 0x1654) == 0 && GRef<s8>(Rules() + 0x34) != -1) QueueRadioMessage(GRef<s8>(Rules() + 0x34));
        if (W(Scene() + 0x1654) == 0x3C) {
            ShowCenterMessage(0, 9, 0);
            PlayMenuSe(10, 0x45);
            func_0021F190(10.0f, 0, 0, 0, 2);
        }
        st = Status();
        flags = W(st + 0x20);
    } else {
        if (W(Scene() + 0x1654) == 0) {
            func_0021FDF8(1);
            func_0021F190(0.0f, 0, 0xFF, 0xFF, 1);
            func_0021FD40(0, 0x23);
        }
        const u8 r = GRef<u8>(Status() + 0x7D);
        st = Status();
        if ((r & 2) != 0 || (r & 8) != 0) {
            if (W(Scene() + 0x1654) == 0) {
                if ((r & 2) != 0) {
                    const u8 id = Game(0x27);
                    if ((id == 4 || id == 0xB || id == 0xF) && (~W(st + 0x20) & 4u) != 0) GRef<u32>(st + 0x20) |= 0x400u;
                    ShowCenterMessage(0, 0xF, 0);
                } else {
                    ShowCenterMessage(0, 0x10, 0);
                }
            }
            if (W(Scene() + 0x1654) == 0x78) ShowCenterMessage(0, 5, 0);
            st = Status();
            flags = W(st + 0x20);
        } else if ((r & 4) == 0) {
            if (W(Scene() + 0x1654) == 0) ShowCenterMessage(0, 5, 0);
            st = Status();
            flags = W(st + 0x20);
        } else {
            if (W(Scene() + 0x1654) == 0) {
                ShowCenterMessage(0, 5, 0);
                if (MissionKind() == 0) {
                    const u32 sf = StatusFlags();
                    if ((sf & 0x2000u) == 0) {
                        if ((sf & 0x4000u) != 0) QueueRadioChatter(7, 0, GRef<u8>(Radio() + 0x83), 0x31, 0, 0);
                    } else if (GRef<u8>(u0 + 0x63) != 0xFF) {
                        QueueRadioChatter(6, 0, GRef<u8>(Radio() + 0x83), GRef<u8>(Unit(GRef<u8>(u0 + 0x63)) + 0x99), 0, 0);
                    }
                }
            }
            st = Status();
            flags = W(st + 0x20);
        }
    }
    (void)check_tail;
    if ((flags & 1u) != 0 && GRef<s16>(u0 + 0x5E) == 0) {
        GRef<u8>(st + 0x7D) = static_cast<u8>(GRef<u8>(st + 0x7D) | 4);
        GRef<u8>(Status() + 0x7D) = static_cast<u8>(GRef<u8>(Status() + 0x7D) & 0xFE);
        GRef<u32>(Status() + 0x20) = (W(Status() + 0x20) & 0xFFFFFFFEu) | 0x8000u;
        RequestSubState(0xC);
        return;
    }
    auto fade_out_if = [](bool cond) {
        if (cond && F(Scene() + 0x1670) == 0.0f) func_0010A130(1.0f, 1.0f, 0);
        return F(Scene() + 0x1670);
    };
    if (Game(0x29) == 3) {
        const f32 f = fade_out_if(W(Rules() + 0x54) <= W(Scene() + 0x1654) || StartPressed(0));
        if (f != 1.0f) return;
        func_00219260();
        func_0021AEE0();
        StopAllVoices();
        func_0021FDF8(1);
        GRef<u32>(Status() + 0x20) |= 0x80u;
        GRef<u32>(Scene() + 0x1668) &= 0xFFFEFFFFu;
        RequestSubState(0x10);
        return;
    }
    flags = W(st + 0x20);
    if ((flags & 1u) != 0) {
        const f32 lim = F(Rules() + 0x20);
        const f32 x = F(u0), z = F(u0 + 8);
        bool outside = x < -lim || lim < x || !(z <= lim) || z < -lim;
        bool early = false;
        if (outside) {
            const u8 id = Game(0x27);
            early = !(id == 4 || id == 6);
        }
        const f32 f = fade_out_if(W(Rules() + 0x54) <= W(Scene() + 0x1654) || early || StartPressed(0));
        if (f == 1.0f) {
            if (Game(0x27) == 5 || Game(0x27) == 0xE) {
                SetGame(0x2C, 0);
                RequestSubState(0xD);
            } else {
                func_00219260();
                func_0021AEE0();
                StopAllVoices();
                func_0021FDF8(1);
                if (MissionKind() == 3) {
                    EnterMenu(0x1A);
                } else {
                    GRef<u32>(Scene() + 0x1668) &= 0xFFFEFFFFu;
                    RequestSubState(0x10);
                }
            }
        }
        return;
    }
    u32 limit = 0xF0;
    if ((flags & 0x400u) != 0) {
        const u8 id = Game(0x27);
        const u32 frm = W(Scene() + 0x1654);
        if (id == 4) {
            limit = 600;
            if (frm == 2) func_00252FB8(0x17, 1, 0);
        } else if (id == 0xB) {
            limit = 600;
            if (frm == 2) func_00252FB8(0x22, 1, 0);
        } else if (id == 0xF) {
            limit = 0x1E0;
            if (frm == 2) func_00252FB8(0x23, 1, 0);
        } else if (id == 0x11) {
            limit = 0x30C;
            if (frm == 2) {
                if ((flags & 0x400000u) != 0)
                    func_00252FB8(0x10, 1, 0);
                else if ((flags & 0x800000u) != 0)
                    func_00252FB8(0x12, 1, 0);
            }
        } else if (id == 0x12) {
            limit = 600;
            if (frm == 2) func_00252FB8(0x1A, 1, 0);
        } else {
            limit = 600;
        }
    }
    const f32 f = fade_out_if(!(W(Scene() + 0x1654) < limit && !StartPressed(0)));
    if (f != 1.0f) return;
    if ((GRef<u8>(Status() + 0x7D) & 4) != 0 && (~W(Status() + 0x20) & 0x8000u) != 0) {
        GRef<u32>(Status() + 0x20) |= 0x80u;
        GRef<u32>(Scene() + 0x1668) &= 0xFFFEFFFFu;
        RequestSubState(0x10);
        return;
    }
    RequestSubState(0x1C);
}

// EE: 0x0011A128
// Sub 0xA rules: start events, the start message after rules +0x60 frames, then sub 0xB and the
// first-phase events.
void func_0011A128() {
    const u32 ev = Events();
    if (W(Scene() + 0x1654) == 0 && GRef<s8>(W(ev)) != -1) RunMissionEvents(W(Events()));
    if (W(Rules() + 0x60) <= W(Scene() + 0x1654)) {
        for (s32 p = 0; p < static_cast<s32>(Players()); ++p) ShowCenterMessage(p, 6, 0);
        RequestSubState(0xB);
        if (GRef<s8>(W(Events() + 4)) != -1) RunMissionEvents(W(Events() + 4));
    }
    GRef<s32>(Status() + 0x34) += 1;
    func_0011A7D8();
}

// EE: 0x0011A208
// Sub 0xB rules: objectives (VS: 0x00118CC0), mission time, phase change, segment timers.
void func_0011A208() {
    if (Players() == 2)
        func_00118CC0();
    else
        CheckMissionObjectives();
    u32 st = Status();
    const u32 r = Rules();
    if ((W(st + 0x20) & 8u) == 0) {
        if (W(st + 0x24) < W(r + 0x14)) {
            GRef<u32>(st + 0x24) += 1;
            GRef<s32>(st + 0x28) += 1;
        }
        GRef<s32>(st + 0x34) += 1;
        if ((W(st + 0x20) & 4u) != 0 && GRef<s32>(st + 0x24) == GRef<s32>(r + 0x5C)) {
            ShowCenterMessage(0, 8, GRef<s32>(r + 0x58));
            st = Status();
        }
    } else {
        GRef<u32>(st + 0x2C) = W(st + 0x24);
        GRef<u32>(st + 0x20) = (W(st + 0x20) & 0xFFFFFFF7u) | 4u;
        func_0010FB30();
        func_0017B2F0();
        if (GRef<s8>(W(Events() + 8)) != -1) RunMissionEvents(W(Events() + 8));
        st = Status();
        GRef<u32>(st + 0x24) = 0;
    }
    const u32 f = W(st + 0x20);
    if ((f & 0x31u) != 0) {
        if ((f & 4u) == 0)
            GRef<u32>(st + 0x2C) = W(st + 0x24);
        else
            GRef<u32>(st + 0x30) = W(st + 0x24);
        func_0024D1C8();
    }
    func_0011A7D8();
}

// EE: 0x0011A340
// Sub 0xC rules (VS: 0x00119000).
void func_0011A340() {
    if (Players() == 2)
        func_00119000();
    else
        MissionEndRules();
    GRef<s32>(Status() + 0x34) += 1;
}

// EE: 0x0011A390
void UpdateMissionRules() {
    const u16 sub = Ctx()->sub;
    if (sub == 0xA)
        func_0011A128();
    else if (sub == 0xB)
        func_0011A208();
    else if (sub == 0xC)
        func_0011A340();
}

// EE: 0x0011A7D8
// Timed events: per phase (+0x34 / +0x38 lists of {u16 time/6, u16 offset}, counters +0x44/+0x48)
// on the mission time, and the global list +0x30 (counter +0x40) on the total time +0x34.
void func_0011A7D8() {
    const u32 st = Status(), ev = Events();
    if (Ctx()->sub == 0xB) {
        const bool phase2 = (W(st + 0x20) & 4u) != 0;
        const u32 e = W(ev + (phase2 ? 0x38 : 0x34)) + W(st + (phase2 ? 0x48 : 0x44)) * 4;
        const u16 t = GRef<u16>(e);
        if (t != 0xFFFF && static_cast<u32>(t) * 6 == W(st + 0x24)) {
            RunMissionEvents(W(ev + (phase2 ? 0x14 : 0x10)) + GRef<u16>(e + 2));
            GRef<s32>(Status() + (phase2 ? 0x48 : 0x44)) += 1;
        }
    }
    const u32 s2 = Status(), e2 = Events();
    const u32 g = W(e2 + 0x30) + W(s2 + 0x40) * 4;
    const u16 t = GRef<u16>(g);
    if (t != 0xFFFF && static_cast<u32>(t) * 6 == W(s2 + 0x34)) {
        RunMissionEvents(W(e2 + 0xC) + GRef<u16>(g + 2));
        GRef<s32>(Status() + 0x40) += 1;
    }
}

// EE: 0x0011A920
// Kill-count events of unit kind k: per phase list (+0x68[k] / +0x88[k], cursor +0x54[k] /
// +0x5C[k], count +0x6C[k] / +0x74[k]) and the global list +0x48[k] (cursor +0x4C[k], count
// +0x64[k]); an entry {u8 kind, u8 count, u16 offset} fires when the count matches.
void func_0011A920(u32 k) {
    const u32 st = Status(), ev = Events();
    const bool phase2 = (W(st + 0x20) & 4u) != 0;
    const u32 cnt = st + (phase2 ? 0x74 : 0x6C) + k;
    GRef<u8>(cnt) = static_cast<u8>(GRef<u8>(cnt) + 1);
    const u32 list = W(ev + 4 * k + (phase2 ? 0x88 : 0x68));
    if (list != 0) {
        const u32 e = list + static_cast<u32>(GRef<u8>(st + k + (phase2 ? 0x5C : 0x54))) * 4;
        if (GRef<u8>(e) == k && GRef<u8>(e + 1) == GRef<u8>(st + k + (phase2 ? 0x74 : 0x6C))) {
            RunMissionEvents(W(ev + (phase2 ? 0x2C : 0x28)) + GRef<u16>(e + 2));
            const u32 cur = Status() + (phase2 ? 0x5C : 0x54) + k;
            GRef<u8>(cur) = static_cast<u8>(GRef<u8>(cur) + 1);
        }
    }
    const u32 all = Status() + 0x64 + k;
    GRef<u8>(all) = static_cast<u8>(GRef<u8>(all) + 1);
    const u32 gl = W(Events() + 4 * k + 0x48);
    if (gl != 0) {
        const u32 e = gl + static_cast<u32>(GRef<u8>(Status() + k + 0x4C)) * 4;
        if (GRef<u8>(e) == k && GRef<u8>(e + 1) == GRef<u8>(Status() + k + 0x64)) {
            RunMissionEvents(W(Events() + 0x24) + GRef<u16>(e + 2));
            GRef<u8>(Status() + 0x4C + k) = static_cast<u8>(GRef<u8>(Status() + 0x4C + k) + 1);
        }
    }
}

// EE: 0x0011AAE0
// Mission event script (docs/subsystems/mission-flow.md "Event script"): 4-byte aligned
// commands until 0xFF. Not run in demo playback (game[0x29] == 3).
// fpucheck: 6 adds (3 x position + C for ops 0x0C/0x36/0x37, 3 x offset for op 0x22)
void RunMissionEvents(u32 e) {
    if (Game(0x29) == 3) return;
    const f32 k2a = F(0x0040DD14);
    while (true) {
        const u8 op = GRef<u8>(e);
        if (op == 0xFF) return;
        const u32 r = Rules();
        u32 next = e + 4;
        auto set_pos = [&](f32 add) {
            GRef<u32>(r + 0x10) &= 0xFFFFFBFFu;
            GRef<u32>(r + 0x38) = fpu::FixUnsSfSi(fpu::Add(F(e + 4), add));
            GRef<u32>(r + 0x3C) = fpu::FixUnsSfSi(fpu::Add(F(e + 8), add));
            GRef<u32>(r + 0x40) = fpu::FixUnsSfSi(fpu::Add(F(e + 0xC), add));
        };
        switch (op) {
            case 0x00: {
                const u32 v = W(r + 0x10) & 0xFFFFFFFCu;
                GRef<u32>(r + 0x10) = v;
                const u8 a = GRef<u8>(e + 1);
                if (a >= 1 && a <= 3) GRef<u32>(r + 0x10) = v | a;
                break;
            }
            case 0x01: GRef<s32>(r + 0x14) = GRef<s16>(e + 2) * 0x3C; break;
            case 0x02: GRef<s32>(r + 0x18) = GRef<s16>(e + 2); break;
            case 0x03:
                GRef<u8>(r + 0x28) = GRef<u8>(e + 1);
                GRef<u8>(Status() + 0x7C) = 0;
                break;
            case 0x04:
                if (GRef<u8>(e + 1) == 1)
                    GRef<u32>(r + 0x10) |= 0x200u;
                else
                    GRef<u32>(r + 0x10) &= 0xFFFFFDFFu;
                break;
            case 0x05: GRef<u32>(r + 0x1C) = W(e + 4); next = e + 8; break;
            case 0x06: GRef<u32>(r + 0x20) = W(e + 4); next = e + 8; break;
            case 0x07: {
                const u32 v = W(r + 0x10) & 0xFFFFFFF3u;
                GRef<u32>(r + 0x10) = v;
                const u8 a = GRef<u8>(e + 1);
                if (a == 1)
                    GRef<u32>(r + 0x10) = v | 4u;
                else if (a == 2)
                    GRef<u32>(r + 0x10) = v | 0xCu;
                break;
            }
            case 0x08:
                GRef<u16>(r + 0x24) = GRef<u16>(e + 2);
                GRef<u16>(r + 0x26) = GRef<u16>(e + 4);
                next = e + 8;
                break;
            case 0x09:
                if (GRef<u8>(e + 1) == 1)
                    GRef<u32>(Status() + 0x20) |= 0x100u;
                else
                    GRef<u32>(Status() + 0x20) &= 0xFFFFFEFFu;
                break;
            case 0x0A: func_0011A920(GRef<u8>(e + 1)); break;
            case 0x0B:
                SetGame(0x2C, GRef<u8>(e + 1));
                RequestSubState(0xD);
                break;
            case 0x0C: set_pos(F(0x0040DD10)); next = e + 0x10; break;
            case 0x0D:
                if (GRef<u8>(e + 1) == 1)
                    GRef<u32>(r + 0x10) |= 0x100u;
                else
                    GRef<u32>(r + 0x10) &= 0xFFFFFEFFu;
                break;
            case 0x0E: GRef<s32>(r + 0x44) = GRef<s16>(e + 2); break;
            case 0x0F: GRef<s32>(r + 0x54) = GRef<s16>(e + 2) * 6; break;
            case 0x10: QueueRadioMessage(GRef<s16>(e + 2)); break;
            case 0x11: func_00252FB8(GRef<s16>(e + 2), GRef<s8>(e + 1), 0); break;
            case 0x12:
                func_0017B6A0(F(e + 4), F(e + 8), F(e + 0xC), GRef<u8>(e + 1), GRef<u8>(e + 2));
                next = e + 0x10;
                break;
            case 0x13: GRef<u8>(r + 0x34) = GRef<u8>(e + 2); break;
            case 0x14: GRef<u8>(r + 0x35) = GRef<u8>(e + 2); break;
            case 0x15:
                func_0017B818(F(e + 4), F(e + 8), F(e + 0xC), GRef<u8>(e + 1));
                next = e + 0x10;
                break;
            case 0x16: {
                next = e + 8;
                const s64 need = GRef<s16>(e + 2);
                const s64 have = GRef<u16>(Unit(0) + 0x5C);
                const u8 m = GRef<u8>(e + 1);
                if ((m == 0 && need <= have) || (m == 1 && !(need <= have))) func_00252FB8(GRef<s16>(e + 4), 1, 0);
                break;
            }
            case 0x17:
                func_0017B930(F(e + 4), F(e + 8), F(e + 0xC), GRef<u8>(e + 1));
                next = e + 0x10;
                break;
            case 0x18:
                func_0017BAF0(F(e + 4), F(e + 8), F(e + 0xC), GRef<u8>(e + 1));
                next = e + 0x10;
                break;
            case 0x19: {
                QueueRadioMessage(GRef<u8>(e + 1));
                const u32 st = Status();
                GRef<u16>(st + 0x84) = GRef<u8>(e + 1);
                GRef<s16>(st + 0x86) = static_cast<s16>(GRef<s16>(e + 2) * 6);
                break;
            }
            case 0x20: {
                const u32 u = Unit(GRef<u8>(e + 1));
                if (Ctx()->sub != 0xC && 0 < GRef<s16>(u + 0x5E) && (~W(u + 0x4C) & 0x10000000u) != 0)
                    func_0011BC78(GRef<u8>(e + 1));
                break;
            }
            case 0x21: {
                func_0011BCC0(GRef<u8>(e + 1));
                const u32 u = Unit(GRef<u8>(e + 1));
                GRef<u16>(u + 0x5E) = 0;
                GRef<u32>(u + 0x4C) &= 0xFEFFFFFFu;
                break;
            }
            case 0x22: {
                next = e + 0x10;
                const u32 u = Unit(GRef<u8>(e + 1));
                if (Ctx()->sub == 0xC || GRef<s16>(u + 0x5E) <= 0) break;
                if ((~W(u + 0x4C) & 0x10000000u) == 0) break;
                func_0011BC78(GRef<u8>(u + 0x55));
                if (GRef<u8>(e + 2) == 0xFF)
                    StoreVf0(u);
                else
                    CopyQ(u, Unit(GRef<u8>(e + 2)));
                GRef<f32>(u) = fpu::Add(F(u), F(e + 4));
                GRef<f32>(u + 4) = fpu::Add(F(u + 4), F(e + 8));
                GRef<f32>(u + 8) = fpu::Add(F(u + 8), F(e + 0xC));
                break;
            }
            case 0x23: {
                const u32 u = Unit(GRef<u8>(e + 1));
                if (0 < GRef<s16>(u + 0x5E)) {
                    if (GRef<s16>(e + 2) == 0) GRef<u8>(u + 0x63) = 0xFF;
                    GRef<u16>(u + 0x5E) = GRef<u16>(e + 2);
                }
                break;
            }
            case 0x24: {
                u8 i = GRef<u8>(e + 1);
                const u32 u = Unit(i);
                if (Alive(u)) {
                    func_0011E708(u);
                    i = GRef<u8>(e + 1);
                }
                func_0011BCC0(i);
                GRef<u16>(u + 0x5E) = 0;
                GRef<u32>(u + 0x4C) &= 0xFEFFFFFFu;
                break;
            }
            case 0x25: {
                const u32 u = Unit(GRef<u8>(e + 1));
                const u8 cls = GRef<u8>(u + 0x54);
                if (cls == 1)
                    GRef<u8>(W(u + 0x44) + 0x19) = static_cast<u8>(GRef<u8>(W(u + 0x44) + 0x19) | 0x80);
                else if (cls == 2)
                    GRef<u8>(W(u + 0x44) + 0x41) = static_cast<u8>(GRef<u8>(W(u + 0x44) + 0x41) | 0x80);
                break;
            }
            case 0x26: {
                const u32 u = Unit(GRef<u8>(e + 1));
                const u8 cls = GRef<u8>(u + 0x54);
                if (cls == 1) {
                    const u32 s = W(u + 0x44);
                    GRef<u8>(s + 0x19) = static_cast<u8>(GRef<u8>(s + 0x19) | 0x80);
                    GRef<u8>(s + 0x18) = GRef<u8>(e + 2);
                } else if (cls == 2) {
                    const u32 s = W(u + 0x44);
                    GRef<u8>(s + 0x41) = static_cast<u8>(GRef<u8>(s + 0x41) | 0x80);
                    GRef<u8>(s + 0x40) = GRef<u8>(e + 2);
                }
                break;
            }
            case 0x27: {
                const u32 u = Unit(GRef<u8>(e + 1));
                GRef<u8>(u + 0x63) = 0;
                GRef<u32>(u + 0x50) |= 4u;
                GRef<u32>(u + 0x4C) &= 0xDFFFFFFFu;
                break;
            }
            case 0x28: GRef<u32>(Status() + 0x20) |= 0x200000u; break;
            case 0x29: GRef<u32>(Status() + 0x20) |= 0x80000u; break;
            case 0x2A: {
                guest::GuestFrame f(0x10);
                const u32 v = f.Address();
                ZeroQ(v);
                const u32 u = Unit(GRef<u8>(e + 1));
                func_0020C2A0(k2a, u, v, 2);
                GRef<f32>(v + 0xC) = k2a;
                func_00170B60(0x11, 0, 2, Unit(GRef<u8>(e + 1)), v);
                break;
            }
            case 0x30: GRef<s32>(r + 0x58) = GRef<s16>(e + 2) * 6; break;
            case 0x31: GRef<s32>(r + 0x60) = GRef<s16>(e + 2) * 6; break;
            case 0x32:
                GRef<u32>(r + 0x10) |= 0x400u;
                GRef<u32>(r + 0x48) = fpu::FixUnsSfSi(F(e + 4) * 60.0f);
                GRef<u32>(r + 0x4C) = fpu::FixUnsSfSi(F(e + 8) * 60.0f);
                GRef<u32>(r + 0x50) = fpu::FixUnsSfSi(F(e + 0xC) * 60.0f);
                next = e + 0x10;
                break;
            case 0x33: GRef<s32>(r + 0x5C) = GRef<s16>(e + 2) * 6; break;
            case 0x34:
                if (1 < Game(0x28)) GRef<s32>(r + 0x18) = GRef<s16>(e + 2);
                break;
            case 0x35:
                if (Game(0x28) < 2) GRef<s32>(r + 0x18) = GRef<s16>(e + 2);
                break;
            case 0x36:
                next = e + 0x10;
                if (1 < Game(0x28)) set_pos(F(0x0040DD18));
                break;
            case 0x37:
                next = e + 0x10;
                if (Game(0x28) < 2) set_pos(F(0x0040DD1C));
                break;
            case 0x40: GRef<u8>(Status() + 0x7F) = GRef<u8>(e + 1) == 1 ? 1 : 0; break;
            case 0x41: GRef<u8>(Status() + 0x7E) = GRef<u8>(e + 2); break;
            case 0x42: GRef<u8>(Status() + 0x7F) = GRef<u8>(e + 2); break;
            case 0x43:
                if (GRef<s16>(e + 2) == -1)
                    func_0017BF90();
                else
                    func_0017BF98(static_cast<u32>(GRef<s16>(e + 2)));
                break;
            case 0x44:
                if (GRef<u8>(e + 1) == 1) GRef<u32>(Status() + 0x20) |= 0x1000u;
                break;
            case 0x50:
                if (Ctx()->sub == 0xB && W(Scene() + 0x1654) < 600) break;
                if ((StatusFlags() & 0x100u) != 0) func_0014F470(0xFF, 0, GRef<s16>(e + 2));
                break;
            case 0x51: {
                GRef<u8>(r + 0x2A) = GRef<u8>(e + 1);
                GRef<u8>(r + 0x2B) = GRef<u8>(e + 2);
                GRef<u32>(r + 0x2C) = W(e + 4);
                GRef<u32>(r + 0x30) = W(e + 8);
                next = e + 0xC;
                break;
            }
            case 0x52: GRef<u32>(Status() + 0x20) |= 0x400440u; break;
            case 0x53: GRef<u32>(Status() + 0x20) |= 0x800440u; break;
            case 0x60:
                func_0021F190(0.0f, 0, 0xFF, 0xFF, 1);
                func_0021FD40(0, GRef<u16>(e + 2));
                GRef<s32>(Status() + 0x80) = GRef<s16>(e + 2);
                break;
            case 0x61:
                func_0021F190(0.0f, 0, 0, 0, 1);
                func_0021FDF8(1);
                GRef<s32>(Status() + 0x80) = -1;
                break;
            case 0x62:
                if (GRef<s16>(e + 2) != -1) {
                    func_0021FD40(0, GRef<u16>(e + 2));
                    GRef<s32>(Status() + 0x80) = GRef<s16>(e + 2);
                }
                func_0021F190(fpu::IntToFloat(GRef<u8>(e + 1)), 0, 0xFF, 0xFF, 1);
                break;
            case 0x63:
                func_0021F190(fpu::IntToFloat(GRef<u8>(e + 1)), 0, 0, 0, GRef<s16>(e + 2));
                if (GRef<s16>(e + 2) == 2) GRef<s32>(Status() + 0x80) = -1;
                break;
            default:
                // Codes without a handler leave the script pointer in place: the original loops
                // forever (never seen in the shipped mission data). Stop instead of hanging.
                log::WriteOnce("RunMissionEvents", log::Level::Error, "game",
                               "mission event script: unknown command 0x%02X at 0x%08X", op, e);
                return;
        }
        e = next;
    }
}

// ---- Units -----------------------------------------------------------------------------------

// EE: 0x0011BC78
// Activates unit i (0x10000000; lockable 0x20000000 too when +0x48 has it).
void func_0011BC78(s32 i) {
    const u32 u = Unit(i);
    const u32 f = W(u + 0x4C);
    GRef<u32>(u + 0x4C) = f | 0x10000000u;
    if ((W(u + 0x48) & 0x20000000u) != 0) GRef<u32>(u + 0x4C) = f | 0x30000000u;
}

// EE: 0x0011BCC0
// Deactivates unit i.
void func_0011BCC0(s32 i) {
    const u32 u = Unit(i);
    const u32 f = W(u + 0x4C);
    GRef<u32>(u + 0x4C) = f & 0xEFFFFFFFu;
    if ((W(u + 0x48) & 0x20000000u) != 0) GRef<u32>(u + 0x4C) = f & 0xCFFFFFFFu;
}

// EE: 0x0011BD10
// Unit back to its placement record (block 22 via gp 0x004104CC): position, w = 1, angles from
// degrees (s16 * C 0x0040DD20 / 180); aircraft (class 0) also get the start velocity
// C 0x0040DD24 forward.
void func_0011BD10(u32 u) {
    const f32 k = F(0x0040DD20);
    const u32 rec = W(W(0x004104CC) + 4) + static_cast<u32>(GRef<u8>(u + 0x55)) * 0x18;
    GRef<u32>(u) = W(rec + 4);
    GRef<u32>(u + 4) = W(rec + 8);
    const u32 z = W(rec + 0xC);
    GRef<f32>(u + 0xC) = 1.0f;
    GRef<u32>(u + 8) = z;
    GRef<f32>(u + 0x10) = fpu::IntToFloat(GRef<s16>(rec + 0x10)) * k / 180.0f;
    GRef<f32>(u + 0x14) = fpu::IntToFloat(GRef<s16>(rec + 0x12)) * k / 180.0f;
    const s16 r = GRef<s16>(rec + 0x14);
    GRef<u32>(u + 0x1C) = 0;
    GRef<f32>(u + 0x18) = fpu::IntToFloat(r) * k / 180.0f;
    if (GRef<u8>(u + 0x54) == 0) {
        guest::GuestFrame f(0x50);
        const u32 v = f.Address(0), m = f.Address(0x10);
        ZeroQ(v);
        GRef<u32>(v + 8) = W(0x0040DD24);
        sceVu0UnitMatrix(m);
        func_00102D28(m, m, u + 0x10);
        sceVu0ApplyMatrix(u + 0x20, m, v);
    }
}

// EE: 0x0011D580
// AI aircraft emitters: wingtip vapour at +-radius (0x00146988) when +0x56 bit 0 (cleared when
// the aircraft's group +0x58[+0x5A] is -1; handles +0x7C/+0x80 dropped when out of 0..0x7F),
// missile-trail emitter +0x84 when bit 1.
void func_0011D580(s32 i) {
    const u32 u = Unit(i);
    if (GRef<s8>(u + GRef<u8>(u + 0x5A) + 0x58) == -1) GRef<u16>(u + 0x56) &= 0xFFFEu;
    u16 fl = GRef<u16>(u + 0x56);
    if ((fl & 1) == 0) {
        GRef<s32>(u + 0x80) = -1;
        GRef<s32>(u + 0x7C) = -1;
    } else {
        guest::GuestFrame f(0x60);
        const u32 a = f.Address(0), b = f.Address(0x10), m = f.Address(0x20);
        ZeroQ(a);
        GRef<f32>(a) = -25.0f;
        ZeroQ(b);
        GRef<f32>(b) = 25.0f;
        const f32 r = func_00146988(UnitModels(), i);
        GRef<f32>(b) = r;
        GRef<f32>(a) = -r;
        sceVu0UnitMatrix(m);
        func_00102D28(m, m, u + 0x10);
        sceVu0ApplyMatrix(a, m, a);
        sceVu0AddVector(a, a, u);
        sceVu0ApplyMatrix(b, m, b);
        sceVu0AddVector(b, b, u);
        if (static_cast<u32>(GRef<s32>(u + 0x7C) + 1) >= 0x81) GRef<s32>(u + 0x7C) = -1;
        if (0x80 < static_cast<u32>(GRef<s32>(u + 0x80) + 1)) GRef<s32>(u + 0x80) = -1;
        GRef<u32>(u + 0x7C) = func_00185818(W(u + 0x7C), a, u + 0x20);
        GRef<u32>(u + 0x80) = func_00185818(W(u + 0x80), b, u + 0x20);
        fl = GRef<u16>(u + 0x56);
    }
    if ((fl & 2) != 0) GRef<u32>(u + 0x84) = func_0018AC00(W(u + 0x84), u, u + 0x20);  // a3 (angles) is not read
}

// EE: 0x0011D738
// Ground/naval unit emitters by +0x56 bits (0x200/0x100 fire 0x002061B0 kinds 4/5, 0x400/0x800
// smoke 0x00200B00, 0x1000 0x0019D7E0, 0x2000 0x00199688; handle +0x7C) and the ship wake
// (kind 5 units with +0x4C bits 0x1000000 and 0x1000: 0x0019F230).
void func_0011D738(s32 i) {
    const u32 u = Unit(i);
    if (GRef<u8>(u + 0x6A) == 5 && (GRef<u64>(u + 0x48) & 0x0100100000000000ull) == 0x0100100000000000ull)
        func_0019F230(u, u + 0x10, u + 0x20, GRef<u8>(u + 0x69));
    const u16 f = GRef<u16>(u + 0x56);
    const u32 h = W(u + 0x7C);
    if ((f & 0x200) != 0)
        GRef<u32>(u + 0x7C) = func_002061B0(h, u, u + 0x20, 4);
    else if ((f & 0x100) != 0)
        GRef<u32>(u + 0x7C) = func_002061B0(h, u, u + 0x20, 5);
    else if ((f & 0x400) != 0)
        GRef<u32>(u + 0x7C) = func_00200B00(h, u, u + 0x20, 2, 0x96);
    else if ((f & 0x800) != 0)
        GRef<u32>(u + 0x7C) = func_00200B00(h, u, u + 0x20, 1, 0xD2);
    else if ((f & 0x1000) != 0)
        GRef<u32>(u + 0x7C) = func_0019D7E0(h, u, u + 0x10, u + 0x20, 0);
    else if ((f & 0x2000) != 0)
        GRef<u32>(u + 0x7C) = func_00199688(h, u, u + 0x20, u + 0x10);
    else
        GRef<s32>(u + 0x7C) = -1;
}

// EE: 0x0011D988
// Per frame after the projectiles: the carrier deck lights (unit 0x00410640+0x88, track
// animation 0x001359D0 while status 0x2000000), model part animations of active units (+0x48 bits
// 0x10: (t & 15) / 16, 0x80: (t & 255) / 255, 0x40: t % (address % 120 + 120)), then the emitters
// by class (players 0x00126970, AI aircraft 0x0011D580, ground units 0x0011D738).
void func_0011D988() {
    const u32 t = W(Scene() + 0x1660);
    const f32 a = fpu::IntToFloat(static_cast<s32>(t & 0xF)) * 0.0625f;
    const f32 b = fpu::IntToFloat(static_cast<s32>(t & 0xFF)) / 255.0f;
    const u8 carrier = GRef<u8>(UnitList() + 0x88);
    if (carrier != 0xFF) {
        if ((StatusFlags() & 0x2000000u) == 0) {
            GRef<u16>(Unit(carrier) + 0x56) = 0;
            GRef<s32>(Unit(GRef<u8>(UnitList() + 0x88)) + 0x7C) = -1;
        } else {
            func_001359D0(carrier);
            GRef<u16>(Unit(GRef<u8>(UnitList() + 0x88)) + 0x56) = 0x1000;
        }
    }
    for (s32 i = 0; i < UnitCount(); ++i) {
        const u32 u = Unit(i);
        const u32 nf = ~W(u + 0x4C);
        if ((nf & 0x10000000u) != 0 || (nf & 0x100u) != 0) continue;
        u32 f48 = W(u + 0x48);
        if ((f48 & 0x10) != 0) {
            func_00145D08(a, UnitModels(), i, 1);
            f48 = W(u + 0x48);
        }
        if ((f48 & 0x80) != 0) {
            func_00145D08(b, UnitModels(), i, 1);
            f48 = W(u + 0x48);
        }
        if ((f48 & 0x40) != 0) {
            const s32 per = static_cast<s32>(u) % 0x78 + 0x78;
            const s32 ph = GRef<s32>(Scene() + 0x1660) % per;
            func_00145D08(fpu::IntToFloat(ph) / fpu::IntToFloat(per), UnitModels(), i, 1);
        }
        const u8 cls = GRef<u8>(u + 0x54);
        if (cls == 0)
            UpdatePlayerTrails(i);
        else if (cls == 1)
            func_0011D580(i);
        else if (cls == 2)
            func_0011D738(i);
    }
}

// EE: 0x0011E250
// Respawn of a destroyed unit (+0x4C bit 0x4000000 clear): after 180 frames, by respawn rule
// +0x88 (1-4: up to 1/2/3/14 times; 5-7: up to 14 times after 0x2A30/0x5460/0x7E90 frames) the
// unit is reactivated at its placement (0x0011BD10, hp/score from the definition); AI aircraft
// reappear at least 32768 from the player. Returns 1 on a respawn.
s32 func_0011E250(s32 i) {
    const u32 u = Unit(i);
    const u32 f = W(u + 0x4C);
    if ((f & 0x4000000u) != 0) {
        GRef<u32>(u + 0x4C) = f & 0xBFFFFFFFu;
        return 0;
    }
    const s16 c = static_cast<s16>(GRef<s16>(u + 0x8A) + 1);
    GRef<s16>(u + 0x8A) = c;
    if (c <= 0xB3) return 0;
    bool go = false;
    const s8 n = GRef<s8>(u + 0x89);
    switch (static_cast<s8>(GRef<u8>(u + 0x88) - 1)) {
        case 0: go = n < 1; break;
        case 1: go = n < 2; break;
        case 2: go = n < 3; break;
        case 3: go = n < 14; break;
        case 4: if (n < 14) go = 0x2A2F < GRef<s16>(u + 0x8A); break;
        case 5: if (n < 14) go = 0x545F < GRef<s16>(u + 0x8A); break;
        case 6: if (n < 14) go = 0x7E8F < GRef<s16>(u + 0x8A); break;
        default: GRef<u32>(u + 0x4C) = f & 0xBFFFFFFFu; break;
    }
    if (!go) return 0;
    const u32 g = W(u + 0x4C);
    GRef<s8>(u + 0x89) = static_cast<s8>(GRef<s8>(u + 0x89) + 1);
    GRef<u16>(u + 0x8A) = 0;
    GRef<u32>(u + 0x4C) = g | 0x10000000u;
    if ((W(u + 0x48) & 0x20000000u) != 0) GRef<u32>(u + 0x4C) = g | 0x30000000u;
    func_0011BD10(u);
    GRef<u8>(u + 0x5A) = 0;
    GRef<u16>(u + 0x60) = GRef<u16>(W(u + 0x40) + 4);
    GRef<u16>(u + 0x5E) = GRef<u16>(W(u + 0x40) + 6);
    const u32 p0 = Unit(0);
    const u8 cls = GRef<u8>(u + 0x54);
    if (cls == 1) {
        guest::GuestFrame fr(0x10);
        const u32 d = fr.Address();
        sceVu0SubVector(d, u, p0);
        GRef<u32>(d + 4) = 0;
        func_00104FA8(d);
        const f32 len = GetVectorLength(d);
        if (len < 32768.0f) sceVu0ScaleVector(d, d, 32768.0f / len);
        sceVu0AddVector(u, d, p0);
        GRef<u8>(W(u + 0x44) + 0x18) = 0;
    } else if (cls == 2) {
        GRef<u8>(W(u + 0x44) + 0x40) = 0;
    }
    return 1;
}

// EE: 0x0011E4A0
// Whether unit i can still respawn (rule +0x88 against the count +0x89).
s32 func_0011E4A0(s32 i) {
    const u32 u = Unit(i);
    const s8 n = GRef<s8>(u + 0x89);
    switch (GRef<u8>(u + 0x88)) {
        case 0: return 0;
        case 1: return n < 1 ? 1 : (n < 2 ? 1 : (n < 3 ? 1 : (13 < n ? 0 : 1)));
        case 2: return n < 2 ? 1 : (n < 3 ? 1 : (13 < n ? 0 : 1));
        case 3: return n < 3 ? 1 : (13 < n ? 0 : 1);
        case 4: case 5: case 6: case 7: return 13 < n ? 0 : 1;
        default: return 0;
    }
}

// EE: 0x0011E708
// Unit destroyed: kill-count events of its kind (< 8), its own event script +0x8C and the
// phase script +0x90 / +0x94.
void func_0011E708(u32 u) {
    const u8 kind = GRef<u8>(static_cast<u32>(GRef<u8>(u + 0x55)) * 0x18 + W(W(0x004104CC) + 4) + 0x17);
    if (kind < 8) func_0011A920(kind);
    if (W(u + 0x8C) != 0) RunMissionEvents(W(u + 0x8C));
    if ((StatusFlags() & 4u) == 0) {
        if (W(u + 0x90) != 0) RunMissionEvents(W(u + 0x90));
    } else if (W(u + 0x94) != 0) {
        RunMissionEvents(W(u + 0x94));
    }
}

// EE: 0x0011F0F0
// Mission 0xF zone check of segment (a, b) against box zone z (0x30 bytes: min +0x00, max +0x10,
// axis kind +0x21, id +0x20): entering / leaving through the zone's plane sets or clears *state.
void func_0011F0F0(u32 z, u32 state, u32 a, u32 b) {
    const f32 x0 = F(z);
    if (GRef<u8>(z + 0x21) == 2) {
        if (F(a) <= x0 || F(z + 0x10) <= F(a) || F(a + 8) <= F(z + 8) || F(z + 0x18) <= F(a + 8)) return;
        if (F(b) <= x0 || F(z + 0x10) <= F(b) || F(b + 8) <= F(z + 8) || F(z + 0x18) <= F(b + 8)) return;
        f32 ay = F(a + 4), zy = F(z + 4);
        if (ay <= zy && zy < F(b + 4)) {
            GRef<u8>(state) = GRef<u8>(z + 0x20);
            ay = F(a + 4);
            zy = F(z + 4);
        }
        if (ay < zy) return;
        if (F(b + 4) < zy) GRef<u8>(state) = 0;
        return;
    }
    if (F(a) <= x0 || F(z + 0x10) <= F(a) || F(a + 4) <= F(z + 4) || F(z + 0x14) <= F(a + 4)) return;
    if (F(b) <= x0 || F(z + 0x10) <= F(b) || F(b + 4) <= F(z + 4) || F(z + 0x14) <= F(b + 4)) return;
    f32 az = F(a + 8), zz = F(z + 8);
    if (GRef<u8>(z + 0x21) == 1) {
        if (az <= zz && zz < F(b + 8)) {
            GRef<u8>(state) = 0;
            az = F(a + 8);
            zz = F(z + 8);
        }
        if (az < zz) return;
        if (zz <= F(b + 8)) return;
        GRef<u8>(state) = GRef<u8>(z + 0x20);
        return;
    }
    if (az <= zz && zz < F(b + 8)) {
        GRef<u8>(state) = GRef<u8>(z + 0x20);
        zz = F(z + 8);
        az = F(a + 8);
    }
    if (az < zz) return;
    if (F(b + 8) < zz) GRef<u8>(state) = 0;
}

// EE: 0x0011F2F8
// Mission 0xF (the canyon): zone state +0x183 of the player from the 6 zones at 0x002A23A0;
// leaving zone 3 sets status 0x40000 (out of the canyon), entering it fades in.
void func_0011F2F8(s32 p) {
    const u32 u = Unit(p);
    const u32 P = W(u + 0x44);
    const u8 before = GRef<u8>(P + 0x183);
    guest::GuestFrame f(0x10);
    sceVu0AddVector(f.Address(), u, u + 0x20);
    for (u32 z = 0x002A23A0; z < 0x002A24C0; z += 0x30) func_0011F0F0(z, P + 0x183, u, f.Address());
    if (before == 3 && GRef<u8>(P + 0x183) == 0) GRef<u32>(Status() + 0x20) |= 0x40000u;
    if (before == 0 && GRef<u8>(P + 0x183) == 3) func_0021F190(10.0f, 0, 0, 0, 2);
}

}  // namespace ac04::game
