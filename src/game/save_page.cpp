// Save / load page (scene 4 sub-pages 0x4A-0x59; docs/subsystems/frontend.md "Save / load",
// docs/subsystems/save.md, docs/formats/SAVE.md). Sub-pages 0x4A-0x4E save (mode 0), 0x4F/0x50 load
// (mode 1), 0x51-0x55 and 0x56/0x57 are the HDD variants (modes 2/3, unreachable: the HDD module is
// never initialised). Sub 0x58 waits for DATA 0xB3, sub 0x59 runs the page.
//
// Page object (0x6150 bytes, work heap, *0x00411830): +0x10 data load queue, +0x220 DATA 0xB3 items
// {font block, icon.sys image, icon1.ico}, +0x240 fade object, +0x24C fade level, +0x250 save file
// image (0x4B10: +8 slot mask, +0xC checksum, +0x10 four campaign blocks of 0x12C0), +0x4D60 copy of
// the campaign block before a save, +0x6020 crosshair, +0x605C message box (help line), +0x6080
// panel (6 frame outlines, table 0x003AF230), +0x60CC/+0x60D0/+0x60D4 panel timers a/b/c (text
// type-out), +0x60E4 slot summaries (4 x 0x10: s32 mission (-1 empty), u32 play time, u32 credits,
// u16 difficulty, u16 cleared), +0x6124 mode, +0x6128 state, +0x612C/+0x612E MenuGoTo target on
// cancel, +0x6130/+0x6132 target when done, +0x6134 slot, +0x6138 save after a mission (1, 2 done),
// +0x613C slot mask before an overwrite, +0x6140 status shown, +0x6144 s8 card step owner,
// +0x6145 s8 card port, +0x6146 HDD result, +0x6147 "directory exists" flag, +0x6148 format result.
#include "game/game.h"

#include "common/debug_hooks.h"
#include "common/ee_math.h"
#include "common/guest_stack.h"
#include "sce/sce.h"

