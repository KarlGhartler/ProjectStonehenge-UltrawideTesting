# Slice 5 plan: scene 2 (missions)

Status: **approved by the owner 2026-10-16** (blocks P, A, B as one block, C; `--freeze-sim` for block A). P1 done 2026-10-16 (`validation.md` S5-P1); P2 done 2026-10-17 (`--replay-p2m2`, `--p2m2-offset`; offset 153 for power-on recordings, `validation.md` S5-P2). `--fast-until` added 2026-10-17 (`validation.md` S5-T1). P3 done. P4 done 2026-10-16 for block A (terrain blocks 5–18 checked on all 164 entries, `../formats/MISSION.md`). P5 done 2026-10-16 for block A: all 448 functions documented to packet level (`s5-blockA-inventory.csv`), plus the 9 libvu0 functions block A needs (`../platform/sdk-functions.md`). Translation of block A in progress (2026-10-16): load path complete (mission load sub-states 0-8, 0x1E and their systems: units, effect pools, projectiles, HUD/replay/radio/sound state, terrain/static objects/unit and aircraft models, sky, sea) plus the camera helpers and core math; the build links with no stubs. Per-frame world drawing translated (2026-10-16): view cameras and the collision core they need (0x0013D698, 0x00144DC8, 0x0017C4E8, not in the block A inventory), terrain (culling, texture cache, cells), sea and its CPU clipper, static objects, units, aircraft with shadow passes and weapons, tracers. Calls into slice S5f (DrawSpecialObject 0x001E0A98, effect camera 0x00155900, effect drawer 0x001BCBD8) and the glow pass 0x00142460 are logged once and skipped. Sky group translated 2026-10-16 (0x0010FFB8–0x00118400: dome, glow/blur, stars, clouds and cloud shadow, moon, sun with flare and glare, sun halo, weather particles; two open points in `pcsx2-pending.md`); `BuildFlightDrawLists` 0x0010BD68 and the sub 9 handler 0x0010BA98 translated, `--freeze-sim` implemented (subs 0xA–0xC and 0x13), plus the development capture `--dump-dma-at` (`command-line.md`). First visible result: loading screen and the mission sky (`validation.md` S5-A). VU1 terrain program T (`.vudata`, documented in `vu-programs.md`) and programs A/E implemented as HLE. Mission 1 load checked against PCSX2 2026-10-17 (`validation.md` S5-A2): allocation exact, heap contents equal except timing and the random cursor left by the opening sequence. GS dump of mission 1 drawn through the port's renderer 2026-10-17 (`validation.md` S5-T2): GS memory now in the GS column layout, two renderer bugs fixed, the frame matches PCSX2 except the DATE draws. Missions 3 and 4 checked 2026-10-17 (`validation.md` S5-A3): 0 unexplained RAM ranges outside the scene heap, heap equal except timing and the uninitialised effect clock; renderer: fog, TEX1 filtering with trilinear mipmaps, DATE, AA1, frame buffers in other formats (the three mission dumps now match PCSX2; the black band is gone). Remaining for block A: PABE, COLCLAMP 0, CSM2, partial FBMSK when a frame needs them. Plan for the rest of S5 (owner request 2026-10-17): test tooling (`--mission`, parallel runner), block B decompile-first, then translation. Block B translated 2026-10-18: every S4 and S5a–S5h function has a translation, including the special-object drawers and the glow pass with VU1 program D (`../STATUS.md` section 7); Block C decompiled and translated 2026-10-18: all 194 S5i/S5j functions (`s5-blockC-inventory.csv`), VU0 program S as a C++ rewrite in `src/platform/vu0/` (decision 14); only the S6 VS rules stay stubbed. Validation of blocks B and C against the S5-B / S5-C captures is next. Slice definitions: `slices.md`; validation method: `validation-strategy.md`.

## 1. Size

