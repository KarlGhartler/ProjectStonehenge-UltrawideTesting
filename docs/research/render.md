# Rendering: flight frame, models, terrain, effects, VU programs (priority 2, step 9)

Scope: what the flight scene draws, in which order and with which data. It covers matrix sources and object identity (both needed for ADR-002 interpolation), the VU1/VU0 programs, the effects system and the randomness used while drawing. Packet transport (ordering tables, DMA tags, `SendDmaPacketList`) is in `render-boundary.md`; HUD in `hud.md`; cutscene characters in the "Sequence character engine" section below. Every function has a row in `coverage-functions.csv` (subsystems `render`, `effects`, `sequence`, `camera`, `collision`).

## Frame structure (`BuildFlightDrawLists` 0x0010BD68)

The flight scene (object at gp `0x004104C4`) keeps up to 4 **views** of 0x440 bytes each at offset +0 (count +0x1650). Each view has a flags word (bit 31 active, bit 0 3D, bit 1 HUD, bit 2 radio window, bit 3 centre messages, bit 0x40000000 clear Z) and a camera at +0xB0. Each frame allocates ordering tables from the bucket pool at +0x1540 (16 bytes per table head, counter +0x1651):

| Table | Buckets | Linked into | Contents |
|---|---|---|---|
| view main | 0x10 | VIF1 list | per view: CLAMP (0x00138B50), VU1 program A in buckets 6 and 9 (0x00138BB0), scissor = viewport (0x00138C58), optional Z clear (0x00138DB0), sky (`0x00111B28`), weather (`0x00118330`), world objects (`0x0011E0D8`), projectiles (`DrawProjectiles`), sequence overlays, full-screen scissor in the last bucket (0x00138D18) |
| terrain | 0x20 | main bucket 4 | `.vudata` 0x00402200 in bucket 0 (0x00138C10), terrain (`0x0013EA78`), sea in bucket 0x1E |
| ground objects | 0x20 | main bucket 2 | VU1 program B in bucket 0 (0x00138C28), static map objects (`0x0013EB20`) |
| effects | 0x400 | main bucket 0xC | effect draw-env (0x001558C8), all effects (`0x00155330`), Z write back on in bucket 0x3FF |
| 2D overlay | 4 | VIF1 list | fade (`0x00113248`), HUD table (8 buckets: `DrawHud`, radio window, centre message, pause/game-over/end menus), scripted subtitle timeline in scene-2 sub 0xE |

After the views: replay HUD (`0x0017D8F0`), empty hook 0x00155AF0, then the shared table at +0x1640 is appended.

Draw order inside a frame is therefore fixed by bucket numbers, not by call order. A GPU backend can keep one command list per bucket and execute them in bucket order.

## Cameras and matrices

Camera record (inside the view, +0xB0; built by `func_001029E0` (`hud.md`), or by `func_001420F0` for scaled passes):

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

`func_001420F0(s)` rebuilds all of these with the screen scaled by s. The half-resolution pass uses s = 0.5 and then restores s = 1.0.

### Matrix sources (for interpolation, ADR-002)

| Object | Where the per-frame matrices are built | Identity |
|---|---|---|
| model instance (any) | `UpdateModelHierarchy` 0x0013FE58: node local = T·R from animated keys (`func_0013F4D0`, `func_001089B0`), world = parent × local; two matrix arrays per instance (+0x10, +0x14) | instance pointer (work-heap block, stable for the mission) + node index |
| units (index ≥ 2) | `func_00145DF8` per frame for the 4 LOD instances of the current model state (unit +0x5A) | unit index in `0x002A1F70[]` + model state + LOD |
| player aircraft | `func_001481B8` / `func_00148308` | player index + instance slot |
| map objects | `func_00144728` at map creation (static), selected per map entry `func_00144CC0` | map entry + object index |
| terrain | no matrices: VU1 packets built per frame from the camera (`func_0013A6B8`, `func_0013B2F8`) | cell coordinates |
| projectiles | `DrawProjectiles` (`weapons.md`); missile/special models `func_0014AA78` / `func_0014ACB0` | pool slot number (stable while the slot is occupied) |
| effects | built in each effect draw function from the record (position, velocity, age) | pool offset + record index (lowest free slot allocation, see Effects) |
| cutscene characters | bone table 0x00369240 (`func_00221758`) | sequence character + bone |
| cameras | per view, per frame (`func_001029E0`) | view index |

`EmitModelInstance` 0x001404A8 multiplies the camera matrices (+0xE0, +0x120) with the node world matrices **while it writes the packet**. The final per-node matrices exist only inside the packet, not in RAM. An interpolating renderer must interpolate the node world matrices (instance +0x10/+0x14) or the inputs (unit position and rotation), then redo the multiplications.

## Model format and instances

Model data format: `docs/formats/MODEL.md`. Runtime objects:
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
4. A DMA `call` of the node's packet. Node packets are **pre-built DMA chains in the loaded data, ending with a `ret` tag (0x60000000)**. This answers the open point of `render-boundary.md`.

