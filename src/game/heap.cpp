// Work heap: the game's bump allocator over one large malloc block (docs/game/memory.md,
// docs/port/skeleton-readiness.md §3). Globals: size 0x00411784, base 0x00411788,
// offset 0x0041178C, mark 0x00411790.
#include "game/game.h"

#include "sce/sce.h"

namespace ac04::game {

using guest::GRef;

AC04_GUEST_VAR(u32, g_WorkHeapSize, 0x00411784)
AC04_GUEST_VAR(u32, g_WorkHeapBase, 0x00411788)
AC04_GUEST_VAR(u32, g_WorkHeapOffset, 0x0041178C)
AC04_GUEST_VAR(u32, g_WorkHeapMark, 0x00411790)

namespace {

// The four words 0x00412198-0x004121A4 and the byte 0x00411794 are cleared with the heap state.
void ClearHeapSideState() {
    GRef<u8>(0x00411794) = 0;
    for (s32 i = 3; i >= 0; --i) GRef<u32>(0x00412198 + 4u * static_cast<u32>(i)) = 0;
}

}  // namespace

// EE: 0x00100EC8
// Finds the largest malloc block: tries 0x1FFEFF0 - (heap end + extra) - k*0x100 bytes until malloc
// succeeds. On the PS2 the result is 0x01A318A4 (measured); the port's guest malloc manages exactly
// that region, so the same value comes out (src/sce/libc.cpp).
u32 ProbeLargestMallocSize() {
    u32& heap_ptr = GRef<u32>(0x004104B0);
    heap_ptr += 0x1000;
    const u32 limit = heap_ptr + GRef<u32>(0x004104AC);
    u32 step = 0;
    u32 size = 0;
    u32 block = 0;
    for (;;) {
        size = 0x01FFEFF0u - limit - step;
        block = ee::malloc(size);
        if (block != 0) break;
        step += 0x100;
    }
    ee::free(block);
    return size;
}

// EE: 0x00100F48
void InitWorkHeap() {
    const u32 size = ProbeLargestMallocSize();
    g_WorkHeapSize() = size;
    g_WorkHeapBase() = ee::malloc(size);
    g_WorkHeapOffset() = 0;
    g_WorkHeapMark() = 0;
    ClearHeapSideState();
}

// EE: 0x00100FB0
// Exit path only (unreachable by default): _free_r(_impure_ptr, base).
void FreeWorkHeap() {
    ee::free(g_WorkHeapBase());
}

// EE: 0x00101000
// Aligns the absolute address (base + offset) to `align` (signed modulo, as the original), then
// bumps the offset. Returns 0 when the heap is exhausted; callers do not check (memory.md).
u32 WorkHeapAlloc(s32 size, u64 align) {
    if (align > 1) {
        const s32 rem = static_cast<s32>(g_WorkHeapBase() + g_WorkHeapOffset()) % static_cast<s32>(align);
        if (rem != 0) g_WorkHeapOffset() += static_cast<u32>(static_cast<s32>(align) - rem);
    }
    const u32 next = g_WorkHeapOffset() + static_cast<u32>(size);
    u32 result = g_WorkHeapBase() + g_WorkHeapOffset();
    g_WorkHeapOffset() = next;
    if (g_WorkHeapSize() < next) result = 0;
    return result;
}

// EE: 0x001010A8
u32 SetWorkHeapMark() {
    const u32 offset = g_WorkHeapOffset();
    GRef<u8>(0x00411794) = 0;
    g_WorkHeapMark() = offset;
    for (s32 i = 3; i >= 0; --i) GRef<u32>(0x00412198 + 4u * static_cast<u32>(i)) = 0;
    return offset;
}

// EE: 0x001010F0
u32 ResetWorkHeapToMark() {
    const u32 mark = g_WorkHeapMark();
    GRef<u8>(0x00411794) = 0;
    g_WorkHeapOffset() = mark;
    for (s32 i = 3; i >= 0; --i) GRef<u32>(0x00412198 + 4u * static_cast<u32>(i)) = 0;
    return mark;
}

// EE: 0x00102668
u32 AllocAndLoadDataEntry(s32 entry) {
    const u32 buf = WorkHeapAlloc(static_cast<s32>(GetDataEntryAllocSize(entry)), 0x40);
    LoadDataEntryAsync(entry, buf);
    return buf;
}

}  // namespace ac04::game