Function lists from `tools/research/slices.py --list S5x` (run 2026-10-16, after S4). "Bytes" is code size; "VU0" = functions with COP2 macro instructions; "double" = functions that call the libgcc double-precision routines; "random" = call sites of `NextRandomFloat` (table) and libc `rand`.

| Sub-slice | Content | Functions | Bytes | >= 1 KB | VU0 | MMI | Double | Random calls (table / rand) |
|---|---|---|---|---|---|---|---|---|
| S5a | mission load, system creation | 291 | 52 KB | 9 | 6 | 5 | 3 | 45 / 37 |
| S5b | static world rendering | 157 | 88 KB | 26 | 25 | 18 | 1 | 9 / 2 |
| S5c | player flight, collision | 65 | 43 KB | 12 | 5 | 2 | 3 | 1 / 0 |
| S5d | weapons, effect simulation | 294 | 101 KB | 11 | 23 | 0 | 2 | 48 / 2 |
| S5e | AI, mission rules, radio choice | 250 | 98 KB | 27 | 6 | 5 | 1 | 34 / 12 |
| S5f | effect drawing | 208 | 426 KB | 87 | 87 | 38 | 1 | 13 / 6 |
| S5g | HUD | 86 | 73 KB | 21 | 0 | 14 | 1 | 2 / 0 |
| S5h | mission sound | 49 | 17 KB | 2 | 0 | 7 | 3 | 0 / 0 |
| S5i | sequences, cutscenes | 102 | 37 KB | 9 | 14 | 4 | 1 | 0 / 0 |
| S5j | replay, carrier, game over, rearm | 98 | 43 KB | 12 | 5 | 0 | 1 | 0 / 15 |
| (S4 rest) | menu-object effect drawers, glow pass (deferred to S5f) | 29 | 127 KB | 19 | 17 | 4 | 0 | 0 / 0 |
| **Total** | | **1,629** | **~1.1 MB** | | | | | |

For comparison, S1–S4 together were 663 functions.

## 2. Why the sub-slices cannot be validated one by one

- **One frame, one step.** `FlightPlayFrame` 0x0010B150 runs the draw-list build, HUD sector scan, player sound, terrain/object selection, sound pools, `FlightSimStep` 0x0010C320 and `UpdateViewCameras` 0x0010CB80 every frame. Inside the step, the AI pass (`func_0011D988`) and the player-effects pass (`func_00126970`) spawn effects directly (pools +0x4A830, +0x47800, +0x61630 …), weapons request rumble, and the HUD centre-message tick (`func_0016DB40`) runs in the same step.
- **Random table.** The draw code reads the random table too (S5f: 13 `NextRandomFloat` calls, 6 `rand`; S5b: 9 / 2), and the table cursor is shared by everything in the frame. Until every reader of a frame is translated, RAM can only be compared region by region (`pending-slice`, `risks.md`).
- **Sound and radio.** The radio queue (S5e) waits on SOUND voice status (S5h), so the radio state depends on the sound code.

So a **full-RAM checkpoint during gameplay is only possible when S5c–S5h are all translated**. Translating them as one block is less work than translating them separately with stubs: there are no stubs to write, keep consistent and remove, and only one validation round.

## 3. Proposed blocks

| Block | Sub-slices | Functions | Ends with | Full-RAM checkpoints |
|---|---|---|---|---|
| **P** (prerequisites) | — | 0 game functions | determinism, VU1 programs A/D/E documented, formats documented, input replay | port-vs-port determinism hash |
| **A** | S5a + S5b | 448 | mission loads with its loading screen; the world drawn from the start camera, simulation frozen at the first step (development option) | sub 0xA first step (heap exact), first frame's draw-list build |
| **B** | S5c + S5d + S5e + S5f + S5g + S5h + S4 rest | 1,081 | missions playable start to end with effects, HUD and sound | every 10 s of the input recordings |
| **C** | S5i + S5j | 200 | cutscenes, in-engine sequences, replay, carrier, game over, rearm, title demos; `RequestScene(5)` | sequence frames, replay frames |