Animation tracks (`func_00140F68`, `func_00141000`) store a time per track, and the nodes that use a track are re-evaluated. Node flag 4 marks vertex-animated nodes, whose packet vertices are rewritten from key frames (`func_0013F6C0`, FlushCache).

## Lighting

Light sets (0xE0+ bytes): three directional lights stored transposed as a direction matrix (+0x50) plus a colour matrix (+0x90), and an ambient colour (+0xD0). Colours are packed RGBA × 1/128 (`func_00136B68`, `func_00136DA0`, `func_00136F48`). `func_001370D8` writes the light block into a packet before each model batch. The sun direction comes from the map entry: two angles at terrain object +0x88/+0x8C.

## Terrain (`func_0013EA78`, object `*0x00410694`)

1. `func_0013A110`: camera terrain cell (`func_00139000`, 2048-unit cells), LOD ring radii, fog distance (`powf`), sun light set.
2. `func_0013A6B8`: VU0 clip test per 16×16 block, visible list in scratchpad 0x70000000.
3. `func_0013B2F8`: far LOD packets for the visible blocks (VIF/GIF, terrain sub-table, `.vudata` block in bucket 0).
4. `func_0013CDC0`: near cells as 2-triangle or subdivided strips (`func_0013C1D0`/`func_0013C740`), texture page from the attribute word.
5. Sea (`*0x0041068C`, terrain flag +0xF4 bit 0): noise-displaced grid patches (`func_00137930`, gradient noise `func_00137608`).

The noise gradient table is filled **once per boot from `rand()`** (`func_00137550`, guarded by `D_00410690`).

The height functions used by gameplay (`collision`, `weapons.md`) share the terrain data but not this code.

## Static map objects (`*0x0041069C`)

`func_00144728` builds per-map-entry object lists and model instances. It also builds **collision triangles extracted from the model packets themselves** (`func_00143C10`: walks each node chain up to the `ret` tag, transforms positions by the node matrix, face normals). Gameplay uses them: the terrain segment test `func_0013D698` calls `func_00144DC8` for the objects of each cell. So the port must keep the model packet data loadable as data, even if the GPU path never uses it as packets.

## Aircraft draw and shadow (`func_001483A0`)

- **Ground shadow**:
  - `func_001418C8` finds the terrain hit along the sun direction, builds a drop-shadow matrix there, and draws the model with VU1 program B into an off-screen buffer (GS page 0x2500).
  - The shadow passes 0x00146AB0 / 0x00147540 / 0x00147828 / 0x00146E28 ×3 / 0x00147B30 / 0x001471C8 then copy it to half size, blur it with three offsets, and blend it onto the frame (alpha 0x28808080).
  - Missions in mask 0x00204114 and mission ids 0x3D–0x4E use a variant; below altitude 90 the static objects are drawn into the shadow buffer as well (0x00145578).
- **View mode** (view +7 & 3) picks the VU1 program: external views use program A; mode 2 uses program 0x0029B0F0 and mode 3 uses program 0x00299820 (cockpit-type views).
- **Half-resolution pass** `func_00142460` (H: glow): VU1 program 0x0029A930 loaded at VU address 0xF2, camera scaled 0.5, composited back by `func_00141EE0`.

## VU programs (`.vutext`)

Each block is a ready DMA packet: `ret` tag with the qword count, then `FLUSHE`, `MPG` (load address), microcode. No disassembly was done; headers only.

| Address | QW | MPG address | Used by |
|---|---|---|---|
| 0x00297C00 | 0xEA | 0 | program A: generic model renderer (view buckets 6/9; menu objects with lighting mode 1) |
| 0x00298AC0 | 0xD4 | 0 | program B: ground objects, shadows (lighting mode 0) |
| 0x00299820 | 0x10F | 0 | program C: aircraft view mode 3, menu lighting mode 2 |
| 0x0029A930 | 0x7A | 0xF2 | program D: half-resolution overlay, loaded on top of a resident program |
| 0x0029B0F0 | 0x10B | 0 | program E: aircraft view mode 2 |
| 0x0029C200 | 0x6C (`cnt`) | 0xD8 | **VU0** micro program, uploaded by direct VIF0 DMA (`func_00225408`) for the cutscene character skinning |

The `.data` pointers 0x00299910 and 0x0029A300 noted in `render-boundary.md` fall inside program C, so they are probably coincidental values.

## Effects system (`effects`)

One work block (`D_004106B4`, about 0x625A0 bytes) holds 45 pools. One pool (+0x2010) is empty. Each pool has fixed-size records, an occupancy bitmask (one u32 per 32 records) and a count. Per pool there are three generic wrappers, each driving the type's own functions:

| Phase | Dispatcher | Per pool |
|---|---|---|
| init/reset | `func_00154660` (flight start, replay start) / `func_00154A48` (sequences) | record init |
| update | `func_00154DD0`, **called from the simulation step** | record update; the record is freed when it returns "done" |
| draw | `func_00155330`, per view | draw setup once, then record draw |

