// IOP stand-ins (ADR-004): host replacements for the IOP-side RPC servers the game binds to.
// The SDK layer (src/sce/libsif.cpp) copies the EE send buffer in and the reply out; services never
// see guest memory.
//
// Milestone C stand-ins return the replies measured in PCSX2 (docs/pcsx2/sessions.md run 03):
// - SOUND (0x81000101) fno 0: count 2, then zeros. fno 1 (command batch): the documented reply
//   records (docs/subsystems/sound.md), with transfers finishing at once and no mixing yet.
// - VAG (0x81000102): VAG.IRX's VagAllInit / VagCtrl / VagMain (stream channels driven by the SPU2 IRQ).
// - USBJOY (0x80000210) fno 2: "no device" block.
// Commands the stand-ins do not implement are accepted and logged once per (rpc, fno).
#pragma once

#include <cstdint>
#include <functional>
#include <memory>

#include "platform/audio/spu_mixer.h"

namespace ac04::platform::iop {

class RpcService {
public:
    virtual ~RpcService() = default;
    virtual std::uint32_t RpcNumber() const = 0;
    virtual const char* Name() const = 0;
    // `send` holds `send_size` bytes from the EE; `reply` (reply_size bytes) is copied back to the
    // EE receive buffer afterwards. It starts out as the current contents of that buffer, so a
    // service that does not answer leaves it unchanged.
    virtual void Call(std::uint32_t fno, const std::uint8_t* send, std::uint32_t send_size, std::uint8_t* reply,
                      std::uint32_t reply_size) = 0;
};

// Reads size bytes of IOP RAM at ddr (null when out of range).
using IopRead = std::function<const std::uint8_t*(std::uint32_t addr, std::uint32_t size)>;
// SOUND service; with a mixer the voices are played and the voice status comes from it.
std::unique_ptr<RpcService> MakeSoundService(audio::SpuMixer* mixer = nullptr, IopRead iop_read = {});
// VAG service (VAG.IRX stand-in); with a mixer the streams play on core-1 voices 18-23.
std::unique_ptr<RpcService> MakeVagService(audio::SpuMixer* mixer = nullptr, IopRead iop_read = {});
std::unique_ptr<RpcService> MakeUsbJoyService();

}  // namespace ac04::platform::iop
