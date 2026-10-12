// EE kernel, fileio, libmc and libsif entry points.
// - Threads and semaphores are only used by the HDD path, which is dead in the retail build
//   (docs/port/dead-code.md): logging stubs that return the kernel error value.
// - SIF RPC calls go to the IOP stand-ins (src/platform/iop/) and complete at once.
#include <cstring>
#include <vector>

#include "common/guest_call.h"
#include "common/guest_memory.h"
#include "common/guest_ref.h"
#include "platform/log.h"
#include "sce/runtime.h"
#include "sce/sce.h"
#include "sce/sce_host.h"

using ac04::guest::GRef;

// ---- kernel -----------------------------------------------------------------------------------

// PS2: ends the program (returns to the OSD). Port: ends the game thread.
void Exit(s32 status) {
    AC04_LOG_INFO("game", "Exit(%d) called: the game thread ends", status);
    throw ac04::sce::GameRuntime::GameStop{};
}

// Kernel threads and semaphores (HDD worker only): stubs, error -1 (KE_ERROR).
s32 CreateThread(u32) {
    AC04_SCE_STUB("CreateThread");
    return -1;
}
s32 StartThread(s32, u32) {
    AC04_SCE_STUB("StartThread");
    return -1;
}
s32 ChangeThreadPriority(s32, s32) {
    AC04_SCE_STUB("ChangeThreadPriority");
    return -1;
}
s32 GetThreadId() {
    AC04_SCE_STUB("GetThreadId");
    return -1;
}
s32 CreateSema(u32) {
    AC04_SCE_STUB("CreateSema");
    return -1;
}
s32 SignalSema(s32) {
    AC04_SCE_STUB("SignalSema");
    return -1;
}
s32 iSignalSema(s32) {
    AC04_SCE_STUB("iSignalSema");
    return -1;
}
s32 WaitSema(s32) {
    AC04_SCE_STUB("WaitSema");
    return -1;
}

// PS2: writes back / invalidates the data cache (mode 0) or the instruction cache (mode 2).
// The port has no caches.
void FlushCache(s32) {}

// ---- fileio (HDD path only, plus sceFsReset on the boot path) -------------------------------

// PS2 (0x00267178, hypothesis name): rebinds the fileio RPC after an IOP reboot; no result used.
// Port: nothing to reset (no fileio server); implemented as a no-op.
s32 sceFsReset() { return 0; }

s32 sceFsInit() {
    AC04_SCE_STUB("sceFsInit");
    return -1;
}
s32 sceOpen(u32, s32) {
    AC04_SCE_STUB("sceOpen");
    return -1;
}
s32 sceClose(s32) {
    AC04_SCE_STUB("sceClose");
    return -1;
}
s32 sceLseek(s32, s32, s32) {
    AC04_SCE_STUB("sceLseek");
    return -1;
}
s32 sceRead(s32, u32, s32) {
    AC04_SCE_STUB("sceRead");
    return -1;
}
s32 sceWrite(s32, u32, s32) {
    AC04_SCE_STUB("sceWrite");
    return -1;
}
s32 sceIoctl2(s32, s32, u32, u32, u32, u32) {
    AC04_SCE_STUB("sceIoctl2");
    return -1;
}
s32 sceRemove(u32) {
    AC04_SCE_STUB("sceRemove");
    return -1;
}
s32 sceDopen(u32) {
    AC04_SCE_STUB("sceDopen");
    return -1;
}
s32 sceDclose(s32) {
    AC04_SCE_STUB("sceDclose");
    return -1;
}
s32 sceDread(s32, u32) {
    AC04_SCE_STUB("sceDread");
    return -1;
}
s32 sceGetstat(u32, u32) {
    AC04_SCE_STUB("sceGetstat");
    return -1;
}
s32 sceDevctl(u32, s32, u32, u32, u32, u32) {
    AC04_SCE_STUB("sceDevctl");
    return -1;
}

// ---- libmc ------------------------------------------------------------------------------------

// PS2: binds the MCSERV RPC; returns 0. The save backend (folder memory cards) comes with the save
// milestone; nothing else from libmc is reached before it.
s32 sceMcInit() { return 0; }

