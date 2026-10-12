# VU programs (`.vutext`, `.vudata`)

The VU1 and VU0 microcode blocks, how the game uploads them, and who uses them. Packet transport and bucket order: `rendering.md`. Rules for microcode analysis: `../../.claude/rules/vu-microcode.md` (never create functions or disassemble inside `.vutext`, `.vudata` or `.DVP.overlay.*` in Ghidra). Section addresses: `../platform/elf-sections.md`.

## Blocks

Each `.vutext` block is a ready DMA packet: a `ret` (or `cnt`) tag holding the qword count, then `FLUSHE`, `MPG` (load address) and the microcode. Only the headers have been read; the microcode itself is not analysed.

| Address | QW | MPG address | Program | Used by |
|---|---|---|---|---|
| 0x00297C00 | 0xEA | 0 | VU1 A: generic model renderer | view buckets 6 and 9 (`func_00138BB0`, ref to 0x00297C10); menu 3D objects with lighting mode 1 |
| 0x00298AC0 | 0xD4 | 0 | VU1 B | ground objects (`func_00138C28`, ref to 0x00298AD0, bucket 0), shadows (`DrawAircraftShadow` 0x001418C8), menu lighting mode 0 |
| 0x00299820 | 0x10F | 0 | VU1 C | aircraft view mode 3, menu lighting mode 2 |
| 0x0029A930 | 0x7A | 0xF2 | VU1 D | half-resolution glow pass `func_00142460` (untextured Gouraud output), loaded on top of a resident program |
| 0x0029B0F0 | 0x10B | 0 | VU1 E | aircraft view mode 2 |
| 0x0029C200 | 0x6C (`cnt`) | 0xD8 | **VU0** micro program | cutscene character skinning (`rendering.md`, sequence character engine) |

VU1 data: `.vudata` 0x00402200, size `*0x004021F0`, sent by `func_00138C10` with a `ref` tag into terrain bucket 0.

## How the programs are uploaded

| Caller | Address | How |
|---|---|---|
| `func_00138BB0` | 0x00297C10, 0xEA qwords, buckets 6 and 9 | `ref` via `func_001042D0` (from `BuildFlightDrawLists` 0x0010BD68) |
| `func_00138C28` | 0x00298AD0, 0xD4 qwords, bucket 0 | `ref` via `func_001042D0` |
| `func_00138C10` | `.vudata` 0x00402200, size `*0x004021F0` | `ref` via `func_001042D0` (VU data) |
| `DrawAircraftShadow` 0x001418C8, `func_001483A0`, `func_00142460`, `func_0014B280` | 0x00298AC0, 0x0029B0F0, 0x00299820 | `ref` via `func_001042D0` |
| `func_0014AE60` | 0x00297C00 / 0x00298AC0 / 0x00299820 by lighting mode 1/0/2 | stores the microcode pointer in a render object (menu 3D models, caller `func_00231000`) |
| `UploadVu0Microcode` 0x00225408 | 0x0029C200 | **direct VIF0 DMA**: `D0_QWC = 0`, `D0_TADR = 0x0029C200`, `D0_CHCR = 0x145` (chain mode, start), then `sceDmaSync`; clears `D_STAT`. The only VU0 micro-mode upload |
| `.data` 0x003B29E0, 0x003C9BB0, 0x003C9CD0 | 0x00299910, 0x0029A300 | values inside program C, so probably not microcode pointers (H) |

`.vutext` is laid out as ready-to-DMA VIF packets. The game re-sends microcode inside the VIF1 chain whenever a renderer needs it: there is no one-time upload. A port that replaces VU1 with GPU shaders can treat each `ref` to a `.vutext` address as "bind pipeline X".

## VU0 micro mode

Used only by the sequence character engine: entries 0x00/0x10/0x30/0x40/0x50/0x70/0x80/0x90/0xB0 called with `vcallms` (`func_002250E8`…`func_00225270`), data moved through scratchpad 0x70002200 by DMA channels 8/9. All other VU0 use is macro mode (`vu0_math.h`).

Which entry runs depends on the part type (+0x140) in `FUN_002236A0`: type 0 (from the character data) → `FUN_00224180` → loop 0x002250E8 (entry 0x00); type 1 → `FUN_00224410` → loop 0x00225110 (entry 0x10); type 3 (morph track active, set by `FUN_00223F58`) → `FUN_00224848` → loop 0x00225140 (entry 0x30). The rigid path `FUN_00223830` → `FUN_00224D50` also uses 0x00225110. Each loop: `ctc2` a1 → VI01 and a2 → VI03 (VU0 data-memory pointers), then a3 times `vcallms`, `qmfc2.I`/`sqc2 vf1` to the buffer in a0 (one quadword per call).

Test vectors: path 0x00225110 captured in PCSX2 (`../pcsx2/sessions-05.md`, states `vu0_00225110_before/after`); inputs are the VU0 registers (`tools/p2s_read.py STATE vu0`), `vu0Memory.bin`, scratchpad and EE RAM, output the a0 buffer. Paths 0x002250E8 and 0x00225140 are pending (`../pcsx2/procedures.md` check 1).

## Port consequences

- 5 VU1 programs become GPU shaders (or a CPU VU1 interpreter) selected by the `ref` address.
- The VU0 program 0x0029C200 needs either a VU0 micro-mode interpreter or a C++ rewrite of its nine entry points, checked against PCSX2.
- The microcode is read from the ISO at runtime like the rest of the executable's data; it is never copied into `src/`.

## Open points

- What each VU1 program computes (microcode analysis under the vu-microcode rules).
