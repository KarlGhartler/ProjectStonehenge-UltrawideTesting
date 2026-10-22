# Model data (`SM` + space + NUL), Namco in-house format

Layout derived from the loader code (`BindModelData` 0x00144618, `func_00144238`, `func_00144340`, `CreateModelInstance` 0x0013F018, `EmitModelInstance` 0x001404A8, `func_00143C10`) and checked against one aircraft-viewer model (DATA 206, sub-block 0, item 0). Runtime behaviour: `../subsystems/rendering.md`.

## Where models are found

Models are sub-blocks of DATA containers, usually next to their GIM textures (`GIM.md`):

| Source | Path | Loader |
|---|---|---|
| mission map objects | mission block 16 (`MISSION.md`) | `func_00139718` / `SetupMapFromMissionData` (H) 0x00139B70 |
| mission unit model sets | mission block 21 | `func_001457E0` / `func_001459E0` |
| mission shared model sets | mission block 3 | `func_00149FB8` |
| aircraft (flight) | aircraft block 0/2 (`AIRCRAFT.md`) | `SetupAircraftModels` (H) 0x00147BF0 |
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

## Loader view of slice S4 (formerly `MODEL.md`)

A second description, written while translating the menu object and aircraft viewer (slice S4), kept as written. Where it and the sections above read a field differently (header +0x08 / +0x0C / +0x10 / +0x18), the code is the reference.

Hierarchical rigid/morph models drawn by the VU1 model programs (`../subsystems/vu-programs.md`). Research dump: `python tools/research/model_dump.py ENTRY ITEM [--vif] [--verts N]` (writes to stdout; redirect into `out/`).

Parsed by `BindModelData` 0x00144618, `CountModelNodes` 0x00144238, `BindModelNodes` 0x00144340, `CreateModelInstance` 0x0013F018, `UpdateModelHierarchy` 0x0013FE58, `InterpolatePartPose` 0x0013F4D0, `BuildMorphMesh` 0x0013F6C0 and `EmitModelInstance` 0x001404A8 (S4 inventory: `../port/slices.md` §A.3). All offsets are relative to the start of the model; the loader adds the model address.

Where models are found (S4): the aircraft viewer loads DATA 0xCD + aircraft (1–21): a container whose item 0 is itself a container {item 0: model, items 1–3: GIM textures of the three colours}; DATA 0xE4 + id: special weapon models; DATA 0xCD items: the hangar (confirmed by `0x0022E1F0`, `0x0022E218`, `0x00231000`).

### Header

| Offset | Type | Meaning | Confidence |
|---|---|---|---|
| +0x00 | u32 | magic `0x00204D53` ("SM ") | seen in data; not checked by the code |
| +0x04 | u32 | version (0xD4 in the F-4E); the code branches at ≥ 0xB4 (child tables by offset, bounding radius at +0x38), ≥ 0xBE (+0x18 value), ≥ 0xBF (part index table) | confirmed (code) |
| +0x0C | u32 | copied to instance +0x38 | confirmed (use unknown) |
| +0x10 | u32 | offset of the node area (0x40) | confirmed |
| +0x14 | u32 | offset of the root node | confirmed |
| +0x18 | u32 | instance +0x44 (version ≥ 0xBE) | confirmed (use unknown) |
| +0x20.. | f32 | bounds (min/max) | hypothesis |
| +0x38 | f32 | bounding radius (version ≥ 0xB4; else root node +0x28) | confirmed |

### Node (0x40 bytes and following tables)

| Offset | Type | Meaning |
|---|---|---|
| +0x00 | f32[3] | translation |
| +0x10 | f32[3] | rotation (radians, X Y Z), matrix by `0x001089B0` (VU0 polynomials) |
| +0x20 | u32 | keyframe table offset (0: none) — node record flag 2 |
| +0x24 | u32 | mesh header offset (0: no mesh) |
| +0x2C | u32 | part-value channel (instance part values: 0.5 at creation; set by `SetModelPartValue` 0x00140F68) |
| +0x30 | u32 | child count |
| +0x34 | u32 | children: offset of a u32 offset table (version ≥ 0xB4), else the table starts here |
| +0x38 | u32 | bit 0: hide when the hierarchy is updated with flag 1 (`UpdateModelHierarchy`) |
| +0x3C | u32 | part index (0xFF: none) → instance part map |

Keyframe table (+0x20): +0 key count, +4 offset of the key positions (bytes, percent 0–100), +8 offset of the key values (0x24 bytes each: translation f32[3], rotation f32[3], 3 more floats). The pose is the linear interpolation between the two keys around `part value × 100` (`InterpolatePartPose`, VU0).

### Mesh header (4 × u32)
- +0: offset of a VIF/DMA packet called before the matrices (an empty `ret` in the F-4E).
- +4: offset of the mesh DMA list (below).
- +8: morph table offset (0: none) — node record flag 4; {+0 ?, +4 base offset, +0xC target count, +0x10 target offsets}; `BuildMorphMesh` writes interpolated strips into the instance's own buffer.
- +0xC: flags: bit 0 translucent part (drawn in the second pass: `EmitModelInstance` modes 1/2, PRIM alpha bit), bit 3 PRIM type 5 instead of 4 (fan/strip choice of the GIF tags).

### Mesh DMA list
A chain of DMA `cnt` tags ending with `ret` (`0x60000000`); `CreateModelInstance` counts the strips from the tags. Each strip (≤ 4 vertices in the dumped aircraft):

```
STCYCL CL=4 WL=1
UNPACK V4-16  num=n addr=0x20 (+TOPS)            ST, signed 4.12 fixed point      -> vertex qword 0
UNPACK V4-8   num=n addr=0x21 (+TOPS, unsigned)  RGBA, 0..255 (0x80 = 1.0)        -> vertex qword 1
UNPACK V4-32  num=n addr=0x22 (+TOPS)            position x, y, z, w = 1.0 (f32)  -> vertex qword 2
UNPACK V4-16  num=n addr=0x23 (+TOPS)            normal, signed 4.12              -> vertex qword 3
STCYCL CL=1 WL=1
UNPACK V4-8   num=1 addr=0x17 (+TOPS, unsigned)  strip header: x = vertex count, y = flags, z, w
MSCNT                                             run the resident program on this buffer
```
So the program input is: header at TOP+0x17 (23), vertices from TOP+0x20 (32), 4 qwords each. The per-node constants (matrices 0–11, GIF tags 0x15/0x16/0x19/0x1F) are unpacked by `EmitModelInstance`, the light set (0x0C–0x14) by `WriteLightSetPacket`, the camera/fog qword (0x1E) and texture registers (0x1C) by the menu object functions (`../port/slices.md` §A.3).
