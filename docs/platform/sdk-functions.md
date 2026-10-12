# Linked libraries in SLUS_201.52

Names are applied in Ghidra and listed in `../symbols.csv`. Every rename and comment is logged in `../rename-log.csv` and can be reverted from it. "Hyp." rows have a `HYPOTHESIS:` plate comment in Ghidra and are not renamed.

## `.text` layout (link order) and symbol counts

Boundaries marked ~ come from the first/last identified function or from library data order (`PsII` tags in `.data`); they are not proven module edges. The link order matches the order of the `PsII` tags in `.data` (0x003FAA90 libgraph → 0x003FAB00 libdma → 0x003FAB80 libpkt → 0x003FAB90 libvu0 tables → 0x003FABC0 libkernl → 0x003FB440 libpad → 0x003FB488 libmc → 0x003FB4C0 libcdvd → 0x003FCF30 libpc/libmpeg → 0x003FD950 libipu).

| Range | Module | Version | Confirmed | Hyp. | Evidence |
|---|---|---|---|---|---|
| 0x00100000–0x001000D7 | crt0 (`entry`, `_exit`, `_root`) | SDK 2.3 | 1 | 1 | ELF entry, SetupThread/SetupHeap inline syscalls |
| 0x001000D8–~0x002616E7 | **Game code**, including the save/HDD module ~0x00259000–0x002616E7 (`BISLUS-20152AC04`, `pfs%d:`, `hdd%d:`) | — | 1 (`main`) | 11 | game strings; two scene handlers live at 0x002613F0/0x00261680 |
| ~0x002616E8–~0x00262337 | libgraph | 2.3.0 | 9 | 0 | `sceGs*` error strings |
| ~0x00262338–0x00262E07 | libdma | 2.3.0 | 5 | 0 | `libdma: sync timeout` |
| 0x00262E08–0x00262F0F | libpkt | 2.3.0 | 0 | 5 | `PsIIlibpkt` tag order |
| 0x00262F10–~0x00263C4F | libvu0 | (no tag) | 30 | 2 | VU0 macro-mode shapes, SDK function order |
| 0x00263C80–0x0026450F | libkernl syscall stubs | 2.3.0 | 48 | 0 | `li v1,N; syscall` (92 more uncalled, below). Last stub at 0x00264500 (syscall 0x7F) |
| 0x00264510–0x00264A67 | libkernl/libgraph small C helpers (**not** syscall stubs) | 2.3.0 | 1 | 0 | e.g. `PollVblankStart` 0x00264520 (INTC_STAT VBLANK busy-wait used by `sceGsSyncV`), its SetVSyncFlag variant 0x00264560, INTC/DMAC enable/disable helpers 0x002648B0/0x00264918/0x00264980 |
| 0x00264A68–0x00264ACF | 3 more syscall stubs (`SetSyscall`, `kCopy`, `GetEntryAddress`) | 2.3.0 | 3 | 0 | |
| 0x00264AD0–~0x0026BEFF | libkernl C part (kernel patches, Deci2 TTY, kprintf, sifcmd, sifrpc, fileio, loadfile, iopheap, `sceSifRebootIop`, `_InitSys`) | 2.3.0 | 16 | 7 | `## internel error in libkernl.a!`, `TTY:` strings, SIF cmd ids 0x80000002–0x8000000C, RPC ids 0x80000001/3/6 |
| ~0x0026BF00–~0x0026D2FF | libpad | 2.3.0 | 13 | 0 | `libpad:` strings, padman RPC 0x80000100/0x80000101 |
| ~0x0026D300–~0x0026EBFF | libmc | **2.3.2** | 16 | 1 | `libmc:` strings, mcserv RPC 0x80000400, XMC fno table |
| ~0x0026EC00–0x002707E7 | libcdvd | 2.3.0 | 13 | 1 | `Libcdvd` strings, RPC 0x80000592/593/595/597/59A |
| 0x002707E8–0x0027086F | libpc | 2.3.0 | 2 | 0 | CP0 Perf (PCCR) access |
| 0x00270870–~0x00277FFF | libmpeg (+ libipu) | 2.3.0 | 1 | 5 | `[MPEG ERROR]`, MPEG-2 syntax errors, `CSC handler error` |
| ~0x00278000–~0x0027A4B7 | libc (newlib) misc/stdio helpers | — | 0 | 0 | `0123456789ABCDEF0X`, `(null)` |
| ~0x0027A4B8–~0x00284000 | libstdc++ libio iostream | GCC 2.9x | 0 | 0 | `22_IO_ostream_withassign`, `7filebuf`, … (not used by the game) |
| ~0x00284000–~0x0028A7BF | libm (newlib / fdlibm, float) | — | 11 | 5 | `acosf` … `sqrtf` matherr names |
| ~0x0028A7C0–~0x0028FB1F | libgcc: libgcc2 + fp-bit soft-double + C++ runtime (exception, new, tinfo, frame) | GCC 2.9x | 15 | 3 | `__clz_tab`, fp-bit shapes, `13bad_exception`, `17__class_type_info` |
| ~0x0028FB20–0x00297B33 | libc (newlib, EE-optimised mem*/str*) | — | 22 | 0 | `bug in vfprintf: bad base`, MMI string routines |
| **Total** | | | **201** | **41** | 242 rows in `../symbols.csv` |