namespace ac04::game {

using guest::GRef;

namespace {
AC04_GUEST_VAR(u32, g_SavePage, 0x00411830)    // gp-0x41C0
AC04_GUEST_VAR(u32, g_pMenuFade, 0x00410E88)   // gp-0x4B68
AC04_GUEST_VAR(u16, g_MissionNo, 0x002A15B8)

constexpr u32 kCampaignBlock = 0x002A0318;
constexpr u32 kCampaignBlockSize = 0x12C0;

f32 K(u32 address) { return GRef<f32>(address); }
u64& Q(u32 p, u32 i) { return GRef<u64>(p + 8 * i); }
u64 Sx(s32 v) { return static_cast<u64>(static_cast<s64>(v)); }
s32& State(u32 obj) { return GRef<s32>(obj + 0x6128); }
s8& Owner(u32 obj) { return GRef<s8>(obj + 0x6144); }

void CopyBlock(u32 dst, u32 src) {
    for (u32 i = 0; i < kCampaignBlockSize; i += 8) GRef<u64>(dst + i) = GRef<u64>(src + i);
}

}  // namespace

// EE: 0x0023C7B8
// Save image checksum: byte sum of the 0x4B00 bytes after the 16-byte header.
s32 ComputeSaveChecksum(u32 image) {
    s32 sum = 0;
    for (u32 i = 0; i < 0x4B00; ++i) sum += GRef<u8>(image + 0x10 + i);
    return sum;
}

// EE: 0x0023C7F0
bool CheckSaveChecksum(u32 image) { return ComputeSaveChecksum(image) == GRef<s32>(image + 0xC); }

// EE: 0x0023C820
// Empty save image: magic "AC04", size 0x4B10, no slot, checksum 0, four default campaign blocks.
void InitSaveFileImage(u32 image) {
    GRef<u32>(image) = 0x34304341;
    GRef<u32>(image + 4) = 0x4B10;
    GRef<u32>(image + 8) = 0;
    GRef<u32>(image + 0xC) = 0;
    u32 block = image + 0x10;
    for (s32 i = 3; i >= 0; --i, block += kCampaignBlockSize) func_001053C8(block);
}

// EE: 0x0023C880
// Keeps the shown status unless it is "no card" (-11), 1 or 2, or the new one is not 1/2.
void SetSavePageStatus(u32 obj, s32 status) {
    const s32 cur = GRef<s32>(obj + 0x6140);
    if (cur == -0xB || cur == 1 || cur == 2 || 1u < static_cast<u32>(status - 1)) GRef<s32>(obj + 0x6140) = status;
}

// EE: 0x0023C8B8
// Help message of the save mode for the current state (-1: none).
s16 GetSaveHelpMessage(u32 obj) {
    s16 id = -1;
    switch (State(obj)) {
        case 4: case 5: case 0xD: id = 0; break;
        case 6: id = 0x13; break;
        case 7: id = 0x23; break;
        case 8:
            id = 0x23;
            if (GRef<s32>(obj + static_cast<u32>(GRef<s32>(obj + 0x6134)) * 0x10 + 0x60E4) < 0) {
                id = GRef<s16>(0x003AF290 + static_cast<u32>(GRef<s32>(obj + 0x6134)) * 2);
            }
            break;
        case 9: case 0xB: case 0xC: id = 0x20; break;
        case 0xA: id = 0x4A; break;
        case 0xE: id = 0x3F; break;
        case 0xF: id = 0x42; break;
        case 0x10: id = GRef<s8>(obj + 0x6148) == 1 ? 0x45 : 0x46; break;
        case 0x12: case 0x13: case 0x14:
            SetSavePageStatus(obj, GetMcStatus(func_0025F298(), GRef<s8>(obj + 0x6145)));
            switch (GRef<s32>(obj + 0x6140)) {
                case 2: id = GRef<s16>(obj + 0x606A); break;
                case 5: case 9: case 0xA: id = 0x3D; break;
                case -0xB: case 7: id = 3; break;
                case -3: id = 0xA; break;
                default: id = 5; break;
            }
            break;
        case 0x15: id = 0x26; break;
        case 0x16: id = 0x4D; break;
        case 0x17: id = 0x25; break;
        default: break;
    }
    return id;
}

// EE: 0x0023CA18
// Help message of the load mode for the current state (-1: none).
s16 GetLoadHelpMessage(u32 obj) {
    s16 id = -1;
    switch (State(obj)) {
        case 4: case 5: id = 0; break;
        case 6: id = 0x29; break;
        case 8: id = GRef<s16>(0x003AF290 + (static_cast<u32>(GRef<s32>(obj + 0x6134)) + 4) * 2); break;
        case 0x12: case 0x13: case 0x14:
            SetSavePageStatus(obj, GetMcStatus(func_0025F298(), GRef<s8>(obj + 0x6145)));
            switch (GRef<s32>(obj + 0x6140)) {
                case 2: id = GRef<s16>(obj + 0x606A); break;
                case -0xB: case 7: id = 3; break;
                case -4: case -2: id = 0x3C; break;
                case -3: case 5: case 0xA: id = 0x3D; break;
                default: id = 5; break;
            }
            break;
        case 0x15: id = 0x39; break;
        case 0x16: id = 0x51; break;
        default: break;
    }
    return id;
}

// EE: 0x0023CB20
// Help line: shows the state's message (typed out; messages 0, 0x20 and 0x42 appear at once) or
// hides it (progress -1).
void UpdateSavePageHelp(u32 obj) {
    s16 id = -1;
    if (!(static_cast<u32>(State(obj)) < 4)) {
        if (GRef<s32>(obj + 0x6124) == 0) {
            id = GetSaveHelpMessage(obj);
        } else if (GRef<s32>(obj + 0x6124) == 1) {
            id = GetLoadHelpMessage(obj);
        }
    }
    if (id < 0) {
        GRef<f32>(obj + 0x6064) = -1.0f;
        return;
    }
    const u32 mb = obj + 0x605C;
    if (GRef<s16>(obj + 0x606A) != id) {
        SetMessageBoxText(mb, id);
        GRef<f32>(obj + 0x6064) = (id == 0x42 || id == 0 || id == 0x20) ? 200.0f : K(0x0041032C);
    }
    UpdateMessageBox(K(0x00410330), mb, static_cast<s32>(Ctx()->frame_count << 1));
}

// EE: 0x0023CC28
// Moves the cursor to slot `slot` (0-3, else 0).
void SelectSaveSlot(u32 obj, u64 slot) {
    if (3 < slot) slot = 0;
    GRef<s32>(obj + 0x6134) = static_cast<s32>(slot);
    GRef<u16>(obj + 0x60D8) = 0x480;
    const s32 y = static_cast<s32>(slot) * 0x430 + 0x610;
    GRef<s16>(obj + 0x60DA) = static_cast<s16>(y);
    MoveCrosshairAuto(GRef<u32>(obj + 0x60E0), 0x480, y);
}

// EE: 0x0023CC60
// Slot summaries from the image: mission number, play time, credits, difficulty, cleared flag
// (empty slots: mission -1).
void UpdateSaveSlotSummaries(u32 obj) {
    u32 mask = GRef<u32>(obj + 0x258);
    u32 block = obj;
    for (u32 i = 0; i < 4; ++i, block += kCampaignBlockSize, mask >>= 1) {
        const u32 s = obj + 0x60E4 + i * 0x10;
        if ((mask & 1) == 0) {
            GRef<s32>(s) = -1;
            GRef<u32>(s + 4) = 0;
            GRef<u32>(s + 8) = 0;
            GRef<u16>(s + 0xC) = 0;
            GRef<u16>(s + 0xE) = 0;
        } else {
            const u16 difficulty = GRef<u16>(block + 0x1502);
            GRef<s32>(s) = GRef<u16>(block + 0x1500);
            GRef<u16>(s + 0xC) = difficulty;
            GRef<u32>(s + 4) = GRef<u32>(block + 0x298);
            GRef<u32>(s + 8) = GRef<u32>(block + 0xC78);
            GRef<u16>(s + 0xE) = (GRef<u32>(block + 0x260) & 1) != 0 ? 1 : 0;
        }
    }
}

// EE: 0x0023CD00
// Allocates and sets up the page for the current sub-page: mode and MenuGoTo targets.
u32 CreateSavePage() {
    const u32 save = func_0025F298();
    const u32 obj = WorkHeapAlloc(0x6150, 0x10);
    ee::memset(obj, 0xF4, 0x6150);
    func_0025F320(save);
    GRef<s32>(obj + 0x6128) = 0;
    GRef<s32>(obj + 0x6138) = 0;
    GRef<s32>(obj + 0x6134) = 0;
    GRef<s8>(obj + 0x6145) = 0;
    GRef<s8>(obj + 0x6144) = 0;
    GRef<s32>(obj + 0x6140) = 1;
    GRef<s8>(obj + 0x6146) = 0;
    GRef<s8>(obj + 0x6147) = 0;
    GRef<s8>(obj + 0x6148) = 0;
    InitPanel(obj + 0x6080, 6, 0x003AF230, obj + 0x6020);
    GRef<u16>(obj + 0x6082) |= 0xE;
    InitSaveFileImage(obj + 0x250);
    UpdateSaveSlotSummaries(obj);
    const u16 sub = Ctx()->sub;
    if (sub < 0x4F) {
        GRef<s32>(obj + 0x6124) = 0;
    } else if (sub < 0x51) {
        GRef<s32>(obj + 0x6124) = 1;
    } else {
        GRef<s32>(obj + 0x6124) = 0x55 < sub ? 3 : 2;
    }
    auto both = [obj](u16 target) {
        GRef<u16>(obj + 0x612C) = 1;
        GRef<u16>(obj + 0x612E) = target;
        GRef<u16>(obj + 0x6130) = 1;
        GRef<u16>(obj + 0x6132) = target;
    };
    switch (sub) {
        case 0x4A: case 0x51: both(0x17); break;
        case 0x4B: case 0x52:
            GRef<u16>(obj + 0x612C) = 1;
            GRef<u16>(obj + 0x612E) = 0x25;
            GRef<u16>(obj + 0x6132) = 2;
            GRef<s32>(obj + 0x6138) = 1;
            GRef<u16>(obj + 0x6130) = 0;
            break;
        case 0x4C: case 0x53:
            GRef<u16>(obj + 0x612C) = 1;
            GRef<u16>(obj + 0x612E) = 0x26;
            GRef<s32>(obj + 0x6138) = 1;
            GRef<u16>(obj + 0x6130) = 0;
            GRef<u16>(obj + 0x6132) = 0;
            break;
        case 0x4D: case 0x54: both(0x45); break;
        case 0x4E: case 0x55: both(0x46); break;
        case 0x4F: case 0x56:
            GRef<u16>(obj + 0x6130) = 1;
            GRef<u16>(obj + 0x6132) = 3;
            GRef<u16>(obj + 0x612C) = 1;
            GRef<u16>(obj + 0x612E) = 3;
            break;
        case 0x50: case 0x57:
            GRef<u16>(obj + 0x612C) = 1;
            GRef<u16>(obj + 0x612E) = 0x17;
            GRef<u16>(obj + 0x6132) = 2;
            GRef<u16>(obj + 0x6130) = 0;
            break;
        default:
            break;
    }
    const u32 fade = g_pMenuFade();
    GRef<u16>(fade + 0x30) = 1;
    GRef<u16>(fade + 0x32) = 0;
    InitFade(obj + 0x240, 1);
    return obj;
}

// EE: 0x0023CEF0
// Leaves the page: with a menu target (+0x6130 != 0) only the page fades (state 2), otherwise
// the screen fades out too (state 3, menu page change).
void LeaveSavePage(u32 obj) {
    const u32 fade = g_pMenuFade();
    if (GRef<s16>(obj + 0x6130) == 0) {
        State(obj) = 3;
        StartFade(0.5f, fade, 1);
        StartFade(0.5f, obj + 0x240, 1);
    } else {
        State(obj) = 2;
        StartFade(0.5f, obj + 0x240, 1);
    }
    SetPanelState(obj + 0x6080, 1);
}

// EE: 0x0023CF80
// Before writing: after a mission (+0x6138 == 1, once) keeps a copy of the campaign block, applies
// the results, clamps the statistics and advances the campaign (after mission 12 with target 2:
// return sub 6 and target 0x20); then copies the campaign block into the selected slot and
// updates the checksum.
void PrepareSaveImage(u32 obj) {
    if (GRef<s32>(obj + 0x6138) == 1) {
        GRef<s32>(obj + 0x6138) = 2;
        CopyBlock(obj + 0x4D60, kCampaignBlock);
        ApplyMissionResults();
        ClampStatistics(kCampaignBlock);
        AdvanceCampaignMission();
        if (GRef<s16>(obj + 0x6132) == 2 && g_MissionNo() == 0xC) {
            Ctx()->ret_sub = 6;
            GRef<u16>(obj + 0x6132) = 0x20;
        }
    }
    const u32 slot = GRef<u32>(obj + 0x6134);
    GRef<u32>(obj + 0x258) |= 1u << (slot & 0x1F);
    CopyBlock(obj + 0x260 + slot * kCampaignBlockSize, kCampaignBlock);
    GRef<s32>(obj + 0x25C) = ComputeSaveChecksum(obj + 0x250);
}

// EE: 0x0023D0E0
// Loading a slot: copies it into the campaign block, applies the options and the selection
// (campaign not started: target 0/1), then leaves the page.
void ApplyLoadedSave(u32 obj) {
    CopyBlock(kCampaignBlock, obj + 0x260 + static_cast<u32>(GRef<s32>(obj + 0x6134)) * kCampaignBlockSize);
    ApplyOptionSettings();
    ValidateCampaignSelection();
    if (g_MissionNo() == 0) {
        GRef<u16>(obj + 0x6130) = 0;
        GRef<u16>(obj + 0x6132) = 1;
    } else {
        ApplyCampaignSelection();
    }
    LeaveSavePage(obj);
}

namespace {

// Slots read: summaries, type-out timer b restarted, cursor on the current slot. `checkpoint` is
// the call site of SelectSaveSlot reported to the debug layer (validation-strategy.md §2.1).
void ShowLoadedSlots(u32 obj, u32 checkpoint = 0) {
    UpdateSaveSlotSummaries(obj);
    GRef<u32>(obj + 0x60D0) = 0;
    GRef<u16>(obj + 0x6080) |= 0x200;
    if (checkpoint != 0) debug::OnGuestCheckpoint(checkpoint);
    SelectSaveSlot(obj, GRef<u32>(obj + 0x6134));
}

void ClosePage(u32 obj) {
    State(obj) = 1;
    StartFade(0.5f, obj + 0x240, 1);
    SetPanelState(obj + 0x6080, 1);
}

void ToNewSave(u32 obj) {
    GRef<s32>(obj + 0x25C) = ComputeSaveChecksum(obj + 0x250);
    State(obj) = 10;
}

}  // namespace

// EE: 0x0023D198
// The save/load state machine (frontend.md): 0 wait for the screen, 1-3 leaving, 4 card check,
// 5 read the save file, 6 slot choice, 7/8 confirm, 9 overwrite, 10 confirm a new save, 0xB-0xD
// card space / create / clean up, 0xE-0x10 format, 0x11-0x16 messages, 0x17/0x18 done. In the
// memory-card modes the save directory is re-checked every frame while no request owns the card
// (+0x6144 of -1/0 or 0x18); a missing card or save leads to the messages.
void SavePageUpdate(u32 obj) {
    const u32 save = func_0025F298();
    const u32 hdd = func_002597B8();
    const u32 fade = g_pMenuFade();
    const s8 port = GRef<s8>(obj + 0x6145);
    const bool hdd_mode = 1u < GRef<u32>(obj + 0x6124);
    const u32 edge = Ctx()->pad_buttons[0][1];
    const u32 repeat = Ctx()->pad_buttons[0][2];
    GRef<u16>(fade + 0x32) = 0;
    s32 state = 0;
    if (State(obj) != 0) {
        UpdatePanel(obj + 0x6080);
        state = State(obj);
    }
    const u32 slot_mission = obj + 0x60E4;
    auto slot_used = [&]() { return !(GRef<s32>(slot_mission + static_cast<u32>(GRef<s32>(obj + 0x6134)) * 0x10) < 0); };
    auto done_typing = [&]() { return (GRef<u32>(obj + 0x605C) & 2) != 0; };
    auto confirm_se = [&]() {  // the shared "SE 2 on confirm, else SE 3" tail
        if ((edge & 0x50) == 0) {
            PlayMenuSe(0, 3);
        } else {
            PlayMenuSe(0, 2);
        }
    };
    auto message_exit = [&]() {  // states 0x12/0x13: any button leaves to the final message
        if (!hdd_mode) Owner(obj) = 0x18;
        State(obj) = 0x16;
        confirm_se();
    };
    switch (state) {
        case 0:
            if (K(0x00410334) < GRef<f32>(fade + 0xC)) {
                State(obj) = 4;
                SelectSaveSlot(obj, GRef<u32>(obj + 0x6134));
            }
            break;
        case 1:
            if (hdd_mode) {
                if ((GRef<u32>(obj + 0x240) & 8) != 0) MenuGoTo(GRef<s16>(obj + 0x612C), GRef<u16>(obj + 0x612E));
            } else if ((GRef<u32>(obj + 0x240) & 8) != 0 && static_cast<u8>(Owner(obj)) < 2) {
                MenuGoTo(GRef<s16>(obj + 0x612C), GRef<u16>(obj + 0x612E));
                Owner(obj) = 1;
            }
            break;
        case 2:
        case 3: {
            const u32 flags = state == 2 ? GRef<u32>(obj + 0x240) : GRef<u32>(fade);
            if ((flags & 8) == 0) break;
            if (hdd_mode) {
                MenuGoTo(GRef<s16>(obj + 0x6130), GRef<u16>(obj + 0x6132));
            } else if (state == 2) {
                if (Owner(obj) == 0 || Owner(obj) == 2) MenuGoTo(GRef<s16>(obj + 0x6130), GRef<u16>(obj + 0x6132));
            } else if (Owner(obj) == 0 || Owner(obj) == 3) {
                MenuGoTo(GRef<s16>(obj + 0x6130), GRef<u16>(obj + 0x6132));
                Owner(obj) = 3;
            }
            break;
        }
        case 4:
            if (hdd_mode) {
                const u8 mode = GetHddSaveMode(hdd);
                const s32 r = func_0025AEE0(hdd);
                if (mode == 1) {
                    State(obj) = 0x12;
                } else if (r != 1) {
                    if (r == -1) State(obj) = 0x12;
                } else if (GRef<s8>(hdd + 0x216) < 1) {
                    if (GRef<s32>(obj + 0x6124) == 2) {
                        ToNewSave(obj);
                    } else {
                        State(obj) = 0x11;
                    }
                } else {
                    State(obj) = 5;
                }
            } else {
                State(obj) = 5;
            }
            break;
        case 5:
            if (hdd_mode) {
                s32 r = func_0025BBD0(hdd, 5);
                if (r == 1) {
                    if (!CheckSaveChecksum(obj + 0x250)) {
                        r = -1;
                        func_002597E8(0xFFFFFFFFu);
                    }
                    if (r == 1) {
                        ShowLoadedSlots(obj);
                        State(obj) = 6;
                        break;
                    }
                }
                if (r == -1) State(obj) = 0x12;
            } else {
                s8 owner = Owner(obj);
                if (owner == 0) {
                    owner = 5;
                    GRef<s32>(save + 0xAC4) = 5;
                    Owner(obj) = 5;
                }
                if (owner == 5) {
                    s32 r = LoadMcSaveFile(save, port);
                    Owner(obj) = 5;
                    if (r == 1) {
                        if (!CheckSaveChecksum(obj + 0x250)) {
                            r = -1;
                            GRef<s32>(save + static_cast<u32>(GRef<s8>(obj + 0x6145)) * 0x540 + 0x4E4) = 5;
                        }
                        if (r == 1) {
                            ShowLoadedSlots(obj, 0x0023D4FC);
                            Owner(obj) = 0;
                            State(obj) = 6;
                            break;
                        }
                    }
                    if (r == -1) {
                        const s32 status = GetMcStatus(save, port);
                        InitSaveFileImage(obj + 0x250);
                        if (status != -4 || GRef<s32>(obj + 0x6124) != 0) {
                            State(obj) = 0x12;
                        } else {
                            ToNewSave(obj);
                            Owner(obj) = 0;
                        }
                    }
                }
            }
            break;
        case 6:
            GRef<u16>(fade + 0x32) = 0x33;
            if ((edge & 0x20) != 0) {
                State(obj) = 1;
                StartFade(0.5f, obj + 0x240, 1);
                PlayMenuSe(0, 3);
                SetPanelState(obj + 0x6080, 1);
                return;
            }
            if ((edge & 0x50) != 0) {
                const u32 mode = GRef<u32>(obj + 0x6124);
                if ((mode & 1) != 0 && !slot_used()) {
                    PlayMenuSe(0, 5);
                    return;
                }
                if (((mode ^ 1) & 1) != 0 && slot_used()) {
                    PlayMenuSe(0, 2);
                    State(obj) = 7;
                    return;
                }
                PlayMenuSe(0, 2);
                State(obj) = 8;
                return;
            }
            if ((repeat & 3) != 0) {
                s32 s = GRef<s32>(obj + 0x6134) - static_cast<s32>(repeat & 1);
                if ((repeat & 2) != 0) s += 1;
                s = (s + 4) % 4;
                GRef<s32>(obj + 0x6134) = s;
                SelectSaveSlot(obj, static_cast<u64>(static_cast<s64>(s)));
                PlayMenuSe(0, 4);
            }
            break;
        case 7:
        case 8:
            GRef<u16>(fade + 0x32) = 0x30;
            if (!done_typing()) break;
            if ((edge & 0x20) != 0) {
                State(obj) = 6;
                PlayMenuSe(0, 3);
            } else if ((edge & 0x50) != 0) {
                if ((GRef<u32>(obj + 0x6124) & 1) == 0) {
                    PrepareSaveImage(obj);
                    State(obj) = 9;
                } else {
                    ApplyLoadedSave(obj);
                }
                PlayMenuSe(0, 2);
            }
            break;
        case 9: {
            auto saved = [&]() {
                State(obj) = 0x17;
                UpdateSaveSlotSummaries(obj);
                GRef<u32>(obj + 0x60D0) = 0;
                GRef<u16>(obj + 0x6080) |= 0x200;
            };
            if (hdd_mode) {
                const s32 r = func_0025B948(hdd, 5);
                if (r == 1) {
                    saved();
                } else if (r == -1) {
                    State(obj) = 0x15;
                    GRef<u32>(obj + 0x258) = GRef<u32>(obj + 0x613C);
                }
            } else {
                s8 owner = Owner(obj);
                if (owner == 0) {
                    GRef<s32>(save + 0xAC4) = 5;
                    Owner(obj) = 9;
                    owner = 9;
                    GRef<u32>(obj + 0x613C) = GRef<u32>(obj + 0x258);
                    GRef<u32>(obj + 0x258) |= 1u << (GRef<u32>(obj + 0x6134) & 0x1F);
                }
                if (owner == 9) {
                    const s32 r = OverwriteMcSaveFile(save, port);
                    if (r == 1) {
                        saved();
                    } else if (r == -1) {
                        Owner(obj) = 0x15;
                        GRef<u32>(obj + 0x258) = GRef<u32>(obj + 0x613C);
                        State(obj) = 0x15;
                    }
                }
            }
            break;
        }
        case 0xA:
            GRef<u16>(fade + 0x32) = 0x30;
            if (!done_typing()) break;
            if ((edge & 0x50) != 0) {
                PlayMenuSe(0, 2);
                State(obj) = 0xB;
            } else if ((edge & 0x20) != 0) {
                PlayMenuSe(0, 3);
                State(obj) = 0x16;
            }
            break;
        case 0xB:
            if (hdd_mode) {
                const s32 r = func_0025BED8(hdd);
                if (r == 1) {
                    const s32 a = func_00259898();
                    const s32 b = func_00259878();
                    const s32 c = static_cast<s32>(func_00259C90(hdd));
                    bool check = false;
                    s8 result = 0;
                    if (b < 2) {
                        if (a < 0x100) {
                            check = true;
                        } else {
                            GRef<s8>(obj + 0x6146) = 1;
                            State(obj) = 0x11;
                        }
                    } else if (b < 0x400) {
                        check = true;
                    } else {
                        result = 2;
                        GRef<s8>(obj + 0x6146) = result;
                        State(obj) = 0x11;
                    }
                    if (check) {
                        if (GRef<u32>(hdd + 0x48EC) < 0x25800u && c < 0x200000) {
                            GRef<s8>(obj + 0x6146) = 3;
                            State(obj) = 0x11;
                        } else {
                            State(obj) = 0xC;
                        }
                    }
                } else if (r == -1) {
                    State(obj) = 0x12;
                }
            } else if (Owner(obj) == 0 || Owner(obj) == 0xB) {
                const s32 r = CheckMcCard(save, GRef<s8>(obj + 0x6145));
                Owner(obj) = 0xB;
                if (r == 1) {
                    const u32 block = save + static_cast<u32>(GRef<s8>(obj + 0x6145)) * 0x540;
                    if (GRef<s32>(block + 0x4CC) < 0x4B) {
                        GRef<s32>(block + 0x4E4) = -3;
                        State(obj) = 0x12;
                    } else {
                        Owner(obj) = 0xC;
                        State(obj) = 0xC;
                    }
                } else if (r == -1) {
                    State(obj) = 0x12;
                }
            }
            break;
        case 0xC:
            if (hdd_mode) {
                const s32 r = func_0025B0D8(hdd);
                if (r == 1) {
                    State(obj) = 6;
                } else if (r == -1) {
                    if (func_002597C8() == 0x1C) GRef<s8>(obj + 0x6146) = 3;
                    State(obj) = 0xD;
                }
            } else if (Owner(obj) == 0 || Owner(obj) == 0xC) {
                const s32 r = CreateMcSave(save, port);
                Owner(obj) = 0xC;
                if (r == 1) {
                    Owner(obj) = 0;
                    State(obj) = 6;
                } else if (r == -1) {
                    if (GetMcStatus(save, port) == 9) {
                        GRef<s8>(obj + 0x6147) = 1;
                        State(obj) = 0x12;
                    } else {
                        Owner(obj) = 0xD;
                        State(obj) = 0xD;
                    }
                }
            }
            break;
        case 0xD:
            if (hdd_mode) {
                const s32 r = func_0025B6C8(hdd);
                if (r == 1) {
                    State(obj) = GRef<s8>(obj + 0x6146) != 3 ? 0x12 : 0x11;
                } else if (r == -1) {
                    State(obj) = 0x12;
                }
            } else if (Owner(obj) == 0 || Owner(obj) == 0xD) {
                const s32 r = DeleteMcSave(save, port);
                Owner(obj) = 0xD;
                if (r != 0) State(obj) = 0x12;
            }
            break;
        case 0xE:
            Owner(obj) = 0;
            GRef<u16>(fade + 0x32) = 0x30;
            if (!done_typing()) break;
            if ((edge & 0x50) != 0) {
                PlayMenuSe(0, 2);
                GRef<s8>(obj + 0x6148) = 0;
                State(obj) = 0xF;
            } else if ((edge & 0x20) != 0) {
                Owner(obj) = 0x18;
                State(obj) = 0x16;
                PlayMenuSe(0, 3);
            }
            break;
        case 0xF: {
            Owner(obj) = 0xF;
            GRef<u16>(fade + 0x32) = 0;
            const s32 r = FormatMcSaveCard(save, GRef<s8>(obj + 0x6145));
            if (r == 1) {
                GRef<s8>(obj + 0x6148) = 1;
                State(obj) = 0x10;
            } else if (r == -1) {
                GRef<s8>(obj + 0x6148) = 2;
                State(obj) = 0x10;
            } else {
                break;
            }
            Owner(obj) = 0;
            break;
        }
        case 0x10:
            GRef<u16>(fade + 0x32) = 0x10;
            if (GRef<s8>(obj + 0x6148) == 1) {
                if ((edge & 0x50) != 0) {
                    PlayMenuSe(0, 2);
                    ToNewSave(obj);
                }
            } else if ((edge & 0x50) != 0) {
                PlayMenuSe(0, 2);
                Owner(obj) = 0;
                State(obj) = 0x16;
            }
            break;
        case 0x11:
            GRef<u16>(fade + 0x32) = 0x30;
            if ((edge & 0x70) != 0) {
                if (GRef<s32>(obj + 0x6124) == 3) {
                    ClosePage(obj);
                } else {
                    State(obj) = 0x16;
                }
                confirm_se();
            }
            break;
        case 0x12:
            GRef<u16>(fade + 0x32) = 0x30;
            if ((edge & 0x70) != 0) {
                message_exit();
            } else if (hdd_mode) {
                State(obj) = 0x13;
            } else if (GetMcStatus(save, port) == -0xB) {
                Owner(obj) = 0;
                GRef<s8>(obj + 0x6147) = 0;
                State(obj) = 0x14;
                InitSaveFileImage(obj + 0x250);
                UpdateSaveSlotSummaries(obj);
            } else {
                State(obj) = 0x13;
                Owner(obj) = 0x12;
            }
            break;
        case 0x13:
            GRef<u16>(fade + 0x32) = 0x30;
            if (Owner(obj) == 0x12) Owner(obj) = 0;
            if ((edge & 0x70) != 0) message_exit();
            break;
        case 0x14:
            GRef<u16>(fade + 0x32) = 0x30;
            if ((edge & 0x70) != 0) {
                Owner(obj) = 0x18;
                State(obj) = 0x16;
                confirm_se();
            }
            break;
        case 0x15:
            GRef<u16>(fade + 0x32) = 0x30;
            if (!done_typing()) break;
            if ((edge & 0x50) != 0) {
                Owner(obj) = 0x18;
                ClosePage(obj);
                PlayMenuSe(0, 2);
            } else if ((edge & 0x20) != 0) {
                PlayMenuSe(0, 3);
                Owner(obj) = 0;
                State(obj) = 4;
            }
            break;
        case 0x16:
            GRef<u16>(fade + 0x32) = 0x30;
            if (!done_typing()) break;
            if ((edge & 0x50) != 0) {
                ClosePage(obj);
                PlayMenuSe(0, 2);
                if (hdd_mode) return;
                if (Owner(obj) == 0x16) Owner(obj) = 1;
            } else if ((edge & 0x20) != 0) {
                PlayMenuSe(0, 3);
                State(obj) = 4;
                if (hdd_mode) return;
                if (Owner(obj) == 0x16) Owner(obj) = 0;
            }
            break;
        case 0x17:
            GRef<u16>(fade + 0x32) = 0x10;
            if ((edge & 0x50) != 0) {
                PlayMenuSe(0, 2);
                State(obj) = 0x18;
            }
            break;
        case 0x18:
            LeaveSavePage(obj);
            Owner(obj) = 0x18;
            break;
        default:
            break;
    }
    if (hdd_mode) return;
    // Card re-check while no request owns the card.
    const s8 owner = Owner(obj);
    if (1u < static_cast<u8>(owner + 1) && owner != 0x18) return;
    const s32 r = CheckMcSaveDir(save, port);
    const s32 st = State(obj);
    if ((static_cast<u32>(st - 1) < 3 || st == 0x16) && r != 0) {
        Owner(obj) = static_cast<s8>(State(obj));
        return;
    }
    Owner(obj) = -1;
    const u32 block = save + static_cast<u32>(port) * 0x540;
    if (r == 1 && st != 0x10) {
        const s32 entries = GRef<s32>(block + 0x4D8);
        s32 status;
        if (entries == -1) {
            status = 10;  // directory without the save file
        } else if (GRef<s8>(obj + 0x6147) != 0) {
            status = 9;
        } else if (entries != 0) {
            Owner(obj) = 0;
            if (st != 0x14) return;
            State(obj) = 5;
            Owner(obj) = 0;
            return;
        } else if (GRef<s32>(obj + 0x6124) == 0) {
            const u32 b = save + static_cast<u32>(GRef<s8>(obj + 0x6145)) * 0x540;
            if (GRef<s32>(b + 0x4CC) < 0x4B) {
                GRef<s32>(b + 0x4E4) = -3;
                State(obj) = 0x12;
            } else if (static_cast<u32>(st - 0xB) < 2) {
                Owner(obj) = 0;
                return;
            } else {
                ToNewSave(obj);
            }
            Owner(obj) = 0;
            return;
        } else {
            status = -4;
        }
        GRef<s32>(save + static_cast<u32>(GRef<s8>(obj + 0x6145)) * 0x540 + 0x4E4) = status;
        State(obj) = 0x12;
        Owner(obj) = 0;
        return;
    }
    if (r != -1) return;
    const s32 status = GetMcStatus(save, port);
    if (GRef<s32>(obj + 0x6124) == 0) {
        if (GRef<s8>(obj + 0x6147) != 0) GRef<s32>(save + static_cast<u32>(GRef<s8>(obj + 0x6145)) * 0x540 + 0x4E4) = 9;
        if (status == -4) {
            const u32 b = save + static_cast<u32>(GRef<s8>(obj + 0x6145)) * 0x540;
            if (GRef<s32>(b + 0x4CC) < 0x4B) {
                GRef<s32>(b + 0x4E4) = -3;
                State(obj) = 0x12;
            } else if (static_cast<u32>(State(obj) - 0xB) < 2) {
                Owner(obj) = 0;
                return;
            } else {
                ToNewSave(obj);
            }
        } else if (status != -2) {
            State(obj) = 0x12;
        } else if (static_cast<u32>(State(obj) - 0xF) < 2) {
            Owner(obj) = 0;
            return;
        } else {
            State(obj) = 0xE;
        }
    } else {
        State(obj) = 0x12;
    }
    Owner(obj) = 0;
}

// EE: 0x0023E0E8
// Slot texts (text layer): the labels (FILE/TIME/CREDITS/LEVEL) typed out by timer a and the slot
// contents by timer b; sets the type-out SE bit while typing and stops timers a/b once done.
void DrawSaveSlotTexts(u32 list, s32 index, u32 obj) {
    guest::GuestFrame frame(0x100);
    const u32 buf = frame.Address();
    const u32 parts = GRef<u16>(obj + 0x6080);
    if (GRef<f32>(obj + 0x608C) < K(0x00410338)) return;
    GRef<u16>(obj + 0x6080) = static_cast<u16>(parts & 0x7FFF);
    BeginMenuTextLayer(list, index);
    if (0.0f < GRef<f32>(obj + 0x60CC)) {
        f32 t = GRef<f32>(obj + 0x60CC) * 90.0f;
        s32 y = 0x4B0;
        f32 next = t;
        for (s32 i = 0; i < 4;) {
            DrawMenuText(t, 0x300, y, 4, 0x00411260);  // "FILE>"
            const s32 n = i + 1;
            const u64 args[1] = {static_cast<u64>(static_cast<s64>(n))};
            ee::sprintf(buf, 0x00411268, args, 1);  // "%02d"
            next = fpu::Sub(t, 8.0f);
            const s32 y2 = y + 0x160;
            DrawMenuText(fpu::Sub(t, 4.0f), 0x300, y + 0xB0, 3, buf);
            i = n;
            y += 0x430;
            DrawMenuText(next, 0x700, y2, 4, 0x00411270);              // "TIME>"
            DrawMenuText(fpu::Sub(t, 12.0f), 0xF60, y2, 4, 0x004094A8); // "CREDITS>"
            DrawMenuText(fpu::Sub(t, 16.0f), 0x1850, y2, 4, 0x00411278); // "LEVEL>"
            t = next;
        }
        const f32 left = fpu::Sub(t, 15.0f);
        if (left < 0.0f) {
            GRef<u16>(obj + 0x6080) |= 0x8000;
        } else if (6.0f < left && (parts & 0x100) != 0) {
            GRef<u16>(obj + 0x6080) &= 0xFEFF;
        }
    }
    const f32 b = GRef<f32>(obj + 0x60D0);
    if (0.0f < b) {
        f32 limit = b * 90.0f;
        const f32 cap = fpu::Sub(GRef<f32>(obj + 0x60D4) * 90.0f, 8.0f);
        if (cap < limit) limit = cap;
        f32 widest = 0.0f;
        f32 t = limit;
        s32 y_title = 0x4B0;
        s32 y = 0x6C0;
        for (s32 i = 0; i < 4; ++i, y += 0x430, y_title += 0x430) {
            const u32 s = obj + 0x60E4 + static_cast<u32>(i) * 0x10;
            const s32 mission = GRef<s32>(s);
            f32 len;
            if (mission < 0) {
                len = 21.0f;
                ee::sprintf(buf, 0x004094B8, nullptr, 0);  // "MISSION -- / --------"
                DrawMenuText(t, 0x700, y_title, 3, buf);
                DrawMenuText(fpu::Sub(t, 4.0f), 0x700, y, 3, 0x004094D0);
                DrawMenuText(fpu::Sub(t, 8.0f), 0xF60, y, 3, 0x004094E0);
                DrawMenuText(fpu::Sub(t, 12.0f), 0x1850, y, 3, 0x00411280);
            } else {
                const u64 title = mission != 0 ? GRef<u32>(0x003844A0 + static_cast<u32>(mission - 1) * 4) : 0;
                const u64 args[2] = {static_cast<u64>(static_cast<s64>(mission)), title};
                if (GRef<u16>(s + 0xE) != 0) {
                    if (mission == 0) {
                        ee::sprintf(buf, 0x004094F0, nullptr, 0);  // "MISSION CL / MISSION CLEAR<CLEAR>"
                    } else {
                        ee::sprintf(buf, 0x00409518, args, 2);     // "MISSION %02d / %s<CLEAR>"
                    }
                } else if (mission == 0) {
                    ee::sprintf(buf, 0x00409538, nullptr, 0);      // "MISSION 00 / --------"
                } else {
                    ee::sprintf(buf, 0x00409550, args, 2);         // "MISSION %02d / %s"
                }
                DrawMenuText(t, 0x700, y_title, 3, buf);
                len = fpu::IntToFloat(static_cast<s32>(ee::strlen(buf)));
                const s32 time = GRef<s32>(s + 4);
                const u64 targs[3] = {static_cast<u64>(static_cast<s64>(time / 0x34BC0)),
                                      static_cast<u64>(static_cast<s64>((time / 0xE10) % 0x3C)),
                                      static_cast<u64>(static_cast<s64>((time / 0x3C) % 0x3C))};
                ee::sprintf(buf, 0x00409568, targs, 3);  // "%04d:%02d'%02d\""
                DrawMenuText(fpu::Sub(t, 4.0f), 0x700, y, 3, buf);
                const s32 credits = GRef<s32>(s + 8);
                const u64 cargs[3] = {static_cast<u64>(static_cast<s64>(credits / 1000000)),
                                      static_cast<u64>(static_cast<s64>((credits / 1000) % 1000)),
                                      static_cast<u64>(static_cast<s64>(credits % 1000))};
                ee::sprintf(buf, 0x00409578, cargs, 3);  // "~ %02d,%03d,%03d"
                DrawMenuText(fpu::Sub(t, 8.0f), 0xF60, y, 3, buf);
                u32 difficulty = GRef<u16>(s + 0xC);
                if (!(difficulty < 6)) difficulty = 0;
                const u64 dargs[1] = {GRef<u32>(0x003845E0 + difficulty * 4)};
                ee::sprintf(buf, 0x00411288, dargs, 1);  // "%s"
                DrawMenuText(fpu::Sub(t, 12.0f), 0x1850, y, 3, buf);
            }
            len = fpu::Add(len, fpu::IntToFloat(i) * 8.0f);
            if (widest < len) widest = len;
            t = fpu::Sub(t, 8.0f);
        }
        const f32 left = fpu::Sub(limit, widest);
        if (left < 0.0f) {
            GRef<u16>(obj + 0x6080) |= 0x8000;
        } else if (10.0f < left && (parts & 0x200) != 0) {
            GRef<u16>(obj + 0x6080) &= 0xFDFF;
        }
    }
    EndMenuTextLayer();
}

// EE: 0x0023E6A8
// Slot frames (fans in 0xD1C1A9) once their outline time passed K 0x0041033C, and the selection
// bar on the current slot from state 6 on.
void DrawSaveSlotFrames(u32 list, s32 index, u32 obj) {
    const f32 k = K(0x0041033C);
    if (!(k <= GRef<f32>(obj + 0x608C))) return;
    const u32 start = Ctx()->cur_buf.addr;
    u32 p = BuildMenuDrawEnv(start + 0x10, 0x280, 0x1C0);
    Q(p, 0) = 0x1000000000008002ull;
    Q(p, 1) = 0xE;
    Q(p, 2) = 0x8000000068ull;
    Q(p, 3) = 0x43;
    Q(p, 5) = 0x19;
    Q(p, 4) = 0;
    u32 end = p + 0x30;
    const u32 state = GRef<u32>(obj + 0x6128);
    s32 y = 0x430;
    u32 b = p + 0x30;
    for (u32 i = 0; i < 4; ++i, y += 0x430) {
        if (!(k <= GRef<f32>(obj + 0x6094 + i * 4))) continue;
        Q(b, 0) = 0xA400000000008001ull;
        Q(b, 1) = 0x5D5D5D5D10ull;
        Q(b, 2) = 0x24A;
        Q(b, 3) = 0xD1C1A9;
        Q(b, 4) = Sx(y + 0x10) << 0x10 | 0x290;
        Q(b, 5) = Sx(y + 0x3B0) << 0x10 | 0x660;
        Q(b, 6) = Sx(y + 0x3A0) << 0x10 | 0x290;
        Q(b, 7) = Sx(y) << 0x10 | 0x660;
        Q(b, 8) = Sx(y + 0x10) << 0x10 | 0x660;
        Q(b, 9) = Sx(y + 0x3B0) << 0x10 | 0x660;
        Q(b, 10) = Sx(y + 0x170) << 0x10 | 0x670;
        Q(b, 11) = Sx(y + 0x170) << 0x10 | 0x2570;
        b += 0x60;
        end = b;
    }
    if (5 < state) {
        const s32 sy = GRef<s32>(obj + 0x6134) * 0x430;
        Q(end, 0) = 0x4400000000008001ull;
        Q(end, 1) = 0x5D10;
        Q(end, 2) = 0x246;
        Q(end, 5) = Sx(sy + 0x7E0) << 0x10 | 0x660;
        Q(end, 3) = 0x1067;
        Q(end, 4) = Sx(sy + 0x440) << 0x10 | 0x290;
        end += 0x30;
    }
    LinkPacket(list, index, start, CloseDmaPacket(start, end));
}

// EE: 0x0023E940
void DrawSavePage(u32 obj) {
    InitBucketList(obj, 1);
    SetPacketListBuckets(kMainContextAddress + offsetof(MainContext, vif1_list), obj);
    DrawPanel(obj, 0, obj + 0x6080);
    DrawSaveSlotFrames(obj, 0, obj);
    DrawSaveSlotTexts(obj, 0, obj);
    UploadMessageBoxGlyphs(obj, 0, obj + 0x605C);
    DrawMessageBox(obj, 0, obj + 0x605C);
    DrawBlackFade(GRef<f32>(obj + 0x24C), obj, 0);
    DrawMenuFade(obj, 0, g_pMenuFade());
}

// EE: 0x0023E9F0
// Subs 0x4A-0x57: creates the page and queues DATA 0xB3; the menu BGM fades out.
void EnterSavePage() {
    PopWorkHeapMark();
    PushWorkHeapMark();
    MainContext* ctx = Ctx();
    ctx->draw_mode = 2;
    ctx->drawenv_pkt[4] = 0x1310000DCull;
    ctx->drawenv_pkt[20] = 0x1310000DCull;
    const u32 obj = CreateSavePage();
    g_SavePage() = obj;
    ResetMenuDataLoad(obj + 0x10);
    QueueMenuDataLoad(obj + 0x10, 0xB3, 3, obj + 0x220);
    FadeOutMenuBgm(2.0f);
    DrawMenuFadeFrame(obj, g_pMenuFade());
    RequestSubState(0x58);
}

// EE: 0x0023EAA0
// Sub 0x58: when DATA 0xB3 is in, registers icon.sys, icon1.ico and the save image with the
// memory-card layer (mask 0x23) and starts the page.
void LoadSavePage() {
    const u32 obj = g_SavePage();
    const u32 save = func_0025F298();
    if (UpdateMenuDataLoad(obj + 0x10) < 0) {
        LoadFontBlock(GRef<u32>(obj + 0x220));
        InitMessageBox(obj + 0x605C);
        RegisterMcSaveFile(save, GRef<s8>(obj + 0x6145), 0, 0x00409590, GRef<u32>(obj + 0x224), 0x3C4);
        RegisterMcSaveFile(save, GRef<s8>(obj + 0x6145), 1, 0x004095A0, GRef<u32>(obj + 0x228), 0xCB98);
        RegisterMcSaveFile(save, GRef<s8>(obj + 0x6145), 5, 0, obj + 0x250, 0x4B10);
        GRef<u32>(save + 0xAC0) = 0x23;
        RequestSubState(0x59);
    }
    DrawMenuFadeFrame(obj, g_pMenuFade());
}

// EE: 0x0023EB68
// Sub 0x59: the page.
void RunSavePage() {
    const u32 obj = g_SavePage();
    debug::OnGuestCheckpoint(0x0023EB68);
    UpdateFade(obj + 0x240);
    UpdateSavePageHelp(obj);
    SavePageUpdate(obj);
    DrawSavePage(obj);
}

AC04_GUEST_FUNCTION(0x0023E9F0, EnterSavePage);
AC04_GUEST_FUNCTION(0x0023EAA0, LoadSavePage);
AC04_GUEST_FUNCTION(0x0023EB68, RunSavePage);

}  // namespace ac04::game
