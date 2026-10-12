# Memory model of the game code

How game code uses memory and pointers, and what that means for a 64-bit port. Counts come from offline scans of game code 0x001000D8–0x002616E7 (`tools/research/hwscan.py`, `../../tools/research/hwregs.py`). Function boundaries in those scans are approximated from `jal` targets and dispatch tables (about 2,400 functions), so counts are lower bounds unless stated otherwise.

## Summary

| Question | Answer | Confidence |
|---|---|---|
| Do game structs store pointers? | **Yes, everywhere.** Pointers are 32-bit fields inside structs, globals and tables, freely mixed with `int` arithmetic | confirmed |
| Are loaded DATA entries patched in place? | **No** at the container level: offsets stay in the file image and code converts them into separate pointer tables or computes `base + offset` on each access. Model data offsets are also resolved by the loader without patching (`../formats/MODEL.md`) | confirmed for containers and models; other sub-blocks H |
| Does code depend on 32-bit pointers? | **Yes.** Pointers are stored in 4-byte fields, cast to `int`, and written into DMA tags and IOP request records as 32-bit physical addresses | confirmed |
| Does code depend on alignment? | **Yes.** 128-bit `lq`/`sq` need 16-byte alignment, DMA packets must be qword aligned, and the allocator aligns to the requested power of two | confirmed |
| Fixed addresses? | Scratchpad (0x70000000, 16 KB) is used directly by 73 functions. A few hardware addresses are hard-coded (`../subsystems/rendering.md`, hardware access) | confirmed |

## Allocation

- **Work heap.** `InitWorkHeap` 0x00100F48 takes one `malloc` block (size probed by `ProbeLargestMallocSize` 0x00100EC8). `WorkHeapAlloc(size, align)` 0x00101000 is a bump allocator over it: it rounds the offset up to `align`, returns base + offset, and returns 0 when the block is exhausted (83 callers). Globals: base 0x00411788, size 0x00411784, offset 0x0041178C, mark 0x00411790. `func_00101060(n)` = `WorkHeapAlloc(n·16, 0x10)`. There is no `new`/`delete` and no per-object free (`../platform/toolchain.md`).
- **Scene lifetime.** `SetWorkHeapMark` 0x001010A8 stores the current offset once in `main`. `SceneStep` calls `ResetWorkHeapToMark` 0x001010F0 on every graphics reset request (`req & 4`), which `RequestScene` always sets. **Everything allocated after boot is freed on each scene change.** Both helpers also clear the 4 words at 0x00412198–0x004121A4.
- Sub-scene rewinds also exist: the flight scene stores a mark at flight object +0x1664 for retries (`../subsystems/mission-flow.md`).
- **Packet double buffer.** `ctx->dbuf` / `ctx->cur_buf` (`main-context.md`) is a second allocator: draw code bumps `cur_buf`. 61 functions read or write `cur_buf` directly, and about 233 go through the packet helpers (`../subsystems/rendering.md`).
- DATA.CDP entries are loaded into work-heap buffers (`AllocAndLoadDataEntry` 0x00102668; `GetDataEntryAllocSize` rounds up to whole sectors).
- Some sizes depend on the free heap: the replay ring takes the remaining heap (`../subsystems/mission-flow.md`), and the records page allocates `((frame_count & 0xF) + 1) · 0x10` bytes (`../subsystems/frontend.md`).

Port consequence: lifetimes are scene-scoped, and the heap offsets must match the PS2 exactly where sizes depend on the free space. A port can keep the same arena model with 64-bit pointers; the arena itself does not need 32-bit addresses.

## Pointers stored in structs and tables

- `MainContext` (0x0029F380) stores `cur_buf`, `dbuf[2]`, the DMA list heads/tails and `data0_table[4]` as 32-bit fields at fixed offsets.
- Many gp globals point at scene work structs: `g_GameState` 0x004104A4 → 0x0029F350, the flight scene object (gp 0x004104C4), the startup work struct (`*0x00411714`), and so on.
- `.data` holds about **4,100 32-bit words that look like absolute pointers**: 1,848 into `.text` (dispatch tables, callbacks), 1,130 into `.data`, 1,138 into `.rodata`/`.sdata`/`.bss`, 3 into `.vutext`. The `.text` count includes the `.eh_frame` FDE entries (`../port/dead-code.md`), and some words are coincidental values, so this is an upper bound for initialised pointers.
- Pointer arithmetic through `int` is the normal style, e.g. `LoadBootData` does `ctx->data0_table[i] = (void *)((int)buf + buf[i+1])`, and `StoreFlightDataPointers` 0x0010B038 does `*(int *)(table + i*4) = (int)entry + entry[i+1]`.

Port consequence: every struct that holds a pointer changes size and layout on x86-64 if translated naively. Structs whose layout is fixed by **file data** or **hardware** (DMA tags, IOP records, save data) must keep 32-bit fields. Structs that live only in EE RAM can use native pointers.

## Loaded data: offsets, not pointers

DATA.CDP entries are `count + offsets` containers (`../formats/CDP.md`). The code never rewrites those offsets in place. Three patterns:

