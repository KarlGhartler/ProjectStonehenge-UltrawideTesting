# Render boundary: GS/VIF/GIF packets, VU microcode, hardware access

Phase 2.5. Question: where does the game build graphics packets, how does VU microcode get to the VUs, and how much code touches hardware directly? Sources: decompiled helpers and offline scans (`out/scratch/hwscan2.py` with constant propagation, `curbuf.py`, `callers.py`, `vuref.py`). Function counts use approximate boundaries (`jal` targets + dispatch tables), so they are lower bounds.

## Summary

- **Packet transport is central; packet content is spread out.** One small set of helpers at 0x00103FD8–0x00104438 manages the frame's DMA chains (ordering-table buckets, `next`/`ref`/`call`/`end` tags), and the main loop kicks them once per frame. But about **233 game functions** write packet contents (GIF tags, GS register data, VIF codes, vertex data) themselves.
- **Two DMA chains per frame**: a VIF1 chain (channel 1, TTE on: VU1 microcode, VU1 data, PATH1/PATH2 GS data) and a GIF chain (channel 2, PATH3). Plus the display/draw environment packet sent directly on channel 2.
- **VU1 microcode is uploaded inline every frame** through DMA `ref` tags that point into `.vutext` (which holds VIF `MPG` packets). There is no separate one-time upload.
- **VU0** is used in macro mode by game math (148 functions with COP2, 311 calling `sceVu0*`). VU0 micro mode is also used: one VIF0 DMA upload of microcode at 0x0029C200 (`func_00225408`).
- **Direct hardware register access is rare**: 14 game functions write EE registers (timers, DMA channels 0/2/8/9, `D_CTRL`, ring buffer). **No** game function touches GS privileged registers or VU memory directly (libgraph and DMA do that).

## Frame DMA structure

`MainContext` (`main-context.md`) holds two lists: `vif1_list_head/tail` (+0x60) and `gif_list_head/tail` (+0x50).

| Step | Code | What |
|---|---|---|
| start of frame | `SendDmaPacketList(1, &ctx->vif1_list_head, tte=1)`, `SendDmaPacketList(2, &ctx->gif_list_head, 0)` (main loop) | `sceDmaSend` of **last** frame's chains, so the GPU side runs one frame behind the CPU |
| scene setup | `func_00103FD8(ot, n)` (30 caller functions) | allocate an **ordering table** of `n` buckets in the packet buffer: each bucket is a DMA `next` tag (`0x20000000`) chained to the following one, terminated by `end` (`0x70000000`) |
| link into frame | `func_001043C0(&ctx->vif1_list_head, ot)` (30 callers) | append the ordering table to the VIF1 list |
| draw calls | packet helpers below (≈233 caller functions in total) | insert packets into bucket `k` (lower `k` = earlier in the chain, H: used as layer/depth order) |
| end of frame | `WaitVSync` → `UpdateDisplayFrame` → `PutDispAndDrawEnv` | field copy (GIF packet at 0x00420000, `func_001049B0`), `sceGsPutDispEnv`, DrawEnv GIF packet (`ctx+0xC0`) on channel 2 |

Packet helpers (all bump `ctx->cur_buf`, the per-frame double buffer):

| Address | Callers | Tag built | Use |
|---|---|---|---|
| 0x00104100 / 0x00104118 | 76 / 76 | open: remember `cur_buf`; close: insert `next` tag pointing at the data written since open | wrap free-form packet data written directly by the caller |
| 0x00104198 | 59 | `next` tag to caller-supplied data, update `cur_buf` | same, caller-managed pointer |
| 0x001041E8 | 77 | `next` tag with `qwc` inline qwords (`0x20000000 | qwc`, 2 extra tag words, e.g. VIF codes), linked into the bucket | small inline packets |
| 0x00104250 / 0x001042A0 | 2 / 2 | `next` with 2 inline tag words / bare `next | qwc` header | special cases |
| 0x001042D0 | 7 | **`ref` tag** (`0x30000000 | qwc`, address) + `next` | reference static data: VU microcode in `.vutext`, VU data in `.vudata` |
| 0x00104338 | 7 | **`call` tag** (`0x50000000 | qwc`, address) + return `next` | call a pre-built chain (H: pre-built model packets in loaded data) |
| 0x00104090 | 1 | splice another list into a bucket | flight renderer setup |

`sceDmaSend` itself is called by only 6 functions: `SendDmaPacketList`, `PutDispAndDrawEnv`, the field-copy `func_001049B0`, `func_001118C8`, `func_001368C0` and the movie path `func_00213C28`.

## VU microcode

`.vutext` (0x00297C00–0x0029C8FF) is referenced from code at these places (`vuref.py`):

| Caller | `.vutext` address / size | How |
|---|---|---|
| `func_00138BB0` | 0x00297C10, 0xEA qwords, into buckets 6 and 9 | `ref` via `func_001042D0` (flight scene setup `func_0010BD68`) |
| `func_00138C28` | 0x00298AD0, 0xD4 qwords, bucket 0 | `ref` via `func_001042D0` |
| `func_00138C10` | `.vudata` 0x00402200, size `*0x004021F0` | `ref` via `func_001042D0` (VU data) |
| `func_001418C8`, `func_001483A0`, `func_00142460`, `func_0014B280` | 0x00298AC0, 0x0029B0F0, 0x00299820 | `ref` via `func_001042D0` |
| `func_0014AE60` | 0x00297C00 / 0x00298AC0 / 0x00299820 selected by mode 1/0/2 | stores the microcode pointer in a render object (menu 3D models, caller `func_00231000`) |
| `func_00225408` | 0x0029C200 | **direct VIF0 DMA**: `D0_QWC = 0`, `D0_TADR = 0x0029C200`, `D0_CHCR = 0x145` (chain mode, start), then `sceDmaSync`. This is the only VU0 micro-mode upload |
| `.data` 0x003B29E0, 0x003C9BB0, 0x003C9CD0 | 0x00299910, 0x0029A300 | microcode pointers in data tables (not followed yet) |

