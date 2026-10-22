// HDD install page (scene 4 subs 0x3E-0x40, page 23; slice S8; docs/subsystems/save.md "HDD"):
// installs DATA.CDP to the HDD (or removes the install). Unreachable in this build: the page needs the
// HDD module, whose init has no caller (dead-code.md).
//
// Page object (0x6A0 bytes, *0x0041184C): +0 bucket list, +0x10 data load queue, +0x220 DATA 0xBA
// (font block), +0x230 fade (+0x23C level), +0x240 menu tree, +0x648 message box (+0x650 type-out
// position, +0x656 shown message), +0x66C state, +0x670 state after an error message, +0x674/+0x676
// u16 MenuGoTo target (1, 4), +0x678 action (0 install, 1 uninstall), +0x67C frame counter, +0x680
// install step, +0x684 result (1 no space, 2 space message shown, 3 error), +0x688 s16 cancel state,
// +0x68A u16 uninstall result (1 done, 2 failed), +0x68C transfer buffer (10 MiB), +0x690 f32 gauge
// level (negative: hidden).
#include "game/game.h"

#include <cstddef>

#include "common/ee_math.h"
#include "common/guest_stack.h"
#include "sce/sce.h"

namespace ac04::game {

using guest::GRef;

namespace {

AC04_GUEST_VAR(u32, g_HddInstallPage, 0x0041184C)  // gp-0x41A4
AC04_GUEST_VAR(u32, g_pMenuFade, 0x00410E88)

s32& State(u32 obj) { return GRef<s32>(obj + 0x66C); }
bool BoxDone(u32 obj) { return (GRef<u32>(obj + 0x648) & 2) != 0; }

}  // namespace

// EE: 0x00245B88
// Help line by state (and the selection, action and result); hidden (type-out -1) when there is none.
void func_00245B88(u32 obj) {
    const s16 sel = GetMenuTreeSelection(obj + 0x240);
    const s32 action = GRef<s32>(obj + 0x678);
    s32 msg = -1;
    switch (State(obj)) {
        case 1: msg = sel == 0 ? 8 : 0xC; break;
        case 2: msg = sel == 0 ? 0x20 : 0x23; break;
        case 3: msg = action == 0 ? 0xA : 0xE; break;
        case 4: msg = action == 0 ? 0x15 : -1; break;
        case 5: msg = action == 0 ? 0x19 : 0x1C; break;
        case 6: msg = action == 0 ? 0x17 : -1; break;
        case 7: {
            const s32 result = GRef<s32>(obj + 0x684);
            if (result == 2 || result == 1)
                msg = action == 0 ? (result == 2 ? 0x13 : 0x10) : -1;
            else if (result == 3)
                msg = 0x2B;
            break;
        }
        case 8:
            if (GRef<s32>(obj + 0x684) == 3) msg = action == 0 ? 0x1A : 0x1D;
            break;
        case 9:
        case 10: msg = 0x29; break;
        case 11: msg = 0x2D; break;
        case 12:
        case 13: msg = 0x26; break;
        default: break;
    }
    if (msg < 0) {
        GRef<f32>(obj + 0x650) = -1.0f;
        return;
    }
    if (GRef<s16>(obj + 0x656) != msg) {
        SetMessageBoxText(obj + 0x648, msg);
        GRef<f32>(obj + 0x650) = K(0x004103E8);
    }
    UpdateMessageBox(K(0x004103EC), obj + 0x648, static_cast<s32>(Ctx()->frame_count));
}

// EE: 0x00245D40
// Creates the page object (work heap, 0x6A0 bytes) and the 10 MiB transfer buffer (alignment 0x40):
// menu fade requests, fade, the one-column menu tree 0x003AF9C0, target MenuGoTo(1, 4), gauge hidden.
u32 func_00245D40() {
    const u32 obj = WorkHeapAlloc(0x6A0, 0x10);
    const u32 buf = WorkHeapAlloc(0xA00000, 0x40);
    const u32 fade = g_pMenuFade();
    State(obj) = 0;
    GRef<u32>(obj + 0x68C) = buf;
    GRef<u16>(fade + 0x30) = 1;
    GRef<u16>(fade + 0x32) = 0;
    InitFade(obj + 0x230, 1);
    GRef<u16>(obj + 0x674) = 1;
    GRef<u16>(obj + 0x676) = 4;
    guest::GuestFrame frame(0x10);
    const u32 masks = frame.Address();
    GRef<u32>(masks) = 0;
    InitMenuTree(obj + 0x240, 1, 0x003AF9C0, 0, masks, 0);
    GRef<u16>(obj + 0x24E) = 0;
    GRef<f32>(obj + 0x690) = -1.0f;
    GRef<u32>(obj + 0x680) = 0;
    GRef<u16>(obj + 0x688) = 0;
    GRef<u16>(obj + 0x68A) = 0;
    return obj;
}

// EE: 0x00245DF8
// Uninstall: runs the HDD remove operation (0x00259F08) once (+0x68A bit 0 done, bit 1 failed);
// when the message is shown: done -> menu BGM 4, back to state +0x670, ctx flag 0x100000 cleared;
// failed -> menu BGM 4, state 8 with result 3.
void func_00245DF8(u32 obj) {
    const u32 ctx = func_002597B8();
    if ((GRef<u16>(obj + 0x68A) & 3) == 0) {
        const s32 r = func_00259F08(ctx);
        if (r == 1)
            GRef<u16>(obj + 0x68A) |= 1;
        else if (r == -1)
            GRef<u16>(obj + 0x68A) |= 2;
    }
    if (!BoxDone(obj)) return;
    const u16 done = GRef<u16>(obj + 0x68A);
    if ((done & 1) != 0) {
        PlayMenuBgm(4);
        State(obj) = GRef<s32>(obj + 0x670);
        Ctx()->flags &= 0xFFEFFFFFu;
    } else if ((done & 2) != 0) {
        PlayMenuBgm(4);
        State(obj) = 8;
        GRef<s32>(obj + 0x684) = 3;
    }
}

// EE: 0x00245ED8
// Install: step 2 copies the disc (HddInstallDataCdp with the 10 MiB buffer; Triangle pauses it,
// state 6), step 3 creates the files (0x0025AB18; done: ctx flag 0x100000, state 5). Errors: state
// 0xC with +0x670 = 8, +0x684 = 3. A cancel returns -2: +0x688 = 2.
void func_00245ED8(u32 obj, u32 pressed) {
    const u32 ctx = func_002597B8();
    const s32 step = GRef<s32>(obj + 0x680);
    if (step == 0 || step == 2) {
        if (step == 0) {
            GRef<u32>(obj + 0x680) = 2;
            GRef<u32>(obj + 0x684) = 0;
        }
        const s32 r = HddInstallDataCdp(ctx, GRef<u32>(obj + 0x68C), 0xA00000);
        if (r == -1) {
            GRef<u32>(obj + 0x684) = 3;
            GRef<u32>(obj + 0x670) = 8;
            State(obj) = 0xC;
        } else if (r == 1) {
            GRef<u32>(obj + 0x680) = 3;
        } else if (r == -2) {
            GRef<u16>(obj + 0x688) = 2;
        } else if (BoxDone(obj) && (pressed & 0x20) != 0) {
            PlayMenuSe(0, 3);
            func_0025A058(1);
            State(obj) = 6;
        }
    } else if (step == 3) {
        const s32 r = func_0025AB18(ctx);
        if (r == 1) {
            Ctx()->flags |= 0x100000;
            State(obj) = 5;
        } else if (r == -1) {
            GRef<u32>(obj + 0x670) = 8;
            GRef<s32>(obj + 0x684) = step;
            State(obj) = 0xC;
        }
    }
}

// EE: 0x00246048
// X on the menu: item 0 installs (only when not installed, ctx flag 0x100000: state 0xE), item 1
// removes (only when installed: state 3); otherwise SE 5 and the "not possible" message (state 2).
void func_00246048(u32 obj, u32 tree) {
    const s16 sel = GetMenuTreeSelection(tree);
    if (sel == 0) {
        if ((Ctx()->flags & 0x100000) == 0) {
            GRef<u32>(obj + 0x678) = 0;
            State(obj) = 0xE;
        } else {
            PlayMenuSe(0, 5);
            State(obj) = 2;
            return;
        }
    } else if (sel == 1) {
        if ((Ctx()->flags & 0x100000) != 0) {
            GRef<u32>(obj + 0x678) = static_cast<u32>(sel);
            State(obj) = 3;
        } else {
            PlayMenuSe(0, 5);
            State(obj) = 2;
            return;
        }
    }
    PlayMenuSe(0, 2);
}

// EE: 0x00246100
// Page state machine (states 0-0x10, jump table 0x00409BC0): fade in, menu, messages, the space check
// (needs more than 0x1FFFFF free; an HDD error 0x10005 opens scene 7), install with the progress
// gauge (copied bytes / 0x2B031000, at most K 0x004103F8), pause and cancel, uninstall; Triangle in
// the menu leaves (fade over K 0x004103FC, then MenuGoTo(+0x674, +0x676)).
void func_00246100(u32 obj) {
    const u32 fade = g_pMenuFade();
    GRef<u16>(fade + 0x32) = 0;
    const u32 pressed = Ctx()->pad_buttons[0][1];
    const u32 repeat = Ctx()->pad_buttons[0][2];
    func_00245B88(obj);
    if (State(obj) != 0) UpdateMenuTree(obj + 0x240, State(obj) == 1 ? repeat : 0);
    const auto leave = [&] {
        StartFade(K(0x004103FC), obj + 0x230, 1);
        State(obj) = 0x10;
    };
    switch (State(obj)) {
        case 0:
            if (K(0x004103F0) < GRef<f32>(fade + 0xC)) State(obj) = 1;
            break;
        case 1:
            GRef<u16>(fade + 0x32) |= 0x33;
            GRef<f32>(obj + 0x690) = -1.0f;
            if ((pressed & 0x20) != 0) {
                PlayMenuSe(0, 3);
                leave();
            } else if ((pressed & 0x50) != 0) {
                if (GetHddSaveMode(func_002597B8()) == 1) {
                    State(obj) = 0xB;
                    PlayMenuSe(0, 5);
                } else {
                    func_00246048(obj, obj + 0x240);
                }
            }
            break;
        case 2:
            GRef<u16>(fade + 0x32) = 0x30;
            if ((pressed & 0x70) == 0) break;
            PlayMenuSe(0, (pressed & 0x50) != 0 ? 2 : 3);
            State(obj) = 1;
            break;
        case 3:
            GRef<u16>(fade + 0x32) = 0x30;
            if (!BoxDone(obj)) break;
            if ((pressed & 0x20) != 0) {
                PlayMenuSe(0, 3);
                State(obj) = 1;
            } else if ((pressed & 0x50) != 0) {
                PlayMenuSe(0, 2);
                GRef<u32>(obj + 0x680) = 0;
                if (GRef<u32>(obj + 0x678) == 0) FadeOutMenuBgm(0.5f);
                GRef<u32>(obj + 0x67C) = 0;
                State(obj) = 4;
            }
            break;
        case 4: {
            GRef<u32>(obj + 0x67C) += 1;
            const u32 action = GRef<u32>(obj + 0x678);
            if (action == 0) {
                GRef<u16>(fade + 0x32) = 0x20;
                func_00245ED8(obj, pressed);
                f32 level = static_cast<f32>(static_cast<s32>(func_0025A048())) / K(0x004103F4);
                if (K(0x004103F8) < level) level = K(0x004103F8);
                GRef<f32>(obj + 0x690) = level;
            } else if (action == 1) {
                GRef<u16>(fade + 0x32) = 0;
                State(obj) = 0xC;
                GRef<u32>(obj + 0x670) = 5;
                GRef<u32>(obj + 0x684) = 0;
                GRef<u16>(obj + 0x68A) = 0;
            }
            break;
        }
        case 5:
            GRef<u16>(fade + 0x32) = 0x10;
            GRef<f32>(obj + 0x690) = GRef<u32>(obj + 0x678) != 0 ? -1.0f : 1.0f;
            if ((pressed & 0x50) == 0) break;
            PlayMenuSe(0, 2);
            PlayMenuBgm(4);
            leave();
            break;
        case 6:
            GRef<u16>(fade + 0x32) = 0x30;
            if (!BoxDone(obj)) break;
            if ((pressed & 0x50) != 0) {
                PlayMenuSe(0, 2);
                func_0025A068();
                GRef<f32>(obj + 0x690) = -1.0f;
                State(obj) = 0xD;
            } else if ((pressed & 0x20) != 0) {
                PlayMenuSe(0, 3);
                func_0025A058(0);
                GRef<u16>(obj + 0x688) = 0;
                State(obj) = 4;
            }
            break;
        case 7: {
            GRef<u16>(fade + 0x32) = 0x10;
            const s32 result = GRef<s32>(obj + 0x684);
            if (result < 3 && 0 < result) {
                if ((pressed & 0x50) == 0) break;
                PlayMenuSe(0, 2);
                if (GRef<s32>(obj + 0x684) == 1)
                    GRef<s32>(obj + 0x684) = 2;
                else
                    State(obj) = 9;
                break;
            }
            if (func_002597C8() == 0x10005) {
                EnterHddErrorScene();
                break;
            }
            if ((pressed & 0x50) == 0) break;
            PlayMenuSe(0, 2);
            State(obj) = 9;
            break;
        }
        case 8:
            GRef<u16>(fade + 0x32) = 0x30;
            if (GRef<s32>(obj + 0x684) == 3 && func_002597C8() == 0x10005) EnterHddErrorScene();
            if (!BoxDone(obj)) break;
            if ((pressed & 0x50) != 0) {
                PlayMenuSe(0, 2);
                PlayMenuBgm(4);
                State(obj) = 1;
            } else if ((pressed & 0x20) != 0) {
                PlayMenuSe(0, 3);
                PlayMenuBgm(4);
                State(obj) = 3;
            }
            break;
        case 9:
        case 10:
            GRef<u16>(fade + 0x32) = 0x30;
            if (!BoxDone(obj)) break;
            if ((pressed & 0x50) != 0) {
                PlayMenuSe(0, 2);
                PlayMenuBgm(4);
                State(obj) = 1;
            } else if ((pressed & 0x20) != 0) {
                PlayMenuSe(0, 3);
                PlayMenuBgm(4);
                State(obj) = 0xE;
            }
            break;
        case 11:
            GRef<u16>(fade + 0x32) = 0x30;
            if (!BoxDone(obj) || (pressed & 0x70) == 0) break;
            PlayMenuSe(0, (pressed & 0x50) != 0 ? 2 : 3);
            if (GRef<u32>(obj + 0x678) == 0) {
                State(obj) = 9;
            } else {
                PlayMenuBgm(4);
                State(obj) = 1;
            }
            break;
        case 12:
            GRef<u16>(fade + 0x32) = 0;
            func_00245DF8(obj);
            break;
        case 13:
            GRef<u16>(fade + 0x32) = 0;
            if (GRef<s16>(obj + 0x688) == 2) {
                GRef<u32>(obj + 0x670) = 1;
                State(obj) = 0xC;
                GRef<u16>(obj + 0x688) = 0;
                GRef<u16>(obj + 0x68A) = 0;
            } else {
                func_00245ED8(obj, 0);
            }
            break;
        case 14: {
            const u32 ctx = func_002597B8();
            GRef<u16>(fade + 0x32) = 0;
            if (GetHddSaveMode(ctx) == 1) {
                State(obj) = 0xB;
                break;
            }
            const s32 r = func_00259D10(ctx);
            if (r == 1) {
                if (0x1FFFFF < func_00259C78(ctx)) {
                    State(obj) = 3;
                } else {
                    GRef<s32>(obj + 0x684) = r;
                    State(obj) = 7;
                }
            } else if (r == -1) {
                State(obj) = 7;
                GRef<u32>(obj + 0x684) = 3;
            }
            break;
        }
        case 15:
        case 16:
            if ((GRef<u32>(obj + 0x230) & 8) != 0)
                MenuGoTo(static_cast<s64>(GRef<u16>(obj + 0x674)), GRef<u16>(obj + 0x676));
            break;
        default:
            break;
    }
}

// EE: 0x00246768
// Progress gauge (level 0-1, hidden below 0) and its "nnn%" text, into bucket `index` of `list`.
void func_00246768(f32 level, u32 list, s32 index) {
    if (level < 0.0f) return;
    if (1.0f < level) level = 1.0f;
    const u32 start = Ctx()->cur_buf.addr;
    const u32 p = BuildMenuDrawEnv(start + 0x10, 0x280, 0x1C0);
    GRef<u64>(p + 8) = 0xE;
    GRef<u64>(p + 0x10) = 0x8000000068ull;
    GRef<u64>(p + 0x18) = 0x43;
    GRef<u64>(p) = 0x1000000000008002ull;
    GRef<u64>(p + 0x28) = 0x19;
    GRef<u64>(p + 0x20) = 0;
    const u32 end = BuildGauge(level, p + 0x30, 0x480, 0xA40, 0x1000);
    LinkPacket(list, index, start, CloseDmaPacket(start, end));
    BeginMenuTextLayer(list, index);
    guest::GuestFrame frame(0x100);
    const u32 buf = frame.Address();
    const u64 args[1] = {static_cast<u64>(static_cast<s64>(fpu::FloatToInt(level * 100.0f)))};
    ee::sprintf(buf, 0x004114D0, args, 1);  // "%03d%%"
    DrawMenuText(20.0f, 0x480, 0xB40, 3, buf);
    EndMenuTextLayer();
}

// EE: 0x002468B0
// Draws the page: menu tree, gauge, help line, black fade, menu fade.
void func_002468B0(u32 obj) {
    InitBucketList(obj, 1);
    SetPacketListBuckets(g_pMainContext().addr + offsetof(MainContext, vif1_list), obj);
    DrawMenuTree(obj, 0, obj + 0x240);
    if (0.0f <= GRef<f32>(obj + 0x690)) func_00246768(GRef<f32>(obj + 0x690), obj, 0);
    UploadMessageBoxGlyphs(obj, 0, obj + 0x648);
    DrawMessageBox(obj, 0, obj + 0x648);
    DrawBlackFade(GRef<f32>(obj + 0x23C), obj, 0);
    DrawMenuFade(obj, 0, g_pMenuFade());
}

// EE: 0x00246960
// Sub 0x3E: menu draw environment, the page created, DATA 0xBA queued, menu BGM 4; sub 0x3F.
void EnterHddInstallPage() {
    PopWorkHeapMark();
    PushWorkHeapMark();
    Ctx()->draw_mode = 2;
    Ctx()->drawenv_pkt[4] = 0x1310000DCull;
    Ctx()->drawenv_pkt[20] = 0x1310000DCull;
    const u32 obj = func_00245D40();
    g_HddInstallPage() = obj;
    ResetMenuDataLoad(obj + 0x10);
    QueueMenuDataLoad(obj + 0x10, 0xBA, 1, obj + 0x220);
    DrawMenuFadeFrame(obj, g_pMenuFade());
    RequestSubState(0x3F);
    PlayMenuBgm(4);
}

// EE: 0x00246A08
// Sub 0x3F: when loaded, the font block and the help line; sub 0x40. The menu fade frame meanwhile.
void LoadHddInstallPage() {
    const u32 obj = g_HddInstallPage();
    if (UpdateMenuDataLoad(obj + 0x10) < 0) {
        LoadFontBlock(GRef<u32>(obj + 0x220));
        InitMessageBox(obj + 0x648);
        RequestSubState(0x40);
    }
    DrawMenuFadeFrame(obj, g_pMenuFade());
}

// EE: 0x00246A58
// Sub 0x40: fade, update, draw.
void HddInstallPageRun() {
    const u32 obj = g_HddInstallPage();
    UpdateFade(obj + 0x230);
    func_00246100(obj);
    func_002468B0(obj);
}

AC04_GUEST_FUNCTION(0x00246960, EnterHddInstallPage);
AC04_GUEST_FUNCTION(0x00246A08, LoadHddInstallPage);
AC04_GUEST_FUNCTION(0x00246A58, HddInstallPageRun);

}  // namespace ac04::game
