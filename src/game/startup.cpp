// Scene 6: startup (docs/subsystems/results-startup.md "Scene 6"). Startup warnings, the memory
// card probe and its messages, the sound bank load, the two splash images and the exit to the
// no-save intro (scene 2) or the title (scene 4).
//
// Scene work `w` (0x210 bytes, work heap, pointer at 0x00411714):
//   +0x000 DATA 333 entry, +0x010 2D camera (0x1C0 bytes), +0x1D0 bucket list (one bucket),
//   +0x1E0/+0x1E4 splash textures, +0x1E8 card message page, +0x1EC f32 fade (1 = black),
//   +0x1F0 flags, +0x1F4 frame counter, +0x1F8 sound bank buffer, +0x1FC sound load step,
//   +0x1FD splash image index, +0x1FE card evaluation result, +0x200 warning step.
// Flags +0x1F0: 0x1/0x2/0x4 splash fade in/hold/fade out, 0x8 redraw the window during the probe,
// 0x10 evaluate the card result, 0x20 skip the card re-check, 0x40 card was missing, 0x80 card
// present, 0x100/0x200 probe done, 0x400/0x800/0x1000/0x2000 sound bank load steps and done,
// 0x4000 AC04 save directory present (title path), 0x8000 card error shown, 0x10000 save found,
// 0x100000..0x800000 startup warnings.
#include "game/game.h"

#include "common/debug_hooks.h"
#include "common/ee_math.h"
#include "common/guest_stack.h"

