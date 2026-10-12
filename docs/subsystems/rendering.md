# Rendering: packet transport, flight frame, models, terrain

How the game builds and sends graphics packets, what the flight scene draws and in which order, and the data behind it: matrix sources and object identity (both needed for ADR-002 interpolation), models, lighting, terrain, map objects, shadows and the cutscene character engine. Effects: `effects.md`. VU microcode: `vu-programs.md`. HUD and cameras: `camera-hud.md`. Every function has a row in `../port/coverage-functions.csv` (subsystems `render`, `effects`, `sequence`, `camera`, `collision`).

Function counts in the transport sections come from offline scans of `jal` targets, dispatch tables and constant-propagated loads/stores (`tools/research/hwscan.py`, `../../tools/research/callers.py`); they are lower bounds.

## Summary

- **Packet transport is central; packet content is spread out.** A small set of helpers at 0x00103FD8–0x00104438 manages the frame's DMA chains (ordering-table buckets, `next`/`ref`/`call`/`end` tags), and the main loop kicks them once per frame. About **233 game functions** write packet contents (GIF tags, GS register data, VIF codes, vertex data) themselves.
- **Two DMA chains per frame**: a VIF1 chain (channel 1, TTE on: VU1 microcode, VU1 data, PATH1/PATH2 GS data) and a GIF chain (channel 2, PATH3). The display/draw environment packet is sent directly on channel 2.
- **VU1 microcode is uploaded inline every frame** through DMA `ref` tags that point into `.vutext` (`vu-programs.md`). There is no separate one-time upload.
- **VU0** is used in macro mode by game math (148 functions with COP2, 311 calling `sceVu0*`) and in micro mode by the cutscene character skinning only.
- **Direct hardware register access is rare** (table below). No game function touches GS privileged registers or VU memory directly (libgraph and DMA do that).

## Frame DMA structure

`MainContext` (`../game/main-context.md`) holds two lists: `vif1_list_head/tail` (+0x60) and `gif_list_head/tail` (+0x50).

| Step | Code | What |
|---|---|---|
| start of frame | `SendDmaPacketList(1, &ctx->vif1_list_head, tte=1)`, `SendDmaPacketList(2, &ctx->gif_list_head, 0)` (main loop) | `sceDmaSend` of **last** frame's chains, so the GPU side runs one frame behind the CPU |
| scene setup | `func_00103FD8(ot, n)` (30 caller functions) | allocate an **ordering table** of `n` buckets in the packet buffer: each bucket is a DMA `next` tag (0x20000000) chained to the following one, terminated by `end` (0x70000000) |
| start the frame list | `func_001043C0(&ctx->vif1_list_head, ot)` (30 callers) | **set** the VIF1 list head/tail to the ordering table (replaces the list) |
| append | `func_001043E8(&ctx->vif1_list_head, ot)` (1 caller, `BuildFlightDrawLists`) | write a `next` tag at the list tail pointing at the table, move the tail |
| draw calls | packet helpers below (about 233 caller functions in total) | insert packets into bucket `k`; lower `k` = earlier in the chain |
| end of frame | `WaitVSync` → `UpdateDisplayFrame` → `PutDispAndDrawEnv` | field copy (GIF packet at 0x00420000, `DrawDisplayTransfer` (H) 0x001049B0), `sceGsPutDispEnv`, DrawEnv GIF packet (`ctx+0xC0`) on channel 2 |

Packet helpers (all bump `ctx->cur_buf`, the per-frame double buffer):

| Address | Callers | Tag built | Use |
|---|---|---|---|
| 0x00104100 / 0x00104118 | 76 / 76 | open: remember `cur_buf`; close: insert a `next` tag pointing at the data written since open | wrap free-form packet data written directly by the caller |
| 0x00104198 | 59 | `next` tag to caller-supplied data, update `cur_buf` | same, caller-managed pointer |
| 0x001041E8 | 77 | `next` tag with `qwc` inline qwords (`0x20000000 | qwc`, 2 extra tag words, e.g. VIF codes), linked into the bucket | small inline packets |
| 0x00104250 / 0x001042A0 | 2 / 2 | `next` with 2 inline tag words / bare `next | qwc` header | special cases |
| 0x001042D0 | 7 | **`ref` tag** (`0x30000000 | qwc`, address) + `next` | static data: VU microcode in `.vutext`, VU data in `.vudata` |
| 0x00104338 | 7 | **`call` tag** (`0x50000000 | qwc`, address) with `qwc` inline qwords, + return `next` | call a pre-built chain |
| 0x00104090 | 1 | splice another ordering table into bucket n | flight renderer setup |
| 0x00104418 | 0 | close a list with a `next` tag to a given chain | dead |

