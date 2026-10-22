# ADR-001: Memory model for translated game code

Status: **Accepted** (User decision, 2026-10-11): **Option B, guest memory with 32-bit PS2 addresses.**

## Context

From `../game/memory.md`, `../subsystems/rendering.md` and `../formats/MISSION.md`:
- Game structs store 32-bit pointers everywhere and do pointer arithmetic through `int`.
- **Initialised pointers in the executable** (`.data` 0x0029C980–0x004021E7):
  - 1,058 words point to function entries (939 distinct functions, 531 of them game code): dispatch tables, callbacks;
  - 790 point to other code addresses (case labels or mid-function);
  - 2,268 point to data (1,130 into `.data`, 1,138 into `.rodata`/`.sdata`/`.bss`);
  - 3 point into `.vutext`.
  - `.rodata`/`.sdata` hold another 2,636 code addresses (compiler switch tables) and 196 data pointers.
  - Some hits are coincidental integers, so these are upper bounds.
- **Asset rule** (CLAUDE.md): `.data`/`.rodata`/`.vutext`/`.vudata` must be loaded from the ELF inside the user's ISO at runtime, never copied into `src/`.
- Loaded DATA entries are not relocated in place. Offsets are resolved at access time or into pointer tables (`../game/memory.md`). Several mission blocks are **ready-to-send DMA/GS packets**, sent by physical address (`block & 0x0FFFFFFF`) or linked with `call` tags (`../formats/MISSION.md`).
- About 233 functions build DMA chains whose tags hold **32-bit addresses** (`../subsystems/rendering.md`).
- **481 game functions (~20 %) pass stack addresses as arguments** (2,362 sites): mostly `sceVu0*` math on local vectors and matrices, plus buffers for SIF/DMA (e.g. `sceSifSetDma` descriptors on the stack).
- 128-bit `lq`/`sq` in ~400 functions; scratchpad (0x70000000) used directly by 73 functions.
- Goal: exact behaviour; PCSX2 is the reference.

## Options

### A. Native 64-bit pointers in game structs
Translate structs with native pointers. Fixed-layout data (files, DMA, IOP, saves) keeps `uint32_t` fields, and PS2 addresses that reach hardware become a separate `Ps2Addr` type resolved by the platform.

### B. Guest memory (User's proposal)
- **Memory:**
  - one 32 MB host buffer is PS2 main RAM;
  - game pointers stay 32-bit PS2 addresses, and host address = base + (addr & mask), handling the 0x2xxxxxxx/0x3xxxxxxx uncached aliases;
  - a separate 16 KB buffer for the scratchpad at 0x70000000.
- **Data:** `.data`/`.rodata`/`.sdata`/`.bss` are loaded from the ISO's ELF at their original addresses, and the game's own allocator (`malloc` block + bump allocator 0x00101000) runs inside the buffer.
- **Access:** through a `ps2ptr<T>` wrapper.

## Comparison

