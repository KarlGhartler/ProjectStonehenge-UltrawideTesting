# Main context struct (`MainContext` @ 0x0029F380)

Phase 2 result. Ghidra: data type `MainContext` (0xA20 bytes), applied as global `g_MainContext` @ 0x0029F380; the gp pointer 0x004104A8 (`gp-0x5548`) is typed `MainContext *` and named `g_pMainContext`. Field names follow the project convention (snake_case, no type prefix; renamed 2026-10-10, `naming-convention.md`).

## How the layout was derived

- `out/scratch/ctxfields.py` scans all of `.text` for `lw rX,-0x5548(gp)` and records every load/store based on `rX` until `rX` is overwritten: 57 distinct offsets (`ctxfields.txt`).
- Fields reached through derived pointers (`ctx+0x40`, `+0x50`, `+0x60`, `+0x90`, `+0x210 + port*0x50`, `+0xA10`) come from the callees: `SwapDoubleBuffer`, `SendDmaPacketList`, `func_00104558`, `PutDispAndDrawEnv`, `PadSystemInit` (H), `func_00102220`, `func_00100768`.
- Size: the only absolute references in 0x0029F000–0x0029FDA0 are the struct base and four globals before it; the struct is followed by the scene table at 0x0029FDA0, and all 0xA20 bytes are zero in the ELF. The end at 0xA20 is a **HYPOTHESIS** (it may be smaller, with padding).

## Fields

Confidence: **C** = confirmed from code; **H** = hypothesis; **?** = only the access width is known.

| Off | Ghidra name | Type | Meaning | Conf. |
|---|---|---|---|---|
| 0x000 | flags | u32 | global flags. Known bits: 0x100 = pause/freeze (stops `+0x2C` and `+0x34`), 0x200/0x400 (main-loop bookkeeping), 0x800 set on every scene change and by `main` (cleared in scene 2 sub-states 0x0B/0x1C), 0x40000 = skip the main loop's pad task `func_00102220` (set by the mission loader), 0x80000 cleared by the boot scene | C (bits partly H) |
| 0x004 | req | u32 | scene request bits: 1 change scene, 2 change sub-state, 4 reset graphics lists, 0x10/0x20 "changed last frame", 0x40 return to `ret_scene` (`scenes.md`) | C |
| 0x008 | scene | u16 | current scene id, index into 0x0029FDA0 | C |
| 0x00A | prev_scene | u16 | scene before the last change | C |
| 0x00C | next_scene | u16 | requested scene (`RequestScene`) | C |
| 0x00E | sub | u16 | current sub-state of the scene | C |
| 0x010 | next_sub | u16 | requested sub-state (`RequestSubState`) | C |
| 0x012 | ret_scene | u16 | scene to return to on `req & 0x40` | C |
| 0x014 | ret_sub | u16 | sub-state to return to; also written 1..11 by the menu code (0x00226B78) before `EnterMenu(0x20)` | C / H |
| 0x016 | unk_16 | u16 | — | ? |
| 0x018 | scene_work[4] | u32[4] | zeroed on every scene change; scene-local scratch | C (use ?) |
| 0x028 | frame_count | u32 | ++ every main-loop frame | C |
| 0x02C | frame_count_unpaused | u32 | ++ every frame unless `flags & 0x100` | C |
| 0x030 | scene_frame | u32 | ++ every `SceneStep`, reset on scene change | C |
| 0x034 | scene_frame_unpaused | u32 | same, but not while `flags & 0x100` | C |
| 0x038 | cur_buf | void* | current half of the double buffer at 0x40, set every frame by `SwapDoubleBuffer(&ctx->dbuf)` | C |
| 0x040 | dbuf[2] | void*[2] | double buffer (each half allocated with `func_00101060(size)`, e.g. 0x10000 bytes in scene 7) | C |
| 0x048 | dbuf_index | u32 | 0/1, toggled every frame | C |
| 0x050 | gif_list_head / 0x054 gif_list_tail | void* | DMA packet list sent on **channel 2 (GIF)** with TTE off every frame | C |
| 0x060 | vif1_list_head / 0x064 vif1_list_tail | void* | DMA packet list sent on **channel 1 (VIF1)** with TTE on every frame; appended by `func_001043C0` | C |
| 0x070 | draw_w | u16 | draw buffer width (0x200 at boot, 0x280 for movies) | C |
| 0x072 | draw_h | u16 | draw buffer height (0x1C0 = 448) | C |
| 0x074 | disp_w | u16 | display width, passed to `sceGsSetDefDispEnv` | C |
| 0x076 | disp_h | u16 | display height (0xE0 = 224 at boot = field mode; 0x1C0 for movies) | C |
| 0x078 | clear_rgba[4] | u8[4] | clear colour R,G,B,A (RGBAQ of the clear sprite at `+0x1D0`) | C |
| 0x07C | frame_psm | u8 | FRAME PSM | C |
| 0x07D | zbuf_psm | u8 | ZBUF PSM (0x31 = PSMZ24, 0x3A = PSMZ16S) | C |
| 0x07E | ztest | u8 | ZTST value in TEST_1/TEST_2 (bits 17–18) | C |
| 0x07F | draw_mode | u8 | selects ALPHA/prim settings in the clear packet (0..3) | H |
| 0x080 | brightness_r, brightness_g, brightness_b | float ×3 | brightness colour scale: set by `ApplyBrightness` (option level 0–6), applied by `DrawDisplayTransfer` when copying the frame | C |
| 0x08C | disp_flags | u16 | display flags: 0x100/0x200 = `sceGsResetGraph` with field/frame mode on next flip, 0x2000/0x8000 select the field copy, 0x4000 skips it; 0x5FFF mask cleared after each flip | C (bits H) |
| 0x08E | zbuf_page | u16 | first free VRAM page after the frame buffers (ZBP) | C |
| 0x090 | dispenv | u64[5] | `sceGsDispEnv` (pmode, smode2, dispfb, display, bgcolor); `sceGsPutDispEnv(ctx+0x90)` every frame | C |
| 0x0C0 | drawenv_pkt | u64[28] | GIF packet: GIFtag (A+D, 13 regs) + FRAME_1, ZBUF_1, XYOFFSET_1, SCISSOR_1, TEST_1, PRMODECONT, COLCLAMP, DTHE, FRAME_2, ZBUF_2, XYOFFSET_2, SCISSOR_2, TEST_2; sent every frame by `PutDispAndDrawEnv` | C |
| 0x1A0 | clear_pkt | u64[14] | GIF packet: GIFtag (6 regs) + TEST, PRIM (sprite), RGBAQ, XYZ2, XYZ2, TEST: full-screen clear | C |
| 0x210 | pad_state | 2 × 0x50 bytes | per-port pad state, filled from the raw `scePadRead` buffers (0x004121D8, 100 bytes per port) by `func_00101858` | C (internals ?) |
| 0x2B0 | pad_buttons | 2 × 4 × u32 | per-port button words (`func_00101CF0`); `+0x2B4` (port 0, word 1) is the most used: bit 0x10 / 0x50 are confirm-type buttons in menus and the startup scene | C (bits H) |
| 0x2D0 | pad_ext | 2 × 0x20 bytes | per-port extra pad data (`func_00101E28`, with a constant table at 0x0029FFC0 + port*0x50); u16 at `+0x2DE`/`+0x2FE` | ? |
| 0x310 | unk_310 | 0x700 bytes | not accessed through `g_pMainContext` in any scanned code | ? |
| 0xA10 | data0_table | void*[4] | pointers into DATA.CDP entry 0 (`buf + buf[i+1]` for `i < buf[0]`), filled once by `func_00100768` | C (count H) |

