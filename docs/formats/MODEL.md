# Model data (`SM` + space + NUL), Namco in-house format

Priority 2, step 10. Layout derived from the loader code (`BindModelData` 0x00144618, `func_00144238`, `func_00144340`, `CreateModelInstance` 0x0013F018, `EmitModelInstance` 0x001404A8, `func_00143C10`) and checked against one aircraft-viewer model (DATA 206, sub-block 0, item 0). Runtime behaviour: `docs/research/render.md`.

## Where models are found

Models are sub-blocks of DATA containers, usually next to their GIM textures (`GIM.md`):

| Source | Path | Loader |
|---|---|---|
| mission map objects | mission block 16 (`MISSION.md`) | `func_00139718` / `func_00139B70` |
| mission unit model sets | mission block 21 | `func_001457E0` / `func_001459E0` |
| mission shared model sets | mission block 3 | `func_00149FB8` |
| aircraft (flight) | aircraft block 0/2 (`AIRCRAFT.md`) | `func_00147BF0` |
| aircraft viewer (menus) | DATA 206–227 (0xCD + aircraft id), sub-block 0 = container {model, 3 GIM} | `func_0022E1F0`, `func_0014AEF8` |
| weapon models (menus) | DATA 228–258 (0xE4 + weapon id) | `func_0022E218` |
| hangar scene | DATA 205 (5 sub-blocks) | `func_0022E1A8` |

## Header

| Offset | Type | Field |
|---|---|---|
| 0x00 | u32 | magic 0x00204D53 (`"SM \0"`) |
| 0x04 | u32 | version (0xD4 in the menu models). The code tests `< 0xB4` (absolute child arrays inside the node), `< 0xBE` and `< 0xBF` (no node-id table) |
| 0x08 | u32 | (0x100 in the sample) |
| 0x10 | u32 | offset of the model base packet (called before the nodes when the draw mode allows it) |
| 0x14 | u32 | offset of the root node |
| 0x1C | u32 | header extension (instance code reads +4 and +0xC; version ≥ 0xB4 reads +0x38, ≥ 0xBE reads +0x18) |
| 0x20 | float[8] | bounding box (min xyzw, max xyzw) |

All offsets are relative to the start of the model block. The loader adds the base itself; data is not patched in place except the vertex-animated packets.

## Node record (tree, depth first)

| Offset | Type | Field |
|---|---|---|
| 0x00 | float[4] | translation |
| 0x10 | float[4] | rotation (Euler, radians) |
| 0x20 | u32 | offset of the animation block (0 = static). The block holds +4 key data and +8 track info |
| 0x24 | u32 | offset of the packet info: {+0 offset of the alternate packet, +4 offset of the node packet chain, +8 offset of the vertex-animation block (0 = none), +0xC flags (bit 0)} |
| 0x28 | float | radius |
| 0x2C | u32 | depth |
| 0x30 | u32 | child count |
| 0x34 | u32 / u32[] | children: offset of a child-offset array (version ≥ 0xB4), else the array itself starts here |
| 0x38 | u32 | flags: 0x10 / 0x20 hide classes (`func_00140408`), bit 0 used by the hierarchy update |
| 0x3C | u32 | node id (0xFF = none) → id table for animation and show/hide by id |

## Node packet chain

A sequence of 16-byte DMA tags with their data, ending with a DMA `ret` tag (0x60000000):
- The model emitter `call`s it after uploading the node matrices to VU1, so the packets hold VIF `UNPACK` data for VU1 program A/B/C/E (vertex, normal, UV, colour) plus `MSCAL`.
- The triangle count is derived from each tag's QWC: `((qwc − 1)·4)/9 − 2` per strip (`CreateModelInstance`). Nodes with 0 triangles get instance flag 8.
- The same packets are read on the EE to build collision triangles (`func_00143C10`). So vertex positions must stay readable at their PS2 offsets.

## Confidence

- Confirmed (loader code + sample): magic, version, +0x10, +0x14, node fields +0x00, +0x10, +0x20, +0x24, +0x30, +0x34, +0x3C, the `ret`-terminated packet chains.
- Confirmed from code only: node +0x2C, +0x38, the packet-info fields.
- Hypothesis: header +0x08 meaning, +0x20 bounding box (floats in the sample look like min/max), node +0x28 as radius (float, used as the model radius at instance +0x40).

## Open points

- Exact VIF layout of the vertex data inside the node packets (depends on the VU1 programs; analysis only).
- Animation block key format (`func_0013F4D0`: per-track time × 100 clamped to 0..100; key layout not decoded).
- Vertex-animation block (`func_0013F6C0`, skimmed).