## SDK functions the game calls directly (port surface for `src/sce/`)

Counting direct `jal` calls from game code (0x001000D8–0x002616E7):

- **libgraph**: `sceGsResetGraph`, `sceGsResetPath`, `sceGsSetDefDispEnv`, `sceGsPutDispEnv`, `sceGsSyncV`, `sceGsSyncPath`, `sceGsSetDefLoadImage`, `sceGsExecLoadImage`, `sceGsGetGParam`.
- **libdma**: `sceDmaReset`, `sceDmaGetChan`, `sceDmaSend`, `sceDmaSendN`, `sceDmaSync`.
- **libvu0**: about 30 functions. The most used are `sceVu0ApplyMatrix` (141 calls), `sceVu0AddVector` (120), `sceVu0SubVector` (96), `sceVu0ScaleVector` (66), `sceVu0TransMatrix`/`sceVu0CopyVector` (61), `sceVu0MulMatrix`/`sceVu0InversMatrix` (49), `sceVu0UnitMatrix` (144), `sceVu0RotMatrixZ/X/Y` (24/38/40). These are the main candidates for `src/common/vu0_math.h`.
- **libkernl** (counts are total xrefs, SDK-internal ones included): semaphores (`CreateSema` 38, `WaitSema` 45, `SignalSema` 85, `DeleteSema` 80, `PollSema` 21, `iSignalSema` 9), threads (`CreateThread`, `StartThread`, `ChangeThreadPriority`, `GetThreadId`, `ReferThreadStatus`, `SleepThread`, `WakeupThread`, `SuspendThread`), `FlushCache` (27), DMAC/INTC handlers, `SetAlarm`; SIF: `sceSifInitRpc`, `sceSifBindRpc`, `sceSifCallRpc`, `sceSifCheckStatRpc`, `sceSifSetDma`, `sceSifDmaStat`, `sceSifRebootIop`, `sceSifSyncIop`, `sceSifLoadModule`, `sceSifInitIopHeap`, `sceSifLoadIopHeap`, `sceFsReset` (H).
- **libpad**: `scePadInit`, `scePadEnd`, `scePadPortOpen`, `scePadRead`, `scePadGetState`, `scePadGetReqState`, `scePadInfoAct`, `scePadInfoMode`, `scePadSetMainMode`, `scePadSetActDirect`, `scePadSetActAlign`, `scePadInfoPressMode`, `scePadEnterPressMode`.
- **libmc** (called only from the save module ~0x0025E000): `sceMcInit`, `sceMcOpen`, `sceMcClose`, `sceMcRead`, `sceMcWrite`, `sceMcGetInfo`, `sceMcGetDir`, `sceMcChdir`, `sceMcFormat`, `sceMcDelete`, `sceMcFlush`, `sceMcUnformat`.
- **libcdvd**: `sceCdInit`, `sceCdMmode`, `sceCdGetDiskType`, `sceCdSearchFile`, `sceCdRead`, `sceCdSync`, `sceCdDiskReady`, `sceCdStatus`, `sceCdGetError`, `sceCdBreak`, `sceCdSeek`, `sceCdGetToc`, `sceCdReadIOPm` (H). No `sceCdSt*` streaming functions have been identified so far. The game has its own CD request queue (`CdQueueUpdate` (H)).
- **libpc**: `scePcStart`, `scePcStop` (debug profiler class `C_PCCON`).
- **libmpeg**: `sceMpegCreate`, plus `sceMpegInit`/`AddBs`/`Reset`/`IsEnd`/`SetImageBuff` (H).
- **Game-owned IOP RPC servers** (Namco IRX, not Sony): 0x80000210 (bound at 0x001096D8), 0x81000101 (0x002172C8, per-frame `SoundRpcUpdate`), 0x81000102 (0x0021E370, `StreamAudioUpdate`). Which IRX serves which id is not confirmed yet.

## RPC server IDs (confirmed at the `sceSifBindRpc` call sites)

