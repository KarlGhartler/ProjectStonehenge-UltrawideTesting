// EE hardware registers seen by game code (docs/port/skeleton-readiness.md §12).
// - Storage for every register in 0x10000000-0x1000FFFF, so writes read back (DMA channel CHCR set
//   by SendDmaPacketList through sceDmaGetChan, DMAC ring registers, T0 mode).
// - Timer 0 COUNT (0x10000000) counts H-blanks: the main loop resets it every frame and the movie
//   code waits for a count. Each vblank adds one field of lines (262 or 263, NTSC interlaced). Each
//   read inside a field adds one line, so a polling loop always progresses (movie pacing is refined
//   in the movie milestone; the boot path only writes the counter).
#pragma once

#include "common/guest_memory.h"

namespace ac04::sce {

class EeRegisters final : public guest::HardwareHandler {
public:
    static EeRegisters& Get();

    // Maps the register range into guest memory.
    void Install(guest::GuestMemory& memory);
    void OnVBlank(u64 vblank_index);
    // A busy-wait poll lets one H-blank pass (timer 0 + 1); false when the field is over (the
    // caller then lets the vblank pass).
    bool AdvanceLine();

    void Read(u32 address, void* dst, u32 size) override;
    void Write(u32 address, const void* src, u32 size) override;

    u32 Word(u32 address) const;
    void SetWord(u32 address, u32 value);

private:
    static constexpr u32 kBase = 0x10000000u;
    static constexpr u32 kSize = 0x10000u;
    u8 regs_[kSize] = {};
    u32 t0_count_ = 0;
    u32 field_reads_ = 0;  // H-blanks since the last vblank: T0_COUNT reads and busy-wait polls
    u32 field_len_ = 262;  // lines of the current field
};

}  // namespace ac04::sce
