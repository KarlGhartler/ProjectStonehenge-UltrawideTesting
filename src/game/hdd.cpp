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

}  // namespace ac04::game