`sceDmaSend` is called by only 6 functions: `SendDmaPacketList`, `PutDispAndDrawEnv`, the field copy `DrawDisplayTransfer` (H), `func_001118C8`, the movie path `func_00213C28`, and the dead `func_001368C0`.

## Direct hardware access from game code

| Register / area | Functions | Notes |
|---|---|---|
| `T0_COUNT`, `T0_MODE` (0x10000000/10) | 4 / 3 | main loop reset; movie timing (0x00211CA8, 0x002122C0, 0x00212110, 0x00238770, 0x00238810) |
| `D8_*` fromSPR, channel 8 (0x1000D000–D080) | 1 live | `func_0013A6B8` (terrain clip list): copy from the scratchpad to RAM by DMA. Dead: `FlushSprWorkBuffer` 0x00103D98, 0x00103C50, 0x00105210, 0x00105278 |
| `D9_*` toSPR, channel 9 (0x1000D400–D480) | 1 live | `func_0019FFF0` (weather/sky effect draw): copy into the scratchpad. Dead: 0x001052A8, 0x00144D68 |
| `D0_*` VIF0 (0x10008000–30) | 2 | 0x00136818, `UploadVu0Microcode` 0x00225408 (VU0 microcode, VIF0 chain) |
| `D2_CHCR`/`D2_TADR` GIF | 0 live | dead: 0x00103C50 (start the GIF chain from the SPR ring), `FlushSprWorkBuffer` 0x00103D98 |
| `D_CTRL`, `D_STAT`, `D_RBOR`/`D_RBSR` (0x1000E000–50) | 2 | `InitSprRingBuffer` 0x00103C00 (from `main`) sets the ring at 0x00418000, size 0x7FF0, and **clears** `D_CTRL` bits 2–3 (MFD off); only the dead 0x00103C50 sets them (`D_CTRL |= 0xC`). 0x00225408 clears `D_STAT` |
| DMA channels through `sceDmaGetChan` pointers | 2 | sequence character engine: `func_00223CE8` writes TADR/QWC/CHCR, `func_00223CC0` spins on STR (section below) |
| GS privileged (0x12000000) | **0** | only libgraph |
| VU0/VU1 memory (0x11000000–0x1100FFFF) | **0** | only through VIF DMA |
| scratchpad (0x70000000) | 73 | work memory (`../game/memory.md`) |

**11 live game functions** access EE hardware registers through constant addresses: `main` 0x001001F0, the four movie-timer functions, `InitSprRingBuffer`, `func_0013A6B8`, `func_0019FFF0`, `func_00136818`, `UploadVu0Microcode`. The two sequence-engine functions reach DMA registers through `sceDmaGetChan` pointers. Six dead functions also touch DMA registers (`../port/dead-code.md`).

**The scratchpad → GIF ring is never used by live code.** `GetSprWorkBuffer` 0x00103D80 and `FlushSprWorkBuffer` 0x00103D98 are called only by 0x00103CC8, which has no reference of any kind (Ghidra xrefs and a pointer search of the whole image), and the live ring setup leaves the drain (MFD) off. The renderer only needs the `sceDmaSend` chains. SDK use from game code: libgraph called from 13 functions, libdma 10, libpkt 1, libvu0 311.

## Flight frame (`BuildFlightDrawLists` 0x0010BD68)

The flight scene object (gp `0x004104C4`, `mission-flow.md`) has **5 view slots** of 0x440 bytes at +0x0000–0x153F; the number of views in use is +0x1650 (1, or 2 in split screen). Each view has a flags word (bit 31 active, bit 0 3D, bit 1 HUD, bit 2 radio window, bit 3 centre messages, bit 0x40000000 clear Z) and a camera at +0xB0. Ordering tables come from the bucket pool at +0x1540 (16 bytes per table head, counter +0x1651); the shared table +0x1640 has 4 buckets.

