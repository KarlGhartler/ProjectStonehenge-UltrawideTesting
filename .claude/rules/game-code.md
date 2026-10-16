---
paths:
  - "src/game/**/*"
  - "src/common/**/*"
---

# Rules for translated game code

## Types and memory layout
- Use the fixed-width types from `src/common/types.h` (`s8 s16 s32 s64 u8 u16 u32 u64 u128 f32`). Never use `long`: it is 64-bit on ee-gcc but 32-bit on MSVC.
- Guest memory model (ADR-001, Accepted): game data lives in `GuestMemory` (32 MB RAM + 16 KB scratchpad) at the original 32-bit PS2 addresses. `.data`/`.rodata` are loaded there from the ISO.
- Pointers inside guest structs are `ps2ptr<T>` (32-bit guest address), never C++ pointers. Never store a host pointer into guest memory.
- Every guest struct gets `static_assert(sizeof(T) == 0xNN)` and `static_assert(offsetof(...))` for the fields the code uses, matching the original layout (and the Ghidra struct).
- Locals whose address escapes (passed to another function that keeps or writes it, or to the SDK as a guest address) go on the guest stack through `GuestFrame`. Locals whose address never escapes stay host locals.
- Indirect calls go through `CallGuest(addr, ...)`; an unknown address is a hard error that logs the address.
- 128-bit values and anything accessed with `lq`/`sq` need `alignas(16)`.
- PS2 and x86 are both little-endian: data files can be read without byte swapping.

## Hardware access
- Never access hardware addresses directly: `0x1000xxxx` (I/O registers), `0x11xxxxxx` (VU memory), `0x12xxxxxx` (GS). Call `src/sce/` or `src/platform/` instead.
- Where the original code programs a register itself (timer 0, DMAC ring, a DMA channel's CHCR), use the explicit functions in `src/sce/ee_hw.h` (`EeHwRead/EeHwWrite(EeReg::k...)`, `EeDmaReadChcr/EeDmaWriteChcr`); add a register there when a new one is needed.
- RAM through the uncached aliases (`0x2xxxxxxx`/`0x3xxxxxxx`) and the scratchpad (`0x70000000`) are ordinary `GuestMemory` accesses (the address decode maps them).

## Floating point
- Keep `float`; never promote to `double`. Write literals with an `f` suffix.
- Preserve the order of float operations in physics, flight model and AI code (float math is not associative).
- EE FPU results round to nearest (as PCSX2 runs AC04: GameDB `eeRoundMode: 0`; `docs/port/validation.md` E-2); the EE has no NaN/Inf/denormals and clamps overflow to ±FLT_MAX. `cvt.w.s` (float to int) still truncates. Where code can divide by zero or overflow, use the helpers in `src/common/ee_math.h` and add a comment explaining why.
- **Every float add/sub-type instruction** (`add.s`, `sub.s`, `adda.s`, `suba.s`, and the add of `madd.s`/`msub.s`/`madda.s`/`msuba.s`) is written as `fpu::Add` / `fpu::Sub` / `fpu::MulAdd`, never as C++ `+`/`-` on floats: PCSX2 masks the smaller operand before adding, which plain `+` does not reproduce (`docs/port/validation.md` S2a-3; owner decision 2026-10-14). Count the add/sub instructions in the disassembly and check the translation has one helper call per source operation (an add duplicated into a branch-likely delay slot is one operation). Products and quotients may stay plain `*`, `/` (PCSX2 rounds them to nearest like the host); negation and `abs` are exact.

## Integer behaviour
- Preserve signedness and operand width exactly. 32-bit EE ops sign-extend their result into 64-bit registers; keep that when the value is later used as 64-bit.

## Instruction-set specific code
- MMI and 128-bit operations: translate to plain scalar C++ first. Replace with SIMD intrinsics only together with a test showing identical results.
- VU0 macro-mode code: translate using `src/common/vu0_math.h` (`vu0::`, round to nearest like the EE). VU micro-program HLE uses `vu0::micro::` (chop).
- VU0 flag reads (`cfc2` of the MAC/status/clip flags) see the COP2 flag pipeline delay: a read a few instructions after a vector op can still return the flags of an earlier op. Translate the result real hardware gives, not the naive "flags of the last op" (known sites and how they were found: `docs/port/validation.md` B-2).

## Structure
- Call Sony SDK functions through `src/sce/` with their original names and signatures.
- Replace `goto`s and jump tables with loops and `switch` when behaviour is identical.
- Assume game logic advances once per frame at the NTSC rate. Never replace frame counters with wall-clock time.