// ---- libsif -----------------------------------------------------------------------------------
// sceSifClientData: rpcd {paddr, pid, tid, mode} +0x00, command +0x10, buff +0x14, cbuff +0x18,
// end_function +0x1C, end_param +0x20, serve +0x24.

namespace {

ac04::platform::iop::RpcService* FindService(u32 rpc_number) {
    for (auto& s : ac04::sce::Host().iop_services) {
        if (s->RpcNumber() == rpc_number) return s.get();
    }
    return nullptr;
}

}  // namespace

// PS2: initialises the SIF RPC layer. Nothing to do.
void sceSifInitRpc(u32) {}

// PS2 (0x002660B0): sends a bind request for `rpc_number`; when the server exists, the IOP fills
// client->serve (non-zero) — the game loops until it is set. Returns 0 (or -1 on a SIF error).
// Port: binds to the stand-in; serve gets a stand-in address (expected RAM difference).
s32 sceSifBindRpc(u32 client, u32 rpc_number, u32 /*mode*/) {
    if (FindService(rpc_number) == nullptr) {
        ac04::log::WriteOnce("sif:bind-unknown", ac04::log::Level::Error, "sif", "bind to unknown RPC server 0x%08X",
                             rpc_number);
        return -1;
    }
    GRef<u32>(client + 0x10) = rpc_number;
    GRef<u32>(client + 0x24) = 0x000F0000u | (rpc_number & 0xFFFFu);  // stand-in serve address
    return 0;
}

// PS2 (0x00266280): sends `ssize` bytes to function `fno` of the bound server; the reply (rsize
// bytes) is DMAed back into `receive`. Mode 1 = no wait (completion seen through
// sceSifCheckStatRpc). Returns 0. Port: runs the stand-in at once.
s32 sceSifCallRpc(u32 client, u32 fno, u32 /*mode*/, u32 send, s32 ssize, u32 receive, s32 rsize, u32 end_func,
                  u32 /*end_param*/) {
    auto* service = FindService(GRef<u32>(client + 0x10));
    if (service == nullptr) {
        ac04::log::WriteOnce("sif:call-unbound", ac04::log::Level::Error, "sif", "RPC call on an unbound client 0x%08X",
                             client);
        return -1;
    }
    std::vector<u8> in(static_cast<size_t>(ssize > 0 ? ssize : 0));
    if (!in.empty()) ac04::guest::Mem().ReadBytes(send, in.data(), static_cast<u32>(in.size()));
    std::vector<u8> out(static_cast<size_t>(rsize > 0 ? rsize : 0));
    if (!out.empty()) ac04::guest::Mem().ReadBytes(receive, out.data(), static_cast<u32>(out.size()));
    service->Call(fno, in.data(), static_cast<u32>(in.size()), out.data(), static_cast<u32>(out.size()));
    if (!out.empty()) ac04::guest::Mem().WriteBytes(receive, out.data(), static_cast<u32>(out.size()));
    if (end_func != 0) {
        ac04::log::WriteOnce("sif:endfunc", ac04::log::Level::Warning, "sif", "RPC end function 0x%08X not called",
                             end_func);
    }
    return 0;
}

// PS2: non-zero while the last call on this client is in progress. Port: calls complete at once.
s32 sceSifCheckStatRpc(u32) { return 0; }

// PS2: binds the IOP heap RPC; returns 0.
s32 sceSifInitIopHeap() { return 0; }

// PS2: loads an IRX module from the disc; returns the module id (>= 0). Port: the modules are
// replaced by the stand-ins; returns a new id.
s32 sceSifLoadModule(u32 filename, s32, u32) {
    static s32 next_id = 0x20;
    AC04_LOG_INFO("sif", "IOP module %s replaced by the port", ac04::guest::GStr(filename).c_str());
    return next_id++;
}

// PS2: 1 once the IOP finished rebooting.
s32 sceSifSyncIop() { return 1; }

// PS2: reboots the IOP with the given IOPRP image; returns 1 when the request was sent.
s32 sceSifRebootIop(u32) { return 1; }
