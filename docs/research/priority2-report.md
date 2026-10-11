# Priority 2 report: decompilation and documentation of the whole game

Date: 2026-10-11. Scope: the 11 steps of priority 2 (front end, mission flow, flight, AI, weapons, camera/HUD, results/startup/save, sound, rendering, DATA/BD formats, dead code). Steps 1–2 were reported earlier and are summarised only through the totals here. No code was written in `src/`, nothing was committed, and no game data was copied into the repo.

## 1. Coverage

Source: `coverage-functions.csv` (one row per game function, 0x001000D8–0x002616E7) and `coverage.md` (milestones 1–11).

| | Functions | understood | partly | unknown | dead |
|---|---|---|---|---|---|
| Start of step 3 | 2754 | 1389 | 137 | 1161 | 67 |
| End of step 8 | 2755 | 1819 | 92 | 777 | 67 |
| **End of step 11** | **2884** | **2229** | **389** | **0** | **266** |

- **Every game function now has a status.** "partly" means the role and interfaces are known but the body was skimmed or not read. Most of these are effect update/draw bodies (281); the rest are large radio, sound, sequence and terrain functions, each named in its doc's "Open points".
- **Dead** means not reachable from `main`: unreferenced, or referenced only from dead code. Section 11 explains the method. Dead code is still translated (project rule).
- **New Ghidra functions:** 129 created in step 11 (39 from `.eh_frame` FDEs, 88 from code gaps, 2 others), plus 0x0025CF08 in step 7. The 380 FDE functions Ghidra already had match their unwind data exactly.
- **Names:** `symbols.csv` has 540 symbols (264 confirmed game names). `rename-log.csv` records every rename, comment and created function.
- **Ghidra state:** save the program now (File → Save). The edits since the restart are not saved yet (section 7).

## 2. Per-step results

### Step 3: flight model and player control (`flight.md`)
- Two flight paths selected by the CONTROL TYPE option bit (0x002A15D0 bit 0): an Euler path (direct angle add, yaw flip upside down) and a matrix path. Which menu label sets the bit is an open question.
- State: Euler angles plus body-frame velocity, y axis down. Pitch/yaw damping uses double-precision soft-float (`__adddf3` and friends).
- `sinf`/`cosf` are statically linked libm. The port needs bit-exact versions.
- Also documented: landing gear (3 wheel rays), terrain collision, player effects.

### Step 4: AI (`ai.md`)
- Aircraft: three nested state machines (route command, behaviour, manoeuvre), with rotation, speed and weapon controllers on top. Reaction flags at state +0x10.
- Ground and naval units: 9 classes. Classes 8/9 are multi-part large cannons (H: Stonehenge).
- **Randomness: frame-counter arithmetic plus the random table** (`NextRandomFloat`, corrected in step 9, see section 3).

### Step 5: weapons, damage, collision (`weapons.md`)
- Seven projectile pools, each with exact slot rules (ring counters that overwrite, or 64-bit occupancy masks). Replay events refer to slot numbers.
- 13 special-weapon kinds across 17 ids; id 15 is never initialised.
- Hit flags (unit +0x50) become damage one frame late (`<<= 16` at the next frame).
- Terrain is a 256×256 height map of u8 samples × −80. Two different cell-rounding rules exist, each kept where it is used.

### Step 6: camera and HUD (`hud.md`)
- HUD colour green, red when locked or hit. In 16:9 mode the HUD is drawn anamorphically (sprites 3/4 as wide), which confirms ADR-006.
- `DrawHud` changes the radar mode byte inside the draw phase (ADR-002 finding).
- Warning ids 2–8 need a PCSX2 check.

### Step 7: results, startup, save (`results-save.md`)
- Results:
  - reward = (points − first threshold) × 100;
  - credits += reward + rank bonus, capped at 999,999,999;
  - mission records: best rank, best 3 times, best 3 scores.
- Memory card:
  - directory `/BASLUS-20152AC04` with `icon.sys` (0x3C4), `icon1.ico` (0xCB98) and the main file (0x4B10), written in that order;
  - a card status counts only after the same `sceMcGetInfo` result 3 times in a row; 180-frame timeouts;
  - a new save needs 75 free units.
