# Memory model of the game code

Phase 2.5. Question: how does game code use memory and pointers, and what does that mean for a 64-bit PC port? Counts come from offline scans of the ELF (`out/scratch/hwscan.py`, `hwscan2.py`, `fixup.py`, `curbuf.py`) over game code 0x001000D8–0x002616E7. Function boundaries in those scans are approximated from `jal` targets and dispatch tables, about 2,400 functions. Treat counts as lower bounds unless stated otherwise.

## Summary

| Question | Answer | Confidence |
|---|---|---|
| Do game structs store pointers? | **Yes, everywhere.** Pointers are 32-bit fields inside structs, globals and tables, freely mixed with `int` arithmetic. | confirmed |
| Are loaded DATA entries patched in place? | **No in-place patching found** for the container level. Offsets stay in the file image; code converts them into separate pointer tables or computes `base + offset` on each access. Patching *inside* sub-blocks is not ruled out. | confirmed for the container level, H for sub-blocks |
| Does code depend on 32-bit pointers? | **Yes.** Pointers are stored in 4-byte fields, cast to `int`, and written into DMA tags and IOP request records as 32-bit physical addresses. | confirmed |
| Does code depend on alignment? | **Yes.** 128-bit `lq`/`sq` need 16-byte alignment. DMA packets must be qword aligned. The allocator aligns to the requested power of two. | confirmed |
| Fixed addresses? | Scratchpad (0x70000000, 16 KB) is used directly by 73 functions. A few hardware addresses are hard-coded (`render-boundary.md`). | confirmed |

## 1. Allocation

- One `malloc` block, then the game's own bump allocator `WorkHeapAlloc` (H) 0x00101000 `(size, align)` (82 caller functions). There is no `new`/`delete` and no per-object free (`toolchain.md`).
- The per-frame **packet double buffer** `ctx->dbuf` / `ctx->cur_buf` (`main-context.md`) is the second allocator: draw code bumps `cur_buf`. 61 functions read or write `cur_buf` directly, and about 233 go through the packet helpers (`render-boundary.md`).
- DATA.CDP entries are loaded into bump-allocated buffers (`AllocAndLoadDataEntry` 0x00102668, `GetDataEntryAllocSize` rounds up to whole sectors).

Port consequence: lifetimes are scene-scoped (the heap is reset on scene change, H). A port can keep the same arena model with 64-bit pointers. The arena itself does not need 32-bit addresses.

## 2. Pointers stored in structs and tables

- `MainContext` (0x0029F380) stores `cur_buf`, `dbuf[2]`, the DMA list heads/tails, and `data0_table[4]` pointers, all as 32-bit fields at fixed offsets (`main-context.md`).
- The game-state block 0x0029F350 is reached through `g_GameState` (gp 0x004104A4). The flight work struct is reached through gp 0x004104C4, the startup work struct through `*0x00411714`, and so on. Many globals are pointers to scene work structs.
- `.data` holds about **4,100 32-bit words that look like absolute pointers**: 1,848 into `.text` (dispatch tables, callbacks), 1,130 into `.data`, 1,138 into `.rodata`/`.sdata`/`.bss`, 3 into `.vutext`. Some are coincidental values, so this is an upper bound for initialised pointers.
- Pointer arithmetic through `int` is the normal style. Example: `func_00100768` does `ctx->data0_table[i] = (void *)((int)buf + buf[i+1])`, and `LoadFlightDataEntry`'s consumer `func_0010B038` does `*(int *)(table + i*4) = (int)entry + entry[i+1]`.

Port consequence: every struct that holds a pointer changes size and layout on x86-64 if it is translated naively. Structs whose layout is fixed by **file data** or **hardware** (DMA tags, IOP records, save data) must keep 32-bit fields. Structs that only live in EE RAM can use native pointers.

## 3. Loaded data: offsets, not pointers

DATA.CDP entries are `count + offsets` containers (`docs/formats/CDP.md`). The code never rewrites those offsets in place. Three patterns:

| Pattern | Example | Where the pointer lives |
|---|---|---|
| Convert all offsets once into a separate pointer table | `func_00100768` (DATA 0 → `ctx->data0_table`); `func_0010B038` (flight data: sub-state 2 → flight work +0x1694, 36 pointers; sub-state 3 → +0x1754; 4 → +0x1794; 5 → +0x17D4) | in a game struct (32-bit field) |
| Reader object computing `base + off[i]` on each access | `func_00108388(reader, blob)` stores `{count, base, &off[0]}`; `func_001083E0(reader, i)` returns `base + off[i]` (14 caller functions each, e.g. scene 6 0x00260FE8) | returned in a register |
| Object holds base pointers, records hold offsets | `func_00224410` (VU1 model upload, menus): `obj+0x10C/+0x110/+0x114` hold base pointers; each record holds offsets at `+0x10/+0x14/+0x18`; DMA `ref` tag address = base + offset, computed per frame | written into a DMA tag in scratchpad |