| Topic | A: native pointers | B: guest memory |
|---|---|---|
| **The ~4,100 pointer-like words in `.data`** | Must be converted to host pointers at load: every word has to be classified as pointer or integer. The counts above are only heuristic, so a relocation table would have to be derived and maintained by hand (ambiguous words such as `0x00100000` flags or float bit patterns). Wrong guesses are silent corruption. A cannot simply copy them as C++ initialisers either (asset rule) | **No conversion at all.** The ELF image is loaded verbatim at its original addresses and every pointer is valid as-is. Integer/pointer ambiguity never has to be resolved |
| **Function-pointer tables** (1,058 entries, 939 targets) | Need an address → translated-function mapping when the table is read from the ISO at runtime (same as B). Converting them at load means rewriting data | A generated table `EE address → host function` (≈939 + `jalr`/table call sites; game code has only 13 `jalr`). `CallGuest(addr)` looks the address up (sorted array or perfect hash). The compiler switch tables in `.rodata` (2,636 entries) become C++ `switch` and are never read |
| **DMA tags and ready-to-send packets** (MISSION.md blocks 0, 2, 23, 25, 26; ≈233 packet builders) | Packets built by game code would hold 64-bit host pointers in 32-bit tag fields, which doesn't work. A must keep a guest-style packet buffer anyway (option B locally) and translate addresses at every boundary | Natural: tags hold guest addresses, the DMA/VIF/GIF walker reads guest memory, `& 0x0FFFFFFF` and the SPR bit work unchanged. Loaded packets work wherever the allocator puts them, exactly as on the PS2 |
| **128-bit alignment** | `alignas(16)` on every vector/matrix type and every struct that holds one; heap allocations must honour it | Guest addresses keep their original alignment; base buffer aligned to 64 bytes ⇒ host alignment follows. The EE's "ignore low 4 bits" `lq`/`sq` behaviour can be reproduced in the accessor |
| **Address-taken locals** (481 functions) | Free: `&local` is a host pointer, as long as the callee is native too. Where the address reaches DMA/IOP it must be copied into a guest/packet buffer | Every local whose address is taken must live in **guest memory**: a guest stack frame per function (`GuestFrame f(0x40)` bump/pop on a guest stack pointer). This mirrors the PS2 exactly (its stack is in RAM) but is the main mechanical cost of B |
| **Code readability** | Best: plain C++ (`ctx->next_scene`, `p->x`) | Worse: `ps2ptr<MainContext> ctx; ctx->next_scene` works with an `operator->`, but pointer fields become `ps2ptr<T>`/`u32`, and `&local` becomes guest-frame allocation. Acceptable with good wrapper types; noticeably noisier in math code |
| **Performance** | Native | One add (+ optional mask) per dereference. Negligible for a 2001 game on a modern CPU; bounds checks only in debug builds |
| **Byte-for-byte comparison with PCSX2** | Impossible: struct layouts and addresses differ. Only value-level comparisons via hand-written mappings | **Possible**: same addresses, same layouts, same heap (if the allocator and load order match), even the same stack. Dump PCSX2 RAM at a frame and diff the port's guest RAM. A very strong test oracle for a translation project |
| **Debugging** | Native debuggers and sanitizers; but Ghidra/PCSX2 addresses don't match host addresses | Ghidra/PCSX2 addresses are valid in the port. Host sanitizers can't see guest-buffer overruns (add guard checks in debug builds) |
| **Host objects** (`std::` containers, platform state) | Can be mixed freely into game structs (but shouldn't be) | Must live outside guest RAM. A clean separation that matches "no game logic in platform" |
| **SDK shims (`src/sce/`)** | Mixed: some take host pointers, some PS2 addresses | Uniform: every `sce*` takes guest addresses, as on the PS2 (e.g. `sceCdRead(lsn, n, buf_guest_addr)`) |
| **Risk of subtle behaviour change** | Higher: layout changes, pointer/int casts, relocation guesses | Lower: memory semantics identical to the original |

## Assessment

Option A with B used locally for packets was also considered. Three findings decide against it:
1. The **asset rule** forces `.data` to be loaded from the ISO. Under A that means relocating ~4,100 words with an int/pointer ambiguity that can't be resolved statically with certainty. Under B it is a `memcpy`.
2. **Ready-to-send packets in loaded data** and ≈233 packet builders mean the renderer needs guest addresses anyway, so A would end up as A+B with conversions at every boundary.
3. **Byte-level comparison with PCSX2** is the most valuable test oracle for a translation that must behave exactly like the original, and only B provides it.

Where A is genuinely better: readability of math-heavy code, native tooling, and no guest stack. These are real costs of B, but they are mechanical and can be reduced with good wrapper types.

## Decision

**Option B (guest memory)**, accepted by the User on 2026-10-11, with these rules:
- `GuestMemory`: 32 MB RAM + 16 KB scratchpad. Address decode `0x0xxxxxxx`/`0x2xxxxxxx`/`0x3xxxxxxx` → RAM (mask 0x01FFFFFF), `0x70000000` → scratchpad, anything else → hardware handlers or a hard error.
- The ELF segments from the ISO are loaded at their original addresses; `.bss` zeroed; `gp` value kept for reference.
- `ps2ptr<T>`: a 32-bit value type with `operator->`/`operator*`/`operator[]`, alignment-preserving; `static_assert` on every guest struct's size and field offsets (shared with the Ghidra struct definitions).
- Guest stack: a guest stack region (the PS2 stack range) with a `GuestFrame` RAII helper, used only for locals whose address escapes. Locals whose address is never taken stay host locals.
- Function pointers: generated `EE address → translated function` table; `CallGuest(addr, args…)`; unknown address = hard error with the address.
- Pure math helpers called with stack vectors (e.g. `sceVu0*`) may get host-pointer overloads to avoid guest frames, as long as no guest-visible memory changes. This is an optimisation, applied only where the address does not escape.
- Debug builds: bounds/alignment checks on every access; optional write-watch on guest addresses (mirrors PCSX2 memory breakpoints).

## Consequences

- `src/common/` gets `GuestMemory`, `ps2ptr<T>`, `GuestFrame`, `CallGuest`.
- `src/sce/` shims take guest addresses.
- A RAM-diff tool (PCSX2 RAM dump vs. port RAM dump at the same frame) becomes possible and should be planned with the test strategy.
- `.claude/rules/game-code.md` (struct/pointer rules) is written for the guest-memory model (updated 2026-10-11).

## Research findings

The User's working assumption is guest memory: one 32 MB buffer, 32-bit PS2 pointers, `ps2ptr<T>`, a function-pointer table and a guest stack.

- **Work heap (`../pcsx2/sessions.md` run 03).** `InitWorkHeap` gets base **0x004CC760** and size **0x01A318A4** (end 0x01EFE004). The port sets these values directly instead of running `ProbeLargestMallocSize` against the host libc.
- **The heap is almost full in play.** Mission 1 uses 0x019C5180 bytes (98.4 %), with only about 434 KiB free. `WorkHeapAlloc` returns 0 when exhausted, and callers do not check. This confirms the guest-memory design: every structure placed in the work heap must keep its exact PS2 size and alignment. A host-sized struct (e.g. with 64-bit pointers) would overflow the heap.
- Boot-time allocations before `SetWorkHeapMark`: 0x3820 bytes. They persist across scenes.

### Large and frame-dependent heap allocations (front end)

- The in-menu movie player allocates 6 MiB (`StartMenuMovie`), and the HDD install page 10 MiB, from the work heap.
- The records page allocates `((frame_count & 0xF) + 1) * 0x10` bytes before its object (0x002414E0). From that point on, work-heap offsets depend on the frame count. A byte-for-byte RAM comparison with PCSX2 therefore needs identical frame counts (input replay), which ADR-002's fixed step already gives.
