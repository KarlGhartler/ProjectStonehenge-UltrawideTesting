// Boot path: crt0 equivalent, main() and its init sequence (docs/game/boot.md,
// docs/port/skeleton-readiness.md §1-2).
#include "game/game.h"

#include "common/ee_math.h"
#include "common/guest_stack.h"
#include "sce/ee_hw.h"
#include "sce/sce.h"

namespace ac04::game {

using guest::GRef;

// EE: 0x00100008
// crt0 (`entry`). The original zeroes .sbss/.bss (done by the ELF loader), sets gp, calls the
// kernel's SetupThread (stack = top of RAM, 0x100000 bytes; returns sp 0x01FFED60) and SetupHeap
// (heap from the end of .bss, 0x004CC74C), runs _InitSys and FlushCache(0), then
// main(*0x004C55C0, 0x004C55C4) and Exit(result). The kernel parts are the runtime's job.
void Entry() {
    guest::ResetStack(guest::kInitialStackPointer);
    // _InitSys 0x0026B2D0 (SDK helper thread, ChangeThreadPriority): no game-visible effect.
    FlushCache(0);
    const s32 result = Main();
    Exit(result);
}

// EE: 0x001001F0
s32 Main() {
    ee::__main();
    InitMainContext();
    IopBoot();
    InitHardware();
    InitWorkHeap();
    InitGameStateGlobals();
    InitOptionData();
    InitRandomTable();
    InitSubsystems();
    ApplyOptionSettings();
    Ctx()->flags |= 0x800;
    InitGraphics();
    LoadBootData();
    PadSystemInit();
    InitSprRingBuffer();
    SetWorkHeapMark();
    while (sceGsSyncV(0) == 0) {
    }
    for (;;) {
        SendDmaPacketList(1, g_pMainContext().addr + offsetof(MainContext, vif1_list), 1);
        SendDmaPacketList(2, g_pMainContext().addr + offsetof(MainContext, gif_list), 0);
        const u32 buf = SwapDoubleBuffer(g_pMainContext().addr + offsetof(MainContext, dbuf));
        Ctx()->cur_buf = ps2ptr<void>(buf);
        if ((~Ctx()->flags & 0x40000) != 0) UpdatePads();
        FlushCache(0);
        UpdateSoundRpc();
        UpdateVagStream();
        const s32 r = SceneStep();
        const u32 field = WaitVSync();
        sce::EeHwWrite(sce::EeReg::kT0Count, 0);
        UpdateDisplayFrame(field);
        MainContext* ctx = Ctx();
        ctx->frame_count += 1;
        if ((~ctx->flags & 0x100) != 0) ctx->frame_count_unpaused += 1;
        u32 flags = ctx->flags;
        if ((flags & 0x800) == 0) {
            if ((flags & 0x400) != 0) {
                ctx->flags = flags & 0xFFFFDAFFu;
                flags = ctx->flags;
            }
            if ((flags & 0x200) != 0) ctx->flags = (flags & 0xFFFFFDFFu) | 0x100;
        } else {
            ctx->flags = flags & 0xFFFFD8FFu;
        }
        UpdateCdQueue();
        if (r != 0) {
            // Unreachable: SceneStep always returns 0 (translated as in the original).
            ShutdownPads();
            FreeWorkHeap();
            sceCdInit(5);
            ee::printf(0x00403700 /* "Good Bye!\n" */, nullptr, 0);
            return 0;
        }
    }
}

// EE: 0x001000D8
void InitSubsystems() {
    InitCdFileSystem(0x0029F180, 0x0029C980, 0x0029D180);
    BuildCdLsnTable();
    InitSaveSystem(func_0025F298());
    sceSifInitRpc(0);
    while (InitSoundRpc() == 0) {
    }
    func_0021E2D8();  // VAG stream init loop
    func_0021FC20();  // stream request queue init
    func_00217778(GRef<u32>(0x00353C8C));
    func_00219110();  // positional SE defaults
    func_0021ADF8();  // clear the queued positional lists
    func_0021A1C0();  // apply the category volumes
    func_0021A680();  // SOUND commands 6/7: channel 1 setup
    InitMoviePlayback();
}

// EE: 0x00100400
void IopBoot() {
    sceSifInitRpc(0);
    sceCdInit(0);
    const s32 disk_type = sceCdGetDiskType();
    if (disk_type == 0x12) {
        sceCdMmode(1);
    } else if (disk_type == 0x14) {
        sceCdMmode(2);
    }
    sceFsReset();
    while (sceSifRebootIop(0x00403710 /* "cdrom0:\IOPRP23.IMG;1" */) == 0) {
    }
    while (sceSifSyncIop() == 0) {
    }
    sceSifInitRpc(0);
    sceFsReset();
    sceSifInitIopHeap();
    sceCdInit(0);
    if (disk_type == 0x12) {
        sceCdMmode(1);
    } else if (disk_type == 0x14) {
        sceCdMmode(2);
    }
    sceFsReset();
    // The nine IOP modules (SIO2MAN, PADMAN, MCMAN, MCSERV, LIBSD, SOUND, VAG, USBD, USBJOY).
    static const u32 kModules[] = {0x00403728, 0x00403748, 0x00403760, 0x00403780, 0x004037A0,
                                   0x004037B8, 0x004037D0, 0x004037E8, 0x00403800};
    for (u32 path : kModules) {
        while (sceSifLoadModule(path, 0, 0) < 0) {
        }
    }
    sceSifInitIopHeap();
}

// EE: 0x00100620
void InitHardware() {
    sceGsResetPath();
    sceDmaReset(1);
    sceVu0ResetVif0();
    sceCdInit(0);
}

// EE: 0x00100650
void InitMainContext() {
    MainContext& m = g_MainContext();
    m.flags = 0;
    g_pMainContext() = ps2ptr<MainContext>(kMainContextAddress);
    m.req = 0;
    m.scene = 0;
    m.sub = 0;
    m.prev_scene = 0;
    m.next_scene = 0;
    m.scene_work[0] = 0;
    m.scene_work[1] = 0;
    m.scene_work[2] = 0;
    m.scene_work[3] = 0;
    m.frame_count = 0;
    m.frame_count_unpaused = 0;
    ResetDoubleBuffer(kMainContextAddress + offsetof(MainContext, dbuf));
    ResetPacketList(g_pMainContext().addr + offsetof(MainContext, gif_list));
    ResetPacketList(g_pMainContext().addr + offsetof(MainContext, vif1_list));
}

// EE: 0x001006C0
// HYPOTHESIS: InitGameStateGlobals. Defaults of the game-state block 0x0029F350
// (docs/game/main-context.md): per-player entries, mission id, flags.
void InitGameStateGlobals() {
    GRef<u8>(0x0029F379) = 1;
    GRef<u8>(0x0029F378) = 2;
    GRef<u32>(0x002BBC90) = 0;
    GRef<u32>(0x002BBCB0) = 0;
    GRef<u8>(0x0029F37D) = 0;
    g_GameState() = ps2ptr<u8>(kGameStateAddress);
    GRef<u8>(0x0029F374) = 0;
    GRef<u8>(0x0029F375) = 1;
    GRef<u8>(0x0029F376) = 0;
    GRef<u8>(0x0029F377) = 1;
    GRef<u8>(0x0029F37A) = 0;
    GRef<u32>(0x002BBC94) = 0;
    GRef<u32>(0x002BBC98) = 0;
    GRef<u32>(0x0029F370) = 0x002BBC90;
    GRef<u8>(0x0029F37C) = 0;
    GRef<u8>(0x0029F37B) = 0;
    for (s32 i = 0; i < 2; ++i) {
        ps2ptr<u8> gs = g_GameState();
        gs[i * 0x10] = 2;
        gs[i * 0x10 + 1] = 0;
        gs[i * 0x10 + 2] = static_cast<u8>(i);
        gs[i * 0x10 + 3] = 0;
    }
}

// EE: 0x00100768
void LoadBootData() {
    const u32 ticket = QueueCdRead(1, GetCdFileLsn(2), 0, GetTblBuffer(0), 0, 2, ~0ull);
    while (GetCdDoneTicket(1) < ticket) {
        UpdateCdQueue();
        sceGsSyncV(0);
    }
    const u32 entry0 = AllocAndLoadDataEntry(0);
    while (IsDataLoadDone() == 0) {
        UpdateCdQueue();
        sceGsSyncV(0);
    }
    // DATA entry 0 is a count + offsets container: store its sub-block pointers.
    MainContext* ctx = Ctx();
    const s32 count = GRef<s32>(entry0);
    for (s32 i = 0; i < count; ++i) {
        ctx->data0_table[i] = ps2ptr<void>(entry0 + GRef<u32>(entry0 + 4 * (i + 1)));
    }
    for (s32 i = 0; i < 0x15; ++i) func_0021F908(i, func_0021F968(i));  // sound bank LSN table
    LoadDataEntryAsync(0x166, func_0021F8A8());
    while (IsDataLoadDone() == 0) {
        UpdateCdQueue();
        sceGsSyncV(0);
    }
    func_0021F8B8();  // link the 21 stream header sub-blocks
}

// EE: 0x00100CB8
// Dead (no reference): uploads image `src` to VRAM (dbp, dbw, dpsm, x, y, w, h) with
// sceGsSetDefLoadImage / sceGsExecLoadImage after a cache flush, and waits for PATH 2.
void func_00100CB8(s16 dbp, s16 dbw, s16 dpsm, s16 x, s16 y, s16 w, s16 h, u32 src) {
    guest::GuestFrame frame(0x60);
    const u32 lp = frame.Address();
    sceGsSetDefLoadImage(lp, dbp, dbw, dpsm, x, y, w, h);
    FlushCache(0);
    sceGsExecLoadImage(lp, src);
    sceGsSyncPath(0, 0);
}

// EE: 0x00100D38
void InitRandomTable() {
    ee::srand(1);
    ee::srand(123456789);
    // 512 floats at 0x00411998: rand() * 2^-31 (4.656613e-10f), EE rounding (cvt.s.w, mul.s).
    for (u32 i = 0; i < 512; ++i) {
        GRef<f32>(0x00411998 + 4 * i) = fpu::Mul(fpu::IntToFloat(ee::rand()), 4.656613e-10f);
    }
    GRef<u32>(0x00411780) = 0;  // random table cursor
}

// EE: 0x00100DF0
// Dead (no reference): splits a frame count into minutes (out[0]), seconds (out[1]) and hundredths
// (out[2]) at 60 frames per second (k 0x0040DA00 = 1/60); returns min * 6000 + sec * 100 + hundredths.
u32 func_00100DF0(u32 frames, u32 out) {
    const f32 t = fpu::UnsignedToFloat(frames) * GRef<f32>(0x0040DA00);
    const u32 secs = fpu::FixUnsSfSi(t);
    const u32 min = secs / 60;
    const u32 sec = secs % 60;
    GRef<u32>(out) = min;
    GRef<u32>(out + 4) = sec;
    const u32 hund = fpu::FixUnsSfSi(t * 100.0f) % 100;
    GRef<u32>(out + 8) = hund;
    return min * 6000 + sec * 100 + hund;
}

}  // namespace ac04::game