Per frame:
1. The first pool table (4 buckets) becomes the VIF1 list (`func_001043C0`); the shared table +0x1640 is initialised.
2. For each view in use with bit 31 set:
   - **if bit 0 (3D)**: four tables, the view main table appended to the VIF1 list, the others spliced into it:

     | Table | Buckets | Linked into | Contents |
     |---|---|---|---|
     | view main | 0x10 | VIF1 list | CLAMP (0x00138B50), VU1 program A in buckets 6 and 9 (0x00138BB0), scissor = viewport (0x00138C58), optional Z clear (0x00138DB0), sky (0x00111B28), per-view effect pass (0x001D3318), weather (0x00118330, bucket 5), world objects (0x0011E0D8), projectiles (`DrawProjectiles`), full-screen scissor in the last bucket (0x00138D18) |
     | terrain | 0x20 | main bucket 4 | `.vudata` 0x00402200 in bucket 0 (0x00138C10), terrain (`DrawTerrain` 0x0013EA78), sea in bucket 0x1E |
     | ground objects | 0x20 | main bucket 2 | VU1 program B in bucket 0 (0x00138C28), static map objects (`DrawMapObjects` 0x0013EB20) |
     | effects | 0x400 | main bucket 0xC | effect sub-table setup (0x001558C8), all effects (`DrawEffects` 0x00155330), Z write back on in bucket 0x3FF (`effects.md`) |
     | sequence | 1 | main bucket 9 | in scene-2 sub-state 0x13, or when flight object +0x1688 == 1: sequence characters (0x00179008) and sequence overlays (0x00179298) |

   - **2D overlay table** (4 buckets), appended to the VIF1 list: screen fade in bucket 2 (0x00113248) when the view's fade value (+0x1670 + view × 4) is > 0. When any of flags bits 1–3 is set, an 8-bucket HUD table is spliced into overlay bucket 1: scissor, full-screen image from mission data (0x0015F560), `DrawHud` (bit 1), radio window (bit 2, sequence type 0 or 3: `func_002512B8` unless mode 3; tutorial control-hint panel `func_00258690` for type 3, drawn only when the tutorial radio bank is loaded, `sound.md`) in bucket 7, then by sub-state: game-over menu (0x1C, 0x0017E210), carrier rearm menu (0x1D, 0x0017E460), pause menu (ctx flag 0x100, 0x0017E118), otherwise centre messages (bit 3, 0x0016DC08); full-screen scissor last.
   - In scene-2 sub-state 0xE a 1-bucket table in overlay bucket 1 plays the scripted subtitle timeline (`func_002593A0`).
3. After the views: replay HUD (0x0017D8F0) in bucket 2 of the last overlay table, empty hook 0x00155AF0, then the shared table +0x1640 is appended.

Draw order inside a frame is fixed by bucket numbers, not by call order. A GPU backend can keep one command list per bucket and execute them in bucket order.

## Cameras and matrices

Camera record (inside the view, +0xB0; built by `func_001029E0` (`camera-hud.md`), or by `BuildScaledCameraMatrices` 0x001420F0 for scaled passes):

| Offset | Content |
|---|---|
| +0x00 / +0x10 | position / Euler rotation |
| +0x20 | world → view (inverse of the camera transform) |
| +0x60 | view → screen (`sceVu0ViewScreenMatrix`) |
| +0xA0 | view → clip (`func_001031C0`) |
| +0xE0 | **world → screen** (used for every model node) |
| +0x120 | **world → clip** (used for clipping on VU1) |
| +0x160..+0x170 | screen size / projection distance / near / far |
| +0x178 / +0x17C | screen centre (2048-based) |
| +0x188..+0x18E | viewport x, y, w, h (scissor) |

`BuildScaledCameraMatrices(s)` rebuilds all of these with the screen scaled by s. The half-resolution pass uses s = 0.5 and then restores s = 1.0.

### Matrix sources (for interpolation, ADR-002)

| Object | Where the per-frame matrices are built | Identity |
|---|---|---|
| model instance (any) | `UpdateModelHierarchy` 0x0013FE58: node local = T·R from animated keys (`func_0013F4D0`, `func_001089B0`), world = parent × local; two matrix arrays per instance (+0x10, +0x14) | instance pointer (work-heap block, stable for the mission) + node index |
| units (index ≥ 2) | `UpdateUnitModelHierarchies` 0x00145DF8 per frame for the 4 LOD instances of the current model state (unit +0x5A) | unit index in the pointer table `0x002A1F70[]` + model state + LOD |
| player aircraft | `func_001481B8` / `func_00148308` | player index + instance slot |
| map objects | `func_00144728` at map creation (static), selected per map entry `func_00144CC0` | map entry + object index |
| terrain | no matrices: VU1 packets built per frame from the camera (`func_0013A6B8`, `func_0013B2F8`) | cell coordinates |
| projectiles | `DrawProjectiles` (`weapons.md`); missile/special models `func_0014AA78` / `func_0014ACB0` | pool slot number (stable while the slot is occupied) |
| effects | built in each effect draw function from the record (position, velocity, age) | pool offset + record index (lowest-free allocation, `effects.md`) |
| cutscene characters | bone table 0x00369240 (`func_00221758`) | sequence character + bone |
| cameras | per view, per frame (`func_001029E0`) | view index |

