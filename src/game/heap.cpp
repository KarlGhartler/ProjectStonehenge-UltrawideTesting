// Work heap: the game's bump allocator over one large malloc block (docs/game/memory.md,
// docs/port/skeleton-readiness.md §3). Globals: size 0x00411784, base 0x00411788,
// offset 0x0041178C, mark 0x00411790.
#include "game/game.h"

#include <cstring>

#include "common/debug_hooks.h"
#include "sce/sce.h"
#include "game/main_context.h"

#if defined(_MSC_VER)
#include <intrin.h>
#define AC04_CALLER_ADDRESS() _ReturnAddress()
#else
#define AC04_CALLER_ADDRESS() __builtin_return_address(0)
#endif

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
    if (debug::AllocHook hook = debug::GetAllocHook())
        hook(result, size, static_cast<u32>(align), debug::ImageRelative(AC04_CALLER_ADDRESS()));
    if (g_WorkHeapSize() < next) result = 0;
    return result;
}

// EE: 0x00101080
// Free bytes: heap size - used offset.
u32 GetWorkHeapFree() {
    return g_WorkHeapSize() - g_WorkHeapOffset();
}

// EE: 0x00101090
// Rewinds the heap to an absolute address returned earlier by WorkHeapAlloc(0, 0) (the mission
// retry mark S+0x1664) and returns it.
u32 RewindWorkHeapTo(u32 addr) {
    g_WorkHeapOffset() = addr - g_WorkHeapBase();
    return addr;
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
    if (const int fill = debug::HeapFill(); fill >= 0) {  // debug layer: --heap-fill
        const u32 lo = g_WorkHeapBase() + mark;
        const u32 hi = g_WorkHeapBase() + g_WorkHeapSize();
        if (u8* p = guest::Mem().Translate(lo, hi - lo)) std::memset(p, fill, hi - lo);
    }
    return mark;
}

// EE: 0x00101138
// Pushes the current heap offset on the 4-entry mark stack (0x00412198, depth byte 0x00411794,
// unchecked) and returns the free size.
u32 PushWorkHeapMark() {
    const u32 offset = g_WorkHeapOffset();
    u8& depth = GRef<u8>(0x00411794);
    GRef<u32>(0x00412198 + 4u * static_cast<u32>(static_cast<s32>(static_cast<s8>(depth)))) = offset;
    depth = static_cast<u8>(depth + 1);
    return g_WorkHeapSize() - offset;
}

// EE: 0x00101170
// Pops the last pushed mark (if any) back into the heap offset and returns the free size.
u32 PopWorkHeapMark() {
    u8& depth = GRef<u8>(0x00411794);
    if (static_cast<s8>(depth) > 0) {
        depth = static_cast<u8>(depth - 1);
        g_WorkHeapOffset() = GRef<u32>(0x00412198 + 4u * static_cast<u32>(static_cast<s32>(static_cast<s8>(depth))));
    }
    return g_WorkHeapSize() - g_WorkHeapOffset();
}

// EE: 0x00102668
u32 AllocAndLoadDataEntry(s32 entry) {
    const u32 buf = WorkHeapAlloc(static_cast<s32>(GetDataEntryAllocSize(entry)), 0x40);
    LoadDataEntryAsync(entry, buf);
    return buf;
}

// EE: 0x00100FB8
// Dead (no reference): clears the counters 0x0041178C / 0x00411790, the byte 0x00411794 and the four
// words at 0x00412198.
void func_00100FB8() {
    GRef<u32>(0x0041178C) = 0;
    GRef<u8>(0x00411794) = 0;
    GRef<u32>(0x00411790) = 0;
    for (s32 i = 3; i >= 0; --i) GRef<u32>(0x00412198 + 4u * static_cast<u32>(i)) = 0;
}

}  // namespace ac04::game