## Per-frame helpers called from the main loop

| Address | Ghidra | Behaviour |
|---|---|---|
| 0x00104438 | `SendDmaPacketList(chan, list, tte)` | `ch = sceDmaGetChan(chan)`; sets CHCR.TTE (bit 6) if `tte == 1`, clears it otherwise; if `list->head` is non-null, calls `sceDmaSend(ch, list->head)`; then `head = tail = 0`. The main loop calls it as `(1, &ctx->vif1_list_head, 1)` and `(2, &ctx->gif_list_head, 0)`, i.e. it kicks last frame's VIF1 and GIF chains |
| 0x00103E90 | `SwapDoubleBuffer(db)` | `db->index ^= 1; return db->buf[db->index]`; result stored in `ctx->cur_buf` |
| 0x00104950 | `UpdateDisplayFrame(odd_field)` | unless `disp_flags & 0x4000`, calls `func_001049B0(mode, odd_field, &ctx->unk_80)`, which builds a GIF packet at 0x00420000 that copies or blends the draw buffer into the display buffer in 32-pixel strips (interlace field handling) and sends it on DMA ch.2. Then clears flag bits 0x2000/0x8000 and calls `PutDispAndDrawEnv` (0x00104E28: `sceGsPutDispEnv(ctx+0x90)`, optional `sceGsResetGraph(0,1,2,field/frame)`, DMA ch.2 send of the DrawEnv packet `ctx+0xC0`) |

The main loop order is therefore: kick last frame's DMA lists → swap the CPU-side buffer → game update (`SceneStep` builds new lists) → vsync → copy/flip and re-send the draw environment.

## Related globals (before the struct)

| Address | Use |
|---|---|
| 0x0029F350 / 0x0029F351 | player aircraft id / variant (mission loader, H) |
| 0x0029F360 / 0x0029F361 | second aircraft id / variant (H) |
| 0x0029F377 | current mission number 0..18 (H) |
| 0x004104A4 (`gp-0x554C`) | pointer to another game-state struct (`game->unk_20` movie request, `+0x27`, `+0x2A`, `+0x2B`) — next struct to reconstruct |