namespace ac04::game {

using guest::GRef;

namespace {

AC04_GUEST_VAR(u32, g_StartupWork, 0x00411714)   // gp-0x42DC
AC04_GUEST_VAR(u32, g_StartupWindow, 0x00411900) // gp-0x40F0

u32 W() { return g_StartupWork(); }
u32& Flags() { return GRef<u32>(W() + 0x1F0); }
s32& Counter() { return GRef<s32>(W() + 0x1F4); }
u8 WindowState() { return GRef<u8>(g_StartupWindow()); }
// Pad 1 newly pressed buttons (ctx+0x2B4).
u32 PadEdge() { return Ctx()->pad_buttons[0][1]; }

// Draws the message window (bucket list rebuilt for the frame).
void DrawWindow() {
    const u32 list = W() + 0x1D0;
    const u32 cam = W() + 0x10;
    InitBucketList(list, 1);
    SetPacketListBuckets(kMainContextAddress + offsetof(MainContext, vif1_list), list);
    UpdateMessageWindow(g_StartupWindow());
    DrawMessageWindow(g_StartupWindow(), cam, list);
}

}  // namespace

// EE: 0x0025FEF8
// Requests the sound bank load (only once per boot: ctx flag 0x400000 marks it done).
void RequestSoundBankLoad() {
    const u32 w = W();
    if ((Ctx()->flags & 0x400000) != 0) {
        GRef<u32>(w + 0x1F0) |= 0x2000;
        return;
    }
    GRef<u8>(w + 0x1FC) = 0;
    GRef<u32>(w + 0x1F8) = 0x004C35C0;
    GRef<u32>(w + 0x1F0) |= 0xC00;
}

// EE: 0x0025FF48
// Sound bank load steps: DATA 0x165 (bank headers), then BD entry 1 streamed into bank slot 10.
void UpdateSoundBankLoad() {
    const u32 flags = Flags();
    if ((flags & 0x400) == 0) return;
    if ((flags & 0x800) == 0) {
        if ((flags & 0x1000) == 0) return;
        // The original passes two words of a local array {10, 1} indexed by the step byte.
        const s32 args[2] = {10, 1};
        const u8 step = GRef<u8>(W() + 0x1FC);
        if (StreamBdEntryToIop(args[step], args[step + 1]) == 1) {
            const u8 next = static_cast<u8>(GRef<u8>(W() + 0x1FC) + 1);
            GRef<u8>(W() + 0x1FC) = next;
            if (next != 0) {
                GRef<u8>(W() + 0x1FC) = 0;
                Flags() = (Flags() & 0xFFFFEBFFu) | 0x2000;
                Ctx()->flags |= 0x400000;
            }
        }
    } else if (LoadSoundBankData(0x165, GRef<u32>(W() + 0x1F8)) == 1) {
        GRef<u8>(W() + 0x1FC) = 0;
        Flags() = (Flags() & 0xFFFFF7FFu) | 0x1000;
    }
}

// EE: 0x00260078
// Startup warnings (flags 0x100000..0x800000): shows the message, waits for a button, may show a
// follow-up page. Returns 1 when no warning is pending (or the last one was confirmed).
s32 UpdateStartupWarnings() {
    const u32 flags = Flags();
    if ((flags & 0xF00000) == 0) {
        Flags() = flags & 0xFFFDFFFFu;
        return 1;
    }
    const u8 step = GRef<u8>(W() + 0x200);
    const u32 win = g_StartupWindow();
    auto finish = [&]() {
        HideMessageWindow(win);
        Flags() &= 0xFFFDFFFFu;
        return 1;
    };
    if (step == 0) {
        s16 msg;
        if ((flags & 0x800000) != 0) msg = 7;
        else if ((flags & 0x100000) != 0) msg = 5;
        else if ((flags & 0x200000) != 0) msg = 6;
        else msg = 10;  // 0x400000
        ShowMessagePage(win, msg);
        GRef<u8>(W() + 0x200) = 1;
        return 0;
    }
    if (step == 1) {
        if ((PadEdge() & 0x70) == 0) return 0;
        if ((WindowState() & 1) != 0) {
            ShowMessagePage(win, 0xF);
            GRef<u8>(W() + 0x200) = 2;
        }
        return 0;
    }
    if (step == 2) {
        if ((PadEdge() & 0x50) != 0 && (WindowState() & 1) != 0) return finish();
        if ((PadEdge() & 0x20) == 0 || (WindowState() & 1) == 0) return 0;
        const u32 f = Flags();
        s16 msg = -1;
        if ((f & 0x800000) != 0) msg = 0xB;
        else if ((f & 0x100000) != 0) msg = 0xC;
        else if ((f & 0x200000) != 0) msg = 0xE;
        else if ((f & 0x400000) != 0) msg = 0xD;
        if (msg >= 0) ShowMessagePage(win, msg);
        GRef<u8>(W() + 0x200) = 3;
        return 0;
    }
    if (step == 3) {
        if ((PadEdge() & 0x70) == 0) return 0;
        if ((WindowState() & 1) == 0) return 0;
        return finish();
    }
    return 0;
}

// EE: 0x00260288
// Evaluates the probe result into the message to show (w+0x1FE): 0 none, 1 not enough space,
// 2 no card / unusable card, 0xC AC04 directory without save file. Sets 0x4000 when the AC04
// directory exists (or the HDD has the game installed).
void EvaluateCardResult() {
    const u32 save = func_0025F298();
    const u32 hdd = func_002597B8();
    const u32 w = W();
    if (GRef<s32>(save + 0x4D8) == 0 && GRef<u8>(hdd + 0x216) != 1) {
        Flags() &= 0xFFFFBFFFu;
    } else {
        Flags() |= 0x4000;
    }
    GRef<u8>(w + 0x1FE) = 0;
    if ((Flags() & 0xF00000) == 0 && (Ctx()->flags & 0x80000) != 0) return;
    if (GRef<s32>(save + 0x4D8) == -1) GRef<u8>(W() + 0x1FE) = 0xC;
    if (GRef<s32>(save + 0x4C8) == 2) {
        if (GRef<s32>(save + 0x4D4) == -11) {
            GRef<u8>(W() + 0x1FE) = 2;
            Flags() |= 0x40;
        } else if (GRef<s32>(save + 0x4D4) == -2 || (Flags() & 0x10000) != 0) {
            GRef<u8>(W() + 0x1FE) = 0;
        } else if (GRef<s32>(save + 0x4CC) < GetSaveSizeKb()) {
            GRef<u8>(W() + 0x1FE) = 1;
        }
    } else {
        GRef<u8>(W() + 0x1FE) = 2;
        Flags() |= 0x8000;
    }
}

// EE: 0x002603D8
// HDD branch of the evaluation (HDD usable: message 8), else starts the window fade-out.
void func_002603D8() {
    const u32 hdd = func_002597B8();
    if ((GRef<u8>(hdd + 0x10) & 0x70) != 0) {
        if (GetHddState(hdd) == 1) {
            GRef<u8>(W() + 0x1FE) = 7;
            GRef<u32>(W() + 0x1E8) = 8;
            ShowMessagePage(g_StartupWindow(), 8);
            return;
        }
        if (GRef<u8>(W() + 0x1FE) == 0) return;
    }
    const u32 w = W();
    const u32 flags = GRef<u32>(w + 0x1F0);
    if ((flags & 4) == 0) {
        GRef<u8>(w + 0x1FE) = 9;
        GRef<s32>(w + 0x1F4) = 0;
        GRef<u32>(w + 0x1F0) = (flags & 0xFFFFFFFEu) | 4;
    }
}

// EE: 0x00260488
// Sub 7: card messages. Re-checks the card, shows the message chosen by EvaluateCardResult, handles
// confirm (pad 0x50) / cancel (0x20), and fades the window out (2 frames) before sub 8.
void UpdateCardMessages() {
    s64 probe = 0;
    if ((Flags() & 0x10) != 0) {
        EvaluateCardResult();
        if (GRef<u8>(W() + 0x1FE) == 0 && GetHddState(func_002597B8()) == 0) {
            RequestSubState(8);
            return;
        }
    }
    const u32 save = func_0025F298();
    const u32 win = g_StartupWindow();
    if ((Flags() & 0x20) == 0) {
        probe = CheckMcCard(save, 0);
        if (probe != 0 && (Flags() & 0x40) == 0 && GRef<s32>(save + 0x4C8) == 0) {
            Flags() |= 0x40;
            GRef<u8>(W() + 0x1FE) = 2;
            const u32 f = Flags();
            GRef<u32>(W() + 0x1E8) = 1;
            Flags() = f & 0xFFFEFFFFu;
            if ((f & 0x8000) == 0) {
                ShowMessagePage(win, 1);
            } else {
                Flags() = f & 0xFFFE7FFFu;
            }
        }
    }
    if ((Flags() & 0x10) != 0) {
        EvaluateCardResult();
        static const s8 kPage[13] = {-1, 2, 1, 7, 5, -1, -1, -1, -1, -1, 6, 10, 0x10};
        const u8 result = GRef<u8>(W() + 0x1FE);
        if (result == 0) {
            func_002603D8();
        } else if (result < 13 && kPage[result] >= 0) {
            GRef<u32>(W() + 0x1E8) = static_cast<u32>(kPage[result]);
        }
        if (GRef<u8>(W() + 0x1FE) != 0) ShowMessagePage(win, static_cast<s16>(GRef<u32>(W() + 0x1E8)));
        Flags() &= 0xFFFFFFEFu;
    }
    if ((Flags() & 0x20) == 0 && probe != 0 && (Flags() & 0x40) != 0) {
        if (GRef<s32>(save + 0x4C8) != 0) {
            // A card was inserted while the "no card" message was shown: probe again.
            GRef<u32>(W() + 0x1E8) = 0;
            Flags() &= 0xFFFFFFBFu;
            ShowMessagePage(win, 0);
            Flags() |= 8;
            RequestSubState(6);
        }
    }
    if ((PadEdge() & 0x50) != 0 && (WindowState() & 1) != 0) {
        switch (GRef<u8>(W() + 0x1FE)) {
            case 1: case 2: case 3: case 4: case 10: case 11: case 12:
                GRef<u8>(W() + 0x1FE) = 6;
                GRef<u32>(W() + 0x1E8) = 3;
                Flags() &= 0xFFFF7FFFu;
                ShowMessagePage(win, 3);
                break;
            case 6:
                func_002603D8();
                break;
            case 7:
                GRef<u8>(W() + 0x1FE) = 8;
                GRef<u32>(W() + 0x1E8) = 9;
                ShowMessagePage(win, 9);
                break;
            case 8:
                if ((Flags() & 4) == 0) {
                    Counter() = 0;
                    Flags() = (Flags() & 0xFFFFFFFEu) | 4;
                }
                break;
            default:
                break;
        }
    }
    if ((PadEdge() & 0x20) != 0 && (WindowState() & 1) != 0) {
        bool show = true;
        s16 page = 0;
        switch (GRef<u8>(W() + 0x1FE)) {
            case 1: case 2: case 3: case 4: case 10: case 11: case 12:
                GRef<u8>(W() + 0x1FE) = 6;
                GRef<u32>(W() + 0x1E8) = 3;
                Flags() &= 0xFFFF7FFFu;
                page = 3;
                break;
            case 5:
                page = static_cast<s16>(GRef<u32>(W() + 0x1E8));
                Flags() |= 0x10;
                break;
            case 6:
                GRef<u8>(W() + 0x1FE) = 5;
                GRef<u32>(W() + 0x1E8) = 4;
                page = 4;
                break;
            default:
                show = false;
                break;
        }
        if (show) ShowMessagePage(win, page);
    }
    const u32 flags = Flags();
    if ((flags & 4) != 0) {
        const s32 i = Counter();
        if (i < 2) {
            Counter() = i + 1;
            GRef<f32>(W() + 0x1EC) = static_cast<f32>(i) / 60.0f;
            HideMessageWindow(win);
        } else {
            GRef<f32>(W() + 0x1EC) = 1.0f;
            if (probe != 0) {
                Counter() = 0;
                Flags() = flags & 0xFFFFFFFBu;
                RequestSubState(8);
            }
        }
    }
}

// EE: 0x00260900
// Sub 7 drawing: the message window and the fade.
void DrawCardMessages() {
    const u32 list = W() + 0x1D0;
    const u32 cam = W() + 0x10;
    DrawWindow();
    DrawScreenFade(GRef<f32>(W() + 0x1EC), list, 0, 0, cam);
}

// EE: 0x00260980
// HYPOTHESIS: StartupSplashUpdate (scene 6 sub 9). Fade in (60 frames), hold (120 frames, image 1
// skippable with pad 0x50; waits for the sound banks, flag 0x2000), fade out (60 frames). After
// image 0: the no-save intro (scene 2) when no AC04 directory was found, else image 1; after image
// 1: the title (EnterMenu(0)).
void StartupSplashUpdate() {
    UpdateSoundBankLoad();
    const u32 w = W();
    const u32 flags = Flags();
    if ((flags & 1) != 0) {
        const s32 i = Counter();
        if (i < 0x3C) {
            Counter() = i + 1;
            GRef<f32>(w + 0x1EC) = fpu::Sub(1.0f, static_cast<f32>(i) / 60.0f);
        } else {
            Counter() = 0;
            GRef<f32>(w + 0x1EC) = 0.0f;
            Flags() = (flags & 0xFFFFFFFEu) | 2;
        }
        return;
    }
    if ((flags & 2) != 0) {
        if (GRef<u8>(w + 0x1FD) == 1 && (PadEdge() & 0x50) != 0) Counter() = 0x78;
        const s32 i = Counter();
        if (i < 0x78) {
            Counter() = i + 1;
        } else if ((Flags() & 0x2000) != 0) {
            Counter() = 0;
            Flags() = (Flags() & 0xFFFFFFFDu) | 4;
        }
        return;
    }
    if ((flags & 4) == 0) return;
    const s32 i = Counter();
    if (i < 0x3C) {
        Counter() = i + 1;
        GRef<f32>(w + 0x1EC) = static_cast<f32>(i) / 60.0f;
        return;
    }
    const u8 image = GRef<u8>(w + 0x1FD);
    GRef<f32>(w + 0x1EC) = 1.0f;
    Flags() = flags & 0xFFFFFFFBu;
    Counter() = 0;
    if (image == 0) {
        if ((flags & 0x4000) == 0) {
            const guest::ps2ptr<u8> game = g_GameState();
            game[0x25] = 1;
            game[0x27] = 0x23;
            game[0] = 0x11;
            game[1] = 0;
            game[3] = 0;
            game[0x2A] = 0;
            func_0022CC50(0);
            debug::OnGuestCheckpoint(0x00260B54);
            RequestScene(2);
            return;
        }
        GRef<u8>(w + 0x1FD) = 1;
        Flags() |= 1;
    } else if (image == 1) {
        func_0022CC50(1);
        debug::OnGuestCheckpoint(0x00260B78);
        EnterMenu(0);
    }
}

// EE: 0x00260B90
// Sub 9 drawing: the current splash image centred in the viewport (image 0 at GS centre
// 0x8B3/0x8A9, image 1 at 0x8D2/0x800, in pixels before the /2 of the viewport), then the fade.
void DrawSplashImage() {
    const u32 w = W();
    const u32 list = w + 0x1D0;
    const u32 cam = w + 0x10;
    InitBucketList(list, 1);
    SetPacketListBuckets(kMainContextAddress + offsetof(MainContext, vif1_list), list);
    const u32 start = BeginPacket();
    const u32 tex = GRef<u8>(W() + 0x1FD) == 0 ? GRef<u32>(W() + 0x1E0) : GRef<u32>(W() + 0x1E4);
    const u32 p = BuildTextureUpload(tex, start, 1, 0);
    auto put = [p](u32 i, u32 v) { GRef<u32>(p + 4 * i) = v; };
    put(1, 0);
    put(0, 0x10000002);
    put(2, 0);
    put(3, 0x50000002);
    put(4, 1);
    put(5, 0x10000000);
    put(6, 0xE);
    put(8, 0x30003);  // TEST_1: ZTE, ZTST ALWAYS
    put(10, 0x47);
    put(0xC, 0x10000004);
    put(0xD, 0);
    put(0xE, 0);
    put(0xF, 0x50000004);
    put(0x10, 0x8001);
    put(0x11, 0x64000000);  // REGLIST, NREG 6: PRIM RGBAQ UV XYZF2 UV XYZF2
    put(0x12, 0x00434310);
    GRef<u64>(p + 0x50) = 0x116;  // sprite, textured, alpha blend
    put(0x16, 0x80808080);
    GRef<f32>(p + 0x5C) = 1.0f;
    const u32 img = GRef<u32>(tex + 4);
    const s32 hw = GRef<s16>(w + 0x19C) / 2;
    const s32 hh = GRef<s16>(w + 0x19E) / 2;
    const u32 iw = GRef<u16>(img + 0xC);
    const u32 ih = GRef<u16>(img + 0xE);
    const s32 cx = GRef<u8>(W() + 0x1FD) == 0 ? 0x8B3 : 0x8D2;
    const s32 cy = GRef<u8>(W() + 0x1FD) == 0 ? 0x8A9 : 0x800;
    put(0x18, 0x80008);
    put(0x1A, static_cast<u32>((cx - hw) * 0x10) | static_cast<u32>(cy - hh) * 0x100000);
    put(0x1B, 0);
    put(0x1C, (iw * 0x10 + 8) | (ih * 0x10 + 8) * 0x10000);
    put(0x1E, static_cast<u32>((static_cast<s32>(iw) + cx - hw) * 0x10) |
                  static_cast<u32>(static_cast<s32>(ih) + cy - hh) * 0x100000);
    put(0x1F, 0);
    EndPacket(list, 0, p + 0x80);
    DrawScreenFade(GRef<f32>(W() + 0x1EC), list, 0, 0, cam);
}

// EE: 0x00260E80
// Sub 0: 640x448 frame mode (PSMZ16S depth), work and window allocation, packet buffers (2 x 4 MiB),
// VAG fade, SE reset.
void InitStartupScene() {
    MainContext* ctx = Ctx();
    ctx->frame_psm = 0;
    ctx->draw_h = 0x1C0;
    ctx->disp_w = 0x280;
    ctx->draw_w = 0x280;
    ctx->disp_h = 0xE0;
    for (u8& c : ctx->clear_rgba) c = 0;
    ctx->zbuf_psm = 0x3A;
    Ctx()->ztest = 3;
    Ctx()->draw_mode = 0;
    Ctx()->disp_flags |= 0x8000;
    BuildDrawEnvPackets();
    g_StartupWork() = WorkHeapAlloc(0x210, 0x10);
    GRef<u32>(W() + 0x1E0) = WorkHeapAlloc(0x28, 0x10);
    GRef<u32>(W() + 0x1E4) = WorkHeapAlloc(0x28, 0x10);
    const u32 win = WorkHeapAlloc(0x58, 0x10);
    g_StartupWindow() = win;
    InitMessageWindow(win, 1);
    GRef<u32>(W() + 0x1F0) = 0;
    AllocDoubleBuffer(kMainContextAddress + offsetof(MainContext, dbuf), 0x40000);
    const u32 buf = SwapDoubleBuffer(kMainContextAddress + offsetof(MainContext, dbuf));
    Ctx()->cur_buf.addr = buf;
    func_0021FB70(0.0f);
    StopAllVoices();
    func_0021F190(0.0f, 0, 0xFF, 0xFF, 1);
    RequestSubState(1);
    Ctx()->flags |= 0x200000;
}

// EE: 0x00260FC0
// Sub 1: loads DATA 333 (splash images and message window resources).
void LoadStartupData() {
    GRef<u32>(W()) = AllocAndLoadDataEntry(0x14D);
    RequestSubState(2);
}

// EE: 0x00260FE8
// Sub 2: when DATA 333 is loaded: items 0/1/2 = font, text, window textures; item 3 = the splash
// textures (TBP 0x2500, CBP 0x3800).
void SetupStartupData() {
    if (IsDataLoadDone() == 0) return;
    guest::GuestFrame reader(0x10);
    const u32 r = reader.Address();
    InitContainerReader(r, GRef<u32>(W()));
    const u32 font = GetContainerItem(r, 0);
    const u32 text = GetContainerItem(r, 1);
    const u32 splash = GetContainerItem(r, 3);
    const u32 gfx = GetContainerItem(r, 2);
    InitContainerReader(r, splash);
    const u32 tex0 = GRef<u32>(W() + 0x1E0);
    InitGimTexture(tex0, GetContainerItem(r, 0));
    SetTextureVramAddress(GRef<u32>(W() + 0x1E0), 0x2500, 0x3800);
    const u32 tex1 = GRef<u32>(W() + 0x1E4);
    InitGimTexture(tex1, GetContainerItem(r, 1));
    SetTextureVramAddress(GRef<u32>(W() + 0x1E4), 0x2500, 0x3800);
    LoadMessageWindow(g_StartupWindow(), font, text, gfx);
    RequestSubState(3);
}

// EE: 0x00261100
// Sub 3: 2D camera.
void InitStartupCamera() {
    Init2dCamera(W() + 0x10);
    RequestSubState(4);
}

// EE: 0x00261128
// Sub 4: startup warnings.
void ShowStartupWarnings() {
    if (UpdateStartupWarnings() == 1) RequestSubState(5);
    DrawWindow();
}

// EE: 0x002611A0
// Sub 5: prepares the card probe (counter preset to 120: no extra wait).
void PrepareCardProbe() {
    const u32 w = W();
    GRef<u8>(w + 0x1FE) = 0;
    GRef<u32>(w + 0x1E8) = 0;
    GRef<s32>(w + 0x1F4) = 0x78;
    GRef<u32>(w + 0x1F0) &= 0xFFFFFEFFu;
    RequestSubState(6);
}

// EE: 0x002611D8
// Sub 6: memory card probe. First CheckMcCard (card present?), then the save directory check
// (CheckMcSaveDir); when done (0x200) and the counter has reached 120, sub 7.
void ProbeStartupCard() {
    if ((Flags() & 8) != 0) DrawWindow();
    const u32 save = func_0025F298();
    s64 result;
    u32 flags;
    if ((Flags() & 0x80) == 0) {
        result = CheckMcCard(save, 0);
        flags = Flags();
        if ((flags & 0x80) == 0 && result == 1 && (flags & 0x100) == 0) Flags() = flags | 0x80;
        if (result == -1) Flags() |= 0x200;
        flags = Flags();
    } else {
        result = CheckMcSaveDir(save, 0);
        if (result != 0) {
            const u32 f = Flags() & 0xFFFFFF7Fu;
            Flags() = f | 0x300;
            if (result == 1 && GRef<s32>(save + 0x4D8) > 0) Flags() = f | 0x10300;
        }
        flags = Flags();
    }
    s32 counter = Counter();
    if ((flags & 0x200) != 0 && counter >= 0x78 && result != 0) {
        Flags() = (flags & 0xFFFFFCFFu) | 0x10;
        Counter() = 0;
        RequestSubState(7);
        return;
    }
    Counter() = counter + 1;
}

// EE: 0x00261378
// Sub 7.
void RunStartupCardMessages() {
    debug::OnGuestCheckpoint(0x00261378);
    UpdateCardMessages();
    DrawCardMessages();
}

// EE: 0x00261398
// Sub 8: starts splash image 0 and the sound bank load.
void StartStartupSplash() {
    const u32 w = W();
    GRef<u8>(w + 0x1FD) = 0;
    GRef<s32>(w + 0x1F4) = 0;
    GRef<u32>(w + 0x1F0) |= 1;
    RequestSoundBankLoad();
    RequestSubState(9);
}

// EE: 0x002613D0
// Sub 9.
void RunStartupSplash() {
    debug::OnGuestCheckpoint(0x002613D0);
    StartupSplashUpdate();
    DrawSplashImage();
}

// EE: 0x002613F0
// HYPOTHESIS: SceneStartup (scene 6). Dispatch through the sub-state table 0x003F8708.
void SceneStartup() { guest::CallGuest(GRef<u32>(0x003F8708 + 4u * Ctx()->sub)); }

AC04_GUEST_FUNCTION(0x002613F0, SceneStartup);
AC04_GUEST_FUNCTION(0x00260E80, InitStartupScene);
AC04_GUEST_FUNCTION(0x00260FC0, LoadStartupData);
AC04_GUEST_FUNCTION(0x00260FE8, SetupStartupData);
AC04_GUEST_FUNCTION(0x00261100, InitStartupCamera);
AC04_GUEST_FUNCTION(0x00261128, ShowStartupWarnings);
AC04_GUEST_FUNCTION(0x002611A0, PrepareCardProbe);
AC04_GUEST_FUNCTION(0x002611D8, ProbeStartupCard);
AC04_GUEST_FUNCTION(0x00261378, RunStartupCardMessages);
AC04_GUEST_FUNCTION(0x00261398, StartStartupSplash);
AC04_GUEST_FUNCTION(0x002613D0, RunStartupSplash);

}  // namespace ac04::game