Alternative if B is too large to review: **B1** = S5c + S5e + S5g + S5h (flight, AI, mission rules, HUD, sound; 450 functions; weapons and effects stubbed, regional RAM checks only) then **B2** = S5d + S5f + S4 rest (631 functions; full RAM from here). Same total work plus the stubs. Recommendation: **one block B**, translated internally in the order of section 6, reported to the owner at each internal milestone, validated at its end.

## 4. Block P: prerequisites (before any S5 game code)

These need the owner's go-ahead because they touch `src/`.

| Item | Why | Work |
|---|---|---|
| **P1 Determinism** | Input-recording validation and port-vs-port hashes need bit-identical runs. Checked 2026-10-16: the game side is already step-driven (the SPU mixer, SPU IRQs and VAG.IRX run in `AdvanceStep` on the game thread at every vblank; timer 0 advances per read; CD completion counts `sceCdSync` polls; libmc counts vblanks). One solo run and four parallel runs to the S4 title card were byte-identical except **the pad's analog bytes** (0x004121F4/0x00412214): `host.read_pad` overlays the script on the live device state, which the main thread samples once per presented frame. An earlier observation (movie buffers differing between parallel runs, with an older build) did not reproduce. | (1) Input source chosen per run: live devices, or only the script/recording (validation runs ignore live devices). (2) Live input latched per game tick: the main thread publishes timestamped snapshots, the game thread takes one at the start of each tick and logs it (the same log is what a replay or LAN lockstep feeds back). (3) A determinism checker: `--hash-log` writes a hash of guest RAM per vblank (plus per-region hashes), and a host-stress option (random sleeps on the main thread) runs against a calm run; the first differing vblank and region are reported. (4) Audit of the remaining host inputs: memory-card file times (folder card directory entries) and the real-time clock, fixed in validation runs. |
| **P2 Input replay** | S5c+ validation uses PCSX2 input recordings (`.p2m2`, `../formats/p2m2.md`). | Reader for `.p2m2`, pad feed with analog sticks and pressure buttons as `scePadRead` returns them (`libpad.cpp` today serves digital only), latency calibration (procedure in section 8). |
| **P3 VU1 programs A, D, E** | A is the generic model renderer (view buckets 6 and 9, terrain and most of the world, menu lighting mode 1); D draws the glow fins; E the aircraft view mode 2. Only B and C are analysed (`../subsystems/vu-programs.md`). Decompile-first rule. | Microcode disassembly and documentation with `tools/research/vu_disasm.py` (never in Ghidra, `.claude/rules/vu-microcode.md`); compare against the Oct-11 GS dumps (`gameplay_attacking_bombers_explosions_hud`, `first_briefing`). HLE implementation belongs to block A (A, E) and block B (D). |
| **P4 Formats** | Block A reads them. | Mission data blocks 5–18 and 31 (terrain), 22 (unit placement), 28 (setup tables), 30 (effect tables), 34/35 (message/radio system), packets 0 and 26 (`../formats/MISSION.md` "Open"); `tools/research/` parsers checked against the extracted DATA entries. |
| **P5 Function documentation to packet level** | The coverage notes describe the large draw routines at interface level only (`coverage.md`). The decompile-first rule needs every function documented before implementation. | Per block, before its implementation: read every function's disassembly (MMI, `lq/sq`, VU0 macro and `cfc2` sites checked as the rules require), record packet layouts, random reads and double-precision paths in the subsystem docs. Block A first (448 functions), block B while A is validated. |

## 5. Block A: S5a + S5b (448 functions)

