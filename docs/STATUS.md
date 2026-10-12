# Project status

Ace Combat 04 (SLUS_201.52) PC port. Phase 3 (port skeleton) in progress; architecture decisions ADR-001–006 accepted. Skeleton status: section 6. Details: `README.md` (index).

## 1. Decompilation status

Game code 0x001000D8–0x002616E7: **2,884 functions**, every one classified. Unknown: **0**.

| Status | Functions | Share |
|---|---|---|
| understood | 2,612 | 91 % |
| partly | 0 | 0 % |
| dead (not reachable from `main`) | 272 | 9 % |
| unknown | 0 | 0 % |

Names: 541 symbols in `symbols.csv` (265 confirmed game names, 24 game hypotheses).

Validation (2026-10-11, `port/validation.md`): names, totals, classification and dead-code reachability checked by `tools/research/validate.py`; blind re-review of 60 sampled functions.

### Per subsystem

| Subsystem | Functions | Understood | Dead |
|---|---|---|---|
| effects | 585 | 576 (98 %) | 9 (2 %) |
| frontend | 467 | 455 (97 %) | 12 (3 %) |
| render | 214 | 193 (90 %) | 21 (10 %) |
| core | 211 | 196 (93 %) | 15 (7 %) |
| sound | 202 | 149 (74 %) | 53 (26 %) |
| mission | 164 | 164 (100 %) | 0 |
| weapons | 116 | 116 (100 %) | 0 |
| sequence | 108 | 82 (76 %) | 26 (24 %) |
| radio | 98 | 94 (96 %) | 4 (4 %) |
| hdd | 96 | 73 (76 %) | 23 (24 %) |
| hud | 88 | 88 (100 %) | 0 |
| ai | 81 | 81 (100 %) | 0 |
| unreached | 78 | 0 | 78 (100 %) |
| replay | 72 | 72 (100 %) | 0 |
| movie | 57 | 48 (84 %) | 9 (16 %) |
| results | 40 | 40 (100 %) | 0 |
| camera | 35 | 32 (91 %) | 3 (9 %) |
| text | 33 | 30 (91 %) | 3 (9 %) |
| flight | 32 | 32 (100 %) | 0 |
| collision | 30 | 30 (100 %) | 0 |
| startup | 23 | 22 (96 %) | 1 (4 %) |
| save | 21 | 18 (86 %) | 3 (14 %) |
| file | 13 | 1 (8 %) | 12 (92 %) |
| input | 12 | 12 (100 %) | 0 |
| aircraft-data | 7 | 7 (100 %) | 0 |
| boot | 1 | 1 (100 %) | 0 |

The whole HDD subsystem never runs (its init has no caller), although only 23 of its functions are call-graph dead.

### Depth of the notes

Every reachable function has a behaviour note. Game logic (flight, AI, weapons, collision, mission scripting, HUD, results, radio selection, replay recording) is documented down to its rules, constants and random reads. Large draw routines (effect draws, terrain and model packets, the cutscene character engine, movie upload) are documented at interface level: inputs, outputs, random reads, and confirmation that they write no game state; packet details are read from the code at translation time. Details and the list of functions that use double precision: `port/coverage.md`.

## 2. PCSX2 checks still needed

Exact steps for each: `pcsx2/procedures.md` (same numbers).

| # | Check | Purpose | Blocks |
|---|---|---|---|
| 1 | VU0 skinning test vectors for part types 0 and 3 (bp 0x002250E8, 0x00225140) during the no-save intro or the post-mission-18 sequence | inputs/outputs of the remaining two skinning paths | verification of decision 14 only |

Answered in `pcsx2/sessions-05.md` (2026-10-11): carrier takeoff after a rearm (0x19 → 0x1A → 0x1B → 0x0B, rearm menu sub 0x1D), VU0 skinning path 0x00225110 (part type 1) with before/after states. Earlier runs: `pcsx2/sessions-01.md` … `sessions-04.md`.