| ID | Client struct | Bound in | Server |
|---|---|---|---|
| 0x80000001 | 0x004C86C0 | `sceFsInit` (H) 0x00266F48 | fileio (IOP `FILEIO`) |
| 0x80000003 | 0x004C8740 | `sceSifInitIopHeap` 0x00269F18 | iopheap |
| 0x80000006 | 0x004C8B00 | `_lf_bind` (H) 0x0026A218 | loadfile |
| 0x80000100 / 0x80000101 | 0x004C9680 / 0x004C96A8 | `scePadInit` 0x0026C0B0 | PADMAN |
| 0x80000400 | (reg) | `sceMcInit` 0x0026D328 | MCSERV |
| 0x80000592 | (reg) | `sceCdInit` 0x0026F858 | cdvdfsv init |
| 0x80000593 | 0x003FCF08 | 0x0026F6E8 | cdvdfsv S-commands |
| 0x80000595 | 0x003FC690 | 0x0026F3D0 | cdvdfsv N-commands |
| 0x80000597 | (reg) | `sceCdSearchFile` 0x0026F0D0 | cdvdfsv search file |
| 0x8000059A | (reg) | `sceCdDiskReady` 0x0026FB38 | cdvdfsv disk ready |
| 0x80000210 | (reg) | game 0x001096D8 | game IRX (SOUND/VAG/USBJOY?) |
| 0x81000101 | (reg s0) | game 0x002172C8 | game IRX |
| 0x81000102 | (reg s5) | game 0x0021E370 | game IRX |

## Uncalled kernel syscall stubs (no function created, per instruction)

141 instances of the stub pattern `li v1,N / syscall / jr ra` exist: 137 in the main block 0x00263C80–0x00264500, 3 helper stubs at 0x00264A68–0x00264AC0, and `_root` @ 0x001000C0. 49 are named (48 called + `_root`). The 92 below have **no callers**, are not disassembled as functions in Ghidra, and get no symbol. Names come from ps2sdk `ee/kernel/include/syscallnr.h`; `?` = number not in that table.