- HDD layer documented. Step 11 shows it is never initialised (section 2, step 11).

### Step 8: EE-side sound, radio, text, rumble (`sound.md`)
- Voice API: request types 1/2/4 on 0x48-byte voice records. 32-entry bank table, SPU base 0x5A10.
- Engine sound: 7 layers per player. 3D listener with Doppler.
- Radio: six request queues → matchers → scheduler → two voice channels (VAG slots 4/5). It calls `rand`.
- Fonts: `go18`/`go28` magic. Rumble: two pattern slots per player.

### Step 9: rendering (`render.md`, new)
- **Frame structure.** Per view, a 16-bucket table with spliced sub-tables:
  - terrain: 32 buckets;
  - ground objects: 32 buckets;
  - effects: 1,024 buckets;
  - plus a separate 2D/HUD table.

  Draw order is fixed by bucket numbers.
- **Cameras.** The camera record layout is documented (world→view, view→screen, clip, world→screen at +0xE0, world→clip at +0x120, viewport).
- **Models.**
  - Instances hold per-node world matrices, rebuilt every step by `UpdateModelHierarchy`.
  - `EmitModelInstance` uploads three matrices per node to VU1 and `call`s the node's pre-built DMA chain, which ends with a `ret` tag.
  - The camera × node products exist only inside the packets.
- **Object identity for interpolation:**
  - units: unit index + model state + LOD;
  - player aircraft: player index;
  - projectiles and effects: pool slots;
  - cutscene characters: bone table entry.
- **Terrain:** LOD rings, VU0 block culling into scratchpad, far and near packets, noise-displaced sea.
- **Map objects** also provide collision triangles, extracted from their model packets. Gameplay uses them, so the packet data must stay loadable as data.
- **Aircraft shadow:** drawn to an off-screen buffer, blurred and blended (GS pages 0x2500/0x2880). A half-resolution model pass uses VU1 program D.
- **VU programs:** 5 VU1 programs and 1 VU0 program, identified from their `.vutext` headers (no disassembly).
- **Effects:**
  - 45 pools, the lowest free record is allocated, and a full pool drops the spawn;
  - updated **inside the simulation step**, drawn per view;
  - re-spawned from replay events;
  - presentation-only state.
- **Cutscene characters:** a separate engine that skins on **VU0 micro mode** (`vcallms` entries 0x00–0xB0). It also drives DMA channels directly.
- **Five random sources** (table in `render.md`): the random table, libc `rand`, the VU0 R register, an effect LCG and the sound LFSRs.

### Step 10: DATA/BD formats (`CDP.md`, `MODEL.md`)
- **DATA index map** completed from the code: demo flights 173–175, menu pages 176–186, mission titles 187–204, hangar/aircraft/weapon models 205–258, movie subtitles 259–271, briefings 273–290, results backgrounds 334–354.
- **No loader found for DATA 291–332**: 41 images of 66,832 B plus one entry (open question).
- **BD map:**
  - BD 0: common SE (boot);
  - BD 1: menu/3D SE (startup);
  - BD 3–12: engine banks per aircraft family;
  - **BD 2 has no loader** (open question).
- New `MODEL.md`: `SM` model format (header, node tree, `ret`-terminated node packets, version thresholds 0xB4/0xBE/0xBF).

### Step 11: dead code (`dead-code.md`)
- **Method:**
  - raw call graph from every `jal`/`j` site, attributed to the nearest preceding function entry;
  - function-address builds in code (`lui` + `addiu`/`ori`);
  - data pointers, excluding `.eh_frame` FDEs (867 found; they also give exact function boundaries).
- **Results:**
  - 266 dead game functions;
  - **129 functions created in Ghidra** (39 had FDEs but no Ghidra function; 88 sat in code gaps);
  - 8 previously unknown **live** pause-menu callbacks (item table 0x002B8130);
  - 33 unreferenced copies of a g++ STL `char`-compare pair, which are library code;
  - 446 C++ runtime functions above the game range, not created (library).