Spawning (`spawn or refresh` functions, one per type) takes a handle. A handle of −1 allocates the **lowest free record**. A full pool returns −1 and the effect is simply not created. Owners keep the handle and refresh continuous effects every frame. The pool table with each type's evidence is in `coverage-functions.csv` (notes "effect pool +0x…"). The main explosion API is `func_001F08E8` (id, position) / `func_001F0910` (with velocity). Replay playback re-spawns effects from recorded events (`func_00170598`, `func_001706D0`, `func_001708D0`).

Effect code reads game state (mission number, terrain height, unit types) but writes no unit, projectile or mission state. Effects are presentation. Their update still runs inside the simulation step and reads the random table (below).

`func_001E0A98` dispatches "special object" renderers by a record's kind byte (+0x3D, about 20 kinds, 7–17 KB each, parameters in `.data` 0x002B8xxx). It is used by units, aircraft, sequences and menus.

## Randomness used by rendering and effects

The game has five independent random sources:

| Source | State | Seeded | Users | Affects gameplay? |
|---|---|---|---|---|
| random table `func_00100DC8` | cursor `D_00411780`, 512 floats at 0x00411998 | table filled at boot (`InitRandomTable`, `srand(123456789)`); **cursor reset to `frame & 0x1FF` at the start of every gameplay frame** (`FlightPlayFrame` → `func_00100DB8`) | AI, weapons, player, effect updates (sim), map flicker, some effect draws | yes (AI/weapons). Effect updates and draws share it, but only the order inside a frame matters |
| libc `rand` (newlib LCG) | C runtime | `srand(1)`, `srand(123456789)` at boot; `srand(seed)` by the sky generator (`mission-flow.md`) | sky, sun/moon jitter (`func_001145E8`), replay auto-camera, radio chatter and scheduler, noise table, a few effects, boot constructor `func_002596F8` | no direct gameplay use found; affects radio choices and visuals |
| VU0 R register | VU0 | `vrinit` in the effects init `func_00154660` (flight start, replay start) | `FirePlayerWeapons` (result unused) and missile trail segments `func_001DAC38` | no |
| effect LCG `x = x·0x10DCD + 1` | 10 words 0x00410BB0–0x00410C38 | static constructors, then per effect record (`seed·0xEE29 + 1`, `func_001E0A80`) | effect draw functions | no |
| sound LFSRs | `D_00410D30` (`NextSoundRandom`), `D_00410D1C` (unused copy) | — | sound variations | no |

Consequences for the port:
- The draw phase calls `rand()` (sky, sun jitter, replay camera) and reads the random table. The table is reset every gameplay frame, so draw-time reads do not leak into the next frame's gameplay. `rand()` calls from drawing do shift the radio's choices. **Drawing must run exactly once per simulated frame, in the original order, to keep radio and visuals identical** (`ADR-002`, finding added).
- Effects cannot be skipped, reduced or reordered: their simulation-step updates read the random table before the AI and weapons of the same frame.
- The VU0 R register must be emulated (`vu0_math.h`), including `vrinit`/`vrnext`.

## Sequence character engine (0x00220000–0x00226198, `sequence`)

A separate skinned-character library used only by in-engine cutscenes (`mission-flow.md`):
- linked character list (+0xC8), parts of 0x160 bytes, bones of 0xB0 bytes, motion channels (4 per character, rotation order per root via `func_00222800`);
- **skinning runs in VU0 micro mode**: microprogram entries 0x00/0x10/0x30/0x40/0x50/0x70/0x80/0x90/0xB0 called with `vcallms` (`func_002250E8`…`func_00225270`), with data moved through scratchpad 0x70002200 by DMA channels 8/9;
- the engine **starts and waits on DMA channels itself** through `sceDmaGetChan` pointers (`func_00223CE8` writes TADR/QWC/CHCR, `func_00223CC0` spins on STR). These are direct hardware accesses not counted in `render-boundary.md`;
- 4 KB page allocator for VU/texture memory (`func_00225570`), GIF templates 0x00384240/0x00384340.

The port needs either a VU0 micro-mode interpreter for the 0x0029C200 program or a C++ rewrite of these nine entry points, checked against PCSX2. This is the only VU0 micro-mode user.

## Things the port must keep

- Bucket order per view (table above) and the per-view scissor/Z-clear behaviour.
- The node packet data and the collision triangles extracted from it (gameplay uses them).
- Effect pool sizes and lowest-free allocation: a full pool silently drops effects, and replays depend on the same slots.
- Draw-phase `rand()` calls and random-table reads in the original order.
- The VU0 R register and the VU0 micro skinning (or exact rewrites).

## Open points

- What each VU1 program computes (needs the vu-microcode rules; header-level only so far).
- The 111 effect helper functions marked "body not read" in the coverage list, and the names of the effect types marked (H).
- Terrain packet details (`func_0013A6B8`, `func_0013B2F8`, `func_0013CDC0`, skimmed).
- Which half-resolution effect `func_00142460` produces on screen (PCSX2 question in `open-questions.md`).