| Stub | v1 | ps2sdk name |
|---|---|---|
| 0x00263C80 | 0x00 | ? |
| 0x00263C90 | 0x01 | ResetEE |
| 0x00263CB0 | 0x03 | ? |
| 0x00263CD0 | 0x05 | ResumeIntrDispatch |
| 0x00263CE0 | 0x06 | _LoadExecPS2 |
| 0x00263CF0 | 0x07 | _ExecPS2 |
| 0x00263D00 | 0x08 | ResumeT3IntrDispatch |
| 0x00263D10 | 0x09 | RFU009 |
| 0x00263D20 | 0x0A | AddSbusIntcHandler |
| 0x00263D30 | 0x0B | RemoveSbusIntcHandler |
| 0x00263D40 | 0x0C | Interrupt2Iop |
| 0x00263D50 | 0x0D | SetVTLBRefillHandler |
| 0x00263D60 | 0x0E | SetVCommonHandler |
| 0x00263D70 | 0x0F | SetVInterruptHandler |
| 0x00263D80 | 0x10 | AddIntcHandler |
| 0x00263D90 | 0x10 | AddIntcHandler2 |
| 0x00263DC0 | 0x12 | AddDmacHandler2 |
| 0x00263E30 | 0xFD | ?? (numbering conflict, see open-questions) |
| 0x00263E80 | -0xFE | ? (numbering conflict, see open-questions) |
| 0x00263E90 | -0xFF | iReleaseAlarm |
| 0x00263EB0 | 0x21 | DeleteThread |
| 0x00263ED0 | 0x23 | ExitThread |
| 0x00263EF0 | 0x25 | TerminateThread |
| 0x00263F00 | -0x26 | iTerminateThread |
| 0x00263F10 | 0x27 | DisableDispatchThread |
| 0x00263F20 | 0x28 | EnableDispatchThread |
| 0x00263F40 | -0x2A | iChangeThreadPriority |
| 0x00263F60 | -0x2C | _iRotateThreadReadyQueue |
| 0x00263F70 | 0x2D | ReleaseWaitThread |
| 0x00263F80 | -0x2E | iReleaseWaitThread |
| 0x00263FB0 | -0x31 | iReferThreadStatus |
| 0x00263FF0 | 0x35 | CancelWakeupThread |
| 0x00264000 | -0x36 | iCancelWakeupThread |
| 0x00264030 | 0x39 | ResumeThread |
| 0x00264040 | -0x3A | iResumeThread |
| 0x00264050 | 0x3B | RFU059 |
| 0x00264060 | 0x3C | SetupThread |
| 0x00264070 | 0x3D | SetupHeap |
| 0x00264090 | 0x3F | ? |
| 0x00264100 | -0x46 | iPollSema |
| 0x00264110 | 0x47 | ReferSemaStatus |
| 0x00264120 | -0x48 | iReferSemaStatus |
| 0x00264130 | 0x49 | ? |
| 0x00264140 | 0x4A | SetOsdConfigParam |
| 0x00264150 | 0x4B | GetOsdConfigParam |
| 0x00264160 | 0x4C | GetGsHParam |
| 0x00264170 | 0x4D | GetGsVParam |
| 0x00264180 | 0x4E | SetGsHParam |
| 0x00264190 | 0x4F | SetGsVParam |
| 0x002641A0 | 0x50 | CreateEventFlag |
| 0x002641B0 | 0x51 | DeleteEventFlag |
| 0x002641C0 | 0x52 | SetEventFlag |
| 0x002641D0 | -0x53 | ? |
| 0x002641E0 | 0x54 | xlaunch |
| 0x002641F0 | -0x55 | iPutTLBEntry |
| 0x00264200 | 0x56 | _SetTLBEntry |
| 0x00264210 | 0x57 | GetTLBEntry |
| 0x00264220 | -0x58 | iProbeTLBEntry |
| 0x00264230 | 0x59 | ExpandScratchPad |
| 0x00264240 | -0x5A | ? |
| 0x00264250 | 0x5B | GetEntryAddress |
| 0x00264260 | 0x5C | EnableIntcHandler |
| 0x00264270 | -0x5C | iEnableIntcHandler |
| 0x00264280 | 0x5D | DisableIntcHandler |
| 0x00264290 | -0x5D | iDisableIntcHandler |
| 0x002642A0 | 0x5E | EnableDmacHandler |
| 0x002642B0 | -0x5E | iEnableDmacHandler |
| 0x002642C0 | 0x5F | DisableDmacHandler |
| 0x002642D0 | -0x5F | iDisableDmacHandler |
| 0x002642E0 | 0x60 | KSeg0 |
| 0x002642F0 | 0x61 | EnableCache |
| 0x00264300 | 0x62 | DisableCache |
| 0x00264310 | 0x63 | GetCop0 |
| 0x00264330 | 0x66 | CpuConfig |
| 0x00264340 | -0x67 | iGetCop0 |
| 0x00264350 | -0x68 | iFlushCache |
| 0x00264360 | -0x6A | iCpuConfig |
| 0x00264380 | 0x6C | SetCPUTimerHandler |
| 0x00264390 | 0x6D | SetCPUTimer |
| 0x002643A0 | 0x6E | SetOsdConfigParam2 |
| 0x002643B0 | 0x6F | GetOsdConfigParam2 |
| 0x002643C0 | 0x70 | GsGetIMR |
| 0x002643D0 | -0x70 | iGsGetIMR |
| 0x002643F0 | -0x71 | iGsPutIMR |
| 0x00264400 | 0x72 | SetPgifHandler |
| 0x00264420 | 0x74 | SetSyscall |
| 0x00264430 | 0x75 | _print |
| 0x00264450 | -0x76 | isceSifDmaStat |
| 0x002644C0 | 0x7B | _ExecOSD |
| 0x002644E0 | 0x7D | PSMode |
| 0x002644F0 | 0x7E | MachineType |
| 0x00264500 | 0x7F | GetMemorySize |

Other syscall sites: `entry` issues 0x3C (SetupThread) and 0x3D (SetupHeap) inline. Two inline `-0x2F` (`_iGetThreadId`) syscalls are at 0x0026B4B8 and 0x0026B5D0; this is Sony's iWakeupThread workaround, probably inside `iWakeupThread`/`iRotateThreadReadyQueue` wrappers.

## libc / libm / libgcc

Names: see `docs/symbols.csv` (kind `libc`/`libgcc`). Facts relevant to the port:

- The game calls `memset` (53 callers) but **never** `memcpy` directly. Game copies use its own routines or inline `lq/sq`.
- The game calls the float libm wrappers: `sinf` 76, `cosf` 77, `atan2f` 45, `sqrtf` 26, `asinf` 18, `fmodf` 11, `acosf` 8, `floorf` 8, `powf` 3, `tanf` 1. Each wrapper returns the fdlibm float result, which an x86 build must reproduce bit for bit for identical behaviour.
- Soft-double helpers are called directly from game code (`__subdf3` 13, `__cmpdf2`? 15, `__truncdfsf2` 11, `__fixunssfsi`? 23, `__extendsfdf2` 20). Some game code uses `double`.
- `rand`/`srand` (newlib LCG 0x41C64E6D, 0x3039) are used by the game (`rand` 35 callers).
- `printf` (13 game callers) and `sprintf` (17) are used.