- **Corrections to earlier tags.** 11 earlier "no callers" claims were wrong and are fixed. Examples: the replay camera styles (table 0x002B77B0), the sequence Euler-order matrices (switch) and the options handlers.
- **HDD:** the module init (`func_00259910`) has no caller, and nothing sets ctx flag 0x80000 or HDD context +0x4399. **The HDD paths are unreachable on any hardware in this build** (earlier answer corrected).

## 3. Corrections made to earlier documents

| Document | Correction |
|---|---|
| `ai.md`, `weapons.md`, `mission-flow.md` | `func_00100DC8` (`NextRandomFloat`) reads a 512-entry table filled at boot. It does **not** call `rand`, and the sky's `srand` does not affect it. Its cursor is reset to `frame & 0x1FF` every gameplay frame (`SetRandomIndex` in `FlightPlayFrame`). |
| `results-save.md`, `open-questions.md` | The HDD module is never initialised (section 2, step 11). |
| `dead-code.md` | Section 4 rewritten. The "26 orphan gaps / ~30 repeated gaps" are resolved (switch cases, created functions, STL copies). |
| `render-boundary.md` | Node packets are DMA chains called by `EmitModelInstance`, not through `func_00104338`. More direct DMA access exists (sequence engine). VU0 micro mode is used by the cutscene skinning. |
| Coverage notes | 0x00136518–0x00136770 are the projectile-follow (missile camera) state, not a view fade (my own first reading, fixed before publishing). |

## 4. Open questions (`open-questions.md`)

32 open, 23 closed. The ones that block or shape the port:

| Question | Why it matters | How to answer |
|---|---|---|
| CONTROL TYPE label (`flight.md`) | maps the option to the two flight paths | PCSX2 memory watch, steps in the file |
| HUD warning texts, ids 2–8 (`hud.md`) | documentation only | PCSX2 breakpoint 0x0016D278 |
| DATA 291–332 and BD 2 loaders (new) | confirm unused data | conditional breakpoints on 0x002154E8 / 0x00219C80 |
| Half-resolution pass appearance (new) | presentation baseline (resolution scaling) | breakpoint 0x00142460, optional skip |
| Pause-menu option for ctx flag 0x20000 (new) | naming only | memory watch on 0x0029F380 |
| VU0 micro skinning reference data (new) | needed to verify a VU0 interpreter or rewrite | breakpoint 0x002250E8, dump VU0 registers |
| In-menu movie frame pacing (T0) | ADR-002 timing | PCSX2 steps in the file |
| Scene 2 subs 0x14–0x18 and 0x19–0x1B | dead-code classification | PCSX2 steps in the file |
| SMV PCM layout | audio backend (ADR-004) | SPU2 capture |

## 5. Risks per subsystem

| Subsystem | Risk | Mitigation |
|---|---|---|
| Core / random | Five random sources must be reproduced exactly, including call order inside a frame. The random-table cursor reset per frame limits the damage of a mistake to one frame, but `rand` carries over across frames (radio, visuals). | Implement newlib `rand`, the table, the VU0 R register and the effect LCG bit-exactly. Replay tests against PCSX2 recordings. |
| Flight | Double-precision soft-float and libm `sinf`/`cosf` results must match bit for bit. | `ee_math.h` with exact soft-float and the same libm algorithms. Behaviour tests with captured values. |
| AI / weapons | Pool slot rules and one-frame damage delay. Replays reference slot numbers. | Translate the pools literally. Never replace them with dynamic containers. |
| Effects | They run in the simulation step and read the random table, so they **cannot be reduced, skipped or reordered**. 281 bodies are not read yet. | Translate them literally. Any "effects quality" option belongs in the modding layer at best, and it changes gameplay randomness. |
| Rendering | About 233 packet-writing functions and 5 VU1 programs. Collision depends on model packet data. Off-screen passes use fixed GS pages and sizes. | ADR-003 (VU1 HLE, GS replay). Keep packet data as data. Any resolution scaling must scale the shadow and half-resolution passes consistently. |
| Sequence (cutscenes) | VU0 micro mode skinning plus direct DMA channel control. | VU0 micro interpreter or verified rewrite of 9 entry points (PCSX2 reference data). |
| Frontend | Records page heap allocation depends on the frame count (work-heap offsets differ run to run). | Keep the work-heap allocator and frame counter exact. |
| Save | Byte-identical saves need the exact image build, checksum and write order. | Translate the save page and the `sceMc*` step machines. Test against PCSX2 folder cards. |
| Sound / radio | The radio depends on `rand`. Two-voice scheduling and 600-frame repeat blocks. | Keep the radio in the simulation order. The audio backend (ADR-004) reproduces SOUND/VAG replies. |
| Movie | T0-paced in-menu movies; MPEG-2 decoding; PCM layout unconfirmed. | Dependency decision (decoder) at the skeleton phase. PCSX2 capture for PCM. |
| HDD | Unreachable code that still has to be translated; SDK stubs needed. | Logging stubs per the dead-code rule. |
| Data formats | DATA 291–332 and BD 2 unexplained; terrain and VIF vertex layouts only partly decoded. | PCSX2 checks. The port reads everything from the ISO at runtime anyway. |