- **Entry points:** `FlightSceneEnter` 0x0010A898, `StoreFlightDataPointers` 0x0010B038, engine sound banks `func_0010FDE8`, loading screen 0x0010A998, `CreateFlightSystems` 0x0010AA50, `StartFlightSystems` 0x0010AB58, `func_0010FA70`, `SetupMissionUnits` 0x0010E758, `SetupMapFromMissionData` (H) 0x00139B70, `CreateWorldRenderObjects` 0x0013EA28, `InitEffects` 0x00154660; world update/draw 0x00139FE8, 0x00144CC0, 0x0013EC20/38, sky 0x001118C8/0x00113708/0x00115F28, `BuildFlightDrawLists` 0x0010BD68, `UpdateViewCameras` 0x0010CB80.
- **Heap:** mission 1 fills the work heap to 98.4 % and allocations are unchecked (`../game/memory.md`): every allocation size and order must be exact. Checked at sub 0xA.
- **Renderer:** VU1 programs A and E as CPU HLE (B/C exist), ordering-table buckets for the flight views, fog (FGE, already requested by the hangar), CT24 and Z24 read as textures, destination-alpha blending and DATE, PABE, partial FBMSK, COLCLAMP 0 where the scan found them (`../platform/gs-subset.md`). Trilinear mipmapping with automatic mip addresses (TEX1.MTBA: MIPTBP is never written because the GS computes the addresses; corrected 2026-10-17, implemented with fog, DATE, AA1 and frame-buffer format conversion, `validation.md` S5-A3).
- **sce/platform:** CD streaming of large mission entries (5.6 MB blocks) at the measured read latency; the engine-sound bank load (SOUND transfers).
- **Visible:** the loading screen with the objectives, then the world from the start camera (development option `--freeze-sim` or similar: the step not run; decided with the owner).
- **Validation:** RAM at the first frame of sub 0xA (0x0010A230) for missions 1, 3 and 4 (heap mark +0x1664 and every pool header exact); GS-level comparison of the first frame with PCSX2 GS dumps (`tools/research/gsdump.py`, `gifstrips.py`); loading-screen screenshot.

## 6. Block B: S5c–S5h + S4 rest (1,081 functions)

Internal order (each step ends with a build and the regional RAM checks that are possible at that point):
1. Frame skeleton: `FlightPlayFrame`, `FlightSimStep`, sub-state handlers, pause and end-of-mission menus.
2. Player flight and collision (S5c): flight model with the double-precision damping, landing gear, controls from analog input, terrain/object collision with the `cfc2` flag-delay rule (`validation.md` B-2, the three PCSX2-patched sites), replay recording (`RecordReplayFrame` 0x0016E8B8).
3. AI, mission rules, phases, event script, proximity triggers, radio choice (S5e).
4. Weapons and effect simulation (S5d): the 7 projectile pools, 13 special-weapon kinds, damage with its one-frame delay, effect pools with their random reads; pool slot rules literal (`risks.md`).
5. HUD (S5g) and mission sound (S5h): sector scan, radar, gauges, lock-on, messages, 16:9 variants; engine sound with the double-precision throttle compare, positional SE, radio voices through VAG slots, mission BGM.
6. Effect drawing (S5f + the 29 S4 menu-object effect drawers and the glow pass): sprites, trails, smoke ribbons, explosions, heat haze (frame buffer as texture), weather; VU1 program D.
- **Renderer:** VU1 program D, framebuffer feedback (heat haze), the remaining blend forms; performance target: 60 fps in effect-heavy frames without the validation layer (the hangar work, `validation.md` S4-6, applies).
- **Audio:** 3D SE panning/volume, reverb mixing (the effect areas are now known, `validation.md` S4-7), stream slots under load.
- **Replay of the S5-B recordings:** they start from the mission-start states (`.p2m2` `from_savestate` = 1, `../formats/p2m2.md`), so the port runs its route to the same checkpoint (0x0010A230, 0 unexplained ranges in S5-A3) and then feeds recording frame 0 onwards; this needs a development option to start a `.p2m2` at a RAM checkpoint (to add at the start of block B validation; asked for before it is written).
- **Validation:** test vectors (flight step, collision, slot allocation, every random-reading spawn, target selection, radio choice); input recordings of missions 1 and 3 (free flight, firing, full mission) with full-RAM checkpoints every 10 s and at phase changes; GS dumps of two effect-heavy frames; HUD screenshots; an SPU2 capture of a short segment.

