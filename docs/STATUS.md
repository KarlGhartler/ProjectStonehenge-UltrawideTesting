# Project status

Ace Combat 04 (SLUS_201.52) PC port. Phase 3 (port skeleton) done (milestones A–E); architecture decisions ADR-001–008 accepted. Phase 4, translation in vertical slices (ADR-007), started 2026-10-13 with S1 (section 7). Details: `README.md` (index).

## 1. Decompilation status

Game code 0x001000D8–0x002616E7: **2,884 functions**, every one classified. Unknown: **0**.

| Status | Functions | Share |
|---|---|---|
| understood | 2,612 | 91 % |
| partly | 0 | 0 % |
| dead (not reachable from `main`) | 272 | 9 % |
| unknown | 0 | 0 % |

Names: 670 symbols in `symbols.csv` (388 confirmed game names, 25 game hypotheses).

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
| S5-A | input-recording calibration (power-on recording, 2 states); mission start of missions 1, 3, 4 (states at 0x0010A230, GS dumps, screenshots) | P2 offset; block A reference RAM and pictures | validation of P2 and block A only |
| S2b-B | BGM player (optional; needs a card with the campaign cleared) | the one S2b page not yet checked | nothing |
| 1 | VU0 skinning test vectors for part types 0 and 3 (bp 0x002250E8, 0x00225140) during the no-save intro or the post-mission-18 sequence | inputs/outputs of the remaining two skinning paths | verification of decision 14 only |

Answered 2026-10-15/16: the slice S4 batch, runs A-C (13 save states, 9 screenshots; results in `port/validation.md` S4). Answered in `pcsx2/sessions-08.md` (2026-10-14): the slice S2b batch (9 save states, 10 screenshots, button sequence) and S2a-R (software-renderer screenshot). Answered in `pcsx2/sessions-07.md` (2026-10-14): the slice S2a batch (4 save states, 3 screenshots, S1 splash bug report). Answered in `pcsx2/sessions-06.md` (2026-10-14): the slice S1 batch (6 save states, 5 screenshots, card folders S and E; card messages appear only with no card). Answered in `pcsx2/sessions-05.md` (2026-10-11): carrier takeoff after a rearm (0x19 → 0x1A → 0x1B → 0x0B, rearm menu sub 0x1D), VU0 skinning path 0x00225110 (part type 1) with before/after states. Earlier runs: `pcsx2/sessions-01.md` … `sessions-04.md`.

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

### Decided by the owner (2026-10-13)

| Decision | Choice | Record |
|---|---|---|
| Translation strategy | A: hand translation in vertical slices; B (recompiler) rejected | ADR-007 Accepted |
| Validation | per slice RAM checkpoint + screenshots; gameplay: PCSX2 input recordings replayed in the port with checkpoint frames and the determinism checker; risky functions: entry/exit test vectors | `port/validation-strategy.md` |
| Movie upscaling | approved baseline: separate replaceable stage before the output pass, internal-resolution factor (passthrough at 1×), FSR 1 EASU (+RCAS if better), both video sizes, 2-tick hold unchanged; picture handed over by the decoder side path (option b, 2026-10-14), stage built in S3 | ADR-006 §5, `decisions/port-baseline.md` |
| FSR 1 dependency | approved; v1.0.2, MIT (license checked), FetchContent with SHA-256 | `decisions/dependencies.md` |
| EE FPU rounding | round to nearest, as PCSX2 runs AC04 | `port/validation.md` E-2 |
| Slice order | approved as proposed, plus S9 LAN play | `port/slices.md` |
| VS LAN play | option A: lockstep input sharing, optional layer (category 3), after S8 | ADR-008 Accepted |
| Floating-point compiler flags | approved: explicit no-FMA-contraction flags (MSVC `/fp:precise`, GCC/Clang `-ffp-contract=off`), added when gameplay translation starts | `port/risks.md` |
| README tagline ("static recompilation") | to be reworked later by the owner | — |

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
- Architecture decisions ADR-001 to ADR-006 accepted (no open sub-decisions); ADR-007 (translation strategy) accepted 2026-10-13.

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

## 7. Phase 4: translation slices (ADR-007)

Proposed order (`port/slices.md`, assignment by `tools/research/slices.py`, all 2,884 game functions assigned):

| Slice | Content | Functions |
|---|---|---|
| S0 | skeleton (done) | 244 |
| S1 | scene 6 startup (**done 2026-10-14**, accepted by the owner) | 87 |
| S2a / S2b | front end: title and main menu (**validated 2026-10-14**) / options, records, save/load (**validated 2026-10-14**) | 86 / 151 |
| S3 | movies (libmpeg2, SMV audio, FSR 1 stage) (**translated 2026-10-14, validated 2026-10-15**) | 86 |
| S4 | front end: mission title, briefing, campaign menu, hangar, mission select (**translated 2026-10-16**, validation in progress: title card, briefing, campaign menu, hangar with the 3D aircraft and mission select run; PCSX2 runs A-C compared 2026-10-16: VU0 macro mode now rounds to nearest (the 1/16-pixel map sprite differences are gone), `CommitHangarSelection` byte-exact against the run B states, VS mission select 0 unexplained; hangar performance 5 fps -> 60 fps (validation layer now opt-in with `--vk-validation`; S4-6); with a frame-aligned pad script every S4 checkpoint has 0 unexplained ranges (S4-7); the effect drawers and glow pass of the menu object, not reached in S4, are left to S5f) | 253 |
| S5a–S5j | scene 2: load, static world, flight, weapons + effect sim, AI + rules, effect drawing, HUD, sound, cutscenes, replay/carrier/menus | 291 / 157 / 65 / 294 / 250 / 208 / 86 / 49 / 102 / 98 |
| S6 | VS mode | 3 + two-player paths of shared code |
| S7 | scene 5 results, post-mission menu | 46 |
| S8 | remaining code (dead, HDD, scene 7) | 328 |
| S9 | VS LAN play (ADR-008 A) | no game code |