Also before release: a folder memory card round trip with PCSX2 (needs the port's save layer, `formats/folder-memcard.md`).

## 3. Decisions

### Decided by the owner (2026-10-11)

| Decision | Choice | Record |
|---|---|---|
| Memory model | B: guest memory, 32-bit PS2 addresses | ADR-001 Accepted |
| Threading and timing | A: fixed 59.94 Hz step, decoupled interpolated presentation | ADR-002 Accepted |
| Rendering | C: own Vulkan renderer for the AC04 GS subset (Vulkan 1.2) | ADR-003 Accepted |
| Audio | B: HLE at the RPC boundary with own mixer | ADR-004 Accepted |
| User data | everything under `%LOCALAPPDATA%\AC04Port\` (`saves\` folder card, `settings.json`, `logs\` last session only, crash handler) | ADR-005 (revised) |
| Scaling filter | two filters, `sharp` and `smooth`, in `settings.json`; settings struct + JSON + change notification, no menu yet | ADR-006 §3 |
| Baseline features | approved: output scaling with both filters, decoupled frame rate with interpolation, frame cap 60/120/144/240 (360 default upper limit), v-sync on/off, windowed and borderless, `settings.json`. Widescreen: modding layer. MSAA: later | `decisions/port-baseline.md` |

### Decided by the owner (2026-10-11, phase 3 start)

All previously open decisions approved as recommended:

| # | Decision | Choice |
|---|---|---|
| 7 | Interlace handling | present the full 448-line draw buffer (ADR-006 §4) |
| 8a | Input rebinding | approved baseline feature |
| 8b | Audio on focus loss | approved as a setting, default keep playing |
| 8c | Audio device selection | later; system default device |
| 9 | Dependencies | approved: SDL3, Vulkan-Headers, volk, VMA, glslang, doctest, nlohmann/json, Dear ImGui (license re-checked per version when added) |
| 10 | MPEG-2 decoder | libmpeg2 (not needed before the movie phase) |
| 11 | Skeleton | approved, phase 3 |
| 12 | Memory card port 2 | reports empty |
| 13 | Ghidra struct field names | later |
| 14 | VU0 skinning | C++ rewrite (later phase) |
| 15 | Debug overlay | requires `debug.overlay_enabled` (default off; on by default in development builds) |
| 16 | Default scaling filter | sharp |
| 17 | Higher internal resolution | later; renderer scale factor from day one, fixed at 1× |
| — | ISO SHA-1 mismatch | refuse in release builds; development builds may continue with `--allow-unverified-iso` (logged) |

### Open for the owner

| Question | Blocks |
|---|---|
| Ghidra struct field names (A by hand / B scripts) | nothing; later |

## 4. Top risks

| Risk | Doc |
|---|---|
| Five random sources must match bit for bit, including call order inside a frame | `game/random.md` |
| Work heap 98.4 % full in mission 1 and allocations unchecked: struct sizes and heap size must be exact | `game/memory.md` |
| Effects run in the simulation step and read the random table: they cannot be skipped or reduced | `subsystems/effects.md` |
| Draw-list build changes state and consumes `rand`: must run exactly once per step | `decisions/ADR-002-threading.md` |
| About 233 packet-writing functions and 5 VU1 programs; the GS dumps add trilinear mipmapping, CT24, Z24 sampled as a texture and destination-alpha blending to the renderer's required subset | `platform/gs-subset.md` |
| VU0 micro-mode skinning for cutscenes needs an interpreter or exact rewrite | `subsystems/vu-programs.md` |
| Flight damping and a few sound, sequence and clipping paths use double-precision soft-float; libm `sinf`/`cosf` must be bit-exact | `port/coverage.md` |
| Projectile and effect slot rules must stay exact; replays refer to slot numbers | `subsystems/weapons.md` |
| Byte-identical saves need the exact image build, checksum and write order | `formats/SAVE.md` |
| In-menu movies are paced by timer 0 in H-blanks (one loop iteration = 2 fields, measured); SMV PCM layout unconfirmed | `formats/SMV.md` |
| The ghidra-mcp plugin re-adds type prefixes to struct fields; `save_program` can hang | `decisions/naming-convention.md` |

All risks with mitigations: `port/risks.md`.

## 5. Readiness for the skeleton

**Ready:**
- Boot path closure: 199 game functions (about 168 understood, 31 partly, all read) and the 99 SDK entry points they call (`port/skeleton-readiness.md`).
- Boot values measured in PCSX2: work heap base/size, SOUND/VAG first replies, USB joystick "no device" reply.
- Main loop order, scene dispatch, file pipeline (TBL/CDP), ISO verification data, save image and folder-card layout.
- Every game function classified; dead code catalogued; records validated (`port/validation.md`).
- Architecture decisions ADR-001 to ADR-006 accepted (only the interlace sub-decision is open).

**Started (phase 3):** milestones A (build, platform) → B (ISO, guest memory) → C (boot path, main loop) → D (debug overlay) → E (validation). Progress: section 6.

## 6. Skeleton progress (phase 3)

| Milestone | Status |
|---|---|
| A. Build system and platform | done (2026-10-11) |
| B. ISO and guest memory | done (2026-10-11), awaiting owner review: ISO reader, verification with cache, guest memory/ps2ptr/stack/call table, ELF load checked against PCSX2 (`port/validation.md` B-1, B-2) |
| C. Boot path and main loop | done (2026-10-12), reviewed; hardware register writes moved to explicit `src/sce/ee_hw.h` calls: 245 game functions translated (`port/translated-functions.csv`), SDK layer `src/sce/`, IOP stand-ins `src/platform/iop/`, game thread at 59.94 Hz; boot reaches the scene 6 stub after ~33 frames (`port/validation.md` C-1) |
| D. Debug overlay (minimal) | done (2026-10-12), reviewed: Dear ImGui overlay with performance, scene (+ request log), memory viewer with symbols, session log (`port/debug-overlay.md` §7) |
| E. Validation | done (2026-10-12), awaiting owner review: 46 doctest cases; RAM at the first `SceneStep` compared with PCSX2: every difference classified (`port/ramdiff-expected.csv`, 0 unexplained), three fixes (malloc chunk sizes, TOC, reverb address); EE FPU switched to round-to-nearest as PCSX2 runs AC04 (owner decision 2026-10-13; `port/validation.md` E-1, E-2) |

Command-line parameters: `port/command-line.md`.