## 7. Block C: S5i + S5j (200 functions)

- Sequence and character engine, pre-mission cutscenes (`UpdateInEngineSequence` 0x00177058), mid-mission sequences 0x0010B500, the no-save intro; the **VU0 micro-mode skinning** program 0x0029C200 as a verified C++ rewrite of its 9 entry points (decision 14; vectors for part types 0 and 3 still pending, `../pcsx2/procedures.md` #1).
- Replay playback and cameras (`UpdateReplayPlayback` 0x0016E2D0; u8-quantised snapshots), title demos (`StartTitleDemo` 0x0022CC68), carrier landing/takeoff (mission 3, subs 0x14–0x1B), game-over and rearm menus, draw-only frame (sub 9) → `RequestScene(5)`.
- Validation: skinning vectors; RAM at fixed sequence frames; the mission 1 replay; the title demo from the title idle; the carrier recording.

## 8. PCSX2 batches (written into `../pcsx2/procedures.md` when the plan is approved)

| Batch | For | Content |
|---|---|---|
| A | block A | **written 2026-10-16** (`../pcsx2/procedures.md` S5-A, together with P2): save states at the first frame of sub 0xA (0x0010A230) for missions 1, 3, 4 (no card has a mission-10 save); GS dumps of the first gameplay frame; loading-screen screenshots |
| P2 | input replay | **written 2026-10-16** (S5-A run 1): power-on recording with two checkpoint states and their frame numbers |
| C | block C | **written 2026-10-18** (`../pcsx2/procedures.md` S5-C: mission 1 pre-mission sequence, mission 4 replay, mission 3 carrier landing/rearm/takeoff, game over and retry, title demo; optional no-save intro and mid-mission sequence; plus section 1, the VU0 skinning vectors) |
| B | block B | **written 2026-10-17** (`../pcsx2/procedures.md` S5-B: recordings anchored at the S5-A mission 3/4 start states instead of power-on, so the load-length drift does not apply): input recordings: mission 1 free flight 60 s, a firing run (guns, missiles, special weapon), missions 1 and 3 to the end; checkpoint save states every 10 s and at phase changes; ~20 entry/exit pairs for the test vectors; 2 GS dumps of effect-heavy frames; ~6 HUD screenshots (lock-on, warning, map, messages, 16:9); one SPU2 audio capture |
| C | block C | skinning vectors (part types 0 and 3), 3 sequence save states, 4 screenshots, a replay checkpoint, the carrier recording |

Rule for unknowns (owner, 2026-10-16): when a function cannot be translated with confidence from the code and needs PCSX2 to settle it, its translation is **skipped and logged** in `pcsx2-pending.md` (new, one row per function: address, question, what to capture), and the owner collects the answers in the next batch.

## 9. Known risks for S5

- Heap exactness (block A): one byte off breaks every later pool address.
- Double precision: 17 functions call the libgcc double routines (3 each in S5a, S5c and S5h); bit-exact soft-float paths (`ee_math.h`).
- VU0 flag pipeline delay at `cfc2` reads (S5c collision and the projection site; PCSX2 patches them, the port reproduces hardware).
- Random order: 152 table-read and 74 `rand` call sites across the blocks; the translation keeps their order (appendix of `../game/random.md`).
- Performance: S5f is 426 KB of packet code; effect-heavy frames will need the same kind of profiling as the hangar.
- Determinism (P1) must be done first, or the recordings cannot be validated; the known leak is live input (analog bytes), found by a port-vs-port test.

## 10. Questions for the owner

1. Blocks P, A, B, C as proposed (B as one block), or B split into B1/B2?
2. Go-ahead for block P (P1 and P2 touch `src/`), in parallel with the documentation of block A (P3–P5 are research work).
3. The development option to freeze the simulation for block A's world view (name and behaviour).
