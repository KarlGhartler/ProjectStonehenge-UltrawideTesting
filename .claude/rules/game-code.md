---
paths:
  - "src/game/**/*"
  - "src/common/**/*"
---

# Rules for translated game code

## Types and memory layout
- Use the fixed-width types from `src/common/types.h` (`s8 s16 s32 s64 u8 u16 u32 u64 u128 f32`). Never use `long`: it is 64-bit on ee-gcc but 32-bit on MSVC.
- PS2 pointers are 32-bit, PC pointers are 64-bit. Structs whose layout comes from data files, DMA packets or the original memory layout must store pointers as `u32` offsets or indices, never as C++ pointers.
- Every such struct gets `static_assert(sizeof(T) == 0xNN)` with the size seen in the original code.
- 128-bit values and anything accessed with `lq`/`sq` need `alignas(16)`.
- PS2 and x86 are both little-endian: data files can be read without byte swapping.

## Hardware access
- Never dereference PS2 addresses: `0x1000xxxx` (I/O registers), `0x11xxxxxx` (VU memory), `0x12xxxxxx` (GS), `0x70000000` (scratchpad), `0x2xxxxxxx`/`0x3xxxxxxx` (uncached RAM aliases). Call `src/sce/` or `src/platform/` instead.
- Scratchpad use becomes a normal buffer obtained from the platform layer.

## Floating point
- Keep `float`; never promote to `double`. Write literals with an `f` suffix.
- Preserve the order of float operations in physics, flight model and AI code (float math is not associative).
- The EE FPU rounds toward zero, has no NaN/Inf/denormals and clamps overflow to ±FLT_MAX. Where code can divide by zero or overflow, use the helpers in `src/common/ee_math.h` and add a comment explaining why.

## Integer behaviour
- Preserve signedness and operand width exactly. 32-bit EE ops sign-extend their result into 64-bit registers; keep that when the value is later used as 64-bit.

## Instruction-set specific code
- MMI and 128-bit operations: translate to plain scalar C++ first. Replace with SIMD intrinsics only together with a test showing identical results.
- VU0 macro-mode code: translate using `src/common/vu0_math.h`.

## Structure
- Call Sony SDK functions through `src/sce/` with their original names and signatures.
- Replace `goto`s and jump tables with loops and `switch` when behaviour is identical.
- Assume game logic advances once per frame at the NTSC rate. Never replace frame counters with wall-clock time.