A scan for in-place `lw x,k(b); addu x,x,base; sw x,k(b)` sequences found 16 candidate functions. The ones checked (0x00122638, 0x00224410) are vector or counter accumulation, not relocation. **No in-place relocation of loaded data has been found.** Sub-blocks that start with `0x1000xxxx` look like DMA `cnt` tags (`CDP.md`). `cnt` tags carry their data inline and hold no addresses, so such pre-built packets would be position independent. That fits the "no relocation" result, but it is H until a sub-block format is decoded.

Port consequence: loaded files can be kept as byte blobs. Offsets are resolved at access time or into native pointer tables. No relocation pass is needed at load time (H for sub-block internals).

## 4. 32-bit address dependencies

Places where a pointer value **as a 32-bit number** reaches something other than a C pointer dereference:

| Kind | Examples | Count |
|---|---|---|
| DMA tags (`ref`/`next`/`call` address field, 32-bit) | `func_001042D0` (`ref` to `.vutext` microcode), `func_00104118`/`func_00104198`/`func_001041E8`/`func_00104338` (ordering-table `next`/`call` links), `func_00224410` (`ref` to model data) | ≈233 functions use the tag helpers (`render-boundary.md`) |
| Scratchpad DMA flag (bit 31 = SPR) | `func_00224410` passes `0xF0002200`-style addresses; `func_00103D98`, `func_001051F0`, `func_0013A6B8` program `D8_SADR`/`D9_SADR` | 6 functions touch SPR DMA registers |
| Uncached / masked addresses | libipu `& 0x0FFFFFFF`; `0x20xxxxxx` uncached reads such as `DAT_204C8040` in libkernl | SDK code mostly; game code: `lui 0x2000/0x3000` appears 106× in 66 functions, but many of these are DMA tag ids (`0x20000000` = `next`, `0x30000000` = `ref`), not uncached pointers |
| IOP addresses (stored as 32-bit ints, never dereferenced on the EE) | VAG stream slot table 0x00353BF8 (IOP buffer addresses returned by VAG RPC fno 0); `QueueCdRead(..., flags=1, dest=IOP addr)`; `sceSifSetDma` destinations; HDD path IOP buffer 0x07800000 | about 10 functions (`iop-interface.md`) |
| RPC records containing EE pointers | SOUND RPC: the receive-buffer pointer is passed to the IOP | 4 functions |

Port consequence: these must stay explicit `uint32_t` "PS2 address" values, not native pointers. In the port they are handles into an emulated address space (packet buffers, scratchpad, IOP buffers) that the platform layer resolves.

## 5. Alignment

- `lq`: 1,050 instructions in 330 functions. `sq`: 1,242 in 406 functions. `lqc2`/`sqc2` (VU0): 1,970 / 701 in 134 / 166 functions. All need 16-byte-aligned addresses on the EE (unaligned 128-bit access ignores the low 4 bits).
- VU0 macro-mode code (`COP2`, 4,786 instructions in 148 functions; 311 functions call `sceVu0*`) works on `float[4]` vectors and `float[4][4]` matrices in 16-byte-aligned memory.
- DMA packets are built in qword units, and the bump allocator is called with alignment 0x10 for packet and vector data (e.g. `WorkHeapAlloc(0x210, 0x10)` in scene 6).

Port consequence: vector and matrix types need `alignas(16)`. Code that relies on the EE ignoring the low address bits for `lq`/`sq` must be found and handled when translating (open question).

## 6. Scratchpad (0x70000000, 16 KB)

73 game functions use scratchpad addresses directly (1,211 accesses), mostly the flight renderer and math code (0x001115C0–0x00118400, 0x001DC9E8, 0x00202290, 0x00224410). It is used as fast work memory and as a DMA source or destination (`D8`/`D9` channels, 6 functions).

Port consequence: provide a static, 16-byte-aligned 16 KB array and map the `0x70000000` window onto it. Keep the address as a value where code passes it to DMA.

## Open points

- Sub-block internals of DATA entries (models, textures, `GIM\0`): do any contain absolute addresses? Decode one model sub-block (Phase 3).
- Game code that relies on the low 4 address bits being ignored by `lq`/`sq`.
- Whether the heap is reset per scene (the bump allocator's reset callers).