So `.vutext` is laid out as ready-to-DMA VIF packets (`MPG` + code). The game re-sends microcode inside the VIF1 chain whenever a renderer needs it. A port that replaces VU1 with GPU shaders can treat each `ref` to a `.vutext` address as "bind pipeline X". There are about 6 distinct VU1 programs (0x00297C00/0x00297C10, 0x00298AC0/0x00298AD0, 0x00299820, 0x00299910, 0x0029A300, 0x0029B0F0) plus 1 VU0 program (0x0029C200). The exact program boundaries are open (`vu-microcode` rules: no disassembly in `.vutext`).

## Direct hardware access from game code

Constant-propagating scan of all loads/stores whose base register holds a known constant:

| Register / area | Functions | Notes |
|---|---|---|
| `T0_COUNT`, `T0_MODE` (0x10000000/10) | 4 / 3 | main loop reset; movie timing (0x00211CA8, 0x002122C0, 0x00212110, 0x00238770, 0x00238810) |
| `D8_*` toSPR (0x1000D000–D080) | 4 | 0x00103C00, 0x00103D98, 0x001051F0, 0x0013A6B8: copy to scratchpad by DMA |
| `D9_*` fromSPR (0x1000D400–D480) | 3 | 0x001051F0, 0x00144CC0, 0x0019FFF0 |
| `D0_*` VIF0 (0x10008000–30) | 2 | 0x00136818, 0x00225408 (VU0 microcode / VIF0 chain) |
| `D2_CHCR`/`D2_TADR` GIF | 1–2 | 0x00103C00, 0x00103D98 |
| `D_CTRL`, `D_STAT`, `D_RBOR`/`D_RBSR` (0x1000E000–50) | 2 | 0x00103C00 sets a DMA ring buffer at 0x00418000 size 0x7FF0 for scratchpad stall control; 0x00225408 clears `D_STAT` |
| GS privileged (0x12000000) | **0** | only libgraph |
| VU0/VU1 memory (0x11000000–0x1100FFFF) | **0** | only through VIF DMA |
| scratchpad (0x70000000) | 73 | work memory (see `memory-model.md`) |

Total: **14 game functions touch EE hardware registers directly.** SDK use from game code: libgraph called from 13 functions, libdma 10, libpkt 1, libvu0 311.

## Port consequences

- The **transport layer** (ordering tables, tag helpers, `SendDmaPacketList`, `PutDispAndDrawEnv`) is small and can be reimplemented once.
- The **content** is the hard part. About 233 functions write GS/VIF data directly into packets: GIF tags with A+D register writes, `PRIM`/`RGBAQ`/`XYZ2` streams, VIF `UNPACK`/`MSCAL` commands for VU1. A port must interpret those packets (a GS/VIF "replayer" on the GPU) unless every one of those functions is rewritten to call a new render API. See ADR-003.
- VU1 programs (≈6) must become GPU shaders or a CPU VU1 interpreter. VU0 macro code is ordinary vector math (translate with `vu0_math.h`).
- Scratchpad DMA (`D8`/`D9`) is memory copies in disguise.

## Open

- What each VU1 program does (needs the vu-microcode rules: analysis only, never disassemble in `.vutext` with Ghidra).
- Which buckets mean what (sky, terrain, objects, HUD) in the flight ordering table set up by `func_0010BD68`.
- Whether loaded model sub-blocks are pre-built DMA chains called with `call` tags (`func_00104338` callers).

## Update (priority 2, step 9, 2026-10-11)

Details in `render.md`. Answers to the open points above:
- **Pre-built chains:** model node packets in the loaded data are DMA chains ending with a `ret` tag. `EmitModelInstance` 0x001404A8 writes the `call` tags itself (it does not use `func_00104338`). The same packets are read on the EE to build collision triangles.
- **Bucket meanings** (flight view table, 16 buckets): 0 view setup/scissor; 2 ground-object sub-table; 4 terrain sub-table; 6/9 VU1 program A (models); 0xC effects sub-table (1024 buckets; H: effects are sorted into them by depth); last bucket full-screen scissor. 2D overlays use a separate 4-bucket table.
- **VU program boundaries:** every `.vutext` block starts with a DMA `ret`/`cnt` header holding its qword count (5 VU1 programs and 1 VU0 program, table in `render.md`). The two `.data` pointers 0x00299910 / 0x0029A300 fall inside program C.
- **More direct hardware access than counted above:**
  - the sequence character engine starts and waits on DMA channels through `sceDmaGetChan` pointers (`func_00223CE8`, `func_00223CC0`);
  - five functions found in step 11 (0x00103C50, 0x00105210, 0x00105278, 0x001052A8, 0x00144D68) write the GIF and SPR DMA registers directly, but they are dead.
- **VU0 micro mode** is used by the cutscene skinning (`vcallms` entries 0x00–0xB0), not only for the upload in `func_00225408`.
