// HDD module (docs/subsystems/save.md). Unreachable in this build: its init chain starts at
// func_00259910, which has no caller, and the HDD flag that routes DATA.CDP reads here is never set.
// Translated because it is reached by direct calls from the boot path (CLAUDE.md: all game code is
// kept). The worker and power-off thread entry points (0x0025BF70, 0x0025C108) are reached only
// through CreateThread, which the port stubs (dead-code.md); they are translated with the full
// HDD module later.
#include "game/game.h"

#include "common/guest_stack.h"

#include "sce/sce.h"

namespace ac04::game {

using guest::GRef;

namespace {

void GuestCopy(u32 dst, u32 src, u32 size) {
    for (u32 i = 0; i < size; ++i) guest::Mem().Write<u8>(dst + i, guest::Mem().Read<u8>(src + i));
}

}  // namespace

// EE: 0x002597B8
// HDD context (0x4E00+ bytes at 0x004BE0C0).
u32 func_002597B8() {
    return 0x004BE0C0;
}

// EE: 0x00259C98
// HDD state byte +0x4399 of the HDD context (0 when no HDD: always on the retail path).
u8 GetHddState(u32 ctx) { return GRef<u8>(ctx + 0x4399); }

// EE: 0x002597C8
u32 func_002597C8() {
    return GRef<u32>(func_002597B8() + 0x4364);
}

// EE: 0x002598E8
// Sets the context's current operation (< 0x13) and restarts its step counter.
void func_002598E8(u32 ctx, s32 op) {
    if (GRef<s32>(ctx + 4) != op && op < 0x13) {
        GRef<s32>(ctx + 4) = op;
        GRef<s32>(ctx) = 0;
    }
}

// EE: 0x00259910
// Unreferenced (dead): the only path to InitHddModule.
void func_00259910() {
    InitHddModule(func_002597B8());
}

// EE: 0x00259938
// HYPOTHESIS: InitHddModule. Sets up the HDD context: worker, partition and file names
// ("PP.SLUS-20152.AC04.DATA", "pfs%d:..."), install state. Unreachable (see the file comment).
s32 InitHddModule(u32 ctx) {
    func_0025E218(ctx + 0x360);
    const s32 fs = sceFsInit();
    GRef<u8>(ctx + 0x10) = 0;
    GRef<u8>(ctx + 0xE) = 0;
    GRef<u32>(ctx + 4) = 0;
    GRef<u32>(ctx) = 0;
    GRef<u32>(ctx + 0x43BC) = 0;
    GRef<u32>(ctx + 0x43E0) = 0x200000;
    const s32 result = fs >= 0 ? 1 : -1;
    GRef<u32>(ctx + 0x43D0) = 0;
    GRef<u32>(ctx + 0x43E8) = 0;
    GRef<u32>(ctx + 0x43D8) = 0;
    GRef<u16>(ctx + 0xC) = 0;
    GuestCopy(ctx + 0x4600, 0x0040A2F0, 24);  // "PP.SLUS-20152.AC04.DATA"
    GuestCopy(ctx + 0x4621, 0x0040A308, 24);  // "PC.SLUS-20152.AC04.DATA"
    ee::strcpy(ctx + 0x4642, func_0025C2C0(0x0040A320));
    ee::strcpy(ctx + 0x464B, func_0025C2C0(0x0040A330));
    GRef<u8>(ctx + 0x4654) = GRef<u8>(0x00411608);
    GRef<u8>(ctx + 0x4655) = GRef<u8>(0x00411609);
    GRef<u8>(ctx + 0x4656) = GRef<u8>(0x0041160A);
    guest::Mem().Write<u32>(ctx + 0x4659, GRef<u32>(0x00411610));  // unaligned store
    GRef<u32>(ctx + 0x46A4) = 0;
    GRef<u32>(ctx + 0x46AC) = 0;
    GRef<u32>(ctx + 0x46B0) = 0;
    GRef<u32>(ctx + 0x46B4) = 0x20000;
    ee::strcpy(ctx + 0x47BC, ctx + 0x4600);
    ee::strcpy(ctx + 0x47DD, ctx + 0x4642);
    ee::strcpy(ctx + 0x47E6, ctx + 0x464B);
    ee::strcpy(ctx + 0x47EF, ctx + 0x4654);
    ee::strcpy(ctx + 0x47F4, ctx + 0x4659);
    GRef<u32>(ctx + 0x4BEC) = 0x20000;
    GRef<u32>(ctx + 0x4BE4) = 1;
    GRef<u32>(ctx + 0x4BDC) = 0;
    GRef<u32>(ctx + 0x4BE8) = 5;
    GRef<u32>(ctx + 0x48F4) = 0;
    GRef<u32>(ctx + 0x4910) = 5;
    GRef<u32>(ctx + 0x4908) = 0;
    GRef<u8>(ctx + 0x4B8C) = GRef<u8>(0x00411608);
    GRef<u8>(ctx + 0x4B8D) = GRef<u8>(0x00411609);
    GRef<u8>(ctx + 0x4B8E) = GRef<u8>(0x0041160A);
    GRef<u32>(ctx + 0x47FC) = 0;
    GRef<u32>(ctx + 0x4810) = 0;
    GRef<u32>(ctx + 0x4868) = ctx + 0x14;
    {
        const u64 args[2] = {GRef<u32>(ctx + 0x46AC), 0x0040A350};
        ee::sprintf(ctx + 0x14, 0x0040A340 /* "pfs%d:%s" */, args, 2);
    }
    GRef<u32>(ctx + 0x4874) = 0;
    GRef<u32>(ctx + 0x48E0) = ctx + 0x114;
    {
        const u64 args[2] = {GRef<u32>(ctx + 0x46AC), 0x0040A360};
        ee::sprintf(ctx + 0x114, 0x0040A340 /* "pfs%d:%s" */, args, 2);
    }
    GRef<u32>(ctx + 0x48E8) = 0;
    GRef<u8>(ctx + 0x216) = 0xFF;
    GRef<u8>(ctx + 0xF) = 0;
    GRef<u32>(ctx + 0x4D34) = 0;
    for (s32 i = 0; i < 6; ++i) func_0025B0B8(ctx, i, 0, 0, 0);
    return result;
}

// EE: 0x0025ACD0
// HDD read job for one CD request record (unreachable): steps 0 -> 1 -> 2 through the worker.
s32 func_0025ACD0(u32 ctx, u32 rec) {
    func_002598E8(ctx, 0xB);
    s32 result = 0;
    const s32 step = GRef<s32>(ctx);
    bool post = false;
    if (step == 1) {
        post = true;
    } else if (step < 2) {
        if (step == 0) {
            GRef<u32>(ctx + 0x4820) = 0;
            GRef<u32>(ctx + 0x481C) = GRef<u32>(rec + 0x10) << 11;
            GRef<u32>(ctx + 0x4810) = GRef<u32>(rec + 4);
            GRef<u32>(ctx + 0x4808) = GRef<u32>(rec + 0x14) << 11;
            GRef<s32>(ctx) = 1;
            post = true;
        }
    } else if (step == 2) {
        const s32 r = func_0025E248(ctx + 0x360, ctx + 0x47F8, 0x1B);
        if (r != -1) {
            result = r == 1 ? 1 : 0;
        } else {
            result = -2;
        }
    }
    if (post) {
        const s32 r = func_0025E248(ctx + 0x360, ctx + 0x47F8, 0x1A);
        if (r != -1) {
            if (r == 1) GRef<s32>(ctx) = 2;
        } else {
            GRef<s32>(ctx) = 0;
            result = -2;
        }
    }
    if (result != 0) GRef<s32>(ctx) = 0;
    return result;
}

// EE: 0x0025B0B8
void func_0025B0B8(u32 ctx, s32 index, u32 a, u32 b, u32 c) {
    const u32 p = ctx + static_cast<u32>(index) * 0xC + 0x4DA8;
    GRef<u32>(p + 8) = c;
    GRef<u32>(p) = a;
    GRef<u32>(p + 4) = b;
}

// EE: 0x0025BFC0
// Creates the HDD worker semaphore and thread (stack ctx+0x2000, 0x2000 bytes, entry 0x0025BF70).
void func_0025BFC0(u32 worker) {
    guest::GuestFrame frame(0x80);
    const u32 thread = frame.Address(0);  // ThreadParam (0x30 bytes)
    const u32 sema = frame.Address(0x30);  // SemaParam
    GRef<u32>(sema + 4) = 1;
    GRef<u32>(sema + 8) = 0;
    GRef<u32>(sema + 0x14) = 0;
    GRef<s32>(worker + 0x4000) = CreateSema(sema);
    ChangeThreadPriority(GetThreadId(), 2);
    GRef<u32>(thread + 4) = 0x0025BF70;  // entry
    GRef<u32>(thread + 8) = worker + 0x2000;
    GRef<u32>(thread + 0xC) = 0x2000;
    GRef<u32>(thread + 0x10) = 0x004159F0;  // gp
    GRef<u32>(thread + 0x14) = 1;
    StartThread(CreateThread(thread), worker);
    GRef<u32>(worker + 0x4008) = 1;
}

// EE: 0x0025C058
s32 func_0025C058(u32 worker, s32 unit) {
    GRef<s32>(worker + 0x4010) = unit;
    const u64 args[1] = {static_cast<u64>(static_cast<s64>(unit))};
    return ee::sprintf(worker + 0x4014, 0x00411670 /* "hdd%d:" */, args, 1);
}

// EE: 0x0025C090
// The decompiler hides the unit argument: a1 is passed through unchanged from the caller.
s32 func_0025C090(u32 worker, s32 unit) {
    if (GRef<u32>(0x00411678) != 0) return -1;
    GRef<u32>(worker + 0x4008) = 0;
    func_0025C058(worker, unit);
    func_0025C198(worker);
    func_0025BFC0(worker);
    GRef<u32>(0x00411678) = 1;
    return 1;
}

// EE: 0x0025C198
// Creates the power-off semaphore and thread (entry 0x0025C108) and registers the CD power-off
// callback 0x0025C178.
void func_0025C198(u32 worker) {
    guest::GuestFrame frame(0x90);
    const u32 thread = frame.Address(0);
    const u32 sema = frame.Address(0x30);
    GRef<u32>(sema + 4) = 1;
    GRef<u32>(sema + 8) = 0;
    GRef<u32>(sema + 0x14) = 0;
    const s32 s = CreateSema(sema);
    ChangeThreadPriority(GetThreadId(), 2);
    GRef<u32>(thread + 0x14) = 1;
    GRef<u32>(thread + 0xC) = 0x2000;
    GRef<u32>(thread + 0x10) = 0x004159F0;
    GRef<u32>(thread + 4) = 0x0025C108;
    GRef<u32>(thread + 8) = worker;
    StartThread(CreateThread(thread), static_cast<u32>(s));
    sceCdPOffCallback(0x0025C178, static_cast<u32>(s));
}

// EE: 0x0025C240
// Builds the partition path for an HDD request: "<dev><id>,<pw>,..." or "<dev><name>".
s32 func_0025C240(u32 worker, u32 req) {
    if (GRef<u32>(req + 0xC) == 0) {
        const u64 args[6] = {worker + 0x4014, req + 0x234, req + 0x276, req + 0x27F, req + 0x288, req + 0x28D};
        return ee::sprintf(req + 0x34, 0x0040A8E0 /* "%s%s,%s,%s,%s,%s" */, args, 6);
    }
    const u64 args[2] = {worker + 0x4014, GRef<u32>(0x003F6A60 + 4u * GRef<u32>(req + 0xC))};
    return ee::sprintf(req + 0x34, 0x00411690 /* "%s%s" */, args, 2);
}

// EE: 0x0025C2C0
// Decodes an 8-byte obfuscated string (bitwise NOT) into the buffer 0x00411898.
u32 func_0025C2C0(u32 src) {
    for (u32 i = 0; i < 8; ++i) GRef<u8>(0x00411898 + i) = static_cast<u8>(~GRef<u8>(src + i));
    return 0x00411898;
}

// EE: 0x0025E1D0
void func_0025E1D0(u32 job, s32 state) {
    if (GRef<s32>(job + 0x4040) != state) {
        GRef<s32>(job + 0x4040) = state;
        GRef<u32>(job + 0x4044) = 0;
    }
}

// EE: 0x0025E1F0
// Worker request state: 1 when idle and the last job is done, -1 while busy, 0 when not started.
s32 func_0025E1F0(u32 job) {
    s32 r = 0;
    if (GRef<s32>(job + 0x4008) == 1) {
        r = -1;
        if (GRef<s32>(job + 0x4004) == 0) r = 1;
    }
    return r;
}

// EE: 0x0025E218
void func_0025E218(u32 job) {
    func_0025C090(job, 0);
    GRef<u32>(job + 0x4044) = 0;
    GRef<u32>(job + 0x4040) = 0;
}

// EE: 0x0025E248
// Posts a job (op) to the HDD worker and polls its completion.
s32 func_0025E248(u32 job, u32 request, u32 op) {
    func_0025E1D0(job, 2);
    s32 r = 0;
    if (GRef<s32>(job + 0x4044) == 0) {
        if (GRef<s32>(job + 0x4008) == 1) {
            GRef<u32>(request) = op;
            GRef<u32>(request + 4) = 0;
            GRef<u32>(job + 0x400C) = request;
            GRef<u32>(job + 0x4004) = 0;
            SignalSema(GRef<s32>(job + 0x4000));
            GRef<u32>(job + 0x4044) = 1;
        } else {
            r = -1;
        }
    } else if (GRef<s32>(job + 0x4044) == 1) {
        GRef<u32>(job + 0x4004) = GRef<u32>(request + 4);
        r = func_0025E1F0(job);
    }
    if (r != 0) {
        GRef<u32>(job + 0x4044) = 0;
        GRef<u32>(job + 0x4040) = 1;
    }
    return r;
}

// EE: 0x002616B8
// HYPOTHESIS: EnterHddErrorScene. Saves the current scene and switches to scene 7 (HDD read error).
void EnterHddErrorScene() {
    MainContext* ctx = Ctx();
    const u16 disp_h = ctx->disp_h;
    GRef<u32>(GRef<u32>(0x00411718)) = ctx->scene;
    if (disp_h > 0x100) ctx->disp_flags = 0x4200;
    RequestScene(7);
}

// ---- HDD save operations (slice S2b; reached only from the save page's HDD modes) --------------
// Context fields used here: +0 step, +4 operation, +0x360 worker job, +0x48E8 registered-file mask,
// +0x4904 request C (+0x491C argument), +0x4BD8 request A (mount/unmount, +0x4BE0 flag,
// +0x4BE4 pfs unit), +0x4D30 request B (+0x4D34 error, +0x4D40 size, +0x4D48 buffer, +0x4D4C open
// mode, +0x4D50 u16 permissions, +0x4D60 mode word), +0x4DA0 path, +0x4DA8 file table
// (6 x {path, buffer, size}).

namespace {

constexpr u32 kHddPath = 0x003F61E0;   // path buffer
constexpr u32 kHddSaveDir = 0x0040A440;  // "/Your Saves/BISLUS-20152AC04"
constexpr u32 kFmtPfs = 0x0040A340;     // "pfs%d:%s"
constexpr u32 kFmtPfsFile = 0x0040A430; // "pfs%d:%s/%s"

AC04_GUEST_VAR(s32, g_HddDirFailed, 0x00411628)
AC04_GUEST_VAR(u32, g_HddDirError, 0x0041162C)
AC04_GUEST_VAR(s32, g_HddSaveFailed, 0x00411630)
AC04_GUEST_VAR(s32, g_HddSaveFile, 0x00411634)
AC04_GUEST_VAR(u32, g_HddSaveError, 0x00411638)
AC04_GUEST_VAR(s32, g_HddSaveRetry, 0x0041163C)
AC04_GUEST_VAR(s32, g_HddSaveResume, 0x00411890)
AC04_GUEST_VAR(s32, g_HddDeleteFile, 0x00411640)
AC04_GUEST_VAR(s32, g_HddWriteFailed, 0x00411644)
AC04_GUEST_VAR(u32, g_HddWriteError, 0x00411648)
AC04_GUEST_VAR(s32, g_HddReadFailed, 0x0041164C)
AC04_GUEST_VAR(u32, g_HddReadError, 0x00411650)

s32& HStep(u32 ctx) { return GRef<s32>(ctx); }

// The registered files other than the main file (index 5) must all have a path.
bool HddFilesMissingPath(u32 ctx) {
    const u32 mask = GRef<u32>(ctx + 0x48E8);
    for (u32 i = 0; i < 6; ++i) {
        if (((mask >> i) & 1) != 0 && i != 5 && GRef<u32>(ctx + 0x4DA8 + i * 0xC) == 0) return true;
    }
    return false;
}

void FormatHddPath(u32 ctx, u32 name) {
    const u64 args[3] = {GRef<u32>(ctx + 0x4BE4), kHddSaveDir, name};
    ee::sprintf(kHddPath, name == 0 ? kFmtPfs : kFmtPfsFile, args, name == 0 ? 2 : 3);
}

}  // namespace

// EE: 0x002597E8
// HYPOTHESIS: stores the HDD error code (+0x4364, read back by func_002597C8).
void func_002597E8(u32 error) { GRef<u32>(func_002597B8() + 0x4364) = error; }

// EE: 0x00259878
u16 func_00259878() { return GRef<u16>(func_002597B8() + 0x4394); }

// EE: 0x00259898
u16 func_00259898() { return GRef<u16>(func_002597B8() + 0x4396); }

// EE: 0x00259C90
u32 func_00259C90(u32 ctx) { return GRef<u32>(ctx + 0x4390); }

// EE: 0x00259CA0
// HDD mode for saves (+0x4398; 3 on the retail path, written by the boot scene).
u8 GetHddSaveMode(u32 ctx) { return GRef<u8>(ctx + 0x4398); }

// EE: 0x0025AEE0
// HYPOTHESIS: operation 0xD, checks the HDD save directory: mount (job 0x12), open the directory
// "pfs%d:/Your Saves/BISLUS-20152AC04/BISLUS-20152AC04" (job 0x16; +0x216 = found), unmount (0x13).
s32 func_0025AEE0(u32 ctx) {
    s32 ret = 0;
    func_002598E8(ctx, 0xD);
    switch (HStep(ctx)) {
        case 0:
        case 1: {
            if (HStep(ctx) == 0) {
                const u64 args[3] = {GRef<u32>(ctx + 0x4BE4), kHddSaveDir, 0x0040A460};
                ee::sprintf(kHddPath, kFmtPfsFile, args, 3);
                g_HddDirFailed() = 0;
                g_HddDirError() = 0;
                GRef<u32>(ctx + 0x4DA0) = kHddPath;
                HStep(ctx) = 1;
            }
            GRef<u32>(ctx + 0x4BE0) = 1;
            const s32 r = func_0025E248(ctx + 0x360, ctx + 0x4BD8, 0x12);
            if (r == 1) {
                HStep(ctx) = 2;
            } else {
                ret = r == -1 ? -1 : 0;
            }
            break;
        }
        case 2: {
            const s32 r = func_0025E248(ctx + 0x360, ctx + 0x4D30, 0x16);
            if (r == 1) {
                GRef<u8>(ctx + 0x216) = 1;
                HStep(ctx) = 3;
            } else if (r == -1) {
                if (func_002597C8() == 2) {
                    GRef<u8>(ctx + 0x216) = 0;
                    HStep(ctx) = 3;
                } else {
                    HStep(ctx) = 3;
                    g_HddDirError() = func_002597C8();
                    g_HddDirFailed() = -1;
                }
            }
            break;
        }
        case 3: {
            const s32 r = func_0025E248(ctx + 0x360, ctx + 0x4BD8, 0x13);
            if (r == 1) {
                if (g_HddDirFailed() == -1) {
                    ret = -1;
                    func_002597E8(g_HddDirError());
                } else {
                    HStep(ctx) = 4;
                }
            } else if (r == -1) {
                ret = -1;
                if (g_HddDirFailed() == -1) func_002597E8(g_HddDirError());
            }
            break;
        }
        case 4:
            ret = 1;
            break;
        default:
            break;
    }
    if (ret != 0) HStep(ctx) = 0;
    return ret;
}

// EE: 0x0025B0D8
// HYPOTHESIS: operation 0xE, HDD save: mount, create the save directory (jobs 0x25, 0x22, 0x17),
// then each registered file: open (0x18, mode 0x203), write (0x1C), close (0x19), set its mode
// (0x17, 0x8497); unmount. Error 0x1C (HYPOTHESIS: disk full) unmounts and retries through the
// extension jobs 5/1/6 on request C.
s32 func_0025B0D8(u32 ctx) {
    s32 ret = 0;
    const u32 job = ctx + 0x360;
    const u32 req_a = ctx + 0x4BD8;
    const u32 req_b = ctx + 0x4D30;
    const u32 req_c = ctx + 0x4904;
    const u32 files = ctx + 0x4DA8;
    func_002598E8(ctx, 0xE);
    auto fail_to = [&](s32 step, s32 r) {  // records the error and goes to `step`
        HStep(ctx) = step;
        g_HddSaveError() = func_002597C8();
        g_HddSaveFailed() = r;
    };
    s32 step = HStep(ctx);
    if (!(static_cast<u32>(step) < 0xF)) return 0;
    switch (step) {
        case 0:
            if (HddFilesMissingPath(ctx)) ret = -1;
            g_HddSaveFile() = 0;
            g_HddSaveFailed() = 0;
            g_HddSaveError() = 0;
            g_HddSaveRetry() = 0;
            g_HddSaveResume() = 0;
            FormatHddPath(ctx, 0);
            GRef<u16>(ctx + 0x4D50) = 0x1FF;
            GRef<u32>(ctx + 0x4DA0) = kHddPath;
            HStep(ctx) = 1;
            break;
        case 1: {
            GRef<u32>(ctx + 0x4BE0) = 0;
            const s32 r = func_0025E248(job, req_a, 0x12);
            if (r == 1) {
                if (g_HddSaveRetry() == 0) {
                    HStep(ctx) = 2;
                } else {
                    g_HddSaveRetry() = 0;
                    HStep(ctx) = g_HddSaveResume();
                }
            } else if (r == -1) {
                g_HddSaveRetry() = 0;
                ret = -1;
            }
            break;
        }
        case 2: {
            const s32 r = func_0025E248(job, req_a, 0x25);
            if (r == 1) {
                HStep(ctx) = 3;
            } else if (r == -1) {
                fail_to(10, -1);
            }
            break;
        }
        case 3: {
            const s32 r = func_0025E248(job, req_b, 0x22);
            if (r == 1) {
                HStep(ctx) = 4;
            } else if (r == -1) {
                if (GRef<s32>(ctx + 0x4D34) == 0x1C) {
                    g_HddSaveResume() = HStep(ctx);
                    g_HddSaveRetry() = 1;
                    HStep(ctx) = 10;
                } else {
                    fail_to(10, -1);
                }
            }
            break;
        }
        case 4: {
            GRef<u32>(ctx + 0x4D60) = 0xC4A7;
            const s32 r = func_0025E248(job, req_b, 0x17);
            if (r == 1) {
                HStep(ctx) = 5;
            } else if (r == -1) {
                fail_to(10, -1);
            }
            break;
        }
        case 5:
        case 6: {
            if (step == 5) {
                bool found = false;
                do {
                    const s32 next = g_HddSaveFile() + 1;
                    if (((GRef<u32>(ctx + 0x48E8) >> (static_cast<u32>(g_HddSaveFile()) & 0x1F)) & 1) != 0) {
                        found = true;
                        break;
                    }
                    g_HddSaveFile() = next;
                } while (g_HddSaveFile() < 6);
                if (!found) HStep(ctx) = 10;
                if (HStep(ctx) == 10) break;
                const u32 e = files + static_cast<u32>(g_HddSaveFile()) * 0xC;
                FormatHddPath(ctx, GRef<u32>(e));
                GRef<u32>(ctx + 0x4D4C) = 0x203;
                GRef<u16>(ctx + 0x4D50) = 0x1B6;
                GRef<u32>(ctx + 0x4DA0) = kHddPath;
                const u32 buf = GRef<u32>(e + 4);
                GRef<u32>(ctx + 0x4D40) = GRef<u32>(e + 8);
                GRef<u32>(ctx + 0x4D48) = buf;
                HStep(ctx) = 6;
            }
            const s32 r = func_0025E248(job, req_b, 0x18);
            if (r == 1) {
                HStep(ctx) = 7;
            } else if (r == -1) {
                if (GRef<s32>(ctx + 0x4D34) == 0x1C) {
                    g_HddSaveRetry() = 1;
                    g_HddSaveResume() = 5;
                    HStep(ctx) = 10;
                } else {
                    g_HddSaveRetry() = 0;
                    fail_to(10, -1);
                }
            }
            break;
        }
        case 7: {
            const s32 r = func_0025E248(job, req_b, 0x1C);
            if (r == 1) {
                HStep(ctx) = 8;
            } else if (r == -1) {
                if (GRef<s32>(ctx + 0x4D34) != 0x1C) {
                    g_HddSaveRetry() = 0;
                    fail_to(8, -1);
                } else {
                    g_HddSaveRetry() = 1;
                    g_HddSaveResume() = 5;
                    HStep(ctx) = 8;
                }
            }
            break;
        }
        case 8: {
            const s32 r = func_0025E248(job, req_b, 0x19);
            if (r == 1) {
                if (g_HddSaveFailed() != -1 && g_HddSaveRetry() == 0) {
                    HStep(ctx) = 9;
                    break;
                }
                HStep(ctx) = 10;
            } else if (r == -1) {
                if (g_HddSaveFailed() != -1) {
                    g_HddSaveFailed() = -1;
                    g_HddSaveError() = func_002597C8();
                }
                g_HddSaveRetry() = 0;
                HStep(ctx) = 10;
            }
            break;
        }
        case 9: {
            GRef<u32>(ctx + 0x4D60) = 0x8497;
            const s32 r = func_0025E248(job, req_b, 0x17);
            if (r == 1) {
                g_HddSaveFile() = g_HddSaveFile() + 1;
                HStep(ctx) = g_HddSaveFile() < 6 ? 5 : 10;
            } else if (r == -1) {
                g_HddSaveFailed() = -1;
                g_HddSaveError() = func_002597C8();
                HStep(ctx) = 10;
            }
            break;
        }
        case 10: {
            const s32 r = func_0025E248(job, req_a, 0x13);
            if (r != 1) {
                if (r == -1) {
                    ret = -1;
                    if (g_HddSaveFailed() == -1) func_002597E8(g_HddSaveError());
                }
            } else if (g_HddSaveRetry() != 0) {
                g_HddSaveFailed() = 0;
                HStep(ctx) = 0xB;
            } else if (g_HddSaveFailed() != -1) {
                HStep(ctx) = 0xE;
            } else {
                ret = -1;
                func_002597E8(g_HddSaveError());
            }
            break;
        }
        case 0xB: {
            GRef<u32>(ctx + 0x491C) = 3;
            const s32 r = func_0025E248(job, req_c, 5);
            if (r == 1) {
                HStep(ctx) = 0xC;
            } else {
                ret = r == -1 ? -1 : 0;
            }
            break;
        }
        case 0xC: {
            const s32 r = func_0025E248(job, req_c, 1);
            if (r == 1) {
                HStep(ctx) = 0xD;
            } else if (r == -1) {
                fail_to(0xD, -1);
            }
            break;
        }
        case 0xD: {
            const s32 r = func_0025E248(job, req_c, 6);
            if (r != 1) {
                ret = r == -1 ? -1 : 0;
            } else if (g_HddSaveFailed() != -1) {
                HStep(ctx) = 1;
            } else {
                ret = -1;
                func_002597E8(g_HddSaveError());
            }
            break;
        }
        case 0xE:
            ret = 1;
            break;
        default:
            break;
    }
    if (ret != 0) HStep(ctx) = 0;
    return ret;
}

// EE: 0x0025B6C8
// HYPOTHESIS: operation 0xF, HDD delete: mount, remove each registered file (job 0x1D), remove the
// directory (job 0x23), unmount (prints "unmount").
s32 func_0025B6C8(u32 ctx) {
    s32 ret = 0;
    func_002598E8(ctx, 0xF);
    s32 step = HStep(ctx);
    if (!(static_cast<u32>(step) < 7)) return 0;
    switch (step) {
        case 0:
            if (HddFilesMissingPath(ctx)) ret = -1;
            g_HddDeleteFile() = 0;
            HStep(ctx) = 1;
            break;
        case 1: {
            GRef<u32>(ctx + 0x4BE0) = 0;
            const s32 r = func_0025E248(ctx + 0x360, ctx + 0x4BD8, 0x12);
            if (r == 1) {
                HStep(ctx) = 2;
            } else {
                ret = r == -1 ? -1 : 0;
            }
            break;
        }
        case 2:
        case 3: {
            if (step == 2) {
                bool found = false;
                do {
                    if (((GRef<u32>(ctx + 0x48E8) >> (static_cast<u32>(g_HddDeleteFile()) & 0x1F)) & 1) != 0) {
                        found = true;
                        break;
                    }
                    g_HddDeleteFile() = g_HddDeleteFile() + 1;
                } while (g_HddDeleteFile() < 6);
                if (!found) HStep(ctx) = 4;
                if (HStep(ctx) != 2) break;
                FormatHddPath(ctx, GRef<u32>(ctx + 0x4DA8 + static_cast<u32>(g_HddDeleteFile()) * 0xC));
                GRef<u32>(ctx + 0x4D4C) = 0x203;
                GRef<u32>(ctx + 0x4DA0) = kHddPath;
                HStep(ctx) = 3;
            }
            if (func_0025E248(ctx + 0x360, ctx + 0x4D30, 0x1D) != 0) {
                g_HddDeleteFile() = g_HddDeleteFile() + 1;
                HStep(ctx) = 2;
            }
            break;
        }
        case 4:
            FormatHddPath(ctx, 0);
            if (func_0025E248(ctx + 0x360, ctx + 0x4D30, 0x23) != 0) HStep(ctx) = 5;
            break;
        case 5: {
            const s32 r = func_0025E248(ctx + 0x360, ctx + 0x4BD8, 0x13);
            if (r == 1) {
                ee::printf(0x0040A4E0 /* "unmount\n" */, nullptr, 0);
                HStep(ctx) = 6;
            } else {
                ret = r == -1 ? -1 : 0;
            }
            break;
        }
        case 6:
            ret = 1;
            break;
        default:
            break;
    }
    if (ret != 0) HStep(ctx) = 0;
    return ret;
}

namespace {

// Shared body of the single-file HDD operations 0x10 (write) and 0x11 (read): mount, open the
// file (job 0x18), transfer (`xfer_job`), close (0x19), unmount.
s32 HddFileOp(u32 ctx, s32 index, s32 op, u32 mode, bool read, s32 xfer_job, s32& failed, u32& error) {
    s32 ret = 0;
    const u32 req_b = ctx + 0x4D30;
    func_002598E8(ctx, op);
    s32 step = HStep(ctx);
    if (!(static_cast<u32>(step) < 7)) return 0;
    auto fail_to = [&](s32 s, s32 r) {
        HStep(ctx) = s;
        error = func_002597C8();
        failed = r;
    };
    switch (step) {
        case 0:
        case 1: {
            if (step == 0) {
                const u32 e = ctx + 0x4DA8 + static_cast<u32>(index) * 0xC;
                FormatHddPath(ctx, GRef<u32>(e));
                const u32 buf = GRef<u32>(e + 4);
                GRef<u32>(ctx + 0x4D4C) = mode;
                GRef<u32>(ctx + 0x4DA0) = kHddPath;
                const u32 size = GRef<u32>(e + 8);
                if (!read) GRef<u16>(ctx + 0x4D50) = 0x180;
                GRef<u32>(ctx + 0x4D40) = size;
                HStep(ctx) = 1;
                GRef<u32>(ctx + 0x4D48) = buf;
                failed = 0;
                error = 0;
            }
            GRef<u32>(ctx + 0x4BE0) = read ? 1 : 0;
            const s32 r = func_0025E248(ctx + 0x360, ctx + 0x4BD8, 0x12);
            if (r == 1) {
                HStep(ctx) = 2;
            } else {
                ret = r == -1 ? -1 : 0;
            }
            break;
        }
        case 2: {
            const s32 r = func_0025E248(ctx + 0x360, req_b, 0x18);
            if (r == 1) {
                HStep(ctx) = 3;
            } else if (r == -1) {
                fail_to(5, -1);
            }
            break;
        }
        case 3: {
            const s32 r = func_0025E248(ctx + 0x360, req_b, static_cast<u32>(xfer_job));
            if (r == 1) {
                HStep(ctx) = 4;
            } else if (r == -1) {
                fail_to(4, -1);
            }
            break;
        }
        case 4: {
            const s32 r = func_0025E248(ctx + 0x360, req_b, 0x19);
            if (r != 1) {
                if (r != -1) break;
                if (failed != -1) {
                    error = func_002597C8();
                    failed = -1;
                }
            }
            HStep(ctx) = 5;
            break;
        }
        case 5: {
            const s32 r = func_0025E248(ctx + 0x360, ctx + 0x4BD8, 0x13);
            if (r == 1) {
                if (failed == -1) {
                    ret = -1;
                    func_002597E8(error);
                } else {
                    HStep(ctx) = 6;
                }
            } else if (r == -1) {
                ret = -1;
                if (failed == -1) func_002597E8(error);
            }
            break;
        }
        case 6:
            ret = 1;
            break;
        default:
            break;
    }
    if (ret != 0) HStep(ctx) = 0;
    return ret;
}

}  // namespace

// EE: 0x0025B948
// HYPOTHESIS: operation 0x10, writes registered file `index` to the HDD (open mode 3, permissions
// 0x180, job 0x1C).
s32 func_0025B948(u32 ctx, s32 index) {
    return HddFileOp(ctx, index, 0x10, 3, false, 0x1C, g_HddWriteFailed(), g_HddWriteError());
}

// EE: 0x0025BBD0
// HYPOTHESIS: operation 0x11, reads registered file `index` from the HDD (open mode 1, job 0x1B).
s32 func_0025BBD0(u32 ctx, s32 index) {
    return HddFileOp(ctx, index, 0x11, 1, true, 0x1B, g_HddReadFailed(), g_HddReadError());
}

// EE: 0x0025BED8
// HYPOTHESIS: job 0x26 on request A (free space query); 1 done, -1 failed, 0 running.
s32 func_0025BED8(u32 ctx) {
    const s32 r = func_0025E248(ctx + 0x360, ctx + 0x4BD8, 0x26);
    if (r == 1) return 1;
    return r == -1 ? -1 : 0;
}

}  // namespace ac04::game
