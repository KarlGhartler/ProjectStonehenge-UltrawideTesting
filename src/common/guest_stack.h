// Guest stack (ADR-001): stack memory at PS2 addresses for locals whose address escapes (passed to
// a function that keeps or writes it, or to the SDK as a guest address). Locals whose address never
// escapes stay ordinary host locals.
//
// Layout as on the PS2: crt0 calls SetupThread(stack = -1, size = 0x100000), so the stack occupies
// the top 1 MiB of RAM (0x01F00000-0x01FFFFFF). The kernel returns sp = 0x01FFED60 (measured in
// PCSX2: sp = 0x01FFED40 inside main after its 0x20-byte frame, save state bp_00100220).
#pragma once

#include "common/types.h"

namespace ac04::guest {

constexpr u32 kStackBase = 0x01F00000u;
constexpr u32 kStackSize = 0x00100000u;
constexpr u32 kInitialStackPointer = 0x01FFED60u;

// Current guest stack pointer (one game thread, ADR-002).
u32 StackPointer();
void ResetStack(u32 sp = kInitialStackPointer);

// RAII frame: reserves `bytes` (rounded up to 16, the EE ABI alignment) below the current guest
// stack pointer and releases them on destruction. Frames must nest (LIFO).
class GuestFrame {
public:
    explicit GuestFrame(u32 bytes);
    ~GuestFrame();
    GuestFrame(const GuestFrame&) = delete;
    GuestFrame& operator=(const GuestFrame&) = delete;

    // Guest address of the frame (16-byte aligned), and of a byte offset inside it.
    u32 Address(u32 offset = 0) const { return base_ + offset; }

private:
    u32 base_;
    u32 previous_sp_;
};

}  // namespace ac04::guest