`EmitModelInstance` 0x001404A8 multiplies the camera matrices (+0xE0, +0x120) with the node world matrices **while it writes the packet**. The final per-node matrices exist only inside the packet, not in RAM. An interpolating renderer must interpolate the node world matrices (instance +0x10/+0x14) or the inputs (unit position and rotation), then redo the multiplications.

## Model format and instances

Model data format: `../formats/MODEL.md`. Runtime objects:
- **model object** (0x28 bytes, `BindModelData` 0x00144618): pointers into the data, node count, track count, id → node table, flattened 0x50-byte node records (`func_00144340`).
- **model instance** (0x4C bytes, `CreateModelInstance` 0x0013F018). Fields:
  - +0x00 node flags (bit 0 hidden by id, bit 1 hidden by class, bit 2 needs re-evaluation, bit 3 empty node);
  - +0x04 track times;
  - +0x08 node ids;
  - +0x10 / +0x14 world matrices (0x40 per node);
  - +0x18 / +0x1C node origins;
  - +0x20 update parity bytes;
  - +0x24 / +0x28 / +0x2C packet pointers per node (+0x2C current);
  - +0x30 model object;
  - +0x40 radius;
  - +0x48 triangle count.

Per-node emission (`EmitModelInstance` 0x001404A8):
1. Optional DMA `call` of the model's base packet (model data +0x10).
2. For each visible node: VIF `UNPACK` of three 4×4 matrices: world→screen × node, world→clip × node, light × node second matrix.
3. Four GIF tag templates written to VU1 memory at 0x15/0x16/0x19/0x1F. The PRIM bits come from the node flags (triangle strip, Gouraud, textured, fog; context 2 + alpha for the second pair).
4. A DMA `call` of the node's packet. Node packets are **pre-built DMA chains in the loaded data, ending with a `ret` tag (0x60000000)**. `EmitModelInstance` writes these `call` tags itself; it does not use `func_00104338`.

Animation tracks (`func_00140F68`, `func_00141000`) store a time per track, and the nodes that use a track are re-evaluated. Node flag 4 marks vertex-animated nodes, whose packet vertices are rewritten from key frames (`func_0013F6C0`, FlushCache).

## Lighting

Light sets (0xE0+ bytes): three directional lights stored transposed as a direction matrix (+0x50) plus a colour matrix (+0x90), and an ambient colour (+0xD0). Colours are packed RGBA × 1/128 (`func_00136B68`, `func_00136DA0`, `func_00136F48`). `WriteLightSetPacket` 0x001370D8 writes the light block into a packet before each model batch. The sun direction comes from the map entry: two angles at terrain object +0x88/+0x8C.

## Terrain (`DrawTerrain`, object `*0x00410694`)

1. `func_0013A110`: camera terrain cell (`func_00139000`, 2048-unit cells), LOD ring radii, fog distance (`powf`), sun light set.
2. `func_0013A6B8`: VU0 clip test per 16×16 block, visible list in scratchpad 0x70000000.
3. `func_0013B2F8`: far LOD packets for the visible blocks (VIF/GIF, terrain sub-table, `.vudata` block in bucket 0).
4. `func_0013CDC0`: near cells as 2-triangle or subdivided strips (`func_0013C1D0`/`func_0013C740`), texture page from the attribute word.
5. Sea (`*0x0041068C`, terrain flag +0xF4 bit 0, `func_00138350`): flat 2048-unit tiles (lists at terrain object +0x80/+0x84, count +0x7C), two triangles per tile, clipped to the frustum on the CPU (`func_00137930`, Sutherland-Hodgman over 6 planes), UV scrolled by `(frame_count_unpaused % 10240) / 10240`, fog alpha 0..127 from the depth between +0xA8 and +0xA4; four rotating packet buffers.

The sea object allocation (`func_001381F8`) fills a gradient noise table **once per boot from `rand()`** (255 calls, `func_00137550`, guarded by `D_00410690`; `../game/random.md`). The only reader of the table, the 3D noise `func_00137608`, is called only by dead code (`../port/dead-code.md`), so the sea is not displaced; the 255 `rand()` calls still advance the sequence and must be kept.

