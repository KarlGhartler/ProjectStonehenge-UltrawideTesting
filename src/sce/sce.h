// PC implementations of the Sony SDK, kernel and C runtime functions the game calls
// (docs/port/skeleton-readiness.md §11). Original names; pointer parameters are 32-bit guest
// addresses (ADR-001). Functions not implemented yet log once and return the SDK error value
// (.claude/rules/platform.md).
//
// C runtime functions (malloc, rand, sprintf, ...) live in namespace ee:: so they never collide with
// the host C library.
#pragma once

#include "common/types.h"

// ---- libgraph -------------------------------------------------------------------------------
s32 sceGsResetGraph(s16 mode, s16 inter, s16 omode, s16 ffmode);
void sceGsResetPath();
void sceGsSetDefDispEnv(u32 disp, s16 psm, s16 w, s16 h, s16 dx, s16 dy);
void sceGsPutDispEnv(u32 disp);
s32 sceGsSyncV(s32 mode);
s32 sceGsSyncPath(s32 mode, u16 timeout);
s32 sceGsSetDefLoadImage(u32 lp, s16 dbp, s16 dbw, s16 dpsm, s16 x, s16 y, s16 w, s16 h);
s32 sceGsExecLoadImage(u32 lp, u32 srcaddr);

// ---- libdma ---------------------------------------------------------------------------------
u32 sceDmaGetChan(s32 id);  // guest address of the channel's register block
s32 sceDmaReset(s32 mode);
s32 sceDmaSend(u32 chan, u32 tag);

// ---- libmpeg (on libmpeg2; sceMpeg* pointers are guest addresses) ---------------------------
void sceMpegInit();                                  // 0x00270870
u32 sceMpegCreate(u32 mp, u32 work, s32 size);
void sceMpegReset(u32 mp);                           // 0x00270CC8
void sceMpegAddBs(u32 mp, u32 bs, s32 size);         // 0x00270B58
s32 sceMpegIsEnd(u32 mp);                            // 0x00270CA8
s32 sceMpegGetPicture(u32 mp, u32 rgb32, s32 mbcount);  // 0x00270B90

// ---- libsif DMA -------------------------------------------------------------------------------
u32 sceSifSetDma(u32 transfers, s32 count);
s32 sceSifDmaStat(u32 id);

// ---- libpkt (sceVif1Pk*, hypothesis names) ---------------------------------------------------
void sceVif1PkInit(u32 pkt, u32 buf);
void sceVif1PkReset(u32 pkt);
u32 sceVif1PkTerminate(u32 pkt);
u32 sceVif1PkSize(u32 pkt);
void sceVif1PkEnd(u32 pkt, u32 opt);

// ---- libvu0 ---------------------------------------------------------------------------------
void sceVu0ApplyMatrix(u32 v0, u32 m, u32 v1);
void sceVu0InversMatrix(u32 m0, u32 m1);
void sceVu0UnitMatrix(u32 m);
void sceVu0ScaleVector(u32 v0, u32 v1, f32 t);
void sceVu0CopyVector(u32 v0, u32 v1);
void sceVu0CopyMatrix(u32 m0, u32 m1);
void sceVu0InterVector(u32 v0, u32 v1, u32 v2, f32 t);
void sceVu0MulMatrix(u32 m0, u32 m1, u32 m2);
void sceVu0TransMatrix(u32 m0, u32 m1, u32 tv);
void sceVu0ViewScreenMatrix(u32 m, f32 scrz, f32 ax, f32 ay, f32 cx, f32 cy, f32 zmin, f32 zmax, f32 nearz,
                            f32 farz);
void sceVu0RotMatrixZ(u32 m0, u32 m1, f32 rz);
void sceVu0RotMatrixX(u32 m0, u32 m1, f32 rx);
void sceVu0RotMatrixY(u32 m0, u32 m1, f32 ry);
void sceVu0AddVector(u32 out, u32 a, u32 b);
void sceVu0SubVector(u32 out, u32 a, u32 b);
void sceVu0MulVector(u32 out, u32 a, u32 b);
void sceVu0FTOI4Vector(u32 out, u32 v);
void sceVu0FTOI0Vector(u32 out, u32 v);
void sceVu0Normalize(u32 out, u32 v);
void sceVu0RotTransPers(u32 out, u32 m, u32 v, s32 mode);
void sceVu0DropShadowMatrix(u32 m, u32 lp, f32 a, f32 b, f32 c, s32 mode);
void sceVu0TransposeMatrix(u32 out, u32 m);
void sceVu0OuterProduct(u32 out, u32 a, u32 b);
f32 sceVu0InnerProduct(u32 a, u32 b);
void sceVu0ResetVif0();  // hypothesis name (0x00263BF0)

// ---- kernel -----------------------------------------------------------------------------------
void Exit(s32 status);
s32 CreateThread(u32 param);
s32 StartThread(s32 thread, u32 arg);
s32 ChangeThreadPriority(s32 thread, s32 priority);
s32 GetThreadId();
s32 CreateSema(u32 param);
s32 SignalSema(s32 sema);
s32 iSignalSema(s32 sema);
s32 WaitSema(s32 sema);
void FlushCache(s32 mode);

// ---- libsif / RPC -----------------------------------------------------------------------------
void sceSifInitRpc(u32 mode);
s32 sceSifBindRpc(u32 client, u32 rpc_number, u32 mode);
s32 sceSifCallRpc(u32 client, u32 fno, u32 mode, u32 send, s32 ssize, u32 receive, s32 rsize,
                  u32 end_func, u32 end_param);