Status: **order approved 2026-10-13. S1 done and accepted 2026-10-14 (report below). S2a translated and validated 2026-10-14; S2b translated and validated 2026-10-14; S3 translated 2026-10-14 and validated 2026-10-15 (reports below); S4 started 2026-10-15.**

S1 result (plan approved 2026-10-14, reference data `pcsx2/sessions-06.md`):
- 87/87 functions translated (`tools/research/slices.py --list S1` is empty; 331 game functions translated in total).
- RAM: 6 checkpoints (card S sub 7, sub 9, exit; card E sub 7, exit; no card sub 7) with **0 unexplained ranges**; remaining differences are frame timing (CD latency model, `port/ramdiff-expected-s1.csv`) and the owner's controller stick. First SceneStep check still clean.
- Screens: splash 1 (card S), splash 0 (card E) and the first no-card message match PCSX2 at offset 0 (mean difference 0.2-0.6 per channel; edge differences from PCSX2's field output). The second no-card message matches too (reached with `--pad-script`). Not compared: splash 0 of card S (PCSX2 screenshot taken during the fade-in).
- New: first GS renderer path (2D: sprites, lines, CLUT textures, blending, fades), libmc on folder cards, SOUND stand-in matched to SOUND.IRX (reply area, transfer record, reverb address), SPU2-subset mixer clocked per game step, native screenshot (F8, `--screenshot-at`), `--memcard`/`--no-memcard`/`--dump-ram-at`/`--dump-audio`, `tools/research/imgdiff.py`. 56 unit tests pass.
- Owner feedback 2026-10-14: audio OK; controller input missing with `--no-memcard` -> gamepads are now opened at start-up (recheck by the owner); the brightness option's effect is now presented (ADR-006 §4 note).
- Open: PCSX2 I/O timing differs by a frame between cold boots, so exact frame-count matching needs an I/O latency model (`open-questions.md`).

S2a result (2026-10-14, reference data `pcsx2/sessions-07.md`):
- 86/86 functions translated (`slices.py --list S2a` is empty; 418 translated in total): scene 4 handler and navigation, menu framework (list widget, menu tree, connectors, message box, fade object, data load queue, filters, tabs), menu BGM, attract demo, title screen and main menu. Pages of later slices end in a logged stub (46 sub-state handlers).
- Visible: title screen, PRESS START, main menu tree with help line, sub-columns. Music: title and menu tracks stream from `VAG/*.VPK` through the new VAG.IRX stand-in (channels driven by the mixer's SPU2 IRQ, as on the PS2).
- Renderer: destination-alpha blend `(Cs−0)·Ad+Cd`, alpha-test fail modes, exact bilinear texel footprint (fixes an atlas overflow).
- Fixes found on the way: SOUND voice record offsets (+0x0E voice, +0x10 flags; the S1 stand-in played every SE on voice 0) and SOUND voice status from `get_ch_stat` (ENDX/ENVX). S1 checkpoints still 0 unexplained (`port/validation.md` S2a-1).
- 70 Ghidra renames (65 functions, 4 globals, 1 table), 2 plate comments.
- Validation (`port/validation.md` S2a-3 to S2a-5): 4 RAM checkpoints with 0 unexplained ranges (`port/ramdiff-expected-s2a.csv`: CD latency, frame counter, music time, input timing); 3 screens without structural differences (PCSX2 field blend modelled by `imgdiff.py --blend-fields`). Open: a −1 to −2 level G/B bias of the menu frame filter (blend rounding, `pcsx2/procedures.md` S2a-R) and the music by ear.
- Found by validation: PCSX2's `add.s`/`sub.s` mask the smaller operand (`FPU_ADD_SUB`); `fpu::Add`/`Sub` model it and, by the owner's decision, all 83 float add/sub-type instructions of the translated code (30 functions) use them. S1 bug from the owner: PSMCT24 image transfers lost the bytes of a pixel split across GIF packets (green band on the AC04 PROJECT splash); fixed. S1 checkpoints still 0 unexplained; 59 unit tests pass.

S2b result (2026-10-14, reference data `pcsx2/sessions-08.md`):
- 151/151 functions translated (`slices.py --list S2b` is empty; 569 translated in total, `port/translated-functions.csv`). New files: `campaign.cpp`, `results.cpp`, `save_mc.cpp`, `save_page.cpp`, `options_page.cpp`, `records_page.cpp`, `controller_page.cpp`, `sound_page.cpp`, additions to `menu_widgets.cpp`, `menu.cpp`, `options.cpp`; HDD branches in `hdd.cpp` (unreachable, `port/dead-code.md`).
- sce/platform: libmc write path (`sceMcOpen/Close/Read/Write/Flush/Mkdir/Delete/Format`) on the folder card, `_pcsx2_index` written like PCSX2 (`formats/folder-memcard.md` §10.1); request delay 1 vblank (hypothesis until calibrated).
- Visible in the port: SCREEN SETTINGS (list, screen position, brightness patterns), CONTROLLER SETUP, SOUND SETTINGS (changes applied at once), PERSONAL DATA, LOAD (load, then CONTINUE in the main menu). Not reachable with the available cards: the BGM player (needs a cleared campaign). Save mode starts only from later-slice pages, so the byte-identical save check moves to S4.
- EE FPU: PCSX2's add/sub operand masking reproduced in `fpu::Add/Sub` and applied to all translated code (owner decision 2026-10-14; `port/validation.md` S2a-3); `tools/research/fpucheck.py` checks the count per function. 60 unit tests pass.
- About 140 Ghidra renames logged (`rename-log.csv`, evidence "(S2b)").
- Validation (`port/validation.md` S2b-1, S2b-2): 9 RAM checkpoints with 0 unexplained ranges (`port/ramdiff-expected-s2b.csv`: frame parity, CD latency, input timing, and extra LOAD visits in the owner's run, confirmed); 10 screens with the same content, including 16:9 and CONTINUE after LOAD. The reddish rectangle and the empty usage bars are in PCSX2 too (original). Not checked: the BGM player (`pcsx2/procedures.md` S2b-B, optional) and the libmc request delay (the `s2b_load_done` breakpoint was placed after the read it was meant to catch; procedure error).
- S2a-R: PCSX2's software and hardware renderers agree; the port's menu frame filter differs in a radial pattern (centre G/B −1.5/−2.8, edges +1 to +3), not a uniform rounding bias. Open (`open-questions.md`).

S3 result (translation 2026-10-14, validation 2026-10-15 against PCSX2 run 09, `pcsx2/sessions-09.md`):
- 86/86 functions translated (`slices.py --list S3` is empty; 655 in total). New files: `movie.cpp` (SMV reader, picture upload, PCM feeder, arena), `movie_page.cpp` (in-menu movie pages, subtitles, display modes, frame sync), `interlude_page.cpp` (extras interlude viewer), `src/sce/libmpeg.cpp`.
- New dependency in use: libmpeg2 0.5.1 (approved 2026-10-11, fetch confirmed by the owner 2026-10-14; FetchContent with SHA-256, built as portable C).
- sce/platform: sceMpeg* on libmpeg2 with the IPU colour conversion; sceSifSetDma/sceSifDmaStat into IOP RAM; SOUND command 0xA with destination 0 plays a loop-mode PCM block input in the mixer, command 0xD sets its volume, the reply reports the half being played; timer 0 busy-wait model (`EeHwWaitTimer`); `--exit-at` (development builds).
- Visible: New Game → SS01 with sound and subtitles at 30 pictures/s, Start skips; the next page (mission title) is slice S4.
- Ghidra: 85 renames and 9 plate rewrites logged; `sceMpegAddBs`/`sceMpegGetPicture` were swapped in the earlier hypotheses (0x00270B58 adds the bitstream, 0x00270B90 decodes a picture).
- Fixed after the owner's test: vertical seams in movies (GS texture coordinates rounded to the GS 1/16-texel grid; also slightly closer to PCSX2 on the S2a screens) and flickering corrupt pictures at SMV chunk changes (open GOPs; libmpeg2 partial reset keeps the references).
- Movie upscaling stage (ADR-006 §5, option b decided 2026-10-14): the decoder hands each picture to the renderer through a side path, only when the factor is above 1. FSR 1 EASU runs at the internal-resolution factor (×2 per pass) and is a passthrough at 1×. Draws from the movie buffer are identified through DMA source marks. Dev option `--movie-upscale <2-4>`. Sampling the output in the draws comes with the scaled GS backend (internal resolution work).
- Validation (`port/validation.md` S3-3 to S3-5): 3 RAM checkpoints (movie start, mid-movie at picture 662, movie end) 0 unexplained with the new `port/ramdiff-expected-s3.csv`; mid-movie to end 7028 frames on both sides; image buffer the same picture (IDCT rounding, max 10 levels); 2 screens matched (0.8 and 2.4 mean); sound correlates 0.97-0.995 per channel with the PCSX2 capture, no drift. Confirmed: IPU alpha 0x80, PCM 0x200-byte L/R blocks, the SOUND reply's audio half (`GetMovieAudioHalf` renamed in Ghidra). New research tool `tools/research/flac_mp4.py` (decodes PCSX2's FLAC/MP4 audio capture).
- Open: the interlude viewer test (needs a cleared campaign); libmpeg frameCount/flags rule (no game effect).
