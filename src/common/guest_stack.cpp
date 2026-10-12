#include "common/guest_stack.h"

#include <cstdlib>

#include "common/guest_memory.h"
#include "platform/log.h"

namespace ac04::guest {
namespace {

u32 g_sp = kInitialStackPointer;

}  // namespace

u32 StackPointer() { return g_sp; }

void ResetStack(u32 sp) { g_sp = sp; }

GuestFrame::GuestFrame(u32 bytes) : previous_sp_(g_sp) {
    const u32 size = (bytes + 15u) & ~15u;
    if (g_sp - kStackBase < size) {
        // The PS2 would silently overwrite the heap below the stack; the port stops instead,
        // because continuing would corrupt game state in a way the original never did.
        AC04_LOG_ERROR("guest", "guest stack overflow: frame of %u bytes at sp 0x%08X", size, g_sp);
        log::Flush();
        std::abort();
    }
    g_sp -= size;
    base_ = g_sp;
    Mem().Fill(base_, 0, size);  // deterministic contents (the PS2 would hold stale data)
}

GuestFrame::~GuestFrame() { g_sp = previous_sp_; }

}  // namespace ac04::guest