The height functions used by gameplay (`weapons.md`, collision) share the terrain data but not this code.

## Static map objects (`*0x0041069C`)

`func_00144728` builds per-map-entry object lists and model instances. It also builds **collision triangles extracted from the model packets themselves** (`func_00143C10`: walks each node chain up to the `ret` tag, transforms positions by the node matrix, face normals). Gameplay uses them: the terrain segment test `func_0013D698` calls `func_00144DC8` for the objects of each cell. So the port must keep the model packet data loadable as data, even if the GPU path never uses it as packets.

## Aircraft draw and shadow (`func_001483A0`)

- **Ground shadow**:
  - `DrawAircraftShadow` 0x001418C8 finds the terrain hit along the sun direction, builds a drop-shadow matrix there, and draws the model with VU1 program B into an off-screen buffer (GS page 0x2500).
  - The shadow passes 0x00146AB0 / 0x00147540 / 0x00147828 / 0x00146E28 ×3 / 0x00147B30 / 0x001471C8 then copy it to half size, blur it with three offsets, and blend it onto the frame (alpha 0x28808080).
  - Missions in mask 0x00204114 and mission ids 0x3D–0x4E use a variant; below altitude 90 the static objects are drawn into the shadow buffer as well (0x00145578).
- **View mode** (view +7 & 3) picks the VU1 program: external views use program A; mode 2 uses program E (0x0029B0F0) and mode 3 program C (0x00299820) (cockpit-type views).
- **Half-resolution glow pass** `func_00142460`: VU1 program D (0x0029A930) loaded at VU address 0xF2, camera scaled 0.5; draws untextured alpha-blended Gouraud triangles into a 256-pixel-wide target at block 0x2500 (Z24 at 0x2A00), blurs it with sprite passes between 0x2500 and 0x2A00, and **adds** it to the frame in 256×16 strips (`(Cs−0)·As+Cd`, composite `func_00141EE0`). Called only when the map flag +0xF4 bit 1 is set (seen in the in-engine demonstration and mission 3 external view; `../pcsx2/sessions-04.md`). What lights it is open (H: specular highlights).

## Sequence character engine (0x00220000–0x00226198, `sequence`)

A separate skinned-character library used only by in-engine cutscenes (`mission-flow.md`):
- linked character list (+0xC8), parts of 0x160 bytes, bones of 0xB0 bytes, motion channels (4 per character, rotation order per root via `func_00222800`);
- **skinning runs in VU0 micro mode**: microprogram entries 0x00/0x10/0x30/0x40/0x50/0x70/0x80/0x90/0xB0 called with `vcallms` (`func_002250E8`…`func_00225270`), with data moved through scratchpad 0x70002200 by DMA channels 8/9 (program in `vu-programs.md`);
- the engine **starts and waits on DMA channels itself** through `sceDmaGetChan` pointers (`func_00223CE8` writes TADR/QWC/CHCR, `func_00223CC0` spins on STR);
- 4 KB page allocator for VU/texture memory (`func_00225570`), GIF templates 0x00384240/0x00384340.

## Port consequences

- The **transport layer** (ordering tables, tag helpers, `SendDmaPacketList`, `PutDispAndDrawEnv`) is small and can be reimplemented once.
- The **content** is the hard part. About 233 functions write GS/VIF data directly into packets: GIF tags with A+D register writes, `PRIM`/`RGBAQ`/`XYZ2` streams, VIF `UNPACK`/`MSCAL` commands for VU1. A port must interpret those packets (a GS/VIF "replayer" on the GPU) unless every one of those functions is rewritten to call a new render API (ADR-003).
- VU1 programs become GPU shaders or a CPU VU1 interpreter; VU0 macro code is ordinary vector math (`vu0_math.h`); the VU0 micro skinning needs an interpreter or an exact rewrite (`vu-programs.md`).
- Scratchpad DMA (`D8`/`D9`) is memory copies in disguise.
- Drawing calls `rand()` and reads the random table: the draw-list build must run exactly once per simulated frame, in the original order (`../game/random.md`, ADR-002).

## Things the port must keep

- Bucket order per view (above) and the per-view scissor/Z-clear behaviour.
- The node packet data and the collision triangles extracted from it (gameplay uses them).
- Draw-phase `rand()` calls and random-table reads in the original order.

## Open points

- Whether the first pool table (step 1 of the flight frame) receives packets from code outside `BuildFlightDrawLists`.
