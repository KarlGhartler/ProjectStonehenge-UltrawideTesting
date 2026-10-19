// HDD module (docs/subsystems/save.md). Unreachable in this build: its init chain starts at
// func_00259910, which has no caller, and the HDD flag that routes DATA.CDP reads here is never set.
// Translated because it is reached by direct calls from the boot path (CLAUDE.md: all game code is
// kept). The worker and power-off thread entry points (0x0025BF70, 0x0025C108) are reached only
// through CreateThread, which the port stubs (dead-code.md); they are translated with the full
// HDD module later.
#include "game/game.h"

#include "common/ee_math.h"
#include "common/guest_stack.h"
#include "game/flight_common.h"

#include "sce/sce.h"

namespace ac04::game {

using guest::GRef;
using namespace fl;

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

// ---- HDD module: context accessors, setup steps, DATA.CDP install, worker (slice S8) -----------
// All dead in this build (see the file comment). Install state block 0x003F62E0: +0 CD ticket, +4
// bytes copied, +8 step saved while paused, +0xC pause request (1 asked, 2 paused), +0x10 cancel.

namespace {

constexpr u32 kInstall = 0x003F62E0;
AC04_GUEST_VAR(s32, g_HddInstallFailed, 0x00411614)  // gp-0x43DC
AC04_GUEST_VAR(u32, g_HddInstallError, 0x00411618)   // gp-0x43D8
AC04_GUEST_VAR(s32, g_HddOpFailed, 0x0041161C)       // gp-0x43D4
AC04_GUEST_VAR(u32, g_HddOpError, 0x00411620)        // gp-0x43D0
AC04_GUEST_VAR(u32, g_HddMountError, 0x00411624)     // gp-0x43CC

// Worker job result as the steps use it: 1 done, -1 failed, 0 still running.
s32 JobResult(s32 r) {
    if (r == 1) return 1;
    return r == -1 ? -1 : 0;
}

// Result of a worker op that only stores the error: 0 -> 1 (error cleared), else -1 (error = -r).
s32 OpResult(u32 req, s32 r) {
    if (r == 0) {
        GRef<u32>(req + 4) = 0;
        return 1;
    }
    GRef<s32>(req + 4) = -r;
    return -1;
}

// The same for results where any value >= 0 succeeds.
s32 OpResultNonNegative(u32 req, s32 r) {
    if (r >= 0) {
        GRef<u32>(req + 4) = 0;
        return 1;
    }
    GRef<s32>(req + 4) = -r;
    return -1;
}

u32 InstallWord(u32 off) { return GRef<u32>(kInstall + off); }

}  // namespace

// EE: 0x00259810
void func_00259810(s8 v) { GRef<s8>(func_002597B8() + 0x214) = v; }

// EE: 0x00259838
s8 func_00259838() { return GRef<s8>(func_002597B8() + 0x214); }

// EE: 0x00259858
// HDD present / partition mounted (+0x215).
s8 func_00259858() { return GRef<s8>(func_002597B8() + 0x215); }

// EE: 0x002598B8
// Step word of the HDD context.
u32 func_002598B8() { return GRef<u32>(func_002597B8()); }

// EE: 0x002598D8
void func_002598D8(u32 p) {
    GRef<u32>(p) = 0;
    GRef<u32>(p + 4) = 0;
}

// EE: 0x00259C38
u32 func_00259C38(u32 ctx) { return GRef<u32>(ctx + 0x437C); }

// EE: 0x00259C40
u32 func_00259C40(u32 ctx) { return GRef<u32>(ctx + 0x4380); }

// EE: 0x00259C48
// Free space estimate: +0x437C - (+0x4384 + +0x4388), at least +0x438C, at most +0x4380.
s32 func_00259C48(u32 ctx) {
    s32 v = GRef<s32>(ctx + 0x437C) - (GRef<s32>(ctx + 0x4384) + GRef<s32>(ctx + 0x4388));
    const s32 lo = GRef<s32>(ctx + 0x438C);
    const s32 hi = GRef<s32>(ctx + 0x4380);
    if (!(lo < v)) v = lo;
    if (hi < v) v = hi;
    return v;
}

// EE: 0x00259C78
// Remaining bytes: +0x437C - +0x4384.
s32 func_00259C78(u32 ctx) { return GRef<s32>(ctx + 0x437C) - GRef<s32>(ctx + 0x4384); }

// EE: 0x00259C88
u32 func_00259C88(u32 ctx) { return GRef<u32>(ctx + 0x4384); }

// EE: 0x00259CA8
// Operation 1: worker op 0xF (HDD status) on request +0x43B0.
s32 func_00259CA8(u32 ctx) {
    func_002598E8(ctx, 1);
    const s32 r = JobResult(func_0025E248(ctx + 0x360, ctx + 0x43B0, 0xF));
    if (r != 0) GRef<u32>(ctx) = 0;
    return r;
}

// EE: 0x00259D10
// Operation 2: worker op 0x11 (directory size totals) on request +0x43B0.
s32 func_00259D10(u32 ctx) {
    func_002598E8(ctx, 2);
    const s32 r = JobResult(func_0025E248(ctx + 0x360, ctx + 0x43B0, 0x11));
    if (r != 0) GRef<u32>(ctx) = 0;
    return r;
}

// EE: 0x00259D78
// Operation 3: worker op 7 (look for the partition) on request +0x43CC; +0x215 = found (cleared
// when the op fails).
s32 func_00259D78(u32 ctx) {
    func_002598E8(ctx, 3);
    const u32 req = ctx + 0x43CC;
    s32 ret = 0;
    s32 step = GRef<s32>(ctx);
    if (step == 0) {
        GRef<s32>(ctx) = 1;
        step = 1;
    }
    if (step == 1) {
        const s32 r = func_0025E248(ctx + 0x360, req, 7);
        if (r == -1) {
            GRef<u8>(ctx + 0x215) = 0;
            ret = -1;
        } else if (r == 1) {
            GRef<s32>(ctx) = 2;
            GRef<u8>(ctx + 0x215) = GRef<u8>(req + 0x10);
        }
    } else if (step == 2) {
        GRef<s32>(ctx) = 0;
        ret = 1;
    }
    if (ret != 0) GRef<s32>(ctx) = 0;
    return ret;
}

// EE: 0x00259E48
// Operation 4: worker op 8 (pfs status) on request +0x46A0.
s32 func_00259E48(u32 ctx) {
    func_002598E8(ctx, 4);
    s32 ret = 0;
    s32 step = GRef<s32>(ctx);
    if (step == 0) {
        GRef<s32>(ctx) = 1;
        step = 1;
    }
    if (step == 1) {
        const s32 r = func_0025E248(ctx + 0x360, ctx + 0x46A0, 8);
        if (r == -1)
            ret = -1;
        else if (r == 1)
            GRef<s32>(ctx) = 2;
    } else if (step == 2) {
        GRef<s32>(ctx) = 0;
        ret = 1;
    }
    if (ret != 0) GRef<s32>(ctx) = 0;
    return ret;
}

// EE: 0x00259F08
// Operation 5 (before an install or uninstall): close the install file (op 0x19), unmount (op 0x13),
// remove the partition (op 3); each step goes on also when its op fails; then +0x215 and +0xE
// cleared.
s32 func_00259F08(u32 ctx) {
    func_002598E8(ctx, 5);
    s32 ret = 0;
    switch (GRef<u32>(ctx)) {
        case 0:
            GRef<s32>(ctx) = 1;
            [[fallthrough]];
        case 1: {
            const s32 r = func_0025E248(ctx + 0x360, ctx + 0x47F8, 0x19);
            if (r == 1 || r == -1) GRef<s32>(ctx) = 2;
            break;
        }
        case 2: {
            const s32 r = func_0025E248(ctx + 0x360, ctx + 0x46A0, 0x13);
            if (r == 1 || r == -1) GRef<s32>(ctx) = 3;
            break;
        }
        case 3: {
            const s32 r = func_0025E248(ctx + 0x360, ctx + 0x43CC, 3);
            if (r == 1 || r == -1) GRef<s32>(ctx) = 4;
            break;
        }
        case 4:
            GRef<u8>(ctx + 0x215) = 0;
            ret = 1;
            GRef<u8>(ctx + 0xE) = 0;
            break;
        default:
            break;
    }
    if (ret != 0) GRef<s32>(ctx) = 0;
    return ret;
}

// EE: 0x0025A048
// Install progress (bytes copied, 0x003F62E4).
u32 func_0025A048() { return GRef<u32>(kInstall + 4); }

// EE: 0x0025A058
// Install pause request (0x003F62EC).
void func_0025A058(u32 v) { GRef<u32>(kInstall + 0xC) = v; }

// EE: 0x0025A068
// Cancels the install: aborts the CD queue and sets the cancel flag (0x003F62F0).
void func_0025A068() {
    func_00214D80();
    GRef<u32>(kInstall + 0x10) = 1;
}

// EE: 0x0025A090
// HYPOTHESIS: HddInstallDataCdp. Operation 6, copies DATA.CDP (0x2B031000 bytes) to the HDD
// partition: create the partition (op 2), format it (op 0x14), write DATA entry 0x167 (loaded into
// `buf` of `size` bytes) into the partition file (ops 5, 0xD, 6), mount (0x12), create the data
// file (0x18) and size it (0x1F), then copy the disc file through `buf` (CD queue 1, writes 0x1C)
// until 0x2B031000 bytes are done, close (0x19), create the second file (0x18 / 0x19), sync (0x15)
// and unmount (0x13); +0x215 = 1 at the end. A pause request (0x003F62EC = 1) holds it in step 0x16;
// the cancel flag ends it with -2 at the next check. Errors are kept in 0x00411614 / 0x00411618.
s32 HddInstallDataCdp(u32 ctx, u32 buf, s32 size) {
    func_002598E8(ctx, 6);
    const u32 part = ctx + 0x43CC;
    const u32 pfs = ctx + 0x46A0;
    const u32 file = ctx + 0x47F8;
    const u32 file2 = ctx + 0x4870;
    const u32 job = ctx + 0x360;
    s32 ret = 0;
    const auto cancelled = [] { return InstallWord(0x10) != 0; };
    const auto fail = [&](s32 r) {
        g_HddInstallFailed() = r;
        g_HddInstallError() = func_002597C8();
    };
    switch (GRef<u32>(ctx)) {
        case 0:
            if (GRef<s8>(ctx + 0x215) != 0)
                ret = -1;
            else
                GRef<s32>(ctx) = 2;
            GRef<s32>(kInstall) = -1;
            GRef<u32>(kInstall + 0x10) = 0;
            GRef<u32>(kInstall + 4) = 0;
            GRef<u32>(kInstall + 8) = 0;
            g_HddInstallFailed() = 0;
            g_HddInstallError() = 0;
            break;
        case 2: {
            const s32 r = func_0025E248(job, part, 2);
            if (r != 1) {
                ret = r == -1 ? -1 : 0;
            } else if (cancelled()) {
                ret = -2;
            } else {
                GRef<s32>(ctx) = 3;
            }
            break;
        }
        case 3: {
            const s32 r = func_0025E248(job, pfs, 0x14);
            if (r != 1) {
                ret = r == -1 ? -1 : 0;
            } else if (cancelled()) {
                ret = -2;
            } else {
                GRef<s32>(ctx) = 4;
            }
            break;
        }
        case 4: {
            GRef<u32>(part + 0x18) = 3;
            const s32 r = func_0025E248(job, part, 5);
            if (r != 1)
                ret = r == -1 ? -1 : 0;
            else
                GRef<s32>(ctx) = cancelled() ? 8 : 5;
            break;
        }
        case 5: {
            const s32 n = static_cast<s32>(GetTblEntrySize(0, 0x167));
            if (cancelled()) {
                GRef<s32>(ctx) = 8;
            } else if (size < n) {
                g_HddInstallFailed() = -1;
                GRef<s32>(ctx) = 8;
            } else {
                LoadDataEntryAsync(0x167, buf);
                GRef<s32>(ctx) = 6;
            }
            break;
        }
        case 6:
            if (IsDataLoadDone() == 0) break;
            GRef<s32>(ctx) = cancelled() ? 8 : 7;
            break;
        case 7: {
            GRef<u32>(part + 0x1C) = buf + GRef<u32>(buf + 4);
            const s32 n = static_cast<s32>(GetTblEntrySize(0, 0x167));
            GRef<s32>(part + 0x20) = n;
            if ((n & 0x1FF) != 0) GRef<s32>(part + 0x20) = (n / 0x200 + 1) << 9;
            const s32 r = func_0025E248(job, part, 0xD);
            if (r != 0 && cancelled()) {
                GRef<s32>(ctx) = 8;
            } else if (r == 1) {
                GRef<s32>(ctx) = 8;
            } else if (r == -1) {
                fail(r);
                GRef<s32>(ctx) = 8;
            }
            break;
        }
        case 8: {
            const s32 r = func_0025E248(job, part, 6);
            if (r != 0 && cancelled()) {
                ret = -2;
            } else if (r != 1) {
                ret = r == -1 ? -1 : 0;
            } else if (g_HddInstallFailed() == -1) {
                func_002597E8(g_HddInstallError());
                ret = -1;
            } else {
                GRef<s32>(ctx) = 9;
            }
            break;
        }
        case 9: {
            GRef<u32>(pfs + 8) = 0;
            const s32 r = func_0025E248(job, pfs, 0x12);
            if (r != 1)
                ret = r == -1 ? -1 : 0;
            else
                GRef<s32>(ctx) = cancelled() ? 0x13 : 0xA;
            break;
        }
        case 0xA: {
            GRef<u16>(file + 0x20) = 0x1B6;
            GRef<u32>(file + 0x1C) = 0x203;
            const s32 r = func_0025E248(job, file, 0x18);
            if (r == 1) {
                GRef<s32>(ctx) = cancelled() ? 0xF : 0xB;
            } else if (r == -1) {
                fail(r);
                GRef<s32>(ctx) = 0x13;
            }
            break;
        }
        case 0xB: {
            GRef<u32>(file + 0x10) = 0x2B031000;
            const s32 r = func_0025E248(job, file, 0x1F);
            if (r == 1) {
                GRef<s32>(ctx) = cancelled() ? 0xF : 0xC;
            } else if (r == -1) {
                fail(r);
                GRef<s32>(ctx) = 0xF;
            }
            break;
        }
        case 0xC: {
            const u32 lsn = GetCdFileLsn(0);
            s32 count = size / 0x800;
            const s32 done = GRef<s32>(kInstall + 4);
            const s32 offset = done / 0x800;
            if (cancelled()) {
                GRef<s32>(ctx) = 0xF;
                break;
            }
            if (0x2B031000 < done + size) count = (0x2B031000 - done) / 0x800 + 1;
            GRef<u32>(file + 0x18) = buf;
            GRef<s32>(file + 0x10) = count << 11;
            GRef<u32>(kInstall) = QueueCdRead(1, lsn, 0, buf, static_cast<u32>(offset), count, -1);
            GRef<s32>(ctx) = 0xD;
            break;
        }
        case 0xD:
            if (GetCdDoneTicket(1) < InstallWord(0)) break;
            GRef<s32>(ctx) = cancelled() ? 0xF : 0xE;
            break;
        case 0xE: {
            const s32 r = func_0025E248(job, file, 0x1C);
            if (r == 1) {
                if (cancelled()) {
                    GRef<s32>(ctx) = 0xF;
                    break;
                }
                const s32 done = GRef<s32>(kInstall + 4) + GRef<s32>(file + 0x14);
                GRef<s32>(kInstall + 4) = done;
                GRef<s32>(ctx) = 0x2B030FFF < done ? 0xF : 0xC;
            } else if (r == -1) {
                fail(r);
                GRef<s32>(ctx) = 0xF;
            }
            break;
        }
        case 0xF: {
            const s32 r = func_0025E248(job, file, 0x19);
            if (r == 1) {
                GRef<s32>(ctx) = cancelled() ? 0x12 : 0x10;
            } else if (r == -1) {
                GRef<s32>(ctx) = 0x12;
                g_HddInstallError() = func_002597C8();
                g_HddInstallFailed() = r;
            }
            break;
        }
        case 0x10: {
            GRef<u16>(file2 + 0x20) = 0x1B6;
            GRef<u32>(file2 + 0x1C) = 0x203;
            const s32 r = func_0025E248(job, file2, 0x18);
            if (r != 0 && cancelled()) {
                GRef<s32>(ctx) = 0x11;
            } else if (r == 1) {
                GRef<s32>(ctx) = 0x11;
            } else if (r == -1) {
                fail(r);
                GRef<s32>(ctx) = 0x13;
            }
            break;
        }
        case 0x11: {
            const s32 r = func_0025E248(job, file2, 0x19);
            if (r != 0 && cancelled()) {
                GRef<s32>(ctx) = 0x12;
            } else if (r == 1) {
                GRef<s32>(ctx) = 0x12;
            } else if (r == -1) {
                GRef<s32>(ctx) = 0x12;
                g_HddInstallError() = func_002597C8();
                g_HddInstallFailed() = r;
            }
            break;
        }
        case 0x12: {
            const s32 r = func_0025E248(job, pfs, 0x15);
            if (r != 0 && cancelled()) {
                GRef<s32>(ctx) = 0x13;
            } else if (r == 1) {
                GRef<s32>(ctx) = 0x13;
            } else if (r == -1) {
                fail(r);
                GRef<s32>(ctx) = 0x13;
            }
            break;
        }
        case 0x13: {
            const s32 r = func_0025E248(job, pfs, 0x13);
            if (r != 0 && cancelled()) {
                ret = -2;
            } else if (r != 1) {
                ret = r == -1 ? -1 : 0;
            } else if (g_HddInstallFailed() == -1) {
                func_002597E8(g_HddInstallError());
                ret = -1;
            } else {
                GRef<s32>(ctx) = 0x15;
            }
            break;
        }
        case 0x15:
            GRef<u8>(ctx + 0x215) = 1;
            ret = 1;
            break;
        case 0x16:
            if (InstallWord(0xC) != 0 && !cancelled()) break;
            GRef<u32>(kInstall + 0xC) = 0;
            GRef<u32>(ctx) = InstallWord(8);
            break;
        default:  // 1, 0x14
            break;
    }
    if (InstallWord(0xC) == 1) {
        GRef<u32>(kInstall + 0xC) = 2;
        GRef<u32>(kInstall + 8) = GRef<u32>(ctx);
        GRef<s32>(ctx) = 0x16;
    }
    if (ret != 0) GRef<s32>(ctx) = 0;
    return ret;
}

// EE: 0x0025A830
// Operation 7: mount the pfs partition (op 0x12, flag 0); +0xF = 1.
s32 func_0025A830(u32 ctx) {
    func_002598E8(ctx, 7);
    const u32 req = ctx + 0x4BD8;
    s32 ret = 0;
    s32 step = GRef<s32>(ctx);
    if (step == 0) {
        GRef<s32>(ctx) = 1;
        step = 1;
    }
    if (step == 1) {
        GRef<u32>(req + 8) = 0;
        const s32 r = func_0025E248(ctx + 0x360, req, 0x12);
        if (r == 1)
            GRef<s32>(ctx) = 2;
        else
            ret = r == -1 ? -1 : 0;
    } else if (step == 2) {
        GRef<u8>(ctx + 0xF) = 1;
        ret = 1;
    }
    if (ret != 0) GRef<s32>(ctx) = 0;
    return ret;
}

// EE: 0x0025A8F8
// Operation 8: unmount (op 0x13); +0xF = 0.
s32 func_0025A8F8(u32 ctx) {
    func_002598E8(ctx, 8);
    s32 ret = 0;
    s32 step = GRef<s32>(ctx);
    if (step == 0) {
        GRef<s32>(ctx) = 1;
        step = 1;
    }
    if (step == 1) {
        const s32 r = func_0025E248(ctx + 0x360, ctx + 0x4BD8, 0x13);
        if (r == 1)
            GRef<s32>(ctx) = 2;
        else
            ret = r == -1 ? -1 : 0;
    } else if (step == 2) {
        GRef<u8>(ctx + 0xF) = 0;
        ret = 1;
    }
    if (ret != 0) GRef<s32>(ctx) = 0;
    return ret;
}

// EE: 0x0025A9B8
// Operation 9 on request +0x4904: open (op 5, mode 3), set the partition attribute (op 1), close
// (op 6); an attribute error is kept (0x0041161C / 0x00411620) and reported after the close. +0xF
// cleared at the end.
s32 func_0025A9B8(u32 ctx) {
    func_002598E8(ctx, 9);
    const u32 req = ctx + 0x4904;
    s32 ret = 0;
    switch (GRef<u32>(ctx)) {
        case 0:
            g_HddOpFailed() = 0;
            g_HddOpError() = 0;
            GRef<s32>(ctx) = 1;
            [[fallthrough]];
        case 1: {
            GRef<u32>(req + 0x18) = 3;
            const s32 r = func_0025E248(ctx + 0x360, req, 5);
            if (r == 1)
                GRef<s32>(ctx) = 2;
            else
                ret = r == -1 ? -1 : 0;
            break;
        }
        case 2: {
            const s32 r = func_0025E248(ctx + 0x360, req, 1);
            if (r == 1) {
                GRef<s32>(ctx) = 3;
            } else if (r == -1) {
                GRef<s32>(ctx) = 3;
                g_HddOpError() = func_002597C8();
                g_HddOpFailed() = r;
            }
            break;
        }
        case 3: {
            const s32 r = func_0025E248(ctx + 0x360, req, 6);
            if (r != 1) {
                ret = r == -1 ? -1 : 0;
            } else if (g_HddOpFailed() == -1) {
                func_002597E8(g_HddOpError());
                ret = -1;
            } else {
                GRef<s32>(ctx) = 4;
            }
            break;
        }
        case 4:
            GRef<u8>(ctx + 0xF) = 0;
            ret = 1;
            break;
        default:
            break;
    }
    if (ret != 0) GRef<s32>(ctx) = 0;
    return ret;
}

// EE: 0x0025AB18
// Operation 0xA (needs +0x215): mount (op 0x12, flag 1), create the second file (0x18) and close
// it (0x19), create the data file (0x18); on any failure unmount (0x13) and report the error kept
// in 0x00411624. +0xE = 1 when done.
s32 func_0025AB18(u32 ctx) {
    func_002598E8(ctx, 0xA);
    const u32 file = ctx + 0x47F8;
    const u32 pfs = ctx + 0x46A0;
    const u32 file2 = ctx + 0x4870;
    s32 ret = 0;
    switch (GRef<u32>(ctx)) {
        case 0:
            if (GRef<s8>(ctx + 0x215) == 1)
                GRef<s32>(ctx) = 1;
            else
                ret = -1;
            g_HddMountError() = 0;
            break;
        case 1: {
            GRef<u32>(pfs + 8) = 1;
            const s32 r = func_0025E248(ctx + 0x360, pfs, 0x12);
            if (r == 1)
                GRef<s32>(ctx) = 2;
            else
                ret = r == -1 ? -1 : 0;
            break;
        }
        case 2: {
            GRef<u32>(file2 + 0x1C) = 1;
            const s32 r = func_0025E248(ctx + 0x360, file2, 0x18);
            if (r == 1)
                GRef<s32>(ctx) = 3;
            else if (r == -1)
                GRef<s32>(ctx) = 5;
            break;
        }
        case 3: {
            const s32 r = func_0025E248(ctx + 0x360, file2, 0x19);
            if (r == 1)
                GRef<s32>(ctx) = 4;
            else if (r == -1)
                GRef<s32>(ctx) = 5;
            break;
        }
        case 4: {
            GRef<u32>(file + 0x1C) = 1;
            const s32 r = func_0025E248(ctx + 0x360, file, 0x18);
            if (r == 1)
                GRef<s32>(ctx) = 6;
            else if (r == -1)
                GRef<s32>(ctx) = 5;
            break;
        }
        case 5:
            if (func_0025E248(ctx + 0x360, pfs, 0x13) != 0) {
                func_002597E8(g_HddMountError());
                ret = -1;
                GRef<u8>(ctx + 0xE) = 0;
            }
            break;
        case 6:
            ret = 1;
            GRef<u8>(ctx + 0xE) = 1;
            break;
        default:
            break;
    }
    if (ret != 0) GRef<s32>(ctx) = 0;
    return ret;
}

// EE: 0x0025ADF0
// Operation 0xC: mount (op 0x12) on request +0x4BD8, the partition size (+0x14 x +0x18) into
// +0x48EC, unmount (op 0x13).
s32 func_0025ADF0(u32 ctx) {
    func_002598E8(ctx, 0xC);
    const u32 req = ctx + 0x4BD8;
    s32 ret = 0;
    s32 step = GRef<s32>(ctx);
    if (step == 0) {
        GRef<s32>(ctx) = 1;
        step = 1;
    }
    if (step == 1) {
        const s32 r = func_0025E248(ctx + 0x360, req, 0x12);
        if (r == 1) {
            GRef<s32>(ctx) = 2;
            GRef<u32>(ctx + 0x48EC) = GRef<u32>(req + 0x14) * GRef<u32>(req + 0x18);
        } else {
            ret = r == -1 ? -1 : 0;
        }
    } else if (step == 2) {
        ret = JobResult(func_0025E248(ctx + 0x360, req, 0x13));
    }
    if (ret != 0) GRef<s32>(ctx) = 0;
    return ret;
}

// EE: 0x0025BE48
// Worker op 2 (create the partition) on request +0x43CC.
s32 func_0025BE48(u32 ctx) { return JobResult(func_0025E248(ctx + 0x360, ctx + 0x43CC, 2)); }

// EE: 0x0025BE90
// Worker op 0x25 (create the save directory tree) on request +0x4BD8.
s32 func_0025BE90(u32 ctx) { return JobResult(func_0025E248(ctx + 0x360, ctx + 0x4BD8, 0x25)); }

// EE: 0x0025BF20
// Runs the posted request of the worker: op table 0x003F6A78 (39 entries), -1 for other ops.
s32 func_0025BF20(u32 worker) {
    const u32 req = GRef<u32>(worker + 0x400C);
    const s32 op = GRef<s32>(req);
    if (!(op < 0x27)) return -1;
    return guest::CallGuest<s32>(GRef<u32>(0x003F6A78 + static_cast<u32>(op) * 4), worker, req);
}

// EE: 0x0025BF70
// Worker thread (never started: CreateThread is a stub in the port): waits for a request, runs it
// (+0x4008 = 2 while busy, 1 when idle), flushes the cache, forever.
void func_0025BF70(u32 worker) {
    const s32 sema = GRef<s32>(worker + 0x4000);
    for (;;) {
        WaitSema(sema);
        GRef<u32>(worker + 0x4008) = 2;
        func_0025BF20(worker);
        GRef<u32>(worker + 0x4008) = 1;
        FlushCache(0);
    }
}

// EE: 0x0025C0F0
// Worker op name (table 0x003F6B18, "HDD0_CREATE_PARTITION", ...).
u32 func_0025C0F0(s32 op) { return GRef<u32>(0x003F6B18 + static_cast<u32>(op) * 4); }

// EE: 0x0025C108
// Power-off thread (never started): on the CD power-off signal, syncs "pfs:" (devctl 0x5003) and
// "hdd:" (0x4806), then powers off, forever.
void func_0025C108(u32 sema) {
    guest::GuestFrame frame(0x10);
    for (;;) {
        WaitSema(static_cast<s32>(sema));
        sceDevctl(0x00411680, 0x5003, 0, 0, 0, 0);
        sceDevctl(0x00411688, 0x4806, 0, 0, 0, 0);
        sceCdPowerOff(frame.Address());
    }
}

// EE: 0x0025C178
// CD power-off callback: signals the power-off thread from the interrupt context.
s32 func_0025C178(u32 sema) { return iSignalSema(static_cast<s32>(sema)); }

// EE: 0x0025C300
// Decodes an 8-byte obfuscated key (bitwise NOT) into 0x004118A0.
u32 func_0025C300(u32 src) {
    for (u32 i = 0; i < 8; ++i) GRef<u8>(0x004118A0 + i) = static_cast<u8>(~GRef<u8>(src + i));
    return 0x004118A0;
}

// ---- HDD worker ops (table 0x003F6A78; worker w, request r; result 1 ok, -1 error = r+4) ------

// EE: 0x0025C340
// Op 0: touch (open with create 0x200, then close) the partition path.
s32 func_0025C340(u32 w, u32 r) {
    func_0025C240(w, r);
    s32 fd = sceOpen(r + 0x34, 0x200);
    if (!(fd < 0)) fd = sceClose(fd);
    if (fd == 0) {
        GRef<u32>(r + 4) = 0;
        return 1;
    }
    GRef<s32>(r + 4) = -fd;
    return -1;
}

// EE: 0x0025C3A0
// Op 1: partition attribute (ioctl2 0x6801 with the string +0x288) on file +8.
s32 func_0025C3A0(u32 w, u32 r) {
    (void)w;
    const u32 s = r + 0x288;
    const s32 len = static_cast<s32>(ee::strlen(s));
    return OpResultNonNegative(r, sceIoctl2(GRef<s32>(r + 8), 0x6801, s, static_cast<u32>(len + 1), 0, 0));
}

// EE: 0x0025C410
// Op 2: creates the partition (size +0x14 sectors) in pieces: the first piece opened with create
// (named by +0xC, or "<dev><id>,<pw>,..,<size>,.." with the size "%dG" / "%dM"; the size halves on
// ENOSPC while above 0x3FFFF), then each further piece added by ioctl2 0x6801 with its size; -0x1C
// (no space) when the size is not below the free space (+0x401C - +0x4024).
s32 func_0025C410(u32 w, u32 r) {
    guest::GuestFrame frame(0x20);
    const u32 sz = frame.Address();
    const u32 path = r + 0x34;
    const u32 path2 = r + 0x134;
    const s32 name_index = GRef<s32>(r + 0xC);
    s32 remain = GRef<s32>(r + 0x14);
    s32 fd = -1;
    s32 ret = 0;
    const auto format_size = [&](s32 chunk) {
        if (0x1FFFFF < chunk) {
            const u64 a[1] = {static_cast<u64>(static_cast<s64>(chunk / 0x200000))};
            ee::sprintf(sz, 0x00411698, a, 1);  // "%dG"
        } else {
            const u64 a[1] = {static_cast<u64>(static_cast<s64>(chunk / 0x800))};
            ee::sprintf(sz, 0x004116A0, a, 1);  // "%dM"
        }
    };
    if (!(remain < GRef<s32>(w + 0x401C) - GRef<s32>(w + 0x4024))) {
        GRef<s32>(r + 4) = -0x1C;
        return -1;
    }
    s32 chunk = GRef<s32>(w + 0x4020);
    while (0x3FFFF < chunk) {
        if (remain < chunk) {
            chunk >>= 1;
            continue;
        }
        if (name_index != 0) {
            const u64 a[2] = {w + 0x4014, GRef<u32>(0x003F6A60 + static_cast<u32>(name_index) * 4)};
            ee::sprintf(path, 0x00411690, a, 2);  // "%s%s"
            ee::strcpy(path2, path);
        } else {
            format_size(chunk);
            const u64 a[6] = {w + 0x4014, r + 0x234, r + 0x276, r + 0x27F, sz, r + 0x28D};
            ee::sprintf(path, 0x0040A8E0, a, 6);  // "%s%s,%s,%s,%s,%s"
            const u64 b[6] = {w + 0x4014, r + 0x255, r + 0x276, r + 0x27F, sz, r + 0x28D};
            ee::sprintf(path2, 0x0040A8E0, b, 6);
        }
        const s32 v = sceOpen(path, 0x200);
        if (!(v < 0)) {
            remain -= chunk;
            fd = v;
            break;
        }
        if (v != -0x1C) {
            ret = -1;
            GRef<s32>(r + 4) = -v;
            break;
        }
        chunk >>= 1;
    }
    if (ret != -1 && 0 < remain) {
        const s32 c = sceClose(fd);
        if (c != 0) {
            ret = -1;
            GRef<s32>(r + 4) = -c;
        } else {
            const s32 v = sceOpen(path, 3);
            if (v < 0) {
                ret = -1;
                GRef<s32>(r + 4) = -v;
            } else {
                fd = v;
            }
        }
        while (0 < remain && ret != -1) {
            chunk = GRef<s32>(w + 0x4020);
            while (0x3FFFF < chunk) {
                if (remain < chunk) {
                    chunk >>= 1;
                    continue;
                }
                format_size(chunk);
                const s32 len = static_cast<s32>(ee::strlen(sz));
                const s32 v = sceIoctl2(fd, 0x6801, sz, static_cast<u32>(len + 1), 0, 0);
                if (v == 0) {
                    remain -= chunk;
                    break;
                }
                if (v == -0x1C) {
                    chunk >>= 1;
                    continue;
                }
                remain = -1;
                GRef<s32>(r + 4) = -v;
                ret = -1;
                break;
            }
            // With no piece size above 0x3FFFF that fits, the original keeps retrying.
        }
    }
    if (fd < 0) return ret;
    const s32 c = sceClose(fd);
    if (c == 0) {
        if (ret == -1) return -1;
        GRef<u32>(r + 4) = 0;
        return 1;
    }
    if (ret == -1) return -1;
    GRef<s32>(r + 4) = -c;
    return -1;
}

// EE: 0x0025C7E0
// Op 3: remove the partition path.
s32 func_0025C7E0(u32 w, u32 r) {
    func_0025C240(w, r);
    return OpResultNonNegative(r, sceRemove(r + 0x34));
}

// EE: 0x0025C838
// Op 4: getstat of the partition path into +0x294 (returns 0; error kept in +4).
s32 func_0025C838(u32 w, u32 r) {
    func_0025C240(w, r);
    const s32 v = sceGetstat(r + 0x34, r + 0x294);
    if (v != 0) GRef<s32>(r + 4) = -v;
    return 0;
}

// EE: 0x0025C888
// Op 5: open the partition path with mode +0x18 (descriptor into +8).
s32 func_0025C888(u32 w, u32 r) {
    func_0025C240(w, r);
    const s32 fd = sceOpen(r + 0x34, GRef<s32>(r + 0x18));
    if (!(fd < 0)) {
        GRef<s32>(r + 8) = fd;
        GRef<u32>(r + 4) = 0;
        return 1;
    }
    GRef<s32>(r + 4) = -fd;
    return -1;
}

// EE: 0x0025C8E8
// Op 6: close descriptor +8 (set to -1).
s32 func_0025C8E8(u32 w, u32 r) {
    (void)w;
    const s32 fd = GRef<s32>(r + 8);
    s32 err = -1;
    if (!(fd < 0)) {
        const s32 v = sceClose(fd);
        if (!(v < 0)) {
            GRef<u32>(r + 4) = 0;
            GRef<s32>(r + 8) = -1;
            return 1;
        }
        err = -v;
    }
    GRef<s32>(r + 4) = err;
    return -1;
}

// EE: 0x0025C948
// Op 7: looks for the partition name +0x234 in the "hdd0:" directory (+0x10 = found).
s32 func_0025C948(u32 w, u32 r) {
    guest::GuestFrame frame(0x150);
    const u32 ent = frame.Address();
    s32 ret = 0;
    const s32 fd = sceDopen(w + 0x4014);
    if (fd < 0) {
        GRef<u32>(r + 0x10) = 0;
        GRef<s32>(r + 4) = -fd;
        return -1;
    }
    for (;;) {
        const s32 n = sceDread(fd, ent);
        if (n > 0) {
            if (ee::strcmp(ent + 0x40, r + 0x234) != 0) continue;
            GRef<u32>(r + 0x10) = 1;
            break;
        }
        if (n == 0) {
            GRef<u32>(r + 0x10) = 0;
            break;
        }
        GRef<u32>(r + 0x10) = 0;
        GRef<s32>(r + 4) = -n;
        ret = -1;
        break;
    }
    const s32 c = sceDclose(fd);
    if (c < 0) {
        if (ret == -1) return -1;
        GRef<u32>(r + 0x10) = 0;
        GRef<s32>(r + 4) = -c;
        return -1;
    }
    if (ret == -1) return -1;
    GRef<u32>(r + 4) = 0;
    return 1;
}

// EE: 0x0025CA50
// Op 8: pfs status of unit +0xC: devctl 0x5004 ("pfs%d:"); when it returns 1 (+0x4039 = 1)
// devctl 0x5005, else +0x4039 = 0.
s32 func_0025CA50(u32 w, u32 r) {
    guest::GuestFrame frame(0x10);
    const u32 dev = frame.Address();
    const u64 a[1] = {GRef<u32>(r + 0xC)};
    ee::sprintf(dev, 0x004116A8, a, 1);
    const s32 v = sceDevctl(dev, 0x5004, 0, 0, 0, 0);
    if (v != 1) {
        GRef<u8>(w + 0x4039) = 0;
        return 1;
    }
    GRef<u8>(w + 0x4039) = static_cast<u8>(v);
    return OpResult(r, sceDevctl(dev, 0x5005, 0, 0, 0, 0));
}

// EE: 0x0025CB00
// Op 9: dopen "hdd0:" (descriptor +8, entry count +0xC = 0).
s32 func_0025CB00(u32 w, u32 r) {
    const s32 fd = sceDopen(w + 0x4014);
    if (!(fd < 0)) {
        GRef<s32>(r + 8) = fd;
        GRef<u32>(r + 4) = 0;
        GRef<u32>(r + 0xC) = 0;
        return 1;
    }
    GRef<s32>(r + 4) = -fd;
    return -1;
}

// EE: 0x0025CB50
// Op 0xA: dclose descriptor +8.
s32 func_0025CB50(u32 w, u32 r) {
    (void)w;
    const s32 fd = GRef<s32>(r + 8);
    s32 err = -1;
    if (!(fd < 0)) {
        const s32 v = sceDclose(fd);
        if (!(v < 0)) {
            GRef<u32>(r + 4) = 0;
            GRef<s32>(r + 8) = -1;
            return 1;
        }
        err = -v;
    }
    GRef<s32>(r + 4) = err;
    return -1;
}

// EE: 0x0025CBB0
// Op 0xB: dread descriptor +8 into +0x10 (entry count +0xC).
s32 func_0025CBB0(u32 w, u32 r) {
    (void)w;
    const s32 fd = GRef<s32>(r + 8);
    if (fd < 0) {
        GRef<s32>(r + 4) = -1;
        return -1;
    }
    const s32 n = sceDread(fd, GRef<u32>(r + 0x10));
    if (0 < n) {
        GRef<u32>(r + 4) = 0;
        GRef<u32>(r + 0xC) += 1;
        return 1;
    }
    GRef<s32>(r + 4) = -n;
    return -1;
}

namespace {

// Ops 0xC / 0xD: read or write descriptor +8, buffer +0x1C, size +0x20; count into +0x24.
s32 HddTransfer(u32 r, bool write) {
    const s32 fd = GRef<s32>(r + 8);
    const u32 buf = GRef<u32>(r + 0x1C);
    const s32 n = GRef<s32>(r + 0x20);
    if (fd < 0) {
        GRef<s32>(r + 4) = -1;
        return -1;
    }
    const s32 v = write ? sceWrite(fd, buf, n) : sceRead(fd, buf, n);
    if (!(v < 0)) {
        GRef<s32>(r + 0x24) = v;
        GRef<u32>(r + 4) = 0;
        return 1;
    }
    GRef<u32>(r + 0x24) = 0;
    GRef<s32>(r + 4) = -v;
    return -1;
}

}  // namespace

// EE: 0x0025CC20
// Op 0xC: read.
s32 func_0025CC20(u32 w, u32 r) {
    (void)w;
    return HddTransfer(r, false);
}

// EE: 0x0025CC90
// Op 0xD: write.
s32 func_0025CC90(u32 w, u32 r) {
    (void)w;
    return HddTransfer(r, true);
}

// EE: 0x0025CD00
// Op 0xE: lseek descriptor +8 to +0x28 (whence +0x2C); position into +0x30.
s32 func_0025CD00(u32 w, u32 r) {
    (void)w;
    const s32 fd = GRef<s32>(r + 8);
    if (fd < 0) {
        GRef<s32>(r + 4) = -1;
        return -1;
    }
    const s32 v = sceLseek(fd, GRef<s32>(r + 0x28), GRef<s32>(r + 0x2C));
    if (!(v < 0)) {
        GRef<s32>(r + 0x30) = v;
        GRef<u32>(r + 4) = 0;
        return 1;
    }
    GRef<u32>(r + 0x30) = 0;
    GRef<s32>(r + 4) = -v;
    return -1;
}

// EE: 0x0025CD70
// Op 0xF: HDD status (devctl 0x4807 on "hdd0:" into +0x4038, 3 on error); when it is 0, the total
// size (0x4802 -> +0x401C); then while the size is set, the free size (0x480A -> +0x4030) and the
// maximum partition size (0x4801 -> +0x4020).
s32 func_0025CD70(u32 w, u32 r) {
    guest::GuestFrame frame(0x10);
    const u32 out = frame.Address();
    const u32 dev = w + 0x4014;
    s32 ret;
    s32 v = sceDevctl(dev, 0x4807, 0, 0, 0, 0);
    if (!(v < 0)) {
        GRef<u8>(w + 0x4038) = static_cast<u8>(v);
        ret = 1;
        GRef<u32>(r + 4) = 0;
    } else {
        GRef<u8>(w + 0x4038) = 3;
        ret = -1;
        GRef<s32>(r + 4) = -v;
    }
    if (v == 0) {
        if (ret != 1) return ret;
        if (GRef<s8>(w + 0x4038) == 0) {
            v = sceDevctl(dev, 0x4802, 0, 0, 0, 0);
            if (!(v < 0)) {
                GRef<s32>(w + 0x401C) = v;
                GRef<u32>(r + 4) = 0;
            } else {
                GRef<u32>(w + 0x401C) = 0;
                GRef<s32>(r + 4) = -v;
                ret = -1;
            }
        }
    }
    if (ret != 1) return ret;
    if (0 < GRef<s32>(w + 0x401C)) {
        v = sceDevctl(dev, 0x480A, 0, 0, out, 4);
        if (v < 0) {
            GRef<u32>(w + 0x4030) = 0;
            GRef<s32>(r + 4) = -v;
            ret = -1;
        } else {
            GRef<u32>(w + 0x4030) = GRef<u32>(out);
            GRef<u32>(r + 4) = 0;
        }
    }
    if (ret != 1) return ret;
    if (0 < GRef<s32>(w + 0x401C)) {
        v = sceDevctl(dev, 0x4801, 0, 0, 0, 0);
        if (!(v < 0)) {
            GRef<s32>(w + 0x4020) = v;
            GRef<u32>(r + 4) = 0;
        } else {
            GRef<u32>(w + 0x4020) = 0;
            GRef<s32>(r + 4) = -v;
            ret = -1;
        }
    }
    return ret;
}

// EE: 0x0025CF08
// Op 0x10: not supported.
s32 func_0025CF08(u32 w, u32 r) {
    (void)w;
    (void)r;
    return -1;
}

// EE: 0x0025CF10
// Op 0x11: partition sizes of "hdd0:": used by all (+0x4024), free ("__empty" partitions, +0x4028)
// and the largest free one (+0x402C); all -1 on an error.
s32 func_0025CF10(u32 w, u32 r) {
    guest::GuestFrame frame(0x150);
    const u32 ent = frame.Address();
    s32 ret = 0;
    u32 used = 0, empty = 0, largest = 0;
    const s32 fd = sceDopen(w + 0x4014);
    if (fd < 0) {
        GRef<s32>(w + 0x4024) = -1;
        GRef<s32>(w + 0x4028) = -1;
        GRef<s32>(w + 0x402C) = -1;
        GRef<s32>(r + 4) = -fd;
        return -1;
    }
    for (;;) {
        const s32 n = sceDread(fd, ent);
        if (0 < n) {
            const u32 size = GRef<u32>(ent + 8);
            if (ee::strcmp(ent + 0x40, 0x004116B0) != 0) {
                used += size;
            } else {
                empty += size;
                if (largest < size) largest = size;
            }
            continue;
        }
        if (n != 0) {
            GRef<s32>(w + 0x402C) = -1;
            GRef<s32>(w + 0x4024) = -1;
            ret = -1;
            GRef<s32>(w + 0x4028) = -1;
            GRef<s32>(r + 4) = -n;
        }
        break;
    }
    const s32 c = sceDclose(fd);
    if (c < 0) {
        if (ret == -1) return -1;
        GRef<s32>(w + 0x402C) = -1;
        GRef<s32>(w + 0x4024) = -1;
        GRef<s32>(w + 0x4028) = -1;
        GRef<s32>(r + 4) = -c;
        return -1;
    }
    if (ret == -1) return -1;
    GRef<u32>(w + 0x402C) = largest;
    GRef<u32>(w + 0x4024) = used;
    GRef<u32>(w + 0x4028) = empty;
    GRef<u32>(r + 4) = 0;
    return 1;
}

// EE: 0x0025D070
// Counts the directory entries of `fd` other than "." and ".." that are files (mode 0x1000) with
// both private words 0xFFFF; a dread error is returned instead.
s32 func_0025D070(s32 fd) {
    guest::GuestFrame frame(0x150);
    const u32 ent = frame.Address();
    s32 count = 0;
    for (;;) {
        const s32 n = sceDread(fd, ent);
        if (n == 0) return count;
        if (n < 0) return n;
        const u32 name = ent + 0x40;
        if (ee::strcmp(name, 0x004116B8) == 0) continue;
        if (ee::strcmp(name, 0x004116C0) == 0) continue;
        if ((GRef<u32>(ent) & 0xF000) != 0x1000) continue;
        if (GRef<u32>(ent + 0x28) != 0xFFFF) continue;
        if (GRef<u32>(ent + 0x2C) == 0xFFFF) count += 1;
    }
}

// EE: 0x0025D168
// Counts the entries of `fd` ("." and ".." included) except files without attribute bit 0x4000,
// links (0x2000) whose name has a ':', directories (0x4000) and entries whose private words are
// not both 0xFFFF; a dread error is returned instead.
s32 func_0025D168(s32 fd) {
    guest::GuestFrame frame(0x150);
    const u32 ent = frame.Address();
    s32 count = 0;
    for (;;) {
        const s32 n = sceDread(fd, ent);
        if (n == 0) return count;
        if (n < 0) return n;
        const u32 name = ent + 0x40;
        if (ee::strcmp(name, 0x004116B8) == 0 || ee::strcmp(name, 0x004116C0) == 0) {
            count += 1;
            continue;
        }
        const u32 type = GRef<u32>(ent) & 0xF000;
        if (type == 0x1000 && (GRef<u32>(ent + 4) & 0x4000) == 0) continue;
        if (ee::strchr(name, ':') != 0 && type == 0x2000) continue;
        if (type == 0x4000) continue;
        if (GRef<u32>(ent + 0x28) != 0xFFFF) continue;
        if (GRef<u32>(ent + 0x2C) != GRef<u32>(ent + 0x28)) continue;
        count += 1;
    }
}

// EE: 0x0025D2A8
// Op 0x12: mounts the partition (named by +0x10, or "<dev><id>,<pw>,.." from +0x11C..) as
// "pfs<+0xC>:" with flag +8, then its zone size (devctl 0x5001 -> +0x14) and zone count (0x5002 ->
// +0x18); unmounted again on an error.
s32 func_0025D2A8(u32 w, u32 r) {
    guest::GuestFrame frame(0x10);
    const u32 pfs = frame.Address();
    const u32 dev = r + 0x1C;
    const s32 name_index = GRef<s32>(r + 0x10);
    const s32 flag = GRef<s32>(r + 8);
    const u64 a[1] = {GRef<u32>(r + 0xC)};
    ee::sprintf(pfs, 0x004116C8, a, 1);
    if (name_index != 0) {
        const u64 b[2] = {w + 0x4014, GRef<u32>(0x003F6A60 + static_cast<u32>(name_index) * 4)};
        ee::sprintf(dev, 0x004116D0, b, 2);
    } else {
        const u64 b[6] = {w + 0x4014, r + 0x11C, r + 0x13D, r + 0x146, r + 0x14F, r + 0x154};
        ee::sprintf(dev, 0x0040A900, b, 6);
    }
    s32 state = 0;
    s32 ret = 0;
    s32 zone_size = 0;
    while (ret == 0) {
        if (state == 0) {
            const s32 v = sceMount(pfs, dev, flag, 0, 0);
            if (v != 0) {
                ret = -1;
                GRef<s32>(r + 4) = -v;
            } else {
                GRef<u32>(r + 4) = 0;
                state = 1;
            }
        } else if (state == 1) {
            const s32 v = sceDevctl(pfs, 0x5001, 0, 0, 0, 0);
            state = 2;
            if (v < 0) {
                state = 3;
                GRef<s32>(r + 4) = -v;
            } else {
                zone_size = v;
                GRef<u32>(r + 4) = 0;
            }
        } else if (state == 2) {
            const s32 v = sceDevctl(pfs, 0x5002, 0, 0, 0, 0);
            if (!(v < 0)) {
                GRef<s32>(r + 0x18) = v;
                ret = 1;
                GRef<u32>(r + 4) = 0;
                GRef<s32>(r + 0x14) = zone_size;
            } else {
                state = 3;
                GRef<s32>(r + 4) = -v;
            }
        } else if (state == 3) {
            sceUmount(pfs);
            ret = -1;
        }
    }
    return ret;
}

// EE: 0x0025D488
// Op 0x13: unmount "pfs<+0xC>:".
s32 func_0025D488(u32 w, u32 r) {
    (void)w;
    guest::GuestFrame frame(0x10);
    const u32 pfs = frame.Address();
    const u64 a[1] = {GRef<u32>(r + 0xC)};
    ee::sprintf(pfs, 0x004116C8, a, 1);
    return OpResult(r, sceUmount(pfs));
}

// EE: 0x0025D4E8
// Op 0x14: formats the partition (named as in op 0x12) as "pfs:" with the zone size +0x14.
s32 func_0025D4E8(u32 w, u32 r) {
    guest::GuestFrame frame(0x10);
    const u32 arg = frame.Address();
    const u32 dev = r + 0x1C;
    GRef<u32>(arg) = GRef<u32>(r + 0x14);
    const s32 name_index = GRef<s32>(r + 0x10);
    if (name_index == 0) {
        const u64 b[6] = {w + 0x4014, r + 0x11C, r + 0x13D, r + 0x146, r + 0x14F, r + 0x154};
        ee::sprintf(dev, 0x0040A900, b, 6);
    } else {
        const u64 b[2] = {w + 0x4014, GRef<u32>(0x003F6A60 + static_cast<u32>(name_index) * 4)};
        ee::sprintf(dev, 0x004116D0, b, 2);
    }
    return OpResultNonNegative(r, sceFormat(0x004116D8, dev, arg, 4));
}

// EE: 0x0025D5B8
// Op 0x15: sync "pfs<+0xC>:".
s32 func_0025D5B8(u32 w, u32 r) {
    (void)w;
    guest::GuestFrame frame(0x10);
    const u32 pfs = frame.Address();
    const u64 a[1] = {GRef<u32>(r + 0xC)};
    ee::sprintf(pfs, 0x004116C8, a, 1);
    return OpResultNonNegative(r, sceSync(pfs, 0));
}

// EE: 0x0025D618
// Op 0x16: getstat of path +0x70 into +0x2C.
s32 func_0025D618(u32 w, u32 r) {
    (void)w;
    return OpResultNonNegative(r, sceGetstat(GRef<u32>(r + 0x70), r + 0x2C));
}

// EE: 0x0025D668
// Op 0x17: chstat of path +0x70 from +0x2C (mask 2).
s32 func_0025D668(u32 w, u32 r) {
    (void)w;
    return OpResultNonNegative(r, sceChstat(GRef<u32>(r + 0x70), r + 0x2C, 2));
}

// EE: 0x0025D6B8
// Op 0x18: opens path +0x70 with flags +0x1C (descriptor +8, position +0xC and count +0x14 cleared);
// a created file gets mode +0x20 | 0x2000 (chstat mask 1).
s32 func_0025D6B8(u32 w, u32 r) {
    (void)w;
    guest::GuestFrame frame(0x40);
    const u32 stat = frame.Address();
    const s32 flags = GRef<s32>(r + 0x1C);
    const u16 mode = GRef<u16>(r + 0x20);
    const u32 name = GRef<u32>(r + 0x70);
    const s32 fd = sceOpen(name, flags);
    if (fd < 0) {
        GRef<s32>(r + 4) = -fd;
        GRef<s32>(r + 8) = -1;
        return -1;
    }
    GRef<s32>(r + 8) = fd;
    GRef<u32>(r + 0x14) = 0;
    GRef<u32>(r + 0xC) = 0;
    GRef<u32>(r + 4) = 0;
    if ((flags & 0x200) == 0) return 1;
    GRef<u32>(stat) = mode | 0x2000u;
    const s32 v = sceChstat(name, stat, 1);
    if (!(v < 0)) {
        GRef<u32>(r + 4) = 0;
        return 1;
    }
    GRef<s32>(r + 4) = -v;
    return -1;
}

// EE: 0x0025D788
// Op 0x19: close descriptor +8 (-1 after; position cleared).
s32 func_0025D788(u32 w, u32 r) {
    (void)w;
    const s32 v = sceClose(GRef<s32>(r + 8));
    if (!(v < 0)) {
        GRef<u32>(r + 4) = 0;
        GRef<s32>(r + 8) = -1;
        GRef<u32>(r + 0xC) = 0;
        return 1;
    }
    GRef<s32>(r + 4) = -v;
    return -1;
}

// EE: 0x0025D7E0
// Op 0x1A: lseek descriptor +8 to +0x24 (whence +0x28); position into +0xC.
s32 func_0025D7E0(u32 w, u32 r) {
    (void)w;
    const s32 v = sceLseek(GRef<s32>(r + 8), GRef<s32>(r + 0x24), GRef<s32>(r + 0x28));
    if (!(v < 0)) {
        GRef<s32>(r + 0xC) = v;
        GRef<u32>(r + 4) = 0;
        return 1;
    }
    GRef<s32>(r + 4) = -v;
    return -1;
}

namespace {

// Ops 0x1B / 0x1C: read or write descriptor +8, buffer +0x18, size +0x10; count +0x14, position
// +0xC advanced.
s32 HddFileTransfer(u32 r, bool write) {
    const s32 fd = GRef<s32>(r + 8);
    const u32 buf = GRef<u32>(r + 0x18);
    const s32 n = GRef<s32>(r + 0x10);
    const s32 v = write ? sceWrite(fd, buf, n) : sceRead(fd, buf, n);
    if (!(v < 0)) {
        GRef<u32>(r + 4) = 0;
        GRef<s32>(r + 0x14) = v;
        GRef<s32>(r + 0xC) += v;
        return 1;
    }
    GRef<s32>(r + 4) = -v;
    return -1;
}

}  // namespace

// EE: 0x0025D838
// Op 0x1B: read.
s32 func_0025D838(u32 w, u32 r) {
    (void)w;
    return HddFileTransfer(r, false);
}

// EE: 0x0025D8A0
// Op 0x1C: write.
s32 func_0025D8A0(u32 w, u32 r) {
    (void)w;
    return HddFileTransfer(r, true);
}

// EE: 0x0025D908
// Op 0x1D: remove path +0x70.
s32 func_0025D908(u32 w, u32 r) {
    (void)w;
    return OpResultNonNegative(r, sceRemove(GRef<u32>(r + 0x70)));
}

// EE: 0x0025D950
// Op 0x1E: rename path +0x70 to +0x74.
s32 func_0025D950(u32 w, u32 r) {
    (void)w;
    return OpResultNonNegative(r, sceRename(GRef<u32>(r + 0x70), GRef<u32>(r + 0x74)));
}

// EE: 0x0025D9A0
// Op 0x1F: sets the size of descriptor +8 to +0x10 rounded up to 512 bytes (ioctl2 0x7001).
s32 func_0025D9A0(u32 w, u32 r) {
    (void)w;
    guest::GuestFrame frame(0x10);
    const u32 arg = frame.Address();
    const s32 size = GRef<s32>(r + 0x10);
    GRef<s32>(arg) = size;
    if ((size & 0x1FF) != 0) GRef<s32>(arg) = (size / 0x200 + 1) << 9;
    return OpResult(r, sceIoctl2(GRef<s32>(r + 8), 0x7001, arg, 4, 0, 0));
}

// EE: 0x0025DA30
// Op 0x20: dopen path +0x70 (descriptor +8).
s32 func_0025DA30(u32 w, u32 r) {
    (void)w;
    const s32 fd = sceDopen(GRef<u32>(r + 0x70));
    if (!(fd < 0)) {
        GRef<s32>(r + 8) = fd;
        GRef<u32>(r + 4) = 0;
        return 1;
    }
    GRef<s32>(r + 4) = -fd;
    GRef<s32>(r + 8) = -1;
    return -1;
}

// EE: 0x0025DA80
// Op 0x21: dclose descriptor +8.
s32 func_0025DA80(u32 w, u32 r) {
    (void)w;
    const s32 v = sceDclose(GRef<s32>(r + 8));
    if (!(v < 0)) {
        GRef<u32>(r + 4) = 0;
        GRef<s32>(r + 8) = -1;
        return 1;
    }
    GRef<s32>(r + 4) = -v;
    return -1;
}

// EE: 0x0025DAD0
// Op 0x22: mkdir path +0x70 with mode +0x20, then mode | 0x1000 (chstat mask 1).
s32 func_0025DAD0(u32 w, u32 r) {
    (void)w;
    guest::GuestFrame frame(0x40);
    const u32 stat = frame.Address();
    const u32 name = GRef<u32>(r + 0x70);
    const u16 mode = GRef<u16>(r + 0x20);
    const s32 v = sceMkdir(name, mode);
    if (v < 0) {
        GRef<s32>(r + 4) = -v;
        return -1;
    }
    GRef<u32>(r + 4) = 0;
    GRef<u32>(stat) = mode | 0x1000u;
    return OpResultNonNegative(r, sceChstat(name, stat, 1));
}

// EE: 0x0025DB78
// Op 0x23: rmdir path +0x70.
s32 func_0025DB78(u32 w, u32 r) {
    (void)w;
    return OpResultNonNegative(r, sceRmdir(GRef<u32>(r + 0x70)));
}

// EE: 0x0025DBC0
// Op 0x24: reads descriptor +8 to the end of the directory (errors stored, reading goes on).
s32 func_0025DBC0(u32 w, u32 r) {
    (void)w;
    guest::GuestFrame frame(0x150);
    const u32 ent = frame.Address();
    const s32 fd = GRef<s32>(r + 8);
    for (;;) {
        const s32 n = sceDread(fd, ent);
        if (n == 0) break;
        if (n < 0) GRef<s32>(r + 4) = -n;
    }
    GRef<u32>(r + 4) = 0;
    GRef<u32>(r + 0x14) = 0;
    return 1;
}

// EE: 0x0025DC20
// Op 0x25: makes sure "pfs<+0xC>:Your Saves" exists: open it (0x0025D168 counts its entries into
// +0x4034); if missing create it (mode 0x1FF, then 0x11FF); with no space unmount, create the
// "__common" partition (1 GB, ioctl2 0x6801 "1G"), mount it and start over.
s32 func_0025DC20(u32 w, u32 r) {
    guest::GuestFrame frame(0x60);
    const u32 stat = frame.Address();
    const u32 path = frame.Address(0x40);
    const u32 dev = frame.Address(0x50);
    s32 ret = 0;
    s32 state = 0;
    s32 fd = -1;
    s32 count = 0;
    GRef<u32>(r + 4) = 0;
    const auto saves_path = [&] {
        const u64 a[2] = {GRef<u32>(r + 0xC), 0x0040A928};  // "Your Saves"
        ee::sprintf(path, 0x0040A918, a, 2);                // "pfs%d:%s"
    };
    while (ret == 0) {
        switch (state) {
            case 0: {
                saves_path();
                const s32 v = sceDopen(path);
                if (!(v < 0)) {
                    fd = v;
                    state = 3;
                } else if (v == -2) {
                    state = 1;
                } else {
                    ret = -1;
                    GRef<s32>(r + 4) = -v;
                }
                break;
            }
            case 1: {
                saves_path();
                const s32 v = sceMkdir(path, 0x1FF);
                if (!(v < 0)) {
                    state = 2;
                } else if (v == -0x1C) {
                    state = 5;
                } else {
                    ret = -1;
                    GRef<s32>(r + 4) = -v;
                }
                break;
            }
            case 2: {
                GRef<u32>(stat) = 0x11FF;
                const s32 v = sceChstat(path, stat, 1);
                if (v < 0) {
                    ret = -1;
                    GRef<s32>(r + 4) = -v;
                } else {
                    state = 0;
                }
                break;
            }
            case 3: {
                const s32 n = func_0025D168(fd);
                if (!(n < 0))
                    count = n;
                else
                    GRef<s32>(r + 4) = -n;
                state = 4;
                break;
            }
            case 4: {
                const s32 v = sceDclose(fd);
                const s32 err = GRef<s32>(r + 4);
                if (v != 0) {
                    ret = -1;
                    if (err == 0) GRef<s32>(r + 4) = -v;
                } else if (err != 0) {
                    ret = -1;
                } else {
                    GRef<u16>(w + 0x4034) = static_cast<u16>(count);
                    ret = 1;
                }
                break;
            }
            case 5: {
                const u64 a[1] = {GRef<u32>(r + 0xC)};
                ee::sprintf(path, 0x004116C8, a, 1);
                const s32 v = sceUmount(path);
                if (v != 0) {
                    ret = -1;
                    GRef<s32>(r + 4) = -v;
                } else {
                    state = 6;
                }
                break;
            }
            case 6: {
                const u64 a[2] = {w + 0x4014, 0x0040A938};  // "__common"
                ee::sprintf(path, 0x004116D0, a, 2);
                const s32 v = sceOpen(path, 3);
                if (!(v < 0)) {
                    fd = v;
                    state = 7;
                } else {
                    state = 9;
                    GRef<s32>(r + 4) = -v;
                }
                break;
            }
            case 7: {
                const s32 v = sceIoctl2(fd, 0x6801, 0x004116E0, 3, 0, 0);  // "1G"
                state = 8;
                if (v < 0) GRef<s32>(r + 4) = -v;
                break;
            }
            case 8: {
                const s32 v = sceClose(fd);
                state = 9;
                if (v < 0 && GRef<s32>(r + 4) == 0) GRef<s32>(r + 4) = -v;
                break;
            }
            case 9: {
                const u64 a[2] = {w + 0x4014, 0x0040A938};
                ee::sprintf(dev, 0x004116D0, a, 2);
                const u64 b[1] = {GRef<u32>(r + 0xC)};
                ee::sprintf(path, 0x004116C8, b, 1);
                const s32 v = sceMount(path, dev, 0, 0, 0);
                const s32 err = GRef<s32>(r + 4);
                if (v != 0) {
                    ret = -1;
                    if (err == 0) GRef<s32>(r + 4) = -v;
                } else if (err == 0) {
                    state = 1;
                } else {
                    ret = -1;
                }
                break;
            }
            default:
                break;
        }
    }
    return ret;
}

// EE: 0x0025DF68
// Op 0x26: mounts the partition W(0x003F6A74) as "pfs<+0xC>:" and counts, in "Your Saves" and "/",
// the save entries (0x0025D070 of "Your Saves" -> +0x4036) and the other entries (0x0025D168 ->
// +0x4034); a missing "Your Saves" is skipped; then unmounts.
s32 func_0025DF68(u32 w, u32 r) {
    guest::GuestFrame frame(0x50);
    const u32 pfs = frame.Address();
    const u32 path = frame.Address(0x10);
    const u32 names = frame.Address(0x30);
    const u32 saves = frame.Address(0x40);
    const u32 dev = r + 0x1C;
    s32 ret = 0;
    s32 state = 0;
    s32 index = 0;
    s32 fd = 0;
    s32 others = 0;
    GRef<u32>(r + 4) = 0;
    GRef<u32>(saves) = 0;
    GRef<u32>(names) = GRef<u32>(0x004116F0);
    GRef<u32>(names + 4) = GRef<u32>(0x004116F4);
    const u64 a[1] = {GRef<u32>(r + 0xC)};
    ee::sprintf(pfs, 0x004116C8, a, 1);
    while (ret == 0) {
        switch (state) {
            case 0: {
                const u64 b[2] = {w + 0x4014, GRef<u32>(0x003F6A74)};
                ee::sprintf(dev, 0x004116D0, b, 2);
                const s32 v = sceMount(pfs, dev, 0, 0, 0);
                if (v != 0) {
                    ret = -1;
                    GRef<s32>(r + 4) = -v;
                } else {
                    GRef<u32>(r + 4) = 0;
                    state = 1;
                }
                break;
            }
            case 1: {
                const u64 b[2] = {GRef<u32>(r + 0xC), GRef<u32>(names + static_cast<u32>(index) * 4)};
                ee::sprintf(path, 0x0040A918, b, 2);
                const s32 v = sceDopen(path);
                if (!(v < 0)) {
                    state = 2;
                    fd = v;
                    if (index == 0) state = 3;
                } else if (v == -2) {
                    if (index != 0) {
                        state = 5;
                        GRef<s32>(r + 4) = 2;
                    } else {
                        index = 1;
                    }
                } else {
                    state = 5;
                    GRef<s32>(r + 4) = -v;
                }
                break;
            }
            case 2: {
                const s32 n = func_0025D070(fd);
                state = 4;
                if (n < 0)
                    GRef<s32>(r + 4) = -n;
                else
                    GRef<s32>(saves) = n;
                break;
            }
            case 3: {
                const s32 n = func_0025D168(fd);
                if (!(n < 0))
                    others = n;
                else
                    GRef<s32>(r + 4) = -n;
                state = 4;
                break;
            }
            case 4: {
                const s32 v = sceDclose(fd);
                const s32 err = GRef<s32>(r + 4);
                if (v != 0) {
                    ret = -1;
                    if (err == 0) GRef<s32>(r + 4) = -v;
                } else if (err == 0) {
                    index += 1;
                    state = index < 2 ? 1 : 5;
                } else {
                    ret = -1;
                }
                break;
            }
            case 5: {
                const s32 v = sceUmount(pfs);
                if (v != 0) {
                    ret = -1;
                    GRef<s32>(r + 4) = -v;
                } else {
                    GRef<u16>(w + 0x4034) = static_cast<u16>(others);
                    ret = 1;
                    GRef<u16>(w + 0x4036) = static_cast<u16>(GRef<u32>(saves));
                }
                break;
            }
            default:
                break;
        }
    }
    return ret;
}

AC04_GUEST_FUNCTION(0x0025BF70, func_0025BF70);
AC04_GUEST_FUNCTION(0x0025C108, func_0025C108);
AC04_GUEST_FUNCTION(0x0025C178, func_0025C178);
AC04_GUEST_FUNCTION(0x0025C340, func_0025C340);
AC04_GUEST_FUNCTION(0x0025C3A0, func_0025C3A0);
AC04_GUEST_FUNCTION(0x0025C410, func_0025C410);
AC04_GUEST_FUNCTION(0x0025C7E0, func_0025C7E0);
AC04_GUEST_FUNCTION(0x0025C838, func_0025C838);
AC04_GUEST_FUNCTION(0x0025C888, func_0025C888);
AC04_GUEST_FUNCTION(0x0025C8E8, func_0025C8E8);
AC04_GUEST_FUNCTION(0x0025C948, func_0025C948);
AC04_GUEST_FUNCTION(0x0025CA50, func_0025CA50);
AC04_GUEST_FUNCTION(0x0025CB00, func_0025CB00);
AC04_GUEST_FUNCTION(0x0025CB50, func_0025CB50);
AC04_GUEST_FUNCTION(0x0025CBB0, func_0025CBB0);
AC04_GUEST_FUNCTION(0x0025CC20, func_0025CC20);
AC04_GUEST_FUNCTION(0x0025CC90, func_0025CC90);
AC04_GUEST_FUNCTION(0x0025CD00, func_0025CD00);
AC04_GUEST_FUNCTION(0x0025CD70, func_0025CD70);
AC04_GUEST_FUNCTION(0x0025CF08, func_0025CF08);
AC04_GUEST_FUNCTION(0x0025CF10, func_0025CF10);
AC04_GUEST_FUNCTION(0x0025D2A8, func_0025D2A8);
AC04_GUEST_FUNCTION(0x0025D488, func_0025D488);
AC04_GUEST_FUNCTION(0x0025D4E8, func_0025D4E8);
AC04_GUEST_FUNCTION(0x0025D5B8, func_0025D5B8);
AC04_GUEST_FUNCTION(0x0025D618, func_0025D618);
AC04_GUEST_FUNCTION(0x0025D668, func_0025D668);
AC04_GUEST_FUNCTION(0x0025D6B8, func_0025D6B8);
AC04_GUEST_FUNCTION(0x0025D788, func_0025D788);
AC04_GUEST_FUNCTION(0x0025D7E0, func_0025D7E0);
AC04_GUEST_FUNCTION(0x0025D838, func_0025D838);
AC04_GUEST_FUNCTION(0x0025D8A0, func_0025D8A0);
AC04_GUEST_FUNCTION(0x0025D908, func_0025D908);
AC04_GUEST_FUNCTION(0x0025D950, func_0025D950);
AC04_GUEST_FUNCTION(0x0025D9A0, func_0025D9A0);
AC04_GUEST_FUNCTION(0x0025DA30, func_0025DA30);
AC04_GUEST_FUNCTION(0x0025DA80, func_0025DA80);
AC04_GUEST_FUNCTION(0x0025DAD0, func_0025DAD0);
AC04_GUEST_FUNCTION(0x0025DB78, func_0025DB78);
AC04_GUEST_FUNCTION(0x0025DBC0, func_0025DBC0);
AC04_GUEST_FUNCTION(0x0025DC20, func_0025DC20);
AC04_GUEST_FUNCTION(0x0025DF68, func_0025DF68);

// ---- Scene 7: HDD error screen and its message window (slice S8) --------------------------------
// The window is an older copy of the message window widget (message_window.cpp): object of 0x50
// bytes with the same fields up to +0x2B, then +0x2C/+0x2E s16 position, +0x30 s16, +0x32 u8
// type-out speed, +0x33 s8 voice, +0x34 s16 chars typed, +0x36 s16 speed counter, +0x38/+0x3C/+0x40
// textures (cursor glow, frame, page arrow); characters are 16 pixels wide and only control codes 8
// and 11 change the colour.

namespace {

void PutW(u32 p, u32 i, u32 v) { GRef<u32>(p + 4 * i) = v; }

AC04_GUEST_VAR(u32, g_HddErrorWork, 0x00411728)    // gp-0x42C8 (0x1E0 bytes)
AC04_GUEST_VAR(u32, g_HddErrorWindow, 0x00411908)  // gp-0x40E8 (0x50 bytes)

}  // namespace

// EE: 0x0024D470
// Initialises the scene 7 message window; `kind` 1: no frame, darker text, position y 0x50.
void func_0024D470(u32 win, s32 kind) {
    GRef<u16>(win + 4) = 0x2500;
    GRef<u8>(win + 6) = 8;
    GRef<u8>(win + 7) = 0x14;
    GRef<u16>(win + 8) = 0x3000;
    GRef<u16>(win + 0x12) = 0x1E0;
    GRef<u8>(win + 0x26) = 0xC1;
    GRef<u8>(win + 0x28) = 0x99;
    GRef<u8>(win + 0x29) = 0xB1;
    GRef<u8>(win + 0x2B) = 0x80;
    GRef<u8>(win + 0x32) = 1;
    GRef<u8>(win + 0xC) = 0;
    GRef<u8>(win) = 0;
    GRef<u8>(win + 0xA) = 0;
    GRef<u8>(win + 0xB) = 0;
    GRef<u8>(win + 0xD) = 0;
    GRef<u16>(win + 0xE) = 0;
    GRef<u32>(win + 0x18) = 0;
    GRef<u16>(win + 0x16) = 0;
    GRef<u16>(win + 0x10) = 0;
    GRef<u16>(win + 0x14) = 0;
    GRef<u16>(win + 0x34) = 0;
    static const u8 kColours[] = {0x99, 0xB1, 0xC1, 0x80, 0xFF, 0xFF, 0xFF, 0x80, 0xFF, 0xB1};
    for (u32 i = 0; i < sizeof(kColours); ++i) GRef<u8>(win + 0x1C + i) = kColours[i];
    GRef<u8>(win + 0x27) = 0x80;
    GRef<u8>(win + 0x2A) = 0xFF;
    GRef<u16>(win + 0x2C) = 0x38;
    GRef<u16>(win + 0x2E) = 0x166;
    GRef<u16>(win + 0x30) = 0x14;
    GRef<u8>(win + 0x33) = 0xFF;
    GRef<u32>(win + 0x38) = 0;
    GRef<u32>(win + 0x3C) = 0;
    GRef<u32>(win + 0x40) = 0;
    if (kind != 1) return;
    GRef<u8>(win + 0x1E) = 0xE8;
    GRef<u8>(win + 0xC) = 0x10;
    GRef<u16>(win + 0x2E) = 0x50;
    GRef<u8>(win + 0x1C) = 0xE8;
    GRef<u8>(win + 0x1D) = 0xE8;
}

// EE: 0x0024D580
// Loads the font block, relocates the text block's page table and counts each page's characters,
// and (when `gfx` is given) the three window textures at TBP 0x2500 / CBP 0x3E00.
void func_0024D580(u32 win, u32 font, u32 text, u32 gfx) {
    if ((GRef<u8>(win + 0xC) & 1) != 0) return;
    LoadFontBlock(font);
    GRef<s8>(win + 0xB) = static_cast<s8>(GetMessageCount());
    GRef<u32>(win + 0x18) = text;
    GRef<u32>(text) += text;
    if (0 < GRef<s16>(text + 4)) {
        u32 block = text;
        for (s32 i = 0;;) {
            const u32 page = GRef<u32>(block) + static_cast<u32>(i) * 8;
            GRef<u32>(page) += text;
            s32 total = 0;
            for (s32 j = 0; j < GRef<s16>(page + 4); ++j) {
                total += GetMessageLength(static_cast<u32>(static_cast<s32>(GRef<s16>(GRef<u32>(page) + static_cast<u32>(j) * 8 + 4))));
            }
            ++i;
            GRef<u16>(page + 6) = static_cast<u16>(total);
            block = GRef<u32>(win + 0x18);
            if (!(i < GRef<s16>(block + 4))) break;
        }
    }
    if (gfx != 0) {
        guest::GuestFrame reader(0x10);
        InitContainerReader(reader.Address(), gfx);
        for (u32 k = 0; k < 3; ++k) {
            const u32 slot = win + 0x38 + 4 * k;
            GRef<u32>(slot) = WorkHeapAlloc(0x28, 0x10);
            InitGimTexture(GRef<u32>(slot), GetContainerItem(reader.Address(), static_cast<s32>(k)));
            const u32 tex = GRef<u32>(slot);
            GRef<u16>(GRef<u32>(tex + 4)) = 0x2500;
            GRef<u16>(GRef<u32>(tex + 8)) = 0x3E00;
        }
    }
    const u32 block = GRef<u32>(win + 0x18);
    const u8 last = GRef<u8>(block + 7);
    GRef<u8>(win + 0x33) = last;
    if (last != 0xFF) GRef<u8>(win + 0x33) = static_cast<u8>(last - 1);
    if (GRef<u8>(block + 6) == 1) GRef<u8>(win + 0xC) |= 4;
    GRef<u8>(win + 0xC) |= 1;
}

// EE: 0x0024D7A8
// Dead (no reference): restarts the current page of a multi-page window (state bit 1 set, counters
// cleared) and replays its voice (+0x33, unless 0xFF or a voice is still playing).
void func_0024D7A8(u32 win) {
    if (GRef<s8>(win + 0xB) == 0) return;
    const u8 state = GRef<u8>(win + 0xC);
    if ((state & 4) == 0) return;
    const u8 voice = GRef<u8>(win + 0x33);
    GRef<u8>(win + 0xC) = state | 2;
    GRef<u16>(win + 0x34) = 0;
    GRef<u16>(win + 0x36) = 0;
    GRef<u16>(win + 0x14) = 0;
    GRef<u16>(win + 0xE) = 0;
    GRef<u16>(win + 0x10) = 0;
    if (voice == 0xFF) return;
    if (func_00257290() != 0) return;
    func_002572A8(GRef<u8>(win + 0x33), 1);
}

// EE: 0x0024D828
// Dead (no reference): typed-character count (+0x34) to the current page's total + 4 when below it
// and not on the last page; otherwise advanced by the type-out limit (+0x12).
void func_0024D828(u32 win) {
    const u32 block = GRef<u32>(win + 0x18);
    const s32 page = GRef<s8>(win + 0xD);
    const s32 end = GRef<s16>(GRef<u32>(block) + static_cast<u32>(page) * 8 + 6) + 4;
    const u16 typed = GRef<u16>(win + 0x34);
    if (GRef<s16>(win + 0x34) < end && page < GRef<s16>(block + 4) - 1) {
        GRef<s16>(win + 0x34) = static_cast<s16>(end);
        return;
    }
    GRef<u16>(win + 0x34) = static_cast<u16>(typed + GRef<u16>(win + 0x12));
}

// EE: 0x0024D888
// Shows page `page` (ignored in multi-page mode or when out of range).
void func_0024D888(u32 win, s32 page) {
    const u8 state = GRef<u8>(win + 0xC);
    if ((state & 4) != 0) return;
    const u8 cleared = state & 0xFD;
    GRef<u8>(win + 0xC) = cleared;
    if (!(page < GRef<s16>(GRef<u32>(win + 0x18) + 4))) return;
    GRef<s8>(win + 0xD) = static_cast<s8>(page);
    GRef<u8>(win + 0xC) = cleared | 2;
    GRef<u8>(win) &= 0xFE;
    GRef<u16>(win + 0x34) = 0;
    GRef<u16>(win + 0x36) = 0;
    GRef<u16>(win + 0x14) = 0;
    GRef<u16>(win + 0xE) = 0;
    GRef<u16>(win + 0x10) = 0;
}

// EE: 0x0024D8E0
// Dead (no reference): back to page 0, state bit 1 cleared, counters +0xE and +0x16 cleared.
void func_0024D8E0(u32 win) {
    GRef<u8>(win + 0xD) = 0;
    GRef<u16>(win + 0xE) = 0;
    GRef<u8>(win + 0xC) = GRef<u8>(win + 0xC) & 0xFD;
    GRef<u16>(win + 0x16) = 0;
}

// EE: 0x0024D900
// Per-frame type-out (as UpdateMessageWindow 0x0024F230 with the old offsets).
void func_0024D900(u32 win) {
    const u8 page = GRef<u8>(win + 0xD);
    const u32 block = GRef<u32>(win + 0x18);
    const u8 state = GRef<u8>(win + 0xC);
    const s32 cur = static_cast<s8>(page);
    if ((state & 4) == 0 || cur == GRef<s16>(block + 4) - 1) {
        if (GRef<s16>(GRef<u32>(block) + static_cast<u32>(cur) * 8 + 6) < GRef<s16>(win + 0x14)) GRef<u8>(win) |= 1;
    }
    s16 shown = GRef<s16>(win + 0x14);
    if (GRef<s16>(win + 0x12) < shown) {
        if ((state & 4) != 0 && cur < GRef<s16>(block + 4) - 1) {
            GRef<u16>(win + 0x34) = 0;
            GRef<u8>(win + 0xD) = static_cast<u8>(page + 1);
            shown = 0;
            GRef<u16>(win + 0x36) = 0;
            GRef<u16>(win + 0x14) = 0;
        }
    } else {
        const s16 tick = static_cast<s16>(GRef<s16>(win + 0x36) + 1);
        GRef<s16>(win + 0x36) = tick;
        const u32 speed = GRef<u8>(win + 0x32);
        // As in 0x0024F230: tick % (speed - 1) == speed - 1 never holds, so only speed 1 types.
        if (speed == 1 || (tick % static_cast<s32>(speed - 1)) == static_cast<s32>(speed - 1)) {
            GRef<s16>(win + 0x34) = static_cast<s16>(GRef<s16>(win + 0x34) + 1);
        }
    }
    GRef<s16>(win + 0x10) = static_cast<s16>(GRef<s16>(win + 0x10) + 1);
    if (shown < GRef<s16>(win + 0x34)) GRef<s16>(win + 0x14) = GRef<s16>(win + 0x34);
}

// EE: 0x0024DA30
// Dead (no reference): draws message `line` (+4 message id, only when below the window's message
// count +0xB) as one sprite of the uploaded glyph strip: TEXFLUSH, TEST_2 0x30003, ALPHA_2 0x44,
// FBA_2 0, then PRIM 0x356 / TEX0_2 (glyph TBP/TBW/PSM, CLUT) / RGBAQ (+0x1C) / UV / XYZ2 at
// (x +0x2C, y +0x2E + row * 20) with the message width (16-pixel-high strip). Old-widget copy of func_0024F360. Words the
// original leaves unwritten (GIFtag register high words, NOP and UV high words) stay unwritten.
void func_0024DA30(u32 win, u32 list, s32 index, u32 line, s32 row) {
    const s32 msg = GRef<s16>(line + 4);
    if (!(msg < GRef<s8>(win + 0xB))) return;
    UploadMessageGlyphs(list, 0, msg, GRef<u16>(win + 4));
    const s32 width = GetMessageWidth(GRef<s16>(line + 4));
    const u32 p = BeginPacket();
    PutW(p, 0x00, 0x10000002);
    PutW(p, 0x01, 0);
    PutW(p, 0x02, 0);
    PutW(p, 0x03, 0x50000002);
    PutW(p, 0x04, 0x8001);
    PutW(p, 0x05, 0x10000000);
    PutW(p, 0x06, 0xE);
    PutW(p, 0x0A, 0x3F);
    PutW(p, 0x0C, 0x10000004);
    PutW(p, 0x0D, 0);
    PutW(p, 0x0E, 0x11000000);
    PutW(p, 0x0F, 0x50000004);
    PutW(p, 0x10, 0x8003);
    PutW(p, 0x12, 0xE);
    PutW(p, 0x11, 0x10000000);
    PutW(p, 0x14, 0x30003);
    PutW(p, 0x16, 0x48);
    PutW(p, 0x18, 0x44);
    PutW(p, 0x1A, 0x43);
    PutW(p, 0x19, 0);
    PutW(p, 0x1B, 0);
    PutW(p, 0x1C, 0);
    PutW(p, 0x1E, 0x4B);
    PutW(p, 0x20, 0x10000005);
    PutW(p, 0x21, 0);
    PutW(p, 0x22, 0);
    PutW(p, 0x23, 0x50000005);
    PutW(p, 0x24, 0x8001);
    PutW(p, 0x25, 0x84000000);
    PutW(p, 0x26, 0x4343F170);
    GRef<u64>(p + 0xA0) = 0x356;
    PutW(p, 0x2A, static_cast<u32>(GRef<s16>(win + 4)) | 0x80000000u |
                     (static_cast<u32>(GRef<s8>(win + 7)) << 20 | static_cast<u32>(GRef<s8>(win + 6)) << 14) | 0x28000000u);
    PutW(p, 0x2B, static_cast<u32>(GRef<s16>(win + 8)) << 5 | 0x20000000u | (static_cast<u32>(GRef<s8>(win + 0xA)) << 19 | 6));
    PutW(p, 0x2C, GRef<u8>(win + 0x1C) | static_cast<u32>(GRef<u8>(win + 0x1F)) << 24 |
                     (static_cast<u32>(GRef<u8>(win + 0x1E)) << 16 | static_cast<u32>(GRef<u8>(win + 0x1D)) << 8));
    GRef<f32>(p + 0xB4) = 1.0f;
    PutW(p, 0x30, 0x80008);
    const s32 ly = row * 0x14 + 0x720;
    const s32 y = GRef<s16>(win + 0x2E);
    const s32 x = GRef<s16>(win + 0x2C);
    PutW(p, 0x33, 0);
    PutW(p, 0x32, static_cast<u32>(x + 0x6CE) << 4 | static_cast<u32>(y + ly) << 20);
    PutW(p, 0x34, static_cast<u32>(width) << 4 | 0x1080008u);
    PutW(p, 0x36, static_cast<u32>(x + width + 0x6CE) << 4 | static_cast<u32>(y + 0x10 + ly) << 20);
    PutW(p, 0x37, 0);
    PutW(p, 0x38, 0x10000000);
    PutW(p, 0x39, 0);
    PutW(p, 0x3A, 0);
    PutW(p, 0x3B, 0x11000000);
    EndPacket(list, index, p + 0xF0);
}

// EE: 0x0024DD68
// Draws page `page` of the scene 7 window into bucket `index` of `list` (as DrawMessagePage
// 0x0024F698 with 16-pixel characters): cursor glow, page arrow, text lines, three-row frame.
void func_0024DD68(u32 win, u32 list, s32 index, u32 page) {
    s32 line_len[8] = {};
    const s32 nlines = GRef<s16>(page + 4);
    for (s32 i = 0; i < nlines; ++i) {
        line_len[i] = GetMessageLength(static_cast<u32>(static_cast<s32>(GRef<s16>(GRef<u32>(page) + static_cast<u32>(i) * 8 + 4))));
    }

    u32 p = BeginPacket();
    PutW(p, 1, 0);
    PutW(p, 0, 0x10000002);
    PutW(p, 2, 0);
    PutW(p, 3, 0x50000002);
    PutW(p, 4, 0x8001);
    PutW(p, 6, 0xE);
    PutW(p, 5, 0x10000000);
    PutW(p, 0xA, 0x3F);
    PutW(p, 0xC, 0x10000005);
    PutW(p, 0xD, 0);
    PutW(p, 0xE, 0x11000000);
    PutW(p, 0xF, 0x50000005);
    PutW(p, 0x10, 0x8004);
    PutW(p, 0x12, 0xE);
    PutW(p, 0x11, 0x10000000);
    PutW(p, 0x14, 0x30003);
    PutW(p, 0x16, 0x48);
    PutW(p, 0x18, 0x48);
    PutW(p, 0x1A, 0x43);
    PutW(p, 0x19, 0);
    PutW(p, 0x1B, 0);
    PutW(p, 0x1E, 0x4B);
    PutW(p, 0x1C, 0);
    PutW(p, 0x20, 0);
    PutW(p, 0x21, 0x80);
    PutW(p, 0x22, 0x3B);
    EndPacket(list, index, p + 0x90);

    // Cursor glow behind the last four typed characters.
    if ((GRef<u8>(win + 0xC) & 0x10) == 0 && GRef<u32>(win + 0x38) != 0) {
        const u32 glow = GRef<u32>(win + 0x38);
        p = BuildTextureUpload(glow, BeginPacket(), 1, 0);
        const u64 tex0 = GetTextureTex0(glow);
        s32 line_start = 0;
        for (s32 line = 0; line < nlines; ++line) {
            const s32 y = line * 0x14 + 0x71E;
            const s32 shown = GRef<s16>(win + 0x14);
            for (s32 k = 0; k < 4; ++k) {
                const s32 c = (shown + k - 1) - line_start;
                if (c < 0 || !(c < line_len[line])) continue;
                const s32 x = c * 0x10 + 0x6CA;
                PutW(p, 1, 0);
                PutW(p, 0, 0x10000005);
                PutW(p, 2, 0);
                PutW(p, 3, 0x50000005);
                PutW(p, 4, 0x8001);
                PutW(p, 5, 0x84000000);
                PutW(p, 6, 0x4343F710);
                GRef<u64>(p + 0x20) = 0x356;
                const u32 a = fpu::FixUnsSfSi(static_cast<f32>(k + 1) * 0.25f * 128.0f);
                PutW(p, 0xA, a << 24 | 0x808080);
                GRef<f32>(p + 0x2C) = 1.0f;
                GRef<u64>(p + 0x30) = tex0;
                PutW(p, 0x10, 0x80008);
                const s32 wx = GRef<s16>(win + 0x2C);
                const s32 wy = GRef<s16>(win + 0x2E);
                PutW(p, 0x13, 0);
                PutW(p, 0x12, static_cast<u32>(wx + x) << 4 | static_cast<u32>(wy + y) << 20);
                PutW(p, 0x14, 0x2080208);
                PutW(p, 0x17, 0);
                PutW(p, 0x16, static_cast<u32>(wx + 0x20 + x) << 4 | static_cast<u32>(wy + 0x20 + y) << 20);
                p += 0x60;
            }
            line_start += line_len[line];
        }
        EndPacket(list, index, p);
    }

    // Page arrow (multi-page windows) once the page is typed out.
    if (GRef<u32>(win + 0x40) != 0 && (GRef<u8>(win + 0xC) & 4) != 0) {
        s32 end = 0xF;
        for (s32 i = 0; i < nlines; ++i) end += line_len[i];
        if (end < GRef<s16>(win + 0x14) && GRef<s8>(win + 0xD) < GRef<s16>(GRef<u32>(win + 0x18) + 4) - 1) {
            const s32 last = nlines - 1;
            const s32 x = line_len[last] * 0x10 + 0x6CA;
            const u32 arrow = GRef<u32>(win + 0x40);
            p = BuildTextureUpload(arrow, BeginPacket(), 1, 0);
            const u64 tex0 = GetTextureTex0(arrow);
            PutW(p, 1, 0);
            PutW(p, 0, 0x10000005);
            PutW(p, 2, 0);
            PutW(p, 3, 0x50000005);
            PutW(p, 4, 0x8001);
            PutW(p, 5, 0x84000000);
            PutW(p, 6, 0x4343F710);
            GRef<u64>(p + 0x20) = 0x356;
            s32 phase = ((GRef<s16>(win + 0x14) - end) + 0xF) % 0x1E - 0xF;
            if (phase < 0) phase = -phase;
            const u32 a = fpu::FixUnsSfSi((static_cast<f32>(phase) / 30.0f) * 128.0f);
            PutW(p, 0xA, a << 24 | 0x808080);
            GRef<f32>(p + 0x2C) = 1.0f;
            GRef<u64>(p + 0x30) = tex0;
            PutW(p, 0x10, 0x80008);
            const s32 wx = GRef<s16>(win + 0x2C);
            const s32 ly = GRef<s16>(win + 0x2E) + last * 0x14 + 0x72C;
            PutW(p, 0x13, 0);
            PutW(p, 0x12, static_cast<u32>(wx + x) << 4 | static_cast<u32>(ly) << 20);
            PutW(p, 0x14, 0x580108);
            PutW(p, 0x17, 0);
            PutW(p, 0x16, static_cast<u32>(wx + 0x10 + x) << 4 | static_cast<u32>(ly + 5) << 20);
            EndPacket(list, index, p + 0x60);
        }
    }

    // Text lines.
    s32 line_start = 0;
    if (0 < nlines && !(GRef<s16>(win + 0x14) < 0)) {
        s32 line = 0;
        for (;;) {
            const u32 rec = GRef<u32>(page) + static_cast<u32>(line) * 8;
            s32 count = GRef<s16>(win + 0x14) - line_start;
            if (line_len[line] < count) count = line_len[line];
            const s64 msg = GRef<s16>(rec + 4);
            if (msg < GRef<s8>(win + 0xB)) {
                UploadMessageGlyphs(list, 0, msg, GRef<u16>(win + 4));
                p = BeginPacket();
                for (s32 k = 0; k < count; ++k) {
                    const u16 code = GetMessageChar(static_cast<u32>(static_cast<s32>(GRef<s16>(rec + 4))), k);
                    u32 base = 0x1C;
                    if (code == 8)
                        base = 0x24;
                    else if (code == 0xB)
                        base = 0x28;
                    const u32 r = GRef<u8>(win + base), g = GRef<u8>(win + base + 1), b = GRef<u8>(win + base + 2);
                    const u32 alpha = GRef<u8>(win + base + 3);
                    const s32 pos = k + line_start;
                    s32 shown = GRef<s16>(win + 0x14);
                    if (pos == shown - 1) {
                        PlayMenuSe(0, 9);  // type-out tick
                        shown = GRef<s16>(win + 0x14);
                    }
                    PutW(p, 1, 0);
                    PutW(p, 0, 0x10000005);
                    PutW(p, 2, 0);
                    PutW(p, 3, 0x50000005);
                    PutW(p, 5, 0x84000000);
                    PutW(p, 4, 0x8001);
                    PutW(p, 6, 0x4343F170);
                    GRef<u64>(p + 0x20) = 0x356;
                    PutW(p, 0xA, static_cast<u32>(static_cast<s32>(GRef<s16>(win + 4))) | 0x80000000u |
                                     static_cast<u32>(static_cast<s32>(GRef<s8>(win + 7)) << 20) |
                                     static_cast<u32>(static_cast<s32>(GRef<s8>(win + 6)) << 14) | 0x28000000u);
                    PutW(p, 0xB, static_cast<u32>(static_cast<s32>(GRef<s16>(win + 8)) << 5) | 0x20000000u |
                                     static_cast<u32>(static_cast<s32>(GRef<s8>(win + 0xA)) << 19) | 6u);
                    if (pos < shown - 4) {
                        GRef<f32>(p + 0x34) = 1.0f;
                        PutW(p, 0xC, r | alpha << 24 | b << 16 | g << 8);
                    } else {
                        const u32 hb = GRef<u8>(win + 0x22), hg = GRef<u8>(win + 0x21), hr = GRef<u8>(win + 0x20);
                        const u32 ha = GRef<u8>(win + 0x23);
                        const f32 t = static_cast<f32>((shown - pos) - 1) * 0.25f;
                        GRef<f32>(p + 0x34) = 1.0f;
                        const f32 u = fpu::Sub(1.0f, t);
                        const s32 cb = static_cast<s32>(fpu::Add(static_cast<f32>(b) * t, static_cast<f32>(hb) * u));
                        const s32 cg = static_cast<s32>(fpu::Add(static_cast<f32>(g) * t, static_cast<f32>(hg) * u));
                        const s32 cr = static_cast<s32>(fpu::Add(static_cast<f32>(r) * t, static_cast<f32>(hr) * u));
                        const s32 ca = static_cast<s32>(fpu::Add(static_cast<f32>(alpha) * t, static_cast<f32>(ha) * u));
                        PutW(p, 0xC, (static_cast<u32>(cr) & 0xFF) | static_cast<u32>(ca) << 24 |
                                         (static_cast<u32>(cb) & 0xFF) << 16 | (static_cast<u32>(cg) & 0xFF) << 8);
                    }
                    const s32 y = line * 0x14 + 0x722;
                    const s32 wx = GRef<s16>(win + 0x2C);
                    const s32 wy = GRef<s16>(win + 0x2E);
                    PutW(p, 0x10, static_cast<u32>(k) << 8 | 0x80008);
                    PutW(p, 0x12, static_cast<u32>(wx + k * 0x10 + 0x6CE) << 4 | static_cast<u32>(wy + y) << 20);
                    PutW(p, 0x13, 0);
                    PutW(p, 0x14, static_cast<u32>(k + 1) << 8 | 0x1080008);
                    PutW(p, 0x16, static_cast<u32>(wx + (k + 1) * 0x10 + 0x6CE) << 4 | static_cast<u32>(wy + 0x10 + y) << 20);
                    PutW(p, 0x17, 0);
                    p += 0x60;
                }
                PutW(p, 1, 0);
                PutW(p, 0, 0x10000000);
                PutW(p, 2, 0);
                PutW(p, 3, 0x11000000);
                EndPacket(list, index, p + 0x10);
                line_start += line_len[line];
            }
            ++line;
            if (!(line < nlines) || GRef<s16>(win + 0x14) < line_start) break;
        }
    }

    // Frame: three rows faded in as the text reaches them, with a separator line per row.
    if ((GRef<u8>(win + 0xC) & 0x10) != 0) return;
    p = BeginPacket();
    PutW(p, 1, 0);
    PutW(p, 0, 0x10000002);
    PutW(p, 2, 0x11000000);
    PutW(p, 3, 0x50000002);
    PutW(p, 4, 0x8001);
    PutW(p, 5, 0x10000000);
    PutW(p, 6, 0xE);
    PutW(p, 9, 0);
    PutW(p, 0xB, 0);
    PutW(p, 8, 0x48);
    PutW(p, 0xA, 0x43);
    p = BuildTextureUpload(GRef<u32>(win + 0x3C), p + 0x30, 1, 0);
    const u64 tex0 = GetTextureTex0(GRef<u32>(win + 0x3C));
    const s32 wy = GRef<s16>(win + 0x2E);
    const s32 wx = GRef<s16>(win + 0x2C);
    const s32 shown = GRef<s16>(win + 0x14);
    const u32 x_left = static_cast<u32>(wx + 0x6BB) << 4;
    const u32 x_sep = static_cast<u32>(wx + 0x6C0) << 4;
    const u32 x_right = static_cast<u32>(wx + 0x6CB) << 4;
    s32 y_body = wy + 0x720;
    s32 y_top = wy + 0x71B;
    u32 y_body_bits = static_cast<u32>(y_body) << 20;
    s32 y_off = 0x720;
    s32 row_start = 0;
    for (s32 row = 0; row < 3; ++row) {
        s32 a = 0;
        if (!(shown < row_start)) {
            a = 0x80;
            if (!(shown < row_start + 8)) a = shown < row_start + 0x10 ? (0x10 - (shown - row_start)) << 4 : 0;
        }
        if (!(row < nlines)) a = 0;
        const u32 colour = static_cast<u32>(a) << 24 | 0xFFFFFF;
        const u32 y_bottom_bits = static_cast<u32>(wy + 0x14 + y_off) << 20;
        const auto sprite = [&](u32 q, u32 uv0, u32 xy0, u32 uv1, u32 xy1) {
            PutW(q, 0, 0x10000005);
            PutW(q, 1, 0);
            PutW(q, 2, 0);
            PutW(q, 3, 0x50000005);
            PutW(q, 4, 0x8001);
            PutW(q, 5, 0x84000000);
            PutW(q, 6, 0x4343F170);
            GRef<u64>(q + 0x20) = 0x356;
            GRef<u64>(q + 0x28) = tex0;
            PutW(q, 0xC, colour);
            GRef<f32>(q + 0x34) = 1.0f;
            PutW(q, 0x10, uv0);
            PutW(q, 0x12, xy0);
            PutW(q, 0x13, 0);
            PutW(q, 0x14, uv1);
            PutW(q, 0x16, xy1);
            PutW(q, 0x17, 0);
        };
        sprite(p, 0x80008, x_left | static_cast<u32>(y_top) << 20, 0x580108, x_right | y_body_bits);
        sprite(p + 0x60, 0x580008, x_left | y_body_bits, 0x680108, x_right | y_bottom_bits);
        sprite(p + 0xC0, 0x680008, x_left | y_bottom_bits, 0xB80108, x_right | static_cast<u32>(y_top + 0x1E) << 20);
        PutW(p, 0x48, 0x10000003);
        PutW(p, 0x49, 0);
        PutW(p, 0x4A, 0);
        PutW(p, 0x4B, 0x50000003);
        PutW(p, 0x4C, 0x8001);
        PutW(p, 0x4D, 0x44000000);
        PutW(p, 0x4E, 0x4410);
        GRef<u64>(p + 0x140) = 0x241;  // line, alpha blend, context 2
        GRef<f32>(p + 0x14C) = 1.0f;
        PutW(p, 0x52, (shown < row_start || !(row < nlines)) ? 0x80686054u : 0x80D1C1A9u);
        y_top += 0x14;
        PutW(p, 0x55, 0);
        PutW(p, 0x54, x_sep | y_body_bits);
        PutW(p, 0x56, x_sep | static_cast<u32>(y_body + 0x14) << 20);
        PutW(p, 0x57, 0);
        y_body_bits += 0x1400000;
        y_body += 0x14;
        p += 0x160;
        row_start += line_len[row];
        y_off += 0x14;
    }
    EndPacket(list, index, p);
}

// EE: 0x0024EC30
// Draws the scene 7 window when it is loaded and shown: font CLUT upload, draw environment
// registers (ctx+0x150, +0x160 with the high word forced to 1, +0x180, +0x170), the current page.
void func_0024EC30(u32 win, u32 cam, u32 list) {
    (void)cam;
    const u8 state = GRef<u8>(win + 0xC);
    if (((state ^ 1) & 1) != 0 || (state & 2) == 0) return;
    UploadMessageGlyphs(list, 0, -1, GRef<u16>(win + 8));
    const u32 p = BeginPacket();
    PutW(p, 1, 0);
    PutW(p, 0, 0x10000005);
    PutW(p, 2, 0);
    PutW(p, 3, 0x50000005);
    PutW(p, 4, 0x8004);
    PutW(p, 5, 0x10000000);
    PutW(p, 6, 0xE);
    const u32 ctx = CtxAddr();
    CopyQ(p + 0x20, ctx + 0x150);
    CopyQ(p + 0x30, ctx + 0x160);
    GRef<u32>(p + 0x34) = 1;
    CopyQ(p + 0x40, ctx + 0x180);
    CopyQ(p + 0x50, ctx + 0x170);
    EndPacket(list, 0, p + 0x60);
    func_0024DD68(win, list, 0, GRef<u32>(GRef<u32>(win + 0x18)) + static_cast<u32>(static_cast<s32>(GRef<s8>(win + 0xD)) * 8));
}

// EE: 0x00261428
// Scene 7 sub 0: stops the streams and voices, the work (0x1E0 bytes, first-frame flag +0x1D0),
// packet buffers (2 x 0x10000 qwords), and the window from the resources embedded in the executable
// (0x003F8730, 0x2360 bytes: the disc may be unreadable); sub 1.
void InitHddErrorScene() {
    func_0021FB70(0.0f);
    StopAllVoices();
    const u32 w = WorkHeapAlloc(0x1E0, 0x10);
    g_HddErrorWork() = w;
    GRef<u32>(w + 0x1D0) = 1;
    AllocDoubleBuffer(CtxAddr() + 0x40, 0x10000);
    Ctx()->cur_buf.addr = SwapDoubleBuffer(CtxAddr() + 0x40);
    const u32 res = WorkHeapAlloc(0x2360, 0x10);
    for (u32 i = 0; i < 0x236; ++i) CopyQ(res + i * 0x10, 0x003F8730 + i * 0x10);
    guest::GuestFrame frame(0x10);
    const u32 reader = frame.Address();
    InitContainerReader(reader, res);
    const u32 win = WorkHeapAlloc(0x50, 0x10);
    g_HddErrorWindow() = win;
    func_0024D470(win, 1);
    const u32 font = GetContainerItem(reader, 0);
    const u32 text = GetContainerItem(reader, 1);
    func_0024D580(g_HddErrorWindow(), font, text, 0);
    RequestSubState(1);
}

// EE: 0x00261530
// Scene 7 sub 1: on the first frame the 640 x 448 display (PSMZ16S depth), the 2D camera and page 0
// of the window; then the window each frame, and Start (pad 0x10) returns to the saved scene (the
// menu scene re-enters menu 1).
void RunHddErrorScene() {
    const u32 list = g_HddErrorWork() + 0x1C0;
    InitBucketList(list, 1);
    SetPacketListBuckets(CtxAddr() + 0x60, list);
    const u32 w = g_HddErrorWork();
    if ((GRef<u32>(w + 0x1D0) & 1) != 0) {
        MainContext* ctx = Ctx();
        ctx->frame_psm = 0;
        ctx->draw_h = 0x1C0;
        ctx->disp_w = 0x280;
        ctx->disp_h = 0xE0;
        ctx->draw_w = 0x280;
        for (u8& c : ctx->clear_rgba) c = 0;
        Ctx()->zbuf_psm = 0x3A;
        Ctx()->ztest = 3;
        Ctx()->draw_mode = 0;
        Ctx()->disp_flags = 0x8000;
        BuildDrawEnvPackets();
        Init2dCamera(w);
        GRef<u32>(g_HddErrorWork() + 0x1D0) &= 0xFFFFFFFEu;
        func_0024D888(g_HddErrorWindow(), 0);
        return;
    }
    func_0024D900(g_HddErrorWindow());
    func_0024EC30(g_HddErrorWindow(), w, list);
    if ((Ctx()->pad_buttons[0][1] & 0x10) == 0) return;
    const u32 saved = GRef<u32>(0x00411718);
    if (GRef<u32>(saved) == 4) EnterMenu(1);
    RequestScene(GRef<u16>(GRef<u32>(0x00411718)));
}

// EE: 0x00261680
// SceneHddError (scene 7): sub-state table 0x00411720 (2 subs). Reached only from the HDD paths.
void SceneHddError() { guest::CallGuest(GRef<u32>(0x00411720 + 4u * Ctx()->sub)); }

AC04_GUEST_FUNCTION(0x00261680, SceneHddError);
AC04_GUEST_FUNCTION(0x00261428, InitHddErrorScene);
AC04_GUEST_FUNCTION(0x00261530, RunHddErrorScene);

}  // namespace ac04::game
