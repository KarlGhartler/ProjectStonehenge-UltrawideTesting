// Host side of the SDK layer: what src/sce needs from the platform (disc image, pads, IOP
// stand-ins) and the state it exposes back (DMA/GS counters for the renderer and the overlay).
// main.cpp fills SceHost before starting the game thread. The host only touches this state while
// the game thread is parked at a vsync (ADR-002 lockstep), so no locking is needed.
#pragma once

#include <array>
#include <functional>
#include <memory>
#include <vector>

#include "common/types.h"
#include "platform/input.h"
#include "platform/iop/iop_services.h"
#include "platform/iso9660.h"

namespace ac04::sce {

struct SceHost {
    const platform::Iso9660* iso = nullptr;
    std::function<platform::PadState(int port)> read_pad;
    std::vector<std::unique_ptr<platform::iop::RpcService>> iop_services;
};

// Packet intake statistics (milestone C: packets are counted, not drawn).
struct DmaStats {
    std::array<u64, 10> sends{};    // sceDmaSend calls per channel
    std::array<u64, 10> qwords{};   // qwords walked per channel (all time)
    u64 frame_qwords_gif = 0;       // GIF (ch 2) qwords in the last completed step
    u64 frame_qwords_vif1 = 0;      // VIF1 (ch 1) qwords in the last completed step
    u64 step_qwords_gif = 0;        // accumulating in the current step
    u64 step_qwords_vif1 = 0;
    u64 bad_chains = 0;             // chains with an unknown tag or that did not end
    // Last values written by sceGsPutDispEnv (PMODE, SMODE2, DISPFB, DISPLAY, BGCOLOR).
    std::array<u64, 5> dispenv{};
};

SceHost& Host();
DmaStats& Dma();

// 2 MiB of IOP RAM: sceCdReadIOPm reads land here (stream rings); the EE never reads it.
std::vector<u8>& IopRam();

// Unimplemented or stubbed SDK entry: logged once per name.
void LogUnimplemented(const char* name);
#define AC04_SCE_STUB(name) ::ac04::sce::LogUnimplemented(name)

}  // namespace ac04::sce