s32 sceSifCheckStatRpc(u32 rpc_data);
s32 sceSifInitIopHeap();
s32 sceSifLoadModule(u32 filename, s32 args, u32 argp);
s32 sceSifSyncIop();
s32 sceSifRebootIop(u32 imgname);

// ---- fileio -----------------------------------------------------------------------------------
s32 sceFsInit();   // hypothesis name (0x00266F48)
s32 sceFsReset();  // hypothesis name (0x00267178)
s32 sceOpen(u32 filename, s32 flag);
s32 sceClose(s32 fd);
s32 sceLseek(s32 fd, s32 offset, s32 whence);
s32 sceRead(s32 fd, u32 buf, s32 nbyte);
s32 sceWrite(s32 fd, u32 buf, s32 nbyte);
s32 sceIoctl2(s32 fd, s32 request, u32 arg, u32 arglen, u32 buf, u32 buflen);  // hypothesis name
s32 sceRemove(u32 filename);                                                   // hypothesis name
s32 sceDopen(u32 dirname);                                                     // hypothesis name
s32 sceDclose(s32 fd);                                                         // hypothesis name
s32 sceDread(s32 fd, u32 buf);                                                 // hypothesis name
s32 sceGetstat(u32 name, u32 buf);                                             // hypothesis name
s32 sceDevctl(u32 devname, s32 cmd, u32 arg, u32 arglen, u32 buf, u32 buflen); // hypothesis name

// ---- libpad -----------------------------------------------------------------------------------
s32 scePadInit(s32 mode);
s32 scePadEnd();
s32 scePadPortOpen(s32 port, s32 slot, u32 addr);
s32 scePadRead(s32 port, s32 slot, u32 rdata);
s32 scePadGetState(s32 port, s32 slot);
s32 scePadGetReqState(s32 port, s32 slot);
s32 scePadInfoAct(s32 port, s32 slot, s32 actno, s32 term);
s32 scePadInfoMode(s32 port, s32 slot, s32 term, s32 offs);
s32 scePadSetMainMode(s32 port, s32 slot, s32 offs, s32 lock);
s32 scePadSetActDirect(s32 port, s32 slot, u32 data);
s32 scePadSetActAlign(s32 port, s32 slot, u32 data);
s32 scePadInfoPressMode(s32 port, s32 slot);
s32 scePadEnterPressMode(s32 port, s32 slot);

// ---- libmc ------------------------------------------------------------------------------------
s32 sceMcInit();
s32 sceMcGetInfo(s32 port, s32 slot, u32 type, u32 free, u32 format);
s32 sceMcGetDir(s32 port, s32 slot, u32 name, u32 mode, s32 maxent, u32 table);
s32 sceMcSync(s32 mode, u32 cmd, u32 result);
s32 sceMcOpen(s32 port, s32 slot, u32 name, s32 mode);
s32 sceMcClose(s32 fd);
s32 sceMcRead(s32 fd, u32 buf, s32 size);
s32 sceMcWrite(s32 fd, u32 buf, s32 size);
s32 sceMcFlush(s32 fd);
s32 sceMcMkdir(s32 port, s32 slot, u32 name);
s32 sceMcDelete(s32 port, s32 slot, u32 name);
s32 sceMcFormat(s32 port, s32 slot);

// ---- libcdvd ----------------------------------------------------------------------------------
s32 sceCdSearchFile(u32 fp, u32 name);
s32 sceCdSync(s32 mode);
s32 sceCdInit(s32 mode);
s32 sceCdDiskReady(s32 mode);
s32 sceCdMmode(s32 media);
s32 sceCdRead(u32 lsn, u32 sectors, u32 buf, u32 mode);
s32 sceCdReadIOPm(u32 lsn, u32 sectors, u32 buf, u32 mode);  // hypothesis name
s32 sceCdGetToc(u32 toc);
s32 sceCdSeek(u32 lsn);
s32 sceCdGetDiskType();
s32 sceCdBreak();
s32 sceCdPOffCallback(u32 func, u32 addr);  // hypothesis name
s32 sceCdPowerOff(u32 stat);                // hypothesis name

namespace ac04::sce {
// IPU colour conversion of one pixel (libmpeg.cpp), RGBA out.
void IpuCsc(u8 y, u8 cb, u8 cr, u8* rgba);
}  // namespace ac04::sce

// ---- C runtime (newlib), libm, libgcc ----------------------------------------------------------
namespace ee {

constexpr s32 kMaxPrintfArgs = 16;

u32 malloc(u32 size);
void free(u32 ptr);
u32 memset(u32 dst, s32 value, u32 size);
s32 printf(u32 fmt, const u64* args, s32 count);
s32 sprintf(u32 dst, u32 fmt, const u64* args, s32 count);
void srand(u32 seed);
s32 rand();
u32 strcat(u32 dst, u32 src);
s32 strcmp(u32 a, u32 b);
u32 strcpy(u32 dst, u32 src);
u32 strlen(u32 s);
f32 sinf(f32 x);
f32 cosf(f32 x);
f32 atanf(f32 x);
f32 asinf(f32 x);
f32 acosf(f32 x);
f32 powf(f32 x, f32 y);
f32 atan2f(f32 y, f32 x);
f32 sqrtf(f32 x);
f32 floorf(f32 x);
f32 fmodf(f32 x, f32 y);
void __main();
void __register_frame_info(u32 begin, u32 object);
void __deregister_frame_info(u32 begin);

}  // namespace ee
