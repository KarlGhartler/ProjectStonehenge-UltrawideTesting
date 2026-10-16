# SM model (3D models in DATA.CDP)

Hierarchical rigid/morph models drawn by the VU1 model programs (`../subsystems/vu-programs.md`). Research dump: `python tools/research/model_dump.py ENTRY ITEM [--vif] [--verts N]` (writes to stdout; redirect into `out/`).

Parsed by `BindModelData` 0x00144618, `CountModelNodes` 0x00144238, `BindModelNodes` 0x00144340, `CreateModelInstance` 0x0013F018, `UpdateModelHierarchy` 0x0013FE58, `InterpolatePartPose` 0x0013F4D0, `BuildMorphMesh` 0x0013F6C0 and `EmitModelInstance` 0x001404A8 (S4 inventory: `../port/s4-inventory.md` §3). All offsets are relative to the start of the model; the loader adds the model address.

Where models are found (S4): the aircraft viewer loads DATA 0xCD + aircraft (1–21): a container whose item 0 is itself a container {item 0: model, items 1–3: GIM textures of the three colours}; DATA 0xE4 + id: special weapon models; DATA 0xCD items: the hangar (confirmed by `0x0022E1F0`, `0x0022E218`, `0x00231000`).

## Header

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

## Node (0x40 bytes and following tables)

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

## Mesh header (4 × u32)
- +0: offset of a VIF/DMA packet called before the matrices (an empty `ret` in the F-4E).
- +4: offset of the mesh DMA list (below).
- +8: morph table offset (0: none) — node record flag 4; {+0 ?, +4 base offset, +0xC target count, +0x10 target offsets}; `BuildMorphMesh` writes interpolated strips into the instance's own buffer.
- +0xC: flags: bit 0 translucent part (drawn in the second pass: `EmitModelInstance` modes 1/2, PRIM alpha bit), bit 3 PRIM type 5 instead of 4 (fan/strip choice of the GIF tags).

## Mesh DMA list
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
So the program input is: header at TOP+0x17 (23), vertices from TOP+0x20 (32), 4 qwords each. The per-node constants (matrices 0–11, GIF tags 0x15/0x16/0x19/0x1F) are unpacked by `EmitModelInstance`, the light set (0x0C–0x14) by `WriteLightSetPacket`, the camera/fog qword (0x1E) and texture registers (0x1C) by the menu object functions (`../port/s4-inventory.md` §3).