## 6. ADR changes

| ADR | Status | Change in this phase |
|---|---|---|
| ADR-001 memory model | Proposed | none |
| ADR-002 threading/timing | Proposed | Two findings: `DrawHud` changes state in the draw phase (step 6), and the draw phase consumes `rand` and the random table while effects update in the simulation step (step 9). Both require the game's draw-list build to run exactly once per step; extra frames re-use the recorded lists. The matrix sources for interpolation are listed. |
| ADR-003 rendering | Proposed | No text change. `render.md` supplies its inputs: bucket layout, VU program list, node-matrix sources, off-screen passes, VU0 micro skinning. |
| ADR-004 audio boundary | Proposed | none (`sound.md` confirms the request-batch model) |
| ADR-005 platform | Accepted | none. Fallback save location still open (owner) |
| ADR-006 presentation | Accepted | Finding: the flight HUD supports 16:9 anamorphic drawing (step 6). Filter and interlace sub-decisions still open (owner) |

## 7. Baseline proposals (for the owner; `port-baseline.md` is unchanged)

Updates to the candidates list, based on this phase:

| Candidate | Change in assessment |
|---|---|
| Higher internal resolution | Still **medium** risk. Now concrete:<br>• the shadow passes use fixed GS pages (0x2500/0x2880) and half-size copies;<br>• the half-resolution model pass and the field copy must scale together;<br>• the cutscene engine allocates VU/texture pages itself.<br>Feasible only with a GS replay that remaps these buffers. |
| Widescreen (Hor+) | Still **high**. The culling found so far (`SelectUnitModelLods`, terrain block culling) only selects what is drawn, and the HUD already has an anamorphic 16:9 mode. Not checked yet: whether any gameplay rule uses on-screen visibility (for example lock-on or targeting through the camera projection). Draw-phase randomness (sky, sun jitter) must run identically either way. Proposal: keep it in category 3 until that check and a PCSX2 comparison are done. |
| Effects detail / "reduce effects" | **Not possible as a baseline feature.** As a modding option it would change gameplay randomness (effects read the random table in the simulation step), so it would have to be labelled as gameplay-altering. |
| Frame interpolation (approved) | Confirmed feasible with recorded node matrices and camera per step (ADR-002 finding). Effects and particles need their own identity (pool slot) and must snap when a slot is reused. |

## 8. What to do next (owner)

1. **Save the Ghidra program** (File → Save). This session created 129 functions, renamed 25 more and re-applied the lost step-7 edits, all unsaved.
2. Review and commit, one subsystem at a time:
   - new: `render.md`, `MODEL.md`, `priority2-report.md`;
   - changed: `CDP.md`, `dead-code.md`, `ai.md`, `weapons.md`, `mission-flow.md`, `results-save.md`, `render-boundary.md`, `open-questions.md`, `coverage.md`, `coverage-functions.csv`, `symbols.csv`, `rename-log.csv`, ADR-002, `AIRCRAFT.md`.
3. Run the new PCSX2 checks (section 4) when convenient. None of them blocks the skeleton.
4. Owner decisions still pending: ADR-001 to ADR-004 (Proposed), the ADR-005 fallback location, the ADR-006 filter and interlace handling, and the baseline candidates above.