| Pattern | Example | Where the pointer lives |
|---|---|---|
| Convert all offsets once into a separate pointer table | `LoadBootData` (DATA 0 → `ctx->data0_table`); `StoreFlightDataPointers` 0x0010B038 (flight data: sub-state 2 → flight object +0x1694, 36 pointers; 3 → +0x1754; 4 → +0x1794; 5 → +0x17D4) | in a game struct (32-bit field) |
| Reader object computing `base + off[i]` on each access | `InitContainerReader` 0x00108388 stores `{count, base, &off[0]}`; `GetContainerItem` 0x001083E0 returns `base + off[i]` (14 callers each, e.g. scene 6 0x00260FE8) | returned in a register |
| Object holds base pointers, records hold offsets | `func_00224410` (sequence character skinning): object +0x10C/+0x110/+0x114 hold base pointers; each record holds offsets at +0x10/+0x14/+0x18; the DMA `ref` tag address = base + offset, computed per frame | written into a DMA tag in scratchpad |

A scan for in-place `lw x,k(b); addu x,x,base; sw x,k(b)` sequences found 16 candidate functions. The ones checked (0x00122638, 0x00224410) are vector or counter accumulation, not relocation. **No in-place relocation of loaded data exists in the analysed code.** Pre-built packet sub-blocks start with DMA `cnt` tags (data inline, no addresses), and model node packets are `ret`-terminated chains reached through `call` tags written at run time (`../formats/MODEL.md`), so they are position independent.

Port consequence: loaded files can stay byte blobs. Offsets are resolved at access time or into native pointer tables. No relocation pass is needed at load time.

## 32-bit address dependencies

Places where a pointer value **as a 32-bit number** reaches something other than a C pointer dereference:

| Kind | Examples | Count |
|---|---|---|
| DMA tags (`ref`/`next`/`call` address field, 32-bit) | `func_001042D0` (`ref` to `.vutext` microcode), `func_00104118`/`func_00104198`/`func_001041E8`/`func_00104338` (ordering-table `next`/`call` links), `EmitModelInstance` 0x001404A8 (`call` of node packets), `func_00224410` (`ref` to model data) | about 233 functions use the tag helpers |
| Scratchpad DMA flag (bit 31 = SPR) | `func_00224410` passes `0xF0002200`-style addresses; `func_0013A6B8` and `func_0019FFF0` program `D8_SADR`/`D9_SADR` (`FlushSprWorkBuffer` 0x00103D98 and four more DMA helpers are dead) | 2 live functions touch SPR DMA registers (`../subsystems/rendering.md`) |
| Uncached / masked addresses | libipu `& 0x0FFFFFFF`; `0x20xxxxxx` uncached reads such as `DAT_204C8040` in libkernl | SDK code mostly. In game code `lui 0x2000/0x3000` appears 106× in 66 functions, mostly DMA tag ids (`0x20000000` = `next`, `0x30000000` = `ref`), not uncached pointers |
| IOP addresses (32-bit ints, never dereferenced on the EE) | VAG stream slot table 0x00353BF8 (IOP buffer addresses returned by VAG RPC fno 0); `QueueCdRead(..., flags=1, dest=IOP addr)`; `sceSifSetDma` destinations; HDD path IOP buffer 0x07800000 | about 10 functions (`iop-interface.md`) |
| RPC records containing EE pointers | SOUND RPC: the receive-buffer pointer is passed to the IOP | 4 functions |

Port consequence: these must stay explicit `uint32_t` "PS2 address" values. In the port they are handles into an emulated address space (packet buffers, scratchpad, IOP buffers) that the platform layer resolves.

## Alignment

- `lq`: 1,050 instructions in 330 functions; `sq`: 1,242 in 406; `lqc2`/`sqc2` (VU0): 1,970 / 701 in 134 / 166. All need 16-byte-aligned addresses (unaligned 128-bit access ignores the low 4 bits).
- VU0 macro-mode code (`COP2`, 4,786 instructions in 148 functions; 311 functions call `sceVu0*`) works on `float[4]` vectors and `float[4][4]` matrices in 16-byte-aligned memory.
- DMA packets are built in qword units, and the bump allocator is called with alignment 0x10 for packet and vector data (e.g. `WorkHeapAlloc(0x210, 0x10)` in scene 6).

Port consequence: vector and matrix types need `alignas(16)`. Code that relies on the EE ignoring the low address bits for `lq`/`sq` must be found while translating (open question).

## Scratchpad (0x70000000, 16 KB)

73 game functions use scratchpad addresses directly (1,211 accesses), mostly the flight renderer and math code (0x001115C0–0x00118400, 0x001DC9E8, 0x00202290, 0x00224410). It serves as fast work memory and as a DMA source or destination (`D8`/`D9` channels). Notable users: the terrain block list (`../subsystems/rendering.md`), the per-unit hittable table used by bullets (`../subsystems/weapons.md`), the sequence character skinning buffers at 0x70002200.

Port consequence: a static, 16-byte-aligned 16 KB array mapped onto the 0x70000000 window. The address stays a value where code passes it to DMA.

## Open points

- Game code that relies on the low 4 address bits being ignored by `lq`/`sq`.
- Internals of the remaining sub-block formats (terrain, effects): absolute addresses not expected but not checked.
