# Validation record

Every validation run of the project, newest last. How slices are validated: `validation-strategy.md`.

| Section | Contents |
|---|---|
| §1, §2, §3, Samples | the decompilation records (2026-10-11): integrity checks, blind re-review, recommendations |
| §2b–§2d | skeleton milestones B, C, E: executable load, boot, RAM at the first `SceneStep`, FPU rounding |
| §2e–§2i | slices S1, S2a, S2b, S3, S4 against PCSX2 |
| §2j | slice 5 prerequisites and missions (S5-P, S5-A, S5-T) |
| §2k | internal resolution 1-8x, 16:9, clean-up (2026-10-19; IR-1 to IR-4) |
| §2l | opening fixes, GRAPHICS page, Modern control types, caches and leaks (2026-10-20; OP-1 to OP-5) |
| §2p | engine flames back on the nozzles (2026-10-21; EF-1) |
| §2q | hands-on run from zero to mission 1 and debug overlay test (2026-10-22; UX-1 to UX-4) |
| §2r | no-save intro: cloud transitions, stutter; PSMT8 table; renaming (2026-10-22; IN-1, IN-2, RN-1; S2a-6) |
| §2s | GPU texture decoding, generation-based texture cache, texture images (2026-10-22; GD-1 to GD-3) |
| §2t | release 0.9 preparation: development parameters, version, file properties, startup image, no game data (2026-10-22) |
| §2o | new memory card and first save, startup messages (2026-10-20; NS-1, NS-2) |
| §2n | anti-aliasing (FXAA, SMAA) before the HUD (2026-10-20; AA-1 to AA-4) |
| §2m | settings tests (GRAPHICS page, live combinations, controller and save round trip), texture residency, sprite edges at N×, defaults and the frame-rate overlay (2026-10-20; ST-1 to ST-8) |

## Validation of the decompilation records (2026-10-11)

The validation had two parts:
- **Integrity checks** of the records: tool-based and complete.
- **Blind re-review** of a random sample of functions, compared against the docs.

All corrections are already applied to the docs, the coverage CSV and the Ghidra plate comments (logged in `../rename-log.csv`).

## 1. Integrity checks

Tool: `tools/research/validate.py`, read-only. It needs `out/research/funcs.json` (from `ghidra_dump.py`) and `reach.json` (from `reach.py`). Final run: **0 problems**.

| Check | Result |
|---|---|
| `symbols.csv` names vs Ghidra | All 541 rows match: functions by address, `data`/`global` rows against Ghidra labels. Hypothesis rows keep the Ghidra default name or the hypothesis name. No duplicate names |
| Coverage CSV names vs Ghidra | All 2,884 rows match |
| Totals | 2,884 = 2,612 understood + 0 partly + 0 unknown + 272 dead. Per-subsystem sums match `../STATUS.md` and `coverage.md` |
| The User's figure 2,884 = 2,229 + 389 + 266 | Correct for the committed version (HEAD). The last research pass promoted all 389 "partly" functions to understood. The 15 "partly" functions of the blind sample come from those 389 |
| Classification | Ghidra has 3,877 functions, all inside `.text`: 2,884 in the game range (each has a coverage row) and 993 library functions outside it. Every `sdk`/`libc`/`libgcc`/`kernel` symbol is outside the game range; every `game` symbol is inside |
| Dead code vs call graph | Raw graph (all data pointers as roots): 2,619 reachable. Without scenes 1 and 3 as roots (they are only in the scene table and never requested): 2,612 reachable, which is exactly the 2,612 non-dead functions |

Fixed by these checks:
- **V-2 (scene 3 classification).**
  - Problem: scene 1's handler was marked dead, but scene 3 was still "understood". Scene 3 = `SceneMovie` 0x00211CA8, its 4 sub-states, and `SetRequestFlags` 0x00100AF8 (used only by scene 3). Both scenes are reachable only through the scene table and never requested.
  - Fix: the 6 scene-3 functions are now **dead**. New totals: 272 dead, 2,612 understood; the movie subsystem is 48 understood / 9 dead.
  - The dead-code rule now covers this case (`dead-code.md`, `coverage.md`).
  - `SetRequestFlags` had an empty note: it stores `ctx->req = a0`.
- `STATUS.md` symbol counts were off by one (540/264 → 541/265). Corrected.
- `coverage-functions.csv` contained 87 double-encoded UTF-8 sequences (`Â±`, `Ã—`, …) from an earlier rewrite (not present in HEAD). Repaired.

## 2. Blind re-review

### Method

- **Sample.** 60 functions drawn with `random.Random(20261011)`, stratified by subsystem (proportional, largest remainder):
  - **40** understood in HEAD;
  - **15** "partly" in HEAD and promoted to understood in the last pass;
  - **5** dead.
  - The partly pool is mostly effects, so 11 of the 15 are effects functions.
- **Reviewer.** One subagent analysed each function from Ghidra only. Its rules:
  - do not read `docs/`, the CSVs or tool output, and ignore plate comments;
  - check the disassembly for MMI, VU0 and 128-bit code;
  - report any reliance on existing names.
- **Ghidra stayed read-only.** One exception to check: a single `disassemble_bytes` call on existing code at 0x001F3910. It changes nothing; the User can confirm when saving the program.
- **Scoring.** I compared each description with the CSV note and the subsystem docs. Every major and every disputed constant was checked in Ghidra before scoring.
  - **agree**: same behaviour. A terse note counts as agree when it is correct.
  - **minor**: an omission, or a wrong constant or label, that does not change what the function is.
  - **major**: the doc describes different behaviour or a different meaning of the data.
- **Wider re-review rule.** A subsystem with more than 5 % major differences gets a wider re-review.
  - Effects, frontend and mission exceeded it.
  - 50 more functions were drawn with `random.Random(20261012)` and reviewed the same way: effects 18 (10 promoted + 8 understood), frontend 16, mission 16.

Limits:
- The reviewer saw the Ghidra names we gave. It reported each reliance, and a match that rested only on a name did not count.
- One reviewer did all batches.
- Behaviour that needs runtime context to interpret (which screen, which mission) was scored on mechanics only.

### Results

- First sample (60): **40 agree, 16 minor, 4 major** (6.7 % major). By group:
  - understood: 31 agree / 7 minor / 2 major;
  - promoted from partly: 6 / 7 / 2;
  - dead: 3 / 2 / 0.
- Wider re-review (50): **41 agree, 9 minor, 0 major**.
- All 110: 81 agree (74 %), 25 minor (23 %), 4 major (3.6 %).

| Subsystem | Sample (agree / minor / major) | Major % | With wider re-review | Major % |
|---|---|---|---|---|
| ai | 1 (1 / 0 / 0) | 0 % | 1 (1 / 0 / 0) | 0.0 % |
| camera | 1 (1 / 0 / 0) | 0 % | 1 (1 / 0 / 0) | 0.0 % |
| collision | 1 (1 / 0 / 0) | 0 % | 1 (1 / 0 / 0) | 0.0 % |
| core | 4 (3 / 1 / 0) | 0 % | 4 (3 / 1 / 0) | 0.0 % |
| effects | 16 (10 / 4 / 2) | 12 % | 34 (25 / 7 / 2) | 5.9 % |
| flight | 1 (1 / 0 / 0) | 0 % | 1 (1 / 0 / 0) | 0.0 % |
| frontend | 8 (6 / 1 / 1) | 12 % | 24 (20 / 3 / 1) | 4.2 % |
| hdd | 2 (2 / 0 / 0) | 0 % | 2 (2 / 0 / 0) | 0.0 % |
| hud | 2 (1 / 1 / 0) | 0 % | 2 (1 / 1 / 0) | 0.0 % |
| mission | 3 (1 / 1 / 1) | 33 % | 19 (13 / 5 / 1) | 5.3 % |
| movie | 1 (0 / 1 / 0) | 0 % | 1 (0 / 1 / 0) | 0.0 % |
| radio | 2 (1 / 1 / 0) | 0 % | 2 (1 / 1 / 0) | 0.0 % |
| render | 4 (3 / 1 / 0) | 0 % | 4 (3 / 1 / 0) | 0.0 % |
| replay | 2 (1 / 1 / 0) | 0 % | 2 (1 / 1 / 0) | 0.0 % |
| results | 1 (1 / 0 / 0) | 0 % | 1 (1 / 0 / 0) | 0.0 % |
| sequence | 3 (2 / 1 / 0) | 0 % | 3 (2 / 1 / 0) | 0.0 % |
| sound | 4 (3 / 1 / 0) | 0 % | 4 (3 / 1 / 0) | 0.0 % |
| unreached | 2 (0 / 2 / 0) | 0 % | 2 (0 / 2 / 0) | 0.0 % |
| weapons | 2 (2 / 0 / 0) | 0 % | 2 (2 / 0 / 0) | 0.0 % |

Effects and mission stay just above 5 % only because their small first samples are included. The wider samples had no major difference. The cause of each major was then fixed across all notes, not only in the sampled function (V-6, V-8).

### Findings and fixes

| Id | Where | Finding | Fix |
|---|---|---|---|
| V-1 | `func_00224180` / `func_00224410` / `func_00224848` (sequence) | Skinning part types 0 and 1 were swapped in three notes and in `../pcsx2/procedures.md`. Found through the PCSX2 hit (`ra` 0x002246DC) | Notes, `vu-programs.md`, the procedure and the Ghidra plate of 0x002236A0 corrected |
| V-2 | Scene 3 | See section 1 | 6 functions marked dead |
| V-3 | `func_00105178` (core, minor) | The MMI scale-matrix builder leaves the upper word of the sign-extended `mfc1` in row0.z, row1.w and row2.w. That word is 0 for positive scales and 0xFFFFFFFF for negative ones. A translation must keep it | Note |
| V-4 | `func_0014B3F8` (frontend, **major**) | The note said "VU1 light/colour constants". The packet actually uploads one vector plus the camera's world→screen and world→clip matrices (+0xE0..+0x15F) to VU1 and runs MSCAL 0. The light set is written separately | Note |
| V-5 | `func_0010FB30` (mission, **major**) | The note said "at mission end: kill bookkeeping". It is the **mission phase change** (rules state flag 8 → 4). It activates the next phase's units (flags48 0x100; with 0x8000000 only when `g_GameState[0x28]` ≥ 2) and retires the others | Notes of 0x0010FB30 and 0x0011A208, `mission-flow.md` |
| V-6 | Effect draw setup (effects; **major** in `func_001FF818`, minor in `func_002034A8`) | Three mislabels:<br>• Effect work +0x62498/+0x62494 were called "frame range" in **31 notes**. They are the near/far depth distances: `InitEffects` sets 1.0 / 65536.0, and the terrain fog setup rewrites the far value.<br>• +0x624E0 was called "light matrix". It is the effect clip matrix (`func_00155900`).<br>• `func_001FF818` builds the frustum clip structure 0x002BB990, not "trail parameters / colour table" | All 31 notes relabelled; 0x001FF818 rewritten |
| V-7 | `func_001E88C0` (effects, **major**) | The note said "afterburner sprite writer". It is a two-pass **heat-haze distortion**. Pass 1 writes a mask into framebuffer alpha. Pass 2 draws displaced strips of the **framebuffer sampled as a texture** (CT32 1024×1024 at TBP 0), blended with destination alpha | Notes of 0x001E88C0 / 0x001E81F0; added to `../platform/gs-subset.md` and `renderer.md` §1, because the renderer must handle this framebuffer feedback read |
| V-8 | Random-table reads (effects, mission, ai, weapons) | 17 of the 67 `NextRandomFloat` callers had no mention of the read in their note, e.g. the spawn `func_0019C278` with 9 call sites. The behaviour docs listed the gameplay users, but not every effect spawn | Every note now states its call-site count. The full list of 67 functions / 152 call sites is appended to `../game/random.md` |
| — | Minor items | Omitted details:<br>• the target marker on the HUD ladder;<br>• the restore of unit flags after player placement;<br>• the first colour stage;<br>• the addition before the pan wrap;<br>• the libc `rand()` call per fired proximity trigger;<br>• the fade level at sequence start.<br>Also: wrong constants in replay camera styles 8 and 9; a contradictory queue number in the radio matcher; empty notes for `StoreFlightDataPointers` and two unreached functions | Notes corrected |

### What this says about the records

- Mechanics (control flow, offsets, constants, call order) were right in nearly every function.
- The errors were in two areas:
  - **interpretation of data**: what a matrix or a pair of floats means;
  - **omitted side effects**: random reads, extra outputs.
- The promoted-from-partly group had the most problems: 2 major and 7 minor out of 15, all in effect draw code documented at interface level. That matches the stated depth of those notes (`coverage.md`): they are good enough to find code, but the translator must read the packet code itself.
- Gameplay subsystems (ai, weapons, flight, collision, hud) had no major difference.

## 2b. Skeleton milestone B checks (2026-10-11)

### B-1. Executable load against PCSX2

The port loads `SLUS_201.52` from the ISO into guest memory (`src/platform/elf_loader.cpp`). A development-build dump taken right after loading (`--dump-ram-after-load`) was compared with three PCSX2 save states (`bp_00100220`, `bp_00100130`, `mainMenu`) using `tools/research/ramdiff.py`:

| Region | Result |
|---|---|
| `.rodata`, `.lit4`, `.vutext`, `.vudata`, `.gcc_except_table` | identical |
| `.text` | identical except 48 bytes at 4 places, the same in all three states (B-2) |
| `.data`, `.sdata`, `.sbss`, `.bss`, heap, stack, low memory | differ as expected: the states were taken after crt0, the SDK init and part of the game's init had run (the RAM check at the first `SceneStep` is milestone E) |

Loader facts confirmed: one PT_LOAD segment at 0x00100000 (0x31172C bytes from the file, 0x3CC74C in memory, ending exactly at the end of `.bss` 0x004CC74C), NOBITS `.sbss` 0x00411780 (+0x1BC) and `.bss` 0x00411980 (+0xBADCC) zeroed, entry 0x00100008, gp 0x004159F0, executable SHA-1 as in `../platform/executable.md`.

Initial guest stack pointer: crt0's `SetupThread(stack = −1, size 0x100000)` returns **sp = 0x01FFED60**. Measured from `bp_00100220.p2s`: the EE register file sits at offset 0x162 of `PCSX2 Internal Structures.dat` (PCSX2 2.8.2; 32 GPRs × 16 bytes), sp = 0x01FFED40 inside `main`, whose prologue reserves 0x20 bytes.

### B-2. PCSX2 patches three VU0 flag reads

The 48 bytes are PCSX2 game patches (its game database), not game writes: they are present at the start of `main` already. Each patch swaps a `cfc2 rX, $17` (read of the VU0 **MAC flag**) with the vector instruction three `vnop`s before it:

| Site | Function | Original order | Patched (PCSX2) |
|---|---|---|---|
| 0x0010FFC8 / 0x0010FFDC | `func_0010FFB8`, project a point with clip test | `vmaddw.xyzw vf9, vf7, vf8` … 3 × `vnop` … `cfc2 v0, $17` (then `andi 0x11`) | `cfc2` first, `vmaddw` last |
| 0x0017C0C4 / 0x0017C0D4 | `func_0017C020`, point-to-triangle distance (collision) | `vsub.x vf0, vf9, vf10`; `vsub.x vf0, vf12, vf11`; 3 × `vnop`; `cfc2 v1, $17`; `cfc2 a0, $17` (then `andi 0x80` each) | `cfc2 v1` moved before the second `vsub` |
| 0x0017C6C4 / 0x0017C6D4 | `func_0017C688`, AABB overlap test (collision) | same pattern | same swap |

Meaning: on the real EE the MAC flag reaches `cfc2` with a pipeline delay, so each read returns the flags of an **earlier** op. In `func_0017C020` the intent is visible: `v1` receives the sign of `vf9 − vf10` and `a0` the sign of `vf12 − vf11`. PCSX2 updates flags immediately and reorders the code to get the hardware result.

Port consequence: the translation follows the hardware result (= the PCSX2-patched order), never the naive "flags of the last op". Rule added to `.claude/rules/game-code.md`; risk added to `risks.md`; notes on the three functions in `coverage-functions.csv`. Other `cfc2` flag reads in game code need the same check when they are translated (a scan for `cfc2 …, $16/$17/$18` close after vector ops is a later research task).

RAM comparisons with PCSX2 must ignore these 48 bytes (expected difference).

## 2c. Skeleton milestone C checks (2026-10-12)

### C-1. Boot run

`ProjectStonehenge --iso <image>` (development build, RelWithDebInfo) runs `Entry` → `main` on the game thread. The log shows the nine IOP modules replaced, the first vsync wait reached during `InitSubsystems`/`LoadBootData`, then `entered scene 6 (SceneStartup)` about 0.55 s (≈33 vblanks) after start. The main loop then idles in the scene 6 stub: 318 vblanks in 5.3 s, 0 ticks dropped. Scene 0 (`SceneBootLoader`) runs translated: BD.TBL, DATA 0x163/0x164 (sound data), the BD entry 0 transfer to the IOP, `RequestScene(6)`.

The frame count to reach scene 6 is not expected to match PCSX2: CD reads complete one poll after they are issued, while a real drive takes a variable number of frames.

### C-2. Expected RAM differences at the first `SceneStep` (for milestone E)

These come from the SDK layer, not from translated game code. Milestone E checks the rest against PCSX2.

| Area | Why |
|---|---|
| newlib heap headers around 0x004CC758 and malloc's internal state | the port's `malloc` keeps its bookkeeping on the host; it returns the same block (0x004CC760, 0x01A318A4) |
| SIF client structs (`sceSifClientData` at 0x0033E58C, 0x00353F94, 0x00420400): rpcd, `serve` | `serve` holds a stand-in value; the other words are not written by the port |
| pad DMA buffers 0x002A0080 / 0x002A0180 | libpad's driver writes them on the PS2; the port answers `scePadRead` directly |
| `sceCdlFILE` date bytes (on the guest stack, below sp) and the TOC buffer 0x0029C980 | TOC contents are a hypothesis (procedures.md); dates left zero |
| GS GParam 0x003FAAA0 (+6 version, +8 handler) | revision 0x1B assumed (PCSX2's CSR.REV) |
| SDK/kernel data (libpad, libcdvd, libmc, fileio, `_InitSys`, thread/semaphore tables) | the SDK internals are not translated |
| guest stack below sp | frame contents differ (host locals stay on the host) |
| SOUND reply buffers 0x00429800/0x00429E00 and the voice-status bytes 0x00424FD0 | stand-in replies: transfers finish at once, voices never end on their own (no mixer yet) |
| reverb work-area address 0x00410D08 | stand-in value 0x001FE0C0 (procedures.md) |
| frame counters, timer 0 | boot timing differs (C-1) |
| constructor 0x0027C680 (C++ runtime) effects | not run by the port |

This was the prediction before the comparison. The measured result is E-1; the TOC, GParam and reverb rows are settled there.

## 2d. Skeleton milestone E checks (2026-10-12)

### E-1. RAM at the first `SceneStep` against PCSX2

Inputs:
- **PCSX2 side:** `scenestep_first.p2s`, PCSX2 2.8.2, cold boot, Execute breakpoint at `SceneStep` 0x00100B18, first hit. `frame_count` (0x0029F3A8) = 0, so both sides are at the same point: after the boot code, before the first scene handler.
- **Port side:** `ProjectStonehenge --dump-ram-at-first-scenestep` (development build). The dump is taken at the entry of the first `SceneStep`, on the game thread.
- **Comparison:** `python -I tools/research/ramdiff.py <user data>\dumps\ram_first_scenestep.bin scenestep_first.p2s --expected`. The `--expected` table is `ramdiff-expected.csv` (address ranges and reasons only, no memory contents).

**Before the fixes:** 817 differing ranges. 700 of them are in the EE kernel area below 0x00100000; 117 are in the game image, heap and stack.

**Three port errors found and fixed:**

| Address | Found | Cause | Fix |
|---|---|---|---|
| 0x00411784 `g_WorkHeapSize` | port 0x01A317A4, PCSX2 0x01A318A4 | The port's `malloc` rounded requests to 8 bytes, so 0x01A318A4 became 0x01A318A8 and no longer fit; `ProbeLargestMallocSize` then stopped one 0x100 step lower. **Game state difference:** the work heap was 256 bytes smaller. | `malloc` now uses newlib's EE chunk sizes: (n + 4 + 15) & ~15, at least 16. Arena 0x01A318B0 bytes from chunk 0x004CC758. The size word (0x01A318B1) is written before the block as newlib does. Unit test "malloc: newlib chunk sizes…" |
| 0x0029C98C–0x0029C997 TOC | port zeros; PCSX2 `01 02 01 00` at +0x0C and `00 1D 7C 0F` at +0x14 | The TOC hypothesis covered only the first 6 bytes and byte 0x11 | `sceCdGetToc` writes PCSX2's single-layer DVD layout: the physical-format bytes `01 02 01 00`, start PSN 0x030000, and end PSN 0x30000 + sectors − 1 (= 0x1D7C0F for the 1,735,696-sector image), both big-endian. Copy size 0x810. Unit test "libcdvd over a synthetic ISO…" |
| SOUND id-6 reply → 0x00410D08 | PCSX2 0x001DB7C0 after the reply arrives (`scenestep_first_2.p2s`); port stand-in 0x001FE0C0 | The value was a hypothesis | Stand-in returns the measured 0x001DB7C0: the effect end 0x001FFFFF (the game's initial value at 0x00410D08) minus a 0x24840-byte area, plus 1. The game sends id 6 only as (core 1, mode 2). Unit test "SOUND stand-in: reverb mode reply…" |

Confirmed unchanged: GParam 0x003FAAA0 = `01 00 02 00 01 00 1B 00` (interlace, omode 2, ffmode 1, version 0x1B; unit test) and the radio timers 0x004BE0B0. Both are identical in the two states and in the port's dump. The scratchpad is identical.

**After the fixes:** 814 ranges, all classified by `ramdiff-expected.csv`, **0 unexplained**. After the rounding switch of E-2: **796 ranges, 0 unexplained**; the `fpu-rounding` rows were removed from the CSV.

| Category | Ranges | Bytes | Meaning |
|---|---|---|---|
| `kernel` | 701 | 167,572 | EE kernel RAM below 0x00100000, and the kernel thread area above the initial sp 0x01FFED60. The port has no EE kernel and the game never reads either |
| `sdk` | 70 | 1,800 | SDK and runtime internals, each region identified by its xrefs in Ghidra: SIF client structs (`rpcd`), SIF/IOP-heap/libpad/libmc/libcdvd `.data` and `.bss` (search-file cache, TOC copy 0x004CB300), libpad DMA buffers 0x002A0080/0x002A0180, newlib malloc and sbrk state, libgcc frame object, the crt0 argument block 0x004C55C0 (`main` never reads argc/argv), C++ runtime constructor data, the last 16 bytes of the TOC copy (IOP leftovers; the game passes 0x0029D180 to `InitCdFileSystem` but never uses it), and the newlib top-chunk header 0x01EFE00C |
| `stack` | 14 | 919 | below `main`'s sp 0x01FFED40 (dead frames), and `main`'s register-save slots. The port keeps saved registers and host locals on the host |
| `timing` | 6 | 84 | CD poll counter 0x0029F1D0 (7 polls on PCSX2, 1 in the port); pad record 0x004121E0 (PS2: the pad is not stable yet, state 0; port: stable at once, state 0x28; at 10 s PCSX2 shows state 99 / id 7 like the port); SOUND and VAG fno 1 replies (async on the PS2, still unanswered at this point; immediate in the port) |
| `pcsx2-patch` | 4 | 48 | PCSX2 GameDB COP2 reorder patches (B-2) |
| `fpu-rounding` | 18 | 1,618 | before E-2 only: 1-ULP float differences, gone after the switch to round-to-nearest |
| `stack-garbage` | 1 | 1 | 0x00425024: byte +8 of the SOUND id-7 record (reverb depth) is never written by `func_0021CFA0`. On the PS2 it keeps the 2 that the id-6 record left at the same stack slot; `GuestFrame` zero-fills. SOUND.IRX does not read the field |

Other findings:
- At 10 s (`scenestep_first_2.p2s`) the SOUND voice-status bytes 0x00424FFA–0x00424FFF (voices 42–47) are 4, meaning "released, envelope still sounding". These are probably the 6 VAG.IRX streaming voices, which SOUND sees as not keyed on but audible. HYPOTHESIS until the audio backend exists; the stand-in reports 0 for them. Noted in `../subsystems/sound.md`.
- The GameDB entry for SLUS-20152 also sets `eeRoundMode: 0` ("Fixes autopilot being silly and crashing the plane"), which E-2 depends on.

Correction (slice S1, 2026-10-14): the 0x001DB7C0 of the id-6 row comes from a **later** id-6 reply in `scenestep_first_2.p2s` (10 s), not from the boot command. SOUND.IRX replies with the lower of the two cores' effect start addresses as read before the new mode takes effect: the boot command (core 1, mode 2) replies 0x001DFFF0 (core 0 still in mode 0), measured in all three cold boots of run 06; a later id 6 then sees core 1's mode-2 area 0x001DB7C0. The stand-in now models both values (`iop_services.cpp`; unit test "SOUND stand-in: reverb mode reply…"). The first-SceneStep comparison stays at 0 unexplained ranges (the reply has not arrived there).

Second correction (2026-10-16, S4-7): the PCSX2 SPU2 state shows why. In `scenestep_first_2.p2s` **core 0** is in reverb mode 3 (ESA 0x001DB7C0), not core 1 in mode 2; in every later reference state core 1 is in mode 2 with ESA **0x001FE0C0** (work area 0x1F40) and core 0 in mode 0 (0x001DFFF0). The reply is min(ESA0, ESA1) read **after** the command (sceSdSetEffectAttr writes the ESA registers synchronously), with libsd's per-mode work-area table (LIBSD.IRX 0x4148). The "value from before the command" hypothesis and the 0x24840 mode-2 size were wrong; the stand-in now uses the table.

### E-2. EE FPU rounding: PCSX2 runs AC04 with round-to-nearest (decided 2026-10-13: nearest)

All 1-ULP float differences have one cause:
- **Random table 0x00411998** (512 × `rand() * 2^-31`; the multiplier 0x30000000 is an exact power of two). `cvt.s.w` rounds the 31-bit integer: PCSX2 matches round-to-nearest in 512 of 512 entries. The port's `fpu::IntToFloat` truncates, so 247 entries are 1 ULP lower.
- **Sound category volumes 0x0042CA50** (`x / 255.0f`): PCSX2 matches round-to-nearest division; `fpu::Div` truncates.

Cause: PCSX2's GameDB sets `roundModes: eeRoundMode: 0` (Nearest) for SLUS-20152. The reason given is that the autopilot crashes the plane under PCSX2's default Chop mode. So the reference behaviour (PCSX2) is round-to-nearest for the whole EE FPU in this game, while `ee_math.h` and `.claude/rules/game-code.md` model round-toward-zero. Hardware is not IEEE in either mode, so neither is bit-exact by construction. The values that matter for the port are PCSX2's, and those are nearest.

**Decision (User, 2026-10-13): round to nearest.** `ee_math.h` now rounds add/sub/mul/div/sqrt/`cvt.s.w` results to nearest (ties to even), keeping the EE rules: no NaN/Inf, denormals flushed to zero, overflow and x/0 clamped to ±FLT_MAX. `cvt.w.s` still truncates. The rule in `.claude/rules/game-code.md` is updated. Rerun of E-1: the random table and the sound volumes match PCSX2 byte for byte (796 ranges, 0 unexplained). Unit test "EE FPU: round to nearest…".

**Addendum (S2a, 2026-10-14):** add and sub are not plain round-to-nearest in PCSX2. The smaller operand is masked first (`FPU_ADD_SUB`, see S2a-3). `fpu::Add`/`fpu::Sub` now model this, and all translated float adds/subs use them.

## 2e. Slice S1 checks (2026-10-14)

Reference: `../pcsx2/sessions.md` run 06 (6 save states, 5 screenshots, cards S/E). Method: `validation-strategy.md` §2.

### S1-1. RAM checkpoints

| Checkpoint | PCSX2 state | Port run | Result |
|---|---|---|---|
| 0x00261378 sub 7, card S | `s1_S_sub7` | `--memcard <card S> --dump-ram-at 261378` | 0 unexplained |
| 0x00261378 sub 7, card E | `s1_E_sub7` | card E | 0 unexplained |
| 0x00261378 sub 7, no card | `s1_N_sub7` | `--no-memcard` | 0 unexplained |
| 0x002613D0 sub 9, card S | `s1_S_sub9` | card S | 0 unexplained |
| 0x00260B78 `EnterMenu(0)`, card S | `s1_S_exit` | card S | 0 unexplained |
| 0x00260B54 `RequestScene(2)`, card E | `s1_E_exit` | card E | 0 unexplained |

Fixed on the way (all in the IOP/SDK stand-ins; the translated game code needed no fix after review):
- libmc: GetInfo completes one vblank after the request, GetDir after one more poll (the game's poll counters 0x004118A8 / 0x004118CC must match); results 0 / -2 (unformatted: empty `_pcsx2_superblock`) / -11 (no card); free space 7925 KB for the AC04 save = 8001 - file clusters - directory-entry clusters; times are UTC.
- SOUND reply: one persistent 0x600-byte reply area (leftovers of earlier replies visible to the game); records 7-9 write only the id and a status byte at +8; transfer record = last finished ticket (one batch later) and status 0; voices 42-47 report 4; id-6 address (see the E-1 correction).
- VAG fno 1 reply: words 5-8 = 1 and ring indices 2.
- Pads: neutral analog 0x7F as PCSX2's DualShock 2, `scePadRead` byte 31 = 0x5A.

Remaining differences, all classified: frame counters and frame-parity effects (`ramdiff-expected-s1.csv`: the port boots to scene 6 8 frames earlier and loads DATA 333 in 3 frames instead of ~17), newlib malloc bins (`sdk`), the User's stick values (`input`). Note: PCSX2's own I/O timing differs by a frame between cold boots (no card: sub 7 at frame 58, unformatted card: 59), so frame-exact matching needs an I/O latency model calibrated on more runs (`../open-questions.md`).

### S1-2. Screenshots (`tools/research/imgdiff.py`)

| Screen | PCSX2 | Mean diff R/G/B | Pixels > 16 | Note |
|---|---|---|---|---|
| splash 1, card S | `s1_S_splash1` | 0.23 / 0.29 / 0.32 | 0.65 % | best at offset 0 |
| splash 0, card E | `s1_E_splash0` | 0.30 / 0.31 / 0.30 | 0.66 % | |
| no-card message 1 | `s1_N_msg_1` | 0.56 / 0.56 / 0.56 | 1.0 % | glyph edges |
| splash 0, card S | `s1_S_splash0` | 1.05 / 1.24 / 1.45 | 3.0 % | PCSX2 image is 0.80 as bright: taken during the fade-in (card E's splash 0 matches) |
| no-card message 2 | `s1_N_msg_2` | 0.32 / 0.33 / 0.33 | 0.59 % | reached with `--pad-script 320:cross` |

User feedback (2026-10-14): the audio sounds right; with `--no-memcard` the controller gave no input. Cause: the input backend opened gamepads only on hot-plug events, and none arrived in that session (no "port 1: gamepad" line in the log). Fix: `InputBackend::Start` logs every device SDL sees and opens the gamepads at start-up (an Xbox One controller is now opened at once). Also from the User: on PCSX2 the brightness option sits at its default until a save is loaded (game behaviour); the port now shows the option's effect (ADR-006 §4 implementation note).

A bug found on the way: the texture decoder added a bilinear margin to nearest-sampled sprites, so glyphs at texel 0 decoded the whole 1024×1024 font and overflowed the per-frame atlas (first 16 glyphs of each line missing). Fixed (margin only with TEX1.MMAG).

## 2f. Slice S2a checks (2026-10-14)

### S2a-1. S1 checkpoints after the SOUND/VAG changes

S2a corrected the SOUND voice record offsets (voice +0x0E, flags +0x10) and replaced the voice 42-47 special case with SOUND.IRX's `get_ch_stat` rule driven by the mixer (ENDX, ENVX), with the VAG.IRX stream voices now keyed on by the VAG stand-in. The six S1 RAM checkpoints (card S sub 7 / sub 9 / exit, card E sub 7 / exit, no card sub 7) were re-run against `pcsx2/sessions.md` run 06: **0 unexplained ranges** each. An intermediate state (VAG voices keyed but status taken from the mixer's key state) gave 1 instead of 4 for voices 42-47 at 0x00424FFA and in the SOUND reply copies: the measured 4 is now explained by the code.

### S2a-2. Title and main menu (port only, before the PCSX2 batch)

- Title screen with logo, PRESS START and copyright; Start opens the main menu (NEW GAME / LOAD / TUTORIAL / VS MODE / OPTIONS with help line); X on NEW GAME opens the difficulty column with its connector lines (`--pad-script 1200:start,1450:cross`, card S).
- Music (`--dump-audio`): the title track from VAG channel 0 about 1 s after the title appears, then the menu track after Start (crossfade to channel 1). Level only checked by RMS; by ear against PCSX2 in the batch.
- Renderer additions needed for the filters: `(Cs−0)·Ad+Cd`, alpha-test fail modes, bilinear texel footprint (`platform/gs-subset.md`).

### S2a-3. RAM checkpoints against PCSX2 (run 07, `../pcsx2/sessions.md` run 07)

| Checkpoint | PCSX2 state | Port run (card S) | Result |
|---|---|---|---|
| 0x0022DF00 title sub 6 | `s2a_title_sub6` | `--dump-ram-at 22DF00` | 0 unexplained |
| 0x0022D748 Start accepted | `s2a_start` | `--pad-script 1200:start --dump-ram-at 22D748` | 0 unexplained |
| 0x0022D84C menu active | `s2a_menu_active` | as above, `22D84C` | 0 unexplained |
| 0x0022D880 confirm | `s2a_menu_confirm` | `--pad-script 1200:start,1500:down,1515:up,1863:cross --dump-ram-at 22D880` (the User's Down/Up, confirm 644 frames after "menu active" as in PCSX2) | 0 unexplained |

Expected differences: `ramdiff-expected.csv`, `ramdiff-expected-s1.csv` and the new `ramdiff-expected-s2a.csv`. Four classes, each checked on the values:
- **CD latency** (`timing`): PCSX2 loads the title data (DATA 0xB0) about 20 frames slower, so it enters sub 6 with 30 menu-fade steps where the port has 10. At port hit 21 of 0x0022DF00 the fade object is identical byte for byte (time 0x3EFFFFF8, value 0x3F7F4C7E). The menu load-queue timer differs for the same reason.
- **Frame counter / parity** (`timing`): the menu draw packets in both buffer halves (scanline band positions from `BuildScanlineBands`, pulse colours from `GetMenuPulse`, DMA tags), the menu bucket pointers and the VAG send buffers.
- **Music time** (`timing`): VAG slot stream positions, the VAG reply area, and one VAG refill read more or less in the CD queue and request counters. They depend on how long the music has played (boot frames, title time).
- **Input timing** (`input`): title/idle time and the logo ease-out (`g_TitleMenu` +0x688..+0x697), the pad repeat counter, and the SE requests (counter 0x00410D00, the voice table, the SOUND reply status, the menu SE record). The number of type-out ticks depends on when the cursor moved during the help-line type-out: 18 in the port against 17 in PCSX2, with the same sound types on the same voices.

**EE add/sub (fixed in this check).** The fade value showed that PCSX2's `add.s` is not plain round-to-nearest. PCSX2 2.8.2 (`pcsx2/x86/iFPU.cpp`, `FPU_ADD_SUB`, compile-time `FPU_CORRECT_ADD_SUB 1`, not a setting) models the EE adder's missing guard bits. Before an `add.s`/`sub.s`/`adda`/`suba`/`madd`/`msub`, the operand with the smaller exponent keeps only its top 25 − d significand bits (d = exponent difference; sign only when d ≥ 25). The sum is then rounded to nearest. Thirty additions of 1/60 give 0x3EFFFFF8 in PCSX2 and with this model, against 0x3F000003 with plain rounding. So the menu fade ended one frame early in the port. `fpu::Add`/`fpu::Sub` (`src/common/ee_math.h`) now implement the model (unit test "EE FPU: add/sub mask…"). By the User's decision (2026-10-14), every float add/sub-type instruction in the translated code now goes through them: 83 instructions in 30 functions, counted from the disassembly. The game-code rule and the `port-function` skill require it from now on. `mul.s`/`div.s` stay plain round-to-nearest (PCSX2 uses SSE with the round mode for them). The S1 checkpoints were re-run after the change: still 0 unexplained.

### S2a-4. Screens

PCSX2 shows the 224-line field output deinterlaced. On the title and menu, with their fine dot-pattern background, its screenshots match the port's 448-line image with each row averaged with the row above (`imgdiff.py --blend-fields`). This also improves the S1 splash (mean 0.05 instead of 0.23).

| Screen | PCSX2 | Port vblank | Plain: mean R/G/B, > 16 | `--blend-fields`: mean R/G/B, > 16 |
|---|---|---|---|---|
| title | `s2a_title` | 720 | 1.04 / 3.05 / 4.07, 2.5 % | 0.21 / 1.52 / 2.20, 0.00 % |
| main menu | `s2a_menu` | 1376 | 0.97 / 2.63 / 3.86, 1.1 % | 0.78 / 1.45 / 1.96, 0.00 % |
| NEW GAME opened | `s2a_menu_open` | 1996 | 0.76 / 3.12 / 4.64, 2.2 % | 0.22 / 1.67 / 2.78, 0.09 % (help-line type-out glyphs) |

No structural difference: same primitives, textures, positions and blend. One difference remains open: a G/B bias of −1 to −2 levels in the screen centre (x 128–512, y 64–384), where the frame filter's zoom-blur copies overlap. The filter is a feedback effect (each frame samples and blends the previous frame buffer). The port blends with fixed-function float blending, the GS with integer `((A−B)·C >> 7) + D`, so per-frame rounding differences settle into a steady bias. PCSX2's hardware renderer is not GS-exact either, so the reference renderer had to be known first.

**S2a-R (run 08, `../pcsx2/sessions.md` run 08).** The User's settings: renderer Automatic (Vulkan, hardware), blending accuracy Basic, deinterlacing Automatic. The same main-menu frame with the **software** renderer (`s2a_menu_sw`) agrees with the hardware shot within 0.3 levels in the centre (signed mean G +0.34, B +0.26). So both PCSX2 renderers agree and the port is the outlier. Against the software shot the port's difference is **not a uniform bias** but a radial pattern: G/B −1.5/−2.8 in the centre, +1 to +3 at the edges and corners (32×32 block map). Brightness is not involved: both sides have brightness 3 (0x002A15CD) at that point, and the port's native screenshot applies the same field-copy scale. The zoom blur samples a 60×44 PSMT8 glow texture (TBP 0x3E90, CLUT 0x3E84) magnified about 10× with bilinear filtering and adds four scaled copies with `Cs·Ad + Cd`. Tests in the shader: rounding the bilinear result instead of flooring it raises everything by about 1 level, and unquantised weights instead of the GS 4-bit weights deepen the centre deficit (−2.4 instead of −1.6). Neither removes the radial shape, so blend rounding alone does not explain it. Next step (open, `../open-questions.md`): compare the alpha channel (Ad of the zoom blur) and the glow texture in GS local memory with the save state's `GS.bin`, which needs a port GS-memory dump.

### S2a-5. S1 bug: green band on the AC04 PROJECT splash (User report, run 07)

The User saw a green area in the lower part of the AC04 PROJECT splash (splash 1, card S). Image 1 is a PSMCT24 texture (210×432, 3 bytes per pixel, DATA 333). Its upload is split over several IMAGE packets of whole qwords, and 16 is not a multiple of 3. `GsMemory::WriteImage` handled each packet separately and dropped the 1–2 bytes of the pixel split across a packet boundary, so every later pixel was shifted by a byte (port RGB = PCSX2 GBR). In the native screenshot this is faint (rows 414–430, values 0–5): the S1 tolerance (mean ≤ 1) did not catch it, but the presented window made it visible. Fix: `GsState::ImageData` keeps the split pixel's bytes for the next packet (cleared at TRXDIR). Unit test "GIF: CT24 IMAGE transfer with a pixel split across two packets". Splash 1 now differs by at most 2 per channel in that band. Lesson: the screen check also looks at the maximum difference per region, not only the mean (`validation-strategy.md` §2.2).

### S2a-6. Follow-up of S2a-R: GS memory against `s2a_menu_active` (2026-10-22)
- Port GS memory at the main menu (`--dump-vram`, vblank 1376) against `GS.bin` of `s2a_menu_active` (VRAM at offset 0x1A9 of the GS state): the zoom-blur CLUT (0x3E84) is equal; the glow texture blocks 0x3E90–0x3E9D (uploaded every menu entry as PSMT8 64×58, DBW 2) held the same bytes in another order: 214 of 3,584 bytes moved, all in PSMT8 rows 2–3 of each 16-row group.
- Cause: `kCol8` rows 2 and 3 (`gs_memory.cpp`, and the same table in `tools/research/gsvram.py`) had pixels 2–5 and 10–13 in the wrong order; the correct rows follow the same half-swap rule as rows 6–7 (`{33, 37, 49, 53, 1, 5, 17, 21, …}`). With the corrected table the predicted PCSX2 blocks equal the real ones in all 3,584 bytes; the blocks equal to PCSX2's byte for byte rose from 1,313 to 2,420 (every PSMT8 upload). HYPOTHESIS (not rechecked): the 2026-10-17 column-order check matched its 48 T8 blocks because their bytes were equal within those row pairs (HUD font with large uniform areas).
- Effect on screens: none for textures written and read as PSMT8 (both sides used the same wrong table); it matters where a frame buffer or other data is read as PSMT8 (`gs_conv.comp` uses the same layout).
- S2a screens with the current build (`--blend-fields`, mean abs R/G/B): title 0.16/1.39/2.06, main menu 0.78/1.44/1.93, NEW GAME opened 0.21/1.64/2.73, no pixel over 16 except the help-line glyphs (0.09 %). Signed centre G/B −2.1/−3.3 (title), −0.1/−0.4 (menu), −2.3/−4.0 (opened). Unchanged by the table fix; inside the frame-filter exception of `validation-strategy.md` §2.2.
- Blend test: drawing the zoom blur (`Cs·Ad + Cd`, ALPHA 0x58) with the exact integer GS blend in the shader (`../platform/gs-subset.md`, "All blend equations") instead of the fixed-function DST_ALPHA mapping deepens the centre deficit (menu G/B −1.2/−2.1); the fixed-function mapping stays. The remaining radial pattern therefore comes from the blur's inputs (Ad of the frame, the magnified glow sampling), not from blend rounding: still open (`../open-questions.md`).

## 2g. Slice S2b checks (2026-10-14)

Reference: run 08 (`../pcsx2/sessions.md` run 08), card S, one continuous session. The port replays the User's button sequence with `--pad-script` on a fresh copy of card S (press spacing 30-150 frames; the PERSONAL DATA confirm is shifted by 8 frames so that `frame_count & 0xF` is 6 as in PCSX2, because the records page pads its allocation by `((frame & 0xF) + 1) * 0x10` bytes; quirk in `../subsystems/frontend.md`).

### S2b-1. RAM checkpoints

| Checkpoint | PCSX2 state | Port (`--dump-ram-at`) | Result |
|---|---|---|---|
| CONTROLLER SETUP entry 0x002437C8 | `s2b_ctrl_enter` | `2437C8` | 0 unexplained |
| CONTROLLER SETUP exit 0x00226450 | `s2b_ctrl_exit` | `226450,2` | 0 unexplained |
| SOUND SETTINGS entry 0x00244410 | `s2b_snd_enter` | `244410` | 0 unexplained |
| SOUND SETTINGS exit | `s2b_snd_exit` | `226450,4` | 0 unexplained |
| PERSONAL DATA entry 0x002423A0 | `s2b_rec_enter` | `2423A0` | 0 unexplained |
| LOAD entry 0x0023EB68 | `s2b_load_enter` | `23EB68` | 0 unexplained |
| LOAD exit (save loaded) | `s2b_load_exit` | `226450,8` | 0 unexplained |
| SCREEN SETTINGS entry 0x00240F68 | `s2b_opt_enter` | `240F68` | 0 unexplained |
| SCREEN SETTINGS exit (16:9 set) | `s2b_opt_exit` | `226450,10` | 0 unexplained |

Expected differences: the S1 and S2a lists plus `ramdiff-expected-s2b.csv`. Classes, each checked on the values:
- **Frame parity and animation** (`timing`): the page draw packets beyond the S2a range in both buffer halves, the current-half pointers, the tab fade levels in `g_MenuFade`.
- **CD latency** (`timing`): the records page data-load timer (4/60 s in the port, 16/60 s on PCSX2) and a page-data read still in flight on PCSX2; a VAG stream position.
- **Input timing** (`input`): at SOUND SETTINGS exit the pad script pressed X 30 frames after the last change, while the help line was still typing (last SE 9 in the port, 2 on PCSX2; the message box state; the held-button word); a stale main-menu item scale (+0x3FC of the object at 0x00CCFF80, which the options page reuses without clearing) still decaying from 2 to 1.
- **User sequence** (`input`): between LOAD entry and LOAD exit PCSX2 made two more page-data reads (one is DATA 0xB3 into the save page buffer 0x00CD0640), 81 SE requests against 37, more VAG commands, and slot 0 of the menu BGM record holds track 4 (a `PlayMenuBgm(4)` followed by the LOAD page's `FadeOutMenuBgm`). That is the trace of more main-menu → LOAD visits: the User opened the LOAD page several times while checking why breakpoint 0x0023D4FC did not fire (confirmed by the User). From SCREEN SETTINGS entry on, the BGM record is identical again.

Procedure error: breakpoint 0x0023D4FC (`s2b_load_done`) is in the LOAD page's own read of the save image when the page opens (state 5 → 6, port hit at frame 3291, 25 frames after the page entry). In the procedure it was set 2 s after the entry, so it never fired; not a game difference. The libmc request delay therefore stays uncalibrated (hypothesis, `../formats/folder-memcard.md` §10.1).

### S2b-2. Screens

All ten PCSX2 screenshots, compared with `imgdiff.py --blend-fields` against port screenshots of the same replay:

| Screen | Port vblank | Mean R/G/B | > 16 |
|---|---|---|---|
| `s2b_ctrl` | 1780 | 0.63 / 1.66 / 2.31 | 0.55 % |
| `s2b_snd` | 2260 | 0.68 / 2.04 / 3.36 | 0.21 % |
| `s2b_rec` | 2740 | 0.24 / 1.57 / 2.22 | 0.32 % |
| `s2b_rec_2` | 2950 | 0.38 / 1.82 / 2.80 | 0.48 % |
| `s2b_load` | 3460 | 0.29 / 1.61 / 2.21 | 0.15 % |
| `s2b_menu_loaded` | 3760 | 0.49 / 2.11 / 3.08 | 0.77 % |
| `s2b_opt` | 4181 | 0.54 / 2.39 / 3.67 | 2.18 % |
| `s2b_opt_changed` | 4541 | 0.57 / 2.37 / 3.74 | 2.38 % |
| `s2b_menu_169` | 4690 | 0.33 / 2.13 / 3.32 | 0.69 % |
| `s2b_ctrl_169` | 4900 | 0.44 / 2.71 / 5.50 | 7.16 % |

Same layout, texts, values and colours on every screen, including the 16:9 mode and CONTINUE after LOAD. The thin reddish rectangle on the option pages and the empty usage bars on PERSONAL DATA are in the PCSX2 screenshots too (original behaviour). The means are the menu frame filter's G/B pattern (S2a-4). Pixels over 16 are the moving crosshair lines, the pulsing cursor glow (SCREEN SETTINGS) and the panel opening animation (16:9 CONTROLLER SETUP): the screenshots are not at the same animation frame. No pixel offset improves the 16:9 match (±1 in x or y: 19-20 % over 16).

## 2h. Slice S3 checks (2026-10-14, port only; the PCSX2 batch is pending)

### S3-1. SS01 in the port
Run: card S, `--pad-script 1200:start,1500:cross,1700:cross` (Start, NEW GAME, NORMAL).
- Pictures and subtitles shown (screenshots at vblanks 2200-10500; 4:3, letterboxed 640 × 336).
- Pacing: at `MovieFrameSync` hit 100 frame_count 1926, hit 300 frame 2326, hit 1000 frame 3726: 2 frames per picture; 1000 pictures take 33.4 s of wall time (real time). First build: the timer-0 model let a vblank pass on every poll, so the wait took 2 vblanks (3 per picture, 66 % speed). Fixed: a poll is one H-blank, the vblank only comes at the end of the field (`EeRegisters::AdvanceLine`; unit test "RCNT0: a busy-wait on 0x118 H-blanks spans one vblank").
- End: `MenuMovieEnd` first hit at frame 10090 with exactly 4170 pictures (the file has 4170 picture start codes); then page 2 (mission title, slice S4 stub).
- Skip: Start at vblank 2400 → end at frame 2458 (1 s fade), 354 pictures.
- Sound: mixer capture (`--dump-audio --exit-at 3600`): silence before the movie, menu music, then movie audio from 29 s (RMS 1200-5400). PCM layout: 0x200-byte L/R blocks (`../formats/SMV.md`).
- Decode cost: 2.2 ms per picture (libmpeg2 portable C, RelWithDebInfo).
- Regression: the 6 S1 and 4 S2a checkpoints still 0 unexplained (timer model and SOUND reply changes); 62 unit tests pass (new: IPU colour conversion, RCNT0 busy wait).

- User report after the first build: frames flicker, colours break, and vertical lines split the screen when the view moves. Two causes, both fixed (2026-10-14):
  - **Column seams** (renderer): `gs.vert` moves geometry by half a pixel, so a 1:1 sprite's texture coordinate at the pixel centre is an exact integer. Float interpolation gave values like 0.9999, and `floor` picked the texel on the left. In each 16-pixel movie column, texel 0 was shown twice and texel 15 was lost (gradient at column offset 1: 7.8 against ≈21 elsewhere; at offset 0: 26.8). Fix: `gs.frag` rounds texture coordinates to the GS's 1/16-texel fixed point before sampling. After it the column profile is flat (the remaining bumps at offsets 0 and 8 are the MPEG 8×8 block grid). Effect on the validated screens, same frames against PCSX2 (`--blend-fields`): title 0.21/1.52/2.20 (max 15) → 0.16/1.39/2.06 (max 7); main menu 0.43/1.58/2.29 (max 14) → 0.43/1.56/2.25 (max 13). Both closer.
  - **Corrupt pictures at chunk changes** (decoder): open GOPs across chunks (`../formats/SMV.md`); `sceMpegReset` did a full libmpeg2 reset and lost the reference picture. Fix: partial reset. 90 frames around three open-GOP chunk starts (pictures 214, 615, 2115) contain no corrupt blocks (before: over 8,000 saturated-yellow pixels in two pictures at frames 5984-5987). The pictures per chunk equal the file's, and the movie end comes at the same time as before.
  - Regression after both fixes: 10 S1/S2a checkpoints 0 unexplained, 63 unit tests pass.

### S3-2. Movie upscaling stage (ADR-006 §5)
Run: as S3-1 with `--movie-upscale 2` (and 3), `--screenshot-at 3000,5000`.
- ×2: one EASU pass (640×336 → 1280×672), ×3: two passes (log). Output looks clean: no seams at the 16-pixel column edges, colours unchanged.
- The ×2 output averaged back 2×2 matches the native picture best at the letterbox offset y = 56 (mean absolute difference 3.0 levels; 7-14 levels at offsets 52-60). That is the expected EASU edge reconstruction.
- The native screenshots at vblank 5000 are byte-identical with and without `--movie-upscale`: the stage is presentation-only. No Vulkan validation messages.
- Regression after the side-path changes: the 10 S1/S2a checkpoints are still at 0 unexplained. 63 unit tests pass; the new one covers the GIF source marks and `MovieTexelOrigin`.
- RCAS is not applied. Whether it helps is a visual judgement for the User on the stage output.

### S3-3. RAM checkpoints against the PCSX2 batch (run 09, `../pcsx2/sessions.md` run 09)
Run: card S, `--pad-script 1200:start,1500:cross,1700:cross`, expected lists S1, S2a, S2b and the new `ramdiff-expected-s3.csv`.

| Checkpoint | PCSX2 state | Port | Result |
|---|---|---|---|
| 0x00239248 `MenuMoviePlay`, first hit | `s3_ss01_start` | `--dump-ram-at 239248` (frame 1728; PCSX2 12310) | 0 unexplained |
| 0x00238870 `MovieFrameSync` | `s3_ss01_mid` | `--dump-ram-at 238870,668`: hit chosen by movie position, both at 662 pictures / 22.03 s (PCSX2's page counter is at 689: its movie started 22 iterations later) | 0 unexplained |
| 0x002392A8 `MenuMovieEnd` | `s3_ss01_end` | `--dump-ram-at 2392A8` (frame 10090; PCSX2 20714) | 0 unexplained |

- Same SMV reader state, picture count, sceMpeg width/height and chunk position.
- Image buffer: the same picture. Mean difference 0.5 levels, max 10: libmpeg2's IDCT against the IPU's rounding. **Alpha 0x80 on every pixel in both: the hypothesis is confirmed.**
- New expected differences, all without game effect:
  - `sdk`: libmpeg's globals, work area, IPU DMA tags and scratchpad buffers (the port decodes in host memory), the SIF DMA id encoding, and `sceMpeg.frameCount`/`flags` (PCSX2 46/0x19B, port 47/0; the game reads neither).
  - `timing`: the SOUND batch fill and the VAG all-idle volume request at the movie start (IOP latency), the subtitle-font load timer (CD latency), frame-buffer parity, the CD read ticket, and the audio half at the end (silence blocks while draining).
- Pacing: from the mid checkpoint to `MenuMovieEnd`, 7028 frames on both sides, so pictures and the end are frame-exact. Before the first picture the port is 42 frames earlier: CD and IOP latency, the known open question.

### S3-4. Screens
| Screen | PCSX2 | Port frame | Plain: mean R/G/B, > 16 | Notes |
|---|---|---|---|---|
| star field, about 5 s | `s3_ss01_a` | 2036 | 0.83 / 0.73 / 0.99, 0.03 % | best match among frames 1960-2140 |
| TV "LIVE" with subtitle, about 20 s | `s3_ss01_b` | 2960 | 2.45 / 2.20 / 2.50, 2.4 % | the differing pixels are inside the animated TV picture (one movie picture apart), on the picture's top and bottom edge lines (deinterlacing), and in the subtitle |

- Subtitle: same text, position and line breaks; PCSX2's glyph edges are a little crisper.
- Not deinterlace-blended: movie pictures have no fine dot pattern, and `--blend-fields` makes both shots worse.

### S3-5. Movie sound against the PCSX2 capture
`s3_ss01_audio.mp4` (FLAC, decoded by `tools/research/flac_mp4.py`) against the port's mixer output (`--dump-audio --exit-at 4800`). Aligned by envelope correlation, then at sample level.
- Same channel: correlation 0.97-0.995 (L and R, at 2/10/20/35 s). Port L against PCSX2 R: 0.76-0.93. **The 0x200-byte L/R block layout is confirmed.**
- Alignment offset −99 samples at 5 s and −100 at 35 s: no drift, so the rate is exact.
- Level: the port is 1-3 % louder (RMS ratio 1.006-1.031). Not investigated further; within PCSX2's output path differences.

## 2i. Slice S4 checks (2026-10-16; PCSX2 run A of the S4 batch)

Port pad script matching the User's run A: `1200:start,1500:cross,1700:cross,2000:cross,2100:start,2372:cross,10786:start,11400:cross` (superseded by the frame-aligned script of S4-1) (Start skips SS01, Start leaves the map, X on START MISSION). Frame counter 0x0029F3A8: PCSX2 value + 934 = port value at the title, briefing and campaign checkpoints; the map starts 66 frames earlier in the port (PCSX2's briefing stream waits on CD reads; the movie state at map entry is identical).

### S4-1. RAM checkpoints (`ramdiff.py` with `ramdiff-expected-s1..s4.csv`)
Final run 2026-10-16 (S4-7): pad script aligned to the PCSX2 run A frame counters, `--pad-script 700:start,850:cross,950:cross,1166:start,1438:cross,9918:start,10844:cross`, card S, each checkpoint in its own run.
| Checkpoint | Frame (PCSX2 / port) | Unexplained | Notes |
|---|---|---|---|
| title card 0x00230790 | 1232 / 1232 | 0 | |
| briefing movie 0x00238380 | 5094 / 5094 | 0 | |
| briefing map 0x00238440 (hit 1) | 8073 / 8007 | 0 | the port's map starts 66 frames earlier (PCSX2's briefing stream waits on CD reads) |
| campaign menu 0x00230FC8 | 9925 / 9925 | 0 | |
| hangar 0x00235AB8 | 10861 / 10861 | 0 | |
| VS mission select 0x0023C708 | — | 0 | S4-5 |
S1 sub 7 (card S) and the three S3 movie checkpoints rerun with these changes: still 0. Before the alignment (script `1200:start,1500:cross,1700:cross,2000:cross,2100:start,2372:cross,10786:start,11400:cross`) the port's frame counter was 934 frames ahead and the checkpoints had 6/10/8/12/9 unexplained ranges.

### S4-2. Hangar rendering
- VU1 programs B/C (CPU HLE) against the PCSX2 hangar GS dump (`tools/research/gifstrips.py`): per texture the same packet and vertex counts within the camera difference (e.g. 11821 vs 11708 strips for the room/aircraft texture, 6320 vs 6320 for the light-texture pass), the same screen and Z ranges, and mean vertex colours within 1-5.
- The picture matches PCSX2 (aircraft, canopy, floor reflection, room, ceiling lights, panels, texts) after adding the depth buffer and the Z-buffer-as-colour clear (`../platform/gs-subset.md`, S4). Fog (FGE) is not drawn yet.
- Mission select (VS MODE) runs: list, records, thumbnail, help line; checked against PCSX2 in S4-5.

### S4-3. Briefing map frames 2 and 3: VU0 macro mode rounds to nearest (PCSX2 run C)
States `s4_map2`/`s4_map3` (2nd and 3rd hit of 0x00238440), port `--dump-ram-at 238440,2` / `,3`.
- Before the fix the map camera (object 0x00CCFF80 +0x250) differed in 23 words by 1-2 ulp, e.g. the view matrix entry 0x394CE000 (PCSX2) against 0x394CDFFE, 51200 against 51199.996, while its inputs (look-at record +0x410) were identical. The values do not change between frames 2 and 3: the camera is still (map state 2).
- Cause: `_sceVu0ecossin` for the pitch -1.57060099. t = pi/2 + a = 1639 * 2^-23 exactly; the polynomial adds c3 t^3 (-1.2e-12) and smaller terms. With chop rounding each negative term pulls the sum 1 ulp down (0x394CDFFE); with round to nearest it stays t (0x394CE000). A Python model of the code (`out/`, not kept) gives exactly the two values.
- Why nearest although the User's PCSX2 has VU0 Roundmode = 3 (chop): PCSX2 compiles COP2 macro instructions into the EE block (microVU_Macro.inl) with plain SSE add/sub/mul (microVU_Upper.inl `mVU_FMACa`: `SSE_ADDPS`/`SSE_ADDSS`, no operand masking) and never loads the VU0 MXCSR there, so they run under the EE's mode, round to nearest from the GameDB (`eeRoundMode: 0`). Only VU micro programs use the VU rounding setting.
- Fix: `vu0_math.h` macro helpers (`vu0::`) round to nearest; the chop helpers moved to `vu0::micro::` for the VU1 program HLE. After it: the whole map object matches PCSX2 except timing fields; the map packet area (port half A against PCSX2 half B, frame parity) is byte-identical in the terrain sprites: the 803 halfwords that differed by exactly -1 (1/16 pixel) are gone. Left: DMA tags pointing into the other half and the typing text layer (timing).
- Hangar picture at the same vblank before/after the fix: no visible change in the 3D scene (the difference is the menu filter dot pattern and animated gauges, frame phase).

### S4-4. Hangar purchase commit (PCSX2 run B)
User's run: card S, LOAD slot 1, CONTINUE, Start x3 (title card, briefing, map), BUY > AIRCRAFT, X X on the F-5E (52,000; credits 161,000 -> 109,000), Triangle. 0x00233F10 fired on leaving (ra 0x00235C00: the cancel path of `AircraftSelectRun`); `s4_commit_before`/`s4_commit_after`.
- Function test vector (`tests/test_vectors_s4.cpp`, reads the states from `AC04_TEST_STATES`, skipped without it): the entry state's RAM, `CommitHangarSelection(a0 = 0x00CCFF80)`, compared with the exit state over all 32 MiB except the original's stack frame: **0 bytes differ**. The function's writes: game[0] (0x0029F350, 2 -> 1), the purchase state back in save order (h+0x40 and its copy 0x002A0320: F-5E bit set) and the credits 0x002A0D30 = 109,000.
- End to end: the port replays the run (`--pad-script 1200:start,1500:down,1560:cross,1800:cross,1950:cross,2150:up,2200:cross,2600:start,3100:start,3600:start,3800:down,3830:down,3900:cross,4100:cross,4600:cross,4800:cross,5200:triangle --dump-ram-at 233F10`) and reaches the commit with the same purchase state, owned list, selection (h+0xA0..+0xF3), credits, checksum, save copies, game state and campaign selection as PCSX2. Other differences come from the replay's different timing, not from game logic: the hangar timer h+0x164 (7.2 against 18.05 s), viewer camera and object animation, and stale briefing-movie chunks in freed heap (the pad script let the briefing movie run longer before Start). No expected-list entries were added for this run: it is not a fixed-input checkpoint.
- Shop screen after leaving (`s4_shop.png`, BUY highlighted) against the port at vblank 6360 with `--blend-fields`: 0.26/2.20/3.61, 0.44 % over 16, 0.04 % over 32 (pulsing BUY glow, help line typing).

### S4-5. VS mission select (PCSX2 run C)
Port: card S, `--pad-script 1200:start,1500:down,1530:down,1560:down,1620:cross,2300:right --dump-ram-at 23C708`.
- RAM at the first `MissionSelectRun` 0x0023C708: **1 unexplained range** before classification: the TUTORIAL item scale of the main menu object (+0x3AC) still easing from 2 to 1 on PCSX2 (the User's Down presses were faster than the script's 30 frames); added to `ramdiff-expected-s4.csv` (input). 0 unexplained after.
- Screens with `--blend-fields`: `s4_vs.png` against vblank 2180: 0.26/1.45/2.10, 0.21 % over 16, 0.06 % over 32; `s4_vs_right.png` against vblank 2540: 0.21/1.40/1.99, 0.02 % over 16. Left: pulsing button icons, crosshair position, cursor box (animation phase). Same layout, texts, thumbnails (VS STAGE 01/02) and slide animation.

### S4-6. Hangar performance (User report 2026-10-16: about 5 fps)
Measured with temporary stage timers and an in-process sampling profiler (both removed), F-5E hangar, development build. Before: ~200 ms per presented frame (catch-up of ~3.4 ticks): VU1 HLE 15 ms, GIF processing 22 ms, Vulkan recording 16 ms per tick, the last almost all inside the validation layer. Changes (none changes guest RAM or game logic; all verified bit-exact as stated):
| Change | Where | Check |
|---|---|---|
| VU1 micro arithmetic in the host's round-toward-zero mode (MXCSR RZ + FTZ + DAZ, `vu0::micro::ChopScope`, `#pragma fenv_access(on)`) instead of double-precision chop emulation | `vu0_math.h`, `vu1_programs.cpp` | in-run A/B against the former code: every program run of two whole sessions (title to both hangars), output GIF bytes and VU memory identical |
| matrix constants of the model programs read once per strip (re-read per vertex when the backface history at VU 0/1 overlaps them); XGKICK appends a packet at once; `Vu1State::F` inline | `vu1_programs.cpp`, `vif1.h` | same A/B |
| VIF UNPACK fast path (no mask, no ROW mode, not V4-5) | `vif1.cpp` | in-run A/B against the generic loop, both sessions: identical VU memory |
| decoded-texture cache keyed by a content hash of the pages instead of their write generation (the hangar cycles textures through shared GS memory every frame: 723 decodes per frame before, 2 after) | `gs_memory.cpp`, `gs_state.cpp` | same-vblank screenshots identical |
| CLUT loads skipped when the CLUT buffer already holds that palette (raw source bytes compared), and a cache of 8 recent 8-bit palettes (the two passes alternate): 12,420 reloads of 256 entries per frame before | `gs_state.cpp` | in-run self-check (full decode on every hit) over a whole session: 0 mismatches |
| draw state marked dirty only when a register value changes | `gs_state.cpp` | same-vblank screenshots identical |
| GIF step buffers recycled (a hangar step is ~3 MB; it was regrown every tick) | `libgraph.cpp`, `renderer.cpp`, `sce_host.h` | — |
| redundant `vkCmdSetScissor` / `vkCmdBindPipeline` / `vkCmdPushConstants` skipped | `gs_vulkan.cpp` | — |
| Vulkan validation opt-in: `--vk-validation` (development builds; User decision 2026-10-16) | `command_line.cpp` | unit test |
After, per tick: game 5.4 ms (VU1 2.5), GIF 3.3 ms, recording 2.0 ms: a steady 60 fps with ~6 ms of the 16.7 ms frame left. With the validation layer the recording still takes ~11 ms per tick (about 1 µs per Vulkan call; the model and reflection passes alternate per strip with different blend states, so their draws cannot be merged without reordering) and the hangar stays at ~13 fps: validation is therefore off by default and enabled with `--vk-validation`.
Screenshots of different runs cannot be compared for this purpose: host timing reaches the game state (CD and stream timing, `../open-questions.md`), so the camera orbit is at another phase.

### S4-7. The remaining differences (2026-10-16)
- **Input timing (most of them).** The frame counter (0x0029F3A8, counted from boot) was 934 frames higher in the port at every checkpoint: run A's buttons came much sooner after the title screen than the pad script's (the title screen is ready at frame 597 in PCSX2). The menu music therefore played ~15.6 s longer, i.e. ~6-8 more VAG refill reads (one per ~2.4 s), which moved the CD queue-0 tickets, VAG slot 4's ticket and the briefing reader's ticket, and the SS01/briefing movie state words. With the presses moved so that the frame counter matches (title card 1232, briefing 5094, campaign 9925, hangar 10861), those differences disappear; one interleaving of a VAG refill with an SMV chunk read is left (classified `timing`).
- **SOUND transfer ticket (port fix).** Client +0x2C (0x0033E584, the last finished IOP->SPU transfer ticket) was one behind in PCSX2 after every movie. SOUND.IRX: `main_TransBd` (0x135C) starts the transfer and `handler_DmaInt` (0x1300), the SD transfer interrupt, is the only writer of the finished ticket. The movie-audio stop `{src 0, dst 0}` is `sceSdBlockTrans` in mode 2: no DMA, no interrupt, so its ticket is never reported. The SOUND stand-in now does the same (`iop_services.cpp`; unit test "SOUND stand-in: transfer and voice-status records").
- **libmpeg internals (`sdk`).** 0x003FD940 and 0x004CC550-0x004CC55B are read and written only inside libmpeg (Ghidra: `FUN_00276FC8` and its interrupt-handler code at 0x00276E9C-0x00276F84); 0x00FD8300-0x00FD8AFF is libmpeg's decoder block at the start of the briefing movie's work area; the briefing reader's sceMpeg frameCount and flags are the S3 rules at another address. The port's libmpeg works at the call boundary (libmpeg2) and keeps none of this; the game never reads it.
- **0x00410D08 (port fix).** Labelled `g_CdFsState+0xA0` by the tool; it is the SPU RAM limit for sound bank registration (`func_00218A00`). SOUND id 6 (reverb mode) replies with min(ESA core 0, ESA core 1) read with `sceSdGetAddr` right after `sceSdSetEffectAttr` (SOUND.IRX 0xEB8-0xF14), which writes the ESA registers synchronously (LIBSD 0x0900, setter 0x1BA8). The PCSX2 SPU2 state (`SPU2.bin`, core 0 ESA/EEA at 0x211F60, core 1 at 0x213F20, in 16-bit units) shows core 1 in mode 2 at 0x001FE0C0 and core 0 in mode 0 at 0x001DFFF0 in runs 06, 09 and 10, so every reply is 0x001DFFF0; the old 0x001DB7C0 came from core 0 in mode 3 in `scenestep_first_2.p2s`. The stand-in had a wrong mode-2 size (0x24840, derived from that state) and a "read before the command" rule; it now applies the command first and uses libsd's work-area table (LIBSD.IRX 0x4148: mode 2 = 0x1F40 bytes, mode 3 = 0x4840). Unit test "SOUND stand-in: reverb mode reply…".

## 2j. Slice 5 prerequisites

### S5-P1. Determinism (2026-10-16)
- Finding: the only host input that reached guest RAM in the S4 flow was the live pad (analog bytes 0x004121F4/0x00412214 differed between parallel runs: the script was overlaid on the real controller, sampled once per presented frame).
- Change: per-tick pad latching with ticks released one at a time (`main.cpp` `TickInputFeed`), validation runs without live devices, `--record-input` / `--replay-input` (`src/platform/input_log.*`), `--hash-log` (`src/platform/hash_log.*`) with `tools/research/hashdiff.py`, `--stress-host`.
- Results (pad script to the S4 title card and on to vblank 3000, Xbox controller connected): calm run vs `--stress-host` run identical at all 3000 vblanks; recorded run vs its replay identical at all 3000 vblanks. S1 sub 7 and the S4 title-card and hangar checkpoints still 0 unexplained.

### S5-P2. Input-recording calibration (2026-10-17)
- Data: `s5_cal.p2m2` (power-on recording, 2098 frames, `../pcsx2/sessions.md` run 11): Start held in records 1001–1002, X in 1722–1723 (and 1830–1841, after the confirm); breakpoints 0x0022D748 at record 1002 and 0x0022D880 at record 1723; `frame_count` (0x0029F3A8) in the two states 837 and 1558 (difference 721 = 1723 − 1002, so one count per record).
- Port: `--replay-p2m2 s5_cal.p2m2 --dump-ram-at <bp>` with offsets 165 and 153. Offset 165 reaches both breakpoints with `frame_count` 825 / 1546 (12 low); offset **153** gives 837 / 1558, equal to PCSX2. Both checkpoints with offset 153: 0 unexplained ranges (`ramdiff.py --expected` with the S1, S2a, S2b files; remaining timing ranges are the boot-time counters already listed there).
- Result: `--p2m2-offset 153` for power-on recordings: port tick *t* reads record *t* + 153. The 153 frames are PCSX2's BIOS and boot before the game's first vblank minus the port's 12-vblank head start before `frame_count` starts; the pad path adds no extra frame on either side (both accept Start on the record that holds its second pressed frame).
- Limit: valid up to the first load whose length differs between PCSX2 and the port (S4: the briefing map starts 66 frames earlier in the port). Gameplay recordings still need the anchor method of `validation-strategy.md` §3.2, with this offset applied after the anchor.

### S5-A. First mission frames with `--freeze-sim` (2026-10-16, port only)
- Run: card S copy, `--freeze-sim --pad-script 700:start,850:cross,950:cross,1166:start,1438:cross,9918:start,10844:cross,11300:cross,11500:cross,11700:cross` (S4 script to the hangar, X on the aircraft, X on the weapon, X on the loading screen's PRESS START BUTTON). New game mission 1 with the F-4E is mission id 0x25: after sub 8 the scene goes to sub 0x13 (opening sequence), which `--freeze-sim` also covers.
- Correction (2026-10-17, S5-A2): the frame described here is the opening's world (mission id 0x25), not mission 1.
- Visible: the loading screen (01 "SITTING DUCK", objectives, PRESS START BUTTON) and, in the mission, the sky dome gradient over the whole 512 × 448 frame. With the terrain program T and programs A/E implemented (same day) the frame also gets the terrain (333 fans per frame from the start camera, high above the map: a uniform, strongly fogged surface below the horizon) and no "not reimplemented" warning remains. Whether the terrain's texturing and fog match cannot be judged from this camera (the opening sequence's camera is not run under `--freeze-sim`): needs the S5-A GS dump. Effects, HUD, radio and sequence drawers are logged as not translated.
- Bugs found on the way: `func_00138C10` read the `.vudata` qword count as a word (0x60000108 instead of 0x0108), which turned the `ref` tag into an `end` tag and cut the VIF1 chain after the terrain table (no sky, no world); the renderer's frame-as-texture source picked the menus' 640-wide FBP 0 target (`../platform/gs-subset.md`). Both fixed.
- New renderer warnings in the mission (features still missing): destination alpha test (DATE), PABE, COLCLAMP 0, CLUT CSM2, texture PSM 0x5, a reserved PRIM type 7 kick, local→host transfer, a GS blend equation not mapped, VIF1 UNPACK fill mode (WL > CL) approximated, VIF1 code 0x08.
- Not yet done: RAM checkpoints 0x0010A230 / 0x0010BD68 against PCSX2 (`OnGuestCheckpoint` added at both; batch S5-A), GS-level comparison with the PCSX2 gameplay dump.

### S5-A2. Mission 1 load against PCSX2 (2026-10-17)
- Flow found on the way: New Game mission 1 with the default aircraft is mission id **0x25**, an in-engine special (kind 4) with its own load: subs 0–8 load the opening's world, sub 0x13 plays the carrier sequence, and its end (`UpdateInEngineSequence` 0x00177058: `game[0] = 2`, `game[0x27] = 1`, `game[0x2A] = 0`, `RequestScene(2)`) loads mission 1 proper, which reaches sub 0xA (0x0010A230, the PCSX2 state `s5_m1_start`). The narration screen the User saw after the carrier scene is mission 1's loading screen. The frozen frame of S5-A was the opening's world, not mission 1.
- Port run: S5-A pad script plus `11850:start` (`--freeze-sim` keeps the original skip rule in sub 0x13, `command-line.md`), `--dump-ram-at 0x0010A230 --log-allocs`, twice with `--heap-fill 0x00` / `0xFF`; checkpoint at vblank 11978.
- **Allocation exact:** work-heap offset 0x019C5180 (98.4 % of 0x01A318A4) and the retry mark S+0x1664 = 0x012F9480, both equal to PCSX2: every allocation size and its order match for the whole mission 1 load (3,108 live allocations, 27 MB).
- **Heap contents:** 4.79 MB were never written since the scene change (leftovers; masked). Of the written bytes, 8,145 differed in 19 allocations: the AI states' byte +0xF5 and effect pool 19's +0xFC (and one pool-11/12 field), all from `NextRandomFloat`. The random cursor (0x00411780) was 0x22E1 in the port and 0x10A45 in PCSX2: the opening sequence PCSX2 played consumed reads the skipped one did not. Started at the cursor PCSX2 must have had (0x10A45 − 0x1152 reads of the load = 0xF8F3; temporary experiment, removed), **every random-derived byte matched**; the remaining 10 bytes are timing: the double-buffer parity (S+0x1540 bucket pointers, `AllocDoubleBuffer` +0x224), the loading-screen frame counter S+0x166C (PCSX2 350 frames, port 95: CD latency) and S+0x1684.
- **Static areas:** 118 unexplained ranges remain, from the opening's history: the sequence state (0x00410B18, 0x00420730), the terrain holder's per-frame draw state in `.data` (holder 0x002A3110 = *0x00410694: +0x60..+0x6C world window and +0x11C camera cell from the per-view set-up `func_0013A110`, +0x13C draw class, +0x150.. strip bytes, all from the last frame the opening drew), image-like rows at 0x00365F00–0x00385000, and dead stack frames. Also 31 bytes of the holder depend on `--heap-fill`: the frozen opening drew with view-camera words the skipped sequence camera (0x00178DF8) never set. Missions 3 and 4 (no opening; PCSX2 batch S5-A run 2) are the clean check for these areas.
- Tools: `tools/research/heapdiff.py`, `--log-allocs`, `--heap-fill`.

### S5-T2. Mission 1 GS dump through the port's renderer (2026-10-17)
- PCSX2 dump `s5_m1_frame` (zstd-compressed by PCSX2; unpacked for research with Git for Windows' `libzstd.dll` into `out/gs/`, not committed). Statistics (`gsdump.py`, `gifstrips.py`): the port's own mission 1 frame has the same four 3D groups (terrain T8 0x2900 fans, the second context-2 pass with 0x2500, the map GIM 0x3A00, the far triangles with 0x2500), the same primitives and the same mean terrain colour (90 89 90); counts and screen ranges differ with the camera (the port's frozen start camera against a frame 2 s into the flight).
- `--play-gsdump` on the dump first drew the sky, sea and HUD lines but no HUD text and no cloud texture. Causes found and fixed (`../platform/gs-subset.md`): local memory in row-major block order (the HUD font is uploaded as T8 and read as T4), REGLIST PRE ignored, textures read from frame buffers that a transfer had overwritten. After the fixes the frame matches PCSX2's screenshot (HUD text, cyan colours, clouds, MISSION START, radar) except a dark speckled patch near the bottom centre (DATE draws, not implemented).
- Regression: the six S5-T1 screenshots (title to hangar and the opening) are identical to before; the port's own mission 1 frame now shows the island's terrain texture, the sea and the cloud shadow. Open: a black striped band at the top left of that frame (a terrain cell or object drawn wrongly; to investigate).
- Fast-forward cost of the column order: about 2 s over the 11,990-vblank route (CT32 uploads write pixel pairs).

### S5-T1. `--fast-until` (2026-10-17, port only)
- Runs: the S5-A route and pad script (card S copy, `--freeze-sim`), `--hash-log`, screenshots at vblanks 1300, 3000, 6000, 10000, 11000, 11950, exit at 12000: once at normal speed, once with `--fast-until 11990`.
- Result: the RAM hash logs are identical at all 12000 vblanks (`hashdiff.py`); the six screenshots are identical in size and pixels (the normal run took the last one at 11951, a frame with two ticks; the frozen world gives the same picture).
- Time to vblank 11990: about 25 s without `--hash-log` (3 min 20 s at normal speed); the briefing movie (MPEG decode into guest RAM) and the flight frames (about 5.5 ms per tick on the CPU) take most of it.
- Found on the way: what a frame shows depends on how ticks are grouped into frames (the briefing alternates layers between ticks), so the last 60 ticks before a screenshot run one per frame; and the present fallback took its size and its pixels from different FBP-0 targets, so at normal speed the briefing was shown cut to 512 columns (`../platform/gs-subset.md`, fixed).
- Tool: `tools/research/sample_profile.py` (sampling profiler, dbghelp) used to find the cost.

### S5-A3. Missions 3 and 4 against PCSX2; mission renderer features (2026-10-17)
Reference: S5-A run 2, missions 3 and 4 (`../pcsx2/sessions.md` run 12): states `s5_m3_start`, `s5_m4_start` at 0x0010A230, GS dumps `s5_m3_frame`, `s5_m4_frame` (zstd again, unpacked into `out/gs/`), loading and frame screenshots.
- **Routes in the port** (card S copy, `--freeze-sim`): mission 3 `--pad-script 1200:start,1500:down,1560:cross,1800:cross,1950:cross,2150:up,2200:cross,2600:start,3100:start,3600:start,4200:cross,4700:cross,5000:cross,5400:cross` (LOAD file 1, CONTINUE, Start through title card, briefing and map, START MISSION, X on aircraft and weapon, X on PRESS START BUTTON); mission 4 the same with `1650:down,1680:down,1710:down` before the file is picked (file 4, MISSION 04 / BLOCKADE). Both reach 0x0010A230 at vblank 5434 with the game state of the User's runs (mission id 3 / 0x10, aircraft 2, colour 0, weapon 0, mode 1, difficulty 3).
- **Heap** (`heapdiff.py --mask`, fills 0x00/0xFF): mission 3 3,171 live allocations (27.1 MB), mission 4 4,345: allocation sizes, order and addresses equal; differing bytes only in the packet double buffers (loading-screen packets, frame phase), the flight scene's loading counters (+0x166C..+0x1687) and effect-block records that add the effect clock (block +0x6249C).
- **Effect clock (leftover read by the original).** `InitEffects` reads the clock before anything in the scene writes it (`../subsystems/mission-flow.md`), so record start times include whatever the memory held: PCSX2 0x80000000 (mission 3), 0x80312B1F (mission 1), 0 (mission 4); the port 0x808D5853 (mission 3) — RGBA pixels with alpha 0x80 left by the briefing movie's picture buffer, which depend on the moment the briefing was skipped. Listed as `leftover` for the mission 3 layout. An exact match needs the same skipped picture (pad timing and IDCT rounding, S3-3).
- **Static areas:** the ranges left in S5-A2 (terrain holder draw state, sequence state, image rows) do not occur in missions 3 and 4: they were the opening's history. Remaining differences, all classified in the new `ramdiff-expected-s5a.csv`: briefing state at the skip and the movie audio feeder's fade (input timing), VAG slots 2/4 and the VAG command queue (the loading screen's length: CD latency), libkernl internals, newlib's stdio buffer after the work heap (debug printf text), deeper dead stack frames. **0 unexplained ranges** for both missions outside the scene heap, with the expected lists S1, S2a, S2b, S3, S4, S5a.
- **Black screen before gameplay** (User's `*_loading_2.png`): the loading screen fades to black after Start, 0x0010A230 runs on the black frame and the mission fades in over about 30 frames (port: Start at 5400, black at 5430, checkpoint 5434, picture from 5440). The PCSX2 screenshot was taken while paused at the breakpoint.
- **GS dumps through the port's renderer** (`--play-gsdump`, compared with the User's screenshots, `imgdiff.py --blend-fields`). New tool `tools/research/gsdraws.py` (every draw with its state and screen box). Missing features found and implemented (`../platform/gs-subset.md` 2026-10-17): fog (FGE), TEX1 filter choice with trilinear mipmaps (MTBA), DATE, AA1 blending, frame buffers read and written in other formats or widths (compute conversion and target sync), a frame buffer drawn earlier in the same frame as texture source, presented height at most 448 lines.

| Dump | before (mean R/G/B, % over 16) | after |
|---|---|---|
| mission 1 | 9.7 / 10.9 / 14.2, 40.7 % | 1.5 / 1.8 / 2.0, 1.0 % |
| mission 3 | 18.8 / 18.0 / 16.4, 41.8 % | 4.0 / 4.4 / 4.3, 5.8 % (camera moved between the dump and the screenshot: edge differences only) |
| mission 4 | 11.9 / 12.8 / 16.0, 18.2 % | 1.2 / 1.5 / 1.8, 2.2 % |

- The black striped band of the port's own mission 1 frame (S5-T2) is gone (it was the island drawn without fog, mipmaps and the cloud-cover texture). The S5-T1 screenshots at 1300, 3000, 6000, 10000, 11000 are pixel-identical to before; 71 unit tests pass.
- Left: the HUD's AA1 edge coverage, the GS's integer blend rounding, PABE, COLCLAMP 0, CSM2, partial FBMSK (none visible in these frames).

### S5-T3. `--mission` and the parallel regression suite (2026-10-17, port only)
- `--mission <n>` (`command-line.md`): all 18 campaign missions started from card S with `--freeze-sim --pad-script 800:cross,...`: 17 reach 0x0010A230 at vblank 834 with the expected game state (mission id from `GetMissionId(0, n)`, aircraft 2, mode 1, difficulty 2, entry kind 1) and draw their world from the start camera with no renderer warning. Mission 12 (id 0x0A) stops at its first sub-state: the scripted sequence 0x0010B500 (block C) is not translated yet (logged as an unregistered indirect call).
- `tools/research/runmany.py` with `tools/research/regress.json` (24 jobs: front-end route screenshots, the mission 1/3/4 routes with their checkpoints, calm vs `--stress-host` hash logs to vblank 6000, the 18 missions): 111 s with 8 instances in parallel on the development PC. A second run is pixel-identical to the saved baseline for every screenshot, and the calm and stressed hash logs are identical at every common vblank.

## 2k. Internal resolution, 16:9 and code clean-up (2026-10-19)

Reference: the User's Slices folder (S1-S5 PCSX2 screenshots and GS dumps). Tools: `tools/research/full.json` + `runmany.py` (logic oracle), `pcsx2_compare.json` + `pcsx2_compare.py` (screens).

### IR-1. Logic oracle: RAM and hash logs at every scale
- Baseline saved from the build before any change of this session (`out/runs/baseline_full`, not committed): 30 jobs without `--freeze-sim` (S1 card S / card E / no card, S2a, SS01, the S4 front end to the hangar, VS select, the shop commit, the mission 3 and 4 routes, a replayed gameplay run on mission 4, the mission 1 opening, all 18 missions): 47 screenshots, 27 RAM checkpoints, 5 per-vblank RAM hash logs (up to 12,000 vblanks).
- A second run of the unchanged build: identical RAM and hashes; screenshots identical except where the shot was taken at normal speed (one vblank late), so every job now uses `--fast-until` up to its last shot.
- Final build at internal scale 1: every RAM checkpoint and hash log identical, every screenshot identical (the loading fade may differ by up to 6 levels with host presentation timing, `--tolerance 8`). At 2, 4 and 8: every RAM checkpoint and hash log identical to the 1x baseline, the same renderer warnings, no errors. **The internal scale changes no guest state.**
- Known, dev tool only: with `--fast-until` the frames before the 60-tick settle window are not drawn, so which frame buffers the renderer has seen depends on host timing; a shot in the mission 1 opening can show the 224-line display buffer (2 of 6 runs). RAM is unaffected; normal-speed play draws every step.

### IR-2. Screens against PCSX2 (Slices folder)
Best match per reference over a vblank window (`pcsx2_compare.py`); at N > 1 at the 1x best vblank, with the port's N x N box average (`native_<vblank>.png`). Mean R/G/B difference:

| Screens | 1x | 2x | 8x |
|---|---|---|---|
| S1 splash 0/1, card messages (5) | 0.01-0.09 | 0.4-0.9 | same as 2x |
| S2a title, menu, NEW GAME (3) | 0.2-0.7 / 1.4-1.6 / 1.9-2.7 | 1.0-1.6 / 3.1-3.6 / 4.2-5.1 | same |
| S2b pages incl. LOAD, CONTINUE (7) | 0.1-0.5 / 1.5-2.0 / 2.0-3.2 | 0.6-3.7 / 3.8-6.3 / 5.4-7.7 | same |
| S2b 16:9: SCREEN RATIO, menu, CONTROLLER SETUP (3) | 0.3-0.5 / 1.9-2.6 / 2.9-5.0 | 1.2-4.9 / 3.9-8.4 / 5.7-10.6 | same |
| SS01 (2), mission 1 movie | 0.4-0.9 | 1.1-4.0 | same |
| S4 title card, campaign, shop, VS (5) | 0.1-0.3 / 1.1-2.1 / 1.7-3.4 | 0.9-2.7 / 2.6-5.0 / 3.5-6.1 | same |
| S4 briefing map | 2.1 / 3.1 / 3.5 | 4.3 / 6.1 / 6.7 | same |
| S5 loading screens m3/m4 | 1.2 / 4.1 / 5.6 | 1.2 / 3.8 / 5.1 | 1.0 / 3.5 / 5.0 |
| S5 GS dumps m1/m3/m4 (`--play-gsdump`) | 1.2-4.0 / 1.5-4.4 / 1.8-4.3 | 1.7-4.4 / 3.2-5.7 / 3.1-5.5 | same |

- 1x equals the earlier slice results (S1-2 to S5-A3): nothing regressed. The G/B bias is the open menu-filter rounding question (S2a-4).
- Not matched by timing (as before): the hangar (orbit camera phase, 10-12 levels) and the briefing map's second frame (map animation phase, `s4_map`, 2-10 levels).
- At N > 1 the means rise by 1-5 levels: lines one native pixel wide are N host pixels (the average blurs their edges less), the menu filter's feedback blur samples the scaled buffer (the fine dot pattern averages out), and movies come from the FSR picture. 2x, 4x and 8x give the same numbers within 0.3, so the scaling is consistent. Visually checked: flight HUD and terrain, menus, CONTROLLER SETUP, SS01 at 2x/4x (no seams between movie columns or atlas cells).

### IR-3. 16:9 (the game's SCREEN RATIO option)
- Before: `Renderer::SetDisplayAspect` was never called; the port always presented 4:3.
- Now the main loop reads ctx flag 0x10000 after each frame. Run (card S): LOAD slot 1, OPTIONS > SCREEN SETTINGS > SCREEN RATIO > 16:9 (`pcsx2_compare.json` run `s2b_169`): the log shows `display aspect 16:9` when the option is confirmed; the output pass then fits a 16:9 rectangle (`FitAspect`). The anamorphic menus and CONTROLLER SETUP match PCSX2 (IR-2).

### IR-4. Performance (development build, RX 9070 XT)
- Sampling profile of mission 4 gameplay: texture decoding (`GsState::DecodeTexture`, one call through `PixelAddress`/`ReadPixel` per texel) was the largest item, 18 % of all samples (the game simulation about 5 %). `GsMemory::ReadRect` (one loop per format, inline addressing) brought it to 8 %; unit test `ReadRect gives the ReadPixel values for every format`, and the 1x screenshots are pixel-identical.
- Normal speed, hangar route to vblank 12,000: 0 dropped ticks at 1x and at 8x. Mission 4 with a replayed recording: the same 23-24 dropped ticks at 1x and 8x, right after the fast-forward, so not from the scale.

## 2l. Opening fixes, port menu features, caches and leaks (2026-10-20)

User requests of 2026-10-20 (the opening's black aircraft and corrupted frames; performance and caching; clean-up; GRAPHICS page; Modern control types; settings rules; memory leaks). Port features as built: `port-features.md`.

### OP-1. Mission 1 opening: corrupted frames (game-code translation error)
- Seen: about a third of the opening's frames black with a strip of garbage pixels near line 300, and coloured speckles over the sea in the last carrier shots. `gsdraws.py` on a `--dump-dma-at` capture: after a deck-crew strip, 1,747 garbage sprites (TEX0 TBW 53, PSM 0x6); the GIF stream lost sync there, so the field copy at the end of the frame never came.
- Cause: `func_002206C0` (sequence character parts' GS register list) writes the A+D GIF tag at part +0 (`sd v0,0(a0)` at 0x002208AC, NLOOP = count − 1); the translation had it at +0x70. `func_002238E0` copies the list from +0, so the GS read the stale qword at +0 as a tag.
- After the fix: no garbage draws, every frame of the opening presented, the deck crew textured. Guest RAM changes where the opening runs (the character parts and the packet buffers that copy them, heap 0x5F0000–0x630000 and 0xDB0000–0xDD0000): the translation now writes what the original writes.

### OP-2. Mission 1 opening: black F-4E (renderer)
- The player aircraft's texture is T8H at block 0xE00 = FBP 0x70 × 32; the opening draws RGB into FBP 0x70 with FBMSK 0xFF000000, so the renderer took the aircraft's texture from that target, whose top byte never held the indices (`../platform/gs-subset.md`, "Textures in the bits a frame buffer never writes"). Vertex colours (mean 118 per channel) and the light set (ambient 0.48) were fine.
- After the fix the F-4E has its blue camouflage in every carrier shot; no other screen of the regression suite changed (OP-4).

### OP-3. Performance, memory and leaks (development build, RX 9070 XT)
- Sampling profile, mission 4 with the replayed recording, normal speed: main thread 19.8 % → 15.9 % of samples, GIF processing 15.2 % → 10.6 %, `DecodeTexture` 8.4 % → 5.2 %. Cause: the terrain textures (T8 512 × 512 with 3 mip levels at 0x2900) are rebuilt every frame from tile uploads; every change re-decoded the whole texture (11,839 decodes in 4,500 vblanks). Now only the 256-byte blocks written since the last decode are decoded again; unit test `re-decoding only the written blocks equals a full decode` (T8, T4, CT32, unaligned texture, patches across blocks, outside the texture, and a mostly rewritten one).
- Memory: the decoded-texture cache was bounded by 4096 entries only, and the terrain made a new 1.4 MB version almost every frame: private memory climbed to about 6 GB within a minute of flight before the cache emptied. Now bounded at 256 MiB as well: 400–740 MB over the same run (unit test `the decoded-texture cache stays within its byte budget`).
- Dropped ticks in that run: 35–37 → 0. The backend's per-frame lists (texture placement, uploads, vertices) are reused instead of allocated every frame.
- Leaks: no raw `new` / `malloc` in host code; every file opened is closed; a session with `--vk-validation` (menus, the GRAPHICS page changing the internal resolution, clean exit) reports no Vulkan error, including no objects left at device destruction. New Debug-build tests with the CRT debug heap (`tests/test_leaks.cpp`): settings store and GS state allocate nothing that outlives them.

### OP-4. Regression suite
- `full.json` against the baseline of 2026-10-19 (`out/runs/baseline_full`), 1x: 27 of 30 jobs identical (screens, RAM checkpoints, hash logs). Different, all explained: the opening (OP-1 and OP-2: screens and hash log), `s4_frontend` (its last 268 vblanks are the opening: same heap ranges; its opening screenshot shows the fixed aircraft), `m3_route` (one pixel column of the loading fade, at most 6 levels, also in the 2026-10-19 runs: host timing, `--tolerance 8` in IR-1).
- New baseline from this build: `out/runs/baseline_s9` (not committed).
- At 8x against the new baseline (`--shots-only`): every hash log and 29 of 30 RAM checkpoints identical. The other one, `s1_cardE`, is the only job without a pad script, so it reads live devices: 8 bytes of pad 1's raw and converted analog values (0x004121F4, 0x00412214; a connected gamepad's stick at 0x78 / 0x7D instead of the centred 0x7F, the input class of `ramdiff-expected-s1.csv`), not the scale.
- PCSX2 screens (`pcsx2_compare.py`, runs `s2b` and `s2b_169`): every S2b page as before (IR-2) except the two pages that changed on purpose: SCREEN SETTINGS 0.33 / 2.19 / 3.43 (the extra GRAPHICS line) and CONTROLLER SETUP 1.41 / 2.67 / 3.32, 16:9 2.07 / 4.14 / 6.34 (the two extra squares, the longer CONTROL TYPE name and, with Modern, the swapped shoulder rows).

### OP-5. Port menu features (port only)
- GRAPHICS (SCREEN SETTINGS, after HUD UNIT): opened with card S, rows typed out as the BRIGHTNESS view, values changed with left/right; ACCEPT applied 4x at once and wrote `internal_scale: 4` to `settings.json`; CANCEL left the file unchanged; reopening shows the stored values and the list returns with the cursor on GRAPHICS.
- CONTROLLER SETUP: four CONTROL TYPE squares; left from NORMAL (MODERN) runs EASY (CLASSIC), NORMAL (CLASSIC), EASY (MODERN) and back, the game's type bit stepping as over the original two squares; the table shows the swapped shoulder rows with Modern; ACCEPT wrote `modern_controls: [false, true]`. Unit test for the swap (`modern control types: L1/L2 and R1/R2 swapped, buttons and pressures, nothing else`).
- `settings.json`: a file with an invalid value, an unknown key, another version or broken JSON is rejected and replaced with the defaults (unit tests in `test_settings.cpp`).

## 2m. Settings, texture residency, sprite edges at N×, frame-rate overlay (2026-10-20)

User requests of 2026-10-20 (second batch): texture loader, settings defaults and immediate application, the frame-rate overlay, tests of the graphics and controller settings and of saving, the deck crew of the mission 1 opening. Port features as built: `port-features.md`. Runs under `out/cc/` and `out/runs/cc_x1`, `cc_x4` (not committed). Display of the test machine: 3840 × 2160 at 60 Hz (so the defaults are fullscreen with v-sync on), RX 9070 XT.

### ST-1. GRAPHICS page in the menus (normal speed)
- Route with card S: OPTIONS > SCREEN SETTINGS > GRAPHICS (`--pad-script` …2550:cross,2750:up,2900:cross; up from the first item wraps to GRAPHICS). Then every row was changed, CANCEL, every row changed again, ACCEPT, reopened, DEFAULT, CANCEL.
- Each change applied at once (log: `presentation: …` per change, `window …`, `swapchain …, present mode …`): fullscreen 3840 × 2160 ↔ windowed 1280 × 960; v-sync on = FIFO, off = IMMEDIATE (this driver has no MAILBOX in that mode); internal scale 1 → 2 → 3; filter sharp → smooth.
- CANCEL restored the values the page opened with (all four at once); ACCEPT wrote `settings.json` (`windowed`, `vsync false`, `frame_cap 240`, `internal_scale 2`, `smooth`); DEFAULT applied the display defaults (fullscreen, v-sync on, 1×, sharp) and the next CANCEL went back to the saved values.
- With v-sync on, the FRAME RATE CAP row shows "V-SYNC" without squares and the cursor skips it; with v-sync off it shows its value and squares (screenshots 3150, 3300).
- Presented rates per period (`previous presentation: …` lines): v-sync 60.2 fps; cap 360: 355 fps; cap 240: 239.7–240.2 fps; 3× with cap 240: 230.8 fps.

### ST-2. Combinations during gameplay (normal speed)
Mission 4 with the replayed PCSX2 recording from vblank 1500; `settings.json` rewritten every 7 s while the port ran (the watcher reloaded each version within a second).

| Combination | Presented | Ticks dropped |
|---|---|---|
| defaults (fullscreen, v-sync, 1×, sharp) | 59.0 fps (period includes the fast-forward) | 0 |
| fullscreen, v-sync, 4×, smooth | 59.2 fps | 5 |
| fullscreen, v-sync, 8×, sharp | 54.8 fps | 35 |
| windowed, cap 144, 2×, smooth | 142.1 fps | 0 |
| windowed, cap 360, 8×, sharp | 323.4 fps | 31 |
| fullscreen, cap 60, 3×, smooth | 59.3 fps | 1 |
| windowed, v-sync, 6×, sharp | 57.4 fps | 16 |
| fullscreen, cap 360, 1×, sharp | 357.1 fps | 0 |

The dropped ticks are one hitch at the switch to a higher internal scale (render targets rebuilt, grown on the first frames with a device wait), not a lower sustained rate: 385 presented + 35 dropped = 420 = 7 s × 60. 8× windowed still presents 323 fps. Session total 88 dropped ticks, all at switches.

### ST-3. Controller settings and the save data
- CONTROLLER SETUP (card S, after LOAD): NORMAL (MODERN) → EASY (MODERN) → NORMAL (CLASSIC); the button table follows (Modern: L1 YAW LEFT / L2 THROTTLE DOWN; EASY: the yaw rows "------"; Classic: the original rows); REVERSE ON swaps PITCH UP / PITCH DOWN. ACCEPT wrote `modern_controls: [false, true]` to `settings.json`.
- Then CONTINUE > campaign menu > SAVE > slot 01 > overwrite YES: "Save complete". The card file `BASLUS-20152AC04` differs from the card before in exactly two places: player 1's controller word (slot 1 +0x12B8: 0x0008 → 0x000A, REVERSE bit) and the checksum at +0x0C, which is valid. The Modern flag never reaches the card.
- A new session with the same card and `settings.json`: LOAD, then CONTROLLER SETUP shows NORMAL (CLASSIC), REVERSE ON.

### ST-4. Texture residency (performance)
Renderer summary at exit (averages per drawn frame, `--fast-until` part included), same build with residency switched off for the "before" column (a temporary switch, removed):

| Run | Recording ms (before → after) | Textures uploaded per frame | Bytes uploaded per frame |
|---|---|---|---|
| mission 4 replay, 1× | 0.26 → 0.10 | 74 → 0.13 whole + 0.01 in place | 108 KiB → 1 KiB |
| mission 1 opening, 1× | 1.53 → 1.39 | 14.2 → 0.19 | 3,165 KiB → 18 KiB |
| mission 1 opening, 4× | 1.54 → 1.36 | 14.4 → 0.19 | 3,103 KiB → 18 KiB |

GS processing (decoding the game's GIF stream and textures) is unchanged at 0.48 ms (mission 4) and 3.7 ms (opening). In the opening most of that is the game's own per-frame image uploads into GS memory (`GsMemory::WriteImage`) and primitive building; no atlas reset in any run. Unit test: `re-decoding only the written blocks equals a full decode` now also checks that the changed rectangles applied to the previous decode give the new one.

### ST-5. Seams at internal scale 2× and above (renderer fix)
- Seen in the mission 1 opening at 2× and 4× (and in flight): dark vertical lines every 32 native pixels over the whole picture below line ~30. `gsdraws.py --verts` (new option) on a capture of vblank 1700: the glow pass's upsampling draws 2836/2837 are 16 sprites 32 pixels wide that end at x = 31.5 and start at x = 32.0. The GS draws pixel 31 with the first and pixel 32 with the second (sample points at whole coordinates); at N× the host pixels between 31.5 and 32.0 belong to native pixel 32 but neither sprite covered them.
- Fix (`renderer.md` §1 fix 9): at N > 1 sprite edges move to the borders of the native pixels they cover. After the fix the 4× opening has no seams.
- Regression: `full.json` at 4× (`--shots-only`) against `baseline_s9`: all 30 jobs identical in RAM checkpoints and hash logs.

### ST-6. Regression at 1×
`full.json` against `out/runs/baseline_s9`: 29 of 30 jobs identical (screenshots, RAM checkpoints, hash logs). `m4_route` screenshot 5300 differs in one pixel column (x = 409, rows 20–419, at most 12 levels): the loading-fade column of IR-1 (host timing). `m3_route` logged "texture atlas full for this frame" once; the same warning is in earlier runs of that job (`full_check`, `full_s9_x8`) and depends on how ticks were grouped into frames, not on the residency (a frame whose own textures do not fit would have failed before as well).

### ST-7. Defaults, SCREEN RATIO and the frame-rate overlay
- Start-up log: `display: 3840x2160, 60 Hz (wider than 4:3)`; a new `settings.json` gets `fullscreen`, `vsync true`. Unit test `settings: defaults follow the display` covers 50/60/75/120/144/165/240/500 Hz and unknown rates, and the wide-display rule.
- Fresh user data with the port's own saves folder (no save) and live input: `SCREEN RATIO default: 16:9 (monitor wider than 4:3)`, `display aspect 16:9`; the no-save intro runs in the game's 16:9 mode. Every validation run above logged `SCREEN RATIO default: 4:3 (validation run: PS2 default)`.
- `--fps-overlay` in fullscreen 4:3 (desktop capture of the top right corner): "59 FPS / 17.00 ms" in small white text in the pillarbox; the game image and screenshots are unaffected.

### ST-8. Deck crew in the mission 1 opening (User report)
- With this build the deck crew is drawn in every carrier shot of the opening at 1×, 2× and 4× (`--mission-id 0x25`, screenshots every 50 vblanks from 850 to 2400), and in the New Game route's opening (`s4_frontend` screenshot 11950). The crew was missing or garbage before OP-1 (the GIF tag of `func_002206C0`, fixed earlier on 2026-10-20), so the User's observation probably came from a build before that fix. A PCSX2 reference of the opening (S5-C batch, `../pcsx2/procedures.md`) is still needed to compare shot by shot.

## 2n. Anti-aliasing: FXAA and SMAA before the HUD (2026-10-20)

User request: FXAA and SMAA if their licences are GPL-3.0 compatible, with the HUD drawn after the anti-aliasing pass. Licences: SMAA MIT; FXAA 3.11 BSD-3-Clause in NVIDIA's 2014-2015 GraphicsSamples release (the older 2010-2011 header grants no licence and is not used); both compatible (`../decisions/dependencies.md`). Feature as built: `port-features.md` §2.

### AA-1. Where the pass runs
- Mission 4 and the mission 1 opening: the point is built once per view (`BuildFlightDrawLists`, overlay table tag 0x006133B0 / 0x00629E30, rectangle 0 0 511 447), the DMA walker marks the GIF data there (offset 1,491,696 / 1,362,592 of the step), the draw list gets the `aa_point` and the backend finds the target FBP 0, FBW 8, CT32 (temporary log lines, removed).
- HUD: a crop of the mission 4 HUD (GUN / MSSL / UGBM / DMG) is pixel-identical with `off`, `fxaa` and `smaa`: the HUD is drawn after the pass.
- Unit tests: `anti-aliasing point: a mark becomes a draw-list entry between the 3D and the HUD` (position, FRAME_1, rectangle, no merging across the point; a movie mark adds nothing), `settings: anti-aliasing` (values, round trip, an invalid value rejects the file).

### AA-2. Effect
- Pixels changed against `off` (1×): mission 4 frame 2500 (sky, terrain, HUD): FXAA 1,533, SMAA 33 of 229,376; the opening frame 1700 (carrier deck, F-4E, crew): FXAA 15,969, SMAA 4,245. FXAA softens edges and some texture detail; SMAA smooths the stair steps of geometric edges and leaves the rest (zoomed crops, `out/cc/aa/`). At 1× the opening is already soft from the game's glow pass, so the gain is small; at 4× SMAA visibly cleans the strut and gear outlines.
- 4× with `--vk-validation`, FXAA and SMAA: no Vulkan validation message.

### AA-3. Regression
- `full.json`, anti-aliasing off (default), against `baseline_s9`: 29 of 30 jobs identical; `m4_route` screenshot 5300 differs only in the loading-fade column x = 409 (at most 9 levels, ST-6 / IR-1).
- `full.json` with `--antialiasing smaa` (`--shots-only`): every RAM checkpoint and hash log identical in all 30 jobs: the pass changes presentation only.

### AA-4. Movies are not anti-aliased (User rule)
- SS01 (New Game route with card S, screenshots at vblanks 2036, 2960, 3500), `--antialiasing smaa` against `off`: native screenshots identical at 1×; at 2× the internal-resolution screenshots, the native ones and the FSR 1 stage output (`movie_x2_*`) identical. The movies get only the FSR 1 stage.

## 2o. New memory card, first save, startup messages (2026-10-20)

User requests: create a new save from scratch when there is none (compatible with the PCSX2 folder format), make sure the SMV movies are not anti-aliased (AA-4 in §2n), clearer pop-up messages.

### NS-1. A new card and the game's first save
- Fresh user data (no `saves` folder): log `no memory card in …\saves: a new one was created (PCSX2 folder card, 8 MB, formatted)`; the folder holds `_pcsx2_superblock` (0x2000 bytes).
- Superblock: bytes 0x000–0x153 equal those of the User's two PCSX2-formatted cards (`Memory Card Folder`); the rest differs (their leftover block 0 data, not read by PCSX2, `../formats/folder-memcard.md` §9).
- New Game route (`--pad-script 900:start,1050:cross,1200:cross,2200:start,2900:start,3600:start,5200:start,6800:start,7400:start,7800:down ×5 (30 apart),8000:cross,8300:cross,8600:cross,8900:cross,9200:cross`): campaign menu, SAVE, "No AC04 files found on memory card (PS2). Create save file?" YES, File 1, "Save to File 1. Is this OK?" SAVE, "Save complete".
- Files: `BASLUS-20152AC04/` with `icon.sys` (964 bytes) and `icon1.ico` (52,120) byte-identical to the PCSX2-made save, `BASLUS-20152AC04` (19,216; magic `AC04`, size 0x4B10, checksum valid) and `_pcsx2_index` in PCSX2's layout and write order (`icon.sys` 1, `icon1.ico` 2, `BASLUS-20152AC04` 3, `$ROOT` times).
- New session on that folder: LOAD shows "MISSION 01 / SITTING DUCK, NORMAL", load from File 1, CONTINUE goes to the mission 01 title card.
- Unit test `folder card: a new card made from scratch has PCSX2's format fields`.

### NS-2. Startup messages
- Fresh user data without a disc image: the box "AC04 Port - Disc image" shows "No disc image selected", the explanation, "What to do:" with two numbered steps, the supported disc and the settings file, buttons **Select ISO...** and **Quit** (desktop capture; an earlier, longer button label was cut off by the Windows message box and was shortened).
- Select ISO... (Enter), the path typed into the file picker: log `disc image chosen: …`, `settings.json` `paths.iso` set, the SHA-1 computed (17.9 s), a new memory card created, the game ran to vblank 400 and exited cleanly.
- Unit test `disc image messages: headline, steps and details in one layout`.

## 2p. Engine flames off the nozzles (2026-10-21)

User report with a PCSX2 screenshot: in the mission 1 opening (mission id 0x25, the close-up of the F-4E nozzles before the launch) the afterburner flames were drawn far from the nozzles.

### EF-1. Cause and fix
- The flames are pool +0x22C30 flash records (`func_001E43D8`) placed by `UpdatePlayerTrails` 0x00126970 at the engine nodes 0x42 + e: point = body rotation × (node matrix × `one`) + unit position, with `one` = vf0 = (0, 0, 0, 1).
- Port bug (stack layout, not a game behaviour): the node matrix of the engine loop was at frame +0x70, so its translation row overlapped `one` at +0xA0. `func_00149EC8` overwrote `one` with the node translation t, and the point became R·t + t, about twice the nozzle offset. Logged values at vblank 1950: nozzle offsets (7.2, −2.7, −19.6) and (17.1, −2.7, −12.0) instead of (3.56, −1.80, −9.77) and (8.51, −1.80, −5.97).
- Fix: the matrix moved to frame +0xD0 (`src/game/player_flight.cpp`; the original keeps it at sp+0x140, apart from vf0 at sp+0x150).
- Result: `--mission-id 0x25 --mission 1 --pad-script 800:cross --screenshot-at 1950` shows the flame discs inside both nozzles and the haze trailing behind, as in the PCSX2 screenshot. The same path runs in gameplay for every player aircraft (external views), so the flames there move back to the nozzles too.
- Guest RAM: the flash record positions (effect block +0x22C30) now differ from earlier port runs. Earlier `--hash-log` baselines that cover flames need to be recorded again.

## 2q. Hands-on run from zero and debug overlay test (2026-10-22)

User request before the first release (branch `test`): play from zero to mission 1 gameplay at normal speed and test every debug overlay function and its user experience. Development build (RelWithDebInfo), fresh `--user-data`, no other parameters; 3840×2160 60 Hz display (fullscreen, v-sync, 1×), AMD RX 9070 XT; Xbox One controller connected but unused. Driven with real keyboard and mouse input (Windows `SendInput`), observed through desktop captures (`out/ux/`, not in the repo).

### UX-1. Route from zero (passed)
- First start: "No disc image selected" box, **Select ISO...**, file picker, image chosen and verified (SHA-1 computed in 7.8 s), new memory card created, game boots.
- No-save intro (mission id 0x23), title, NEW GAME, NORMAL, opening movie (played to the end), story slides, mission 1 briefing, campaign menu, SAVE to a new file (create, File 1, "Save complete"; folder card files as NS-1), START MISSION, F-4E, weapon, PRESS START, the mission 1 opening (0x25), gameplay: HUD, radio, timer, keyboard flight. A crash into the sea (aircraft left banked with no input) led to the replay and the RETRY / QUIT MISSION / RETURN TO TITLE menu.
- Second start on the same user data: no box, `verified … (cached result)`, LOAD File 1, CONTINUE, mission 1 title card, briefing (skipped with Start), hangar, gameplay. Both sessions ended with `clean exit`.

### UX-2. Debug overlay functions
| Function | Result |
|---|---|
| `\` toggle (open/close) | works, **until an ImGui text field has been used** (UX-3 a) |
| Panel checkboxes, × close, collapse, move, resize, wheel scroll | work; × unticks the checkbox |
| Performance | values live; FPS overlay checkbox works (session only, by design) |
| Scene | scene/sub-state/flags/counters/mission id correct through intro, menus, briefing and flight; request log scrolls |
| Memory | hex address, `0x` prefix, symbol name (`g_MissionNo`), "not found" for an unknown name, `-- not RAM/scratchpad --` past 32 MB and at 0x12000000, scratchpad at 0x70000000, bytes/u32/f32, ±0x100 |
| Session log | lines, colours, filter |
| Keyboard routing | a focused panel (also right after opening) takes the keyboard; clicking the game image gives it back, then flight keys reach the game with the panels open |

### UX-3. Problems found (a–g fixed 2026-10-22, UX-5)
a. **Toggle key dead after using a text field.** `imgui_impl_sdl3` calls `SDL_StopTextInput` when an ImGui text field loses focus (backend lines 191–193); the port starts text input only once (`src/platform/window.cpp:28`) and the toggle listens to `SDL_EVENT_TEXT_INPUT`, so after typing in the Memory address or log filter field `\` no longer opens or closes the overlay until restart. Reproduced twice. Fix idea: start text input again after each ImGui frame when it is off.
b. **No DPI scaling.** At 3840×2160 the panels use the 13 px default font (about 1/5 of the screen width for all four panels); the FPS overlay text is equally small. No `FontGlobalScale`/`ScaleAllSizes` call in `overlay.cpp`.
c. **Default panel layout overlaps**: the Session log (8, 420) covers the bottom of Performance and the left of Memory (360, 330).
d. **Truncated columns at default sizes**: Performance lines ("worst 24.36 m…", the texture KiB), the Scene request column (`RequestSubSt…`, the argument only shows after widening), the Memory label column (`g_MissionNo` without the `+offset`); the Scene request table keeps about 8 rows when the window is made taller.
e. **Log filter is case-sensitive** ("warn" finds nothing, "WARN" finds the warnings).
f. Memory: rows after a small global are all labelled with it (`g_MissionNo+F8` …), because `symbols.csv` has no sizes; the looked-up address is not highlighted. Mission id is shown in decimal (35 for 0x23).
g. Opening the overlay gives a panel keyboard focus, so Start/flight keys go to ImGui until the game image is clicked (UX-2 last row): surprising while playing.

### UX-4. Other observations
- **Renderer warnings in the no-save intro (both sessions):** `texture atlas full for this frame (draws skipped)` and `GS blend equation not mapped (normal alpha blend used)` during the seagull shots; there the Performance panel showed 40–50 fps, GS CPU 34–37 ms, atlas resets rising. Frame times measured during desktop captures are inflated, but the atlas overflow is logged by the renderer itself. Worth a look before the release.
- First-start polish: the ISO box uses the error icon and the log records `ERROR iso No disc image selected` for the normal first start; while the SHA-1 runs (about 8 s) the window shows the milestone-B test pattern with no progress text; the file picker opens at the top-left of the screen. The window title still reads "AC04 Port".
- `pad vibration output not implemented yet` with a gamepad connected: no rumble.
- Not compared with PCSX2 (open): a white band across the hangar MOBILITY row, and the jagged "shattered skies" letters in the no-save intro.

### UX-5. Fixes (2026-10-22, User go-ahead: debugger, intro, texture atlas, renaming, vibration)
- **Debugger** (`port-features.md` §8.7.1): text input is switched on again after every ImGui frame (UX-3 a); UI scale from the window height with the scalable font (b); a layout without overlaps (c); wider defaults, a request table that fills the window, mission id in hex (d, f); case-insensitive log filter (e); the looked-up memory row highlighted (f); no panel focused when the overlay opens (g). Checked at 3840×2160: panels readable, nothing cut off, Start reaches the game with the panels open, `\` closes and reopens them after typing in the Memory field, "sound" finds the SOUND lines.
- **Texture atlas**: the seagull frame needs 34 textures (19 of 1024×1024, several 768×512), about 23.2 M texels, more than a 4096² atlas holds; the atlas now grows once to 8192² (`port-features.md` §6) and rebuilt atlases are packed tallest first. Intro run at normal speed: `texture atlas grown to 8192x8192`, no `atlas full`; average recording time unchanged (1.80 ms against 1.86 ms).
- **Intro blending**: ALPHA 0x54 and 0x56 (and every other equation fixed-function blending cannot express) are blended in the shader from a copy of the target (`../platform/gs-subset.md`, "All blend equations"); no blend warning is logged in the intro any more. Not compared with PCSX2.
- **Renaming**: the window title, message boxes, log banner and Vulkan application name read "ProjectStonehenge"; the executable and the user data folder kept their old names that day and were renamed the same evening (§2r, RN-1).
- **Vibration** (`port-features.md` §1): switching VIBRATION off and on in CONTROLLER SETUP sends motors (1, 255) through `scePadSetActDirect` to the Xbox One controller (`port 1: vibration started`). In 2,400 vblanks of mission 1 flight (afterburner, missiles, gun) the game requested no rumble: AC04 rumbles for damage, nearby explosions, stalls and high G, which that flight did not reach. Felt on the pad: to be confirmed by the User.
- Seen once, not reproduced in four attempts: a fresh-user-data start hung at "verifying disc image 0%" (no CPU, window not responding) while synthetic keys and focus changes were being sent; no debugger was available to take stacks.
- Build: development and release build clean, `ctest` passes.

## 2r. No-save intro: transitions and stutter; renaming (2026-10-22)

User report: in the no-save intro's in-engine cutscenes with the seagulls, wrong colours cover the picture at the transitions, and the playback stutters. Found with screenshots every 10 vblanks (600–2600), then every vblank, a `--dump-dma-at` capture of a transition frame and the draw list (`tools/research/gsdraws.py`, plus the uploads in stream order).

### IN-1. Orange noise over the picture at the two cloud transitions
- Vblanks about 1540–1700 and 2270–2420: the screen filled with orange/red banded noise. The frame (capture at 1560) uploads a 200×200 PSMT8 cloud texture to 0x2500 and its palette to 0x3E00 (CT32), then draws large rotating alpha-blended quads of it over the picture (`(Cs−Cd)·As+Cd`, As from the palette's alpha ramp 0–128).
- The draws' TEX0 has **CPSM 1 (PSMCT24)**, not a documented CLUT format. The port took every CPSM other than 0 and 0xA as CT16 and read the 32-bit palette as 16-bit entries. The GS selects 16-bit entries with CPSM bit 1 (PCSX2's CLUT writers treat CT24 as CT32); fixed in `gs_state.cpp` (`Clut16`): CPSM 1 now reads 32-bit entries with their alpha.
- Result: white clouds fade in over the feathers and out to the city (first transition) and over the F-22s to the seagulls (second). Not compared with a PCSX2 picture of these frames yet.

### IN-2. Stutter
- At normal speed only 31 of 101 requested screenshots (vblanks 900–1000) were written: the port presented about 31 frames for 101 game ticks. A per-frame log showed frames 912–929 with 40–50 ms of GS processing and 20–40 ms of recording, 30–50 texture decodes (3–4 M texels) and an atlas restart every frame.
- Cause 1: triangles decode their whole texture (`gs-subset.md`, S4); the seagull models use T4 textures declared 1024×1024 but in REGION_CLAMP to 16×16 texels, so each draw decoded a million texels. Triangles now decode only the [MIN, MAX] range of a REGION_CLAMP axis (not with mip levels).
- Cause 2: a decoded texture was keyed by a hash of every GS page in the linear span from its first to its last texel (`RegionHash`): for a 1024×1024 texture that span includes the terrain tiles and cloud textures the intro uploads every frame, so the textures were decoded again every frame. Level 0 is now keyed by the pages under the decoded rectangle only (`GsMemory::RectHash`, pages found on a 32-pixel grid).
- Result (normal speed, vblanks 0–2600): the run of slow frames at the seagull close-ups (about vblank 912–929, 40–50 ms of GS processing each) is gone; 2,460 frames presented for 2,600 ticks (2,219 before); the atlas no longer needs to grow. 55 frames still take over 15 ms (41 over 25 ms, worst 77 ms), almost all in one stretch: the seagull shot after the second cloud transition (about vblank 2390–2460).
- Still open there: the terrain below draws many triangles with different 64 × 64 REGION_CLAMP slots of one mipmapped 512 × 512 T8 texture (0x2900), and the game streams new terrain data into that memory between draws: 600–990 of its 1,360 blocks change between consecutive decodes, about 4 full CPU decodes (with mip levels) and atlas uploads per frame. Tried and reverted: decoding only the REGION_CLAMP slot with mip levels (over 1,000 small textures per frame, 230–250 ms per frame), decoding triangles by vertex range (model draws stop merging, about 260 textures per frame), and caching the texel indices across palettes (no measurable gain). The fix would be decoding paletted textures on the GPU (palette applied in the shader, as `gs_conv.comp` already does for frame buffers): a renderer design change for the User to decide. Done the same day (User approval): §2s, the stretch now peaks at 12.5 ms of CPU work per frame instead of 41.

### Regression (`full.json` against `out/runs/baseline_s9`, final build)
- All 30 RAM checkpoints equal; all 18 missions' screenshots equal.
- Hash logs differ in the jobs that reach a mission with the player aircraft (front end, mission 3 and 4 routes, mission 4 fire, opening): first difference at the mission 1 opening (vblank 11734, heap 0x00CD0000), the flame records of EF-1 (§2p).
- Screenshots: card E boot (vblank 600) now shows the narration text "The flames of hatred scorch the skies… Igniting Gaia's funeral pyre." where the baseline had red noise (from the PSMT8 table or the CT24 CLUT fix; which one was not isolated); mission 1 opening (front end 11950): the deck and background behind the aircraft differ slightly (HYPOTHESIS: the glow/depth pass reads frame buffers as PSMT8, S2a-6), not compared with PCSX2; mission 4 route 5300: the known one-pixel column (IR-1).

### RN-1. Renaming finished
- Executable `ProjectStonehenge.exe` (CMake project and target `ProjectStonehenge`), user data folder `%LOCALAPPDATA%\ProjectStonehenge\` (ADR-005 updated; Linux/macOS names for later: `projectstonehenge`, `ProjectStonehenge`), usage text, comments, docs, rules and the research tools' job files. No migration code (first release); on the User's PC the old folder's `settings.json`, `iso_cache.json` and (empty) `saves\` were moved to the new folder and the old folder with its stale log and a RAM dump was deleted. `ac04::` namespaces, `AC04_` macros and `ac04_*` libraries keep their names (they name the game, User decision 2026-10-22). Two records above (§2o NS-2, §2q UX-4) quote the old window title as it was shown that day.

## 2s. GPU texture decoding, generation-based texture cache, texture images (2026-10-22)

User-approved plan `renderer.md` §2 (all three items). Built: GS textures are decoded on the GPU (`shaders/gs_decode.comp`) from a 4 MiB GPU copy of local memory that `GsState` keeps in step through `TextureOp` (memory blocks, then decodes, in stream order); the decoded-texture cache keeps one entry per texture, valid while the GS blocks it read keep their generations (the page content hashes are gone); one image per decoded texture replaces the atlas. The CPU decoder stays as the reference (`--cpu-texture-decode`, `--verify-texture-decode`).

### GD-1. Correctness
- Tests: 87 test cases pass (development build). New: a decode stays valid until a block it reads is written (writes elsewhere and rewrites of the same bytes keep it); the GPU ops of a stream that rewrites its texture between draws, replayed on a second memory and decoded there with the reference decoder, give each texture's CPU decode (T8, T4 with CT32 and CT16 palettes, CT32, CT24, CT16, CT16S, with TEXA). The incremental-decode and cache-budget tests were adapted (no atlas slot updates; one entry per key).
- `--verify-texture-decode` on the six scenes of `renderer.md` §2.5 (no-save intro at 1× and 8×, hangar/shop route, mission 1 opening, mission 1, mission 4 replay): 666,000 GPU decodes, every one byte-identical to the CPU decode. The first run found one difference: the intro samples its Z24 buffer (TBP 0x1500 = FBP 0xA8) as a texture; the CPU decoder had always given 0 for Z formats (an accident of its default branch), the shader the raw word. Both now give 0 explicitly with a log-once warning, so the picture is unchanged; depth sampled as a texture remains unimplemented (`../open-questions.md`).
- Release build: two command-line tests fail (`determinism options`, `--mission options`): a development-only option with a value (`--replay-input D:/in.ac4i`) counts as two errors in release builds (the option, then its value as an unknown argument). Not related to this change; fixed in §2t.

### GD-2. Regression (`full.json`, run `gpu_1022`)
- Against last session's final run (`out/runs/final_1022`): all 27 RAM checkpoints and all 6 RAM hash logs identical; 75 of 77 screenshots pixel-identical; `m3_route` and `m4_route` screenshot 5300 differ only in column x = 409 (400 pixels, at most 5 levels): the loading-fade column of IR-1 (host timing).
- Against `baseline_s9`: the differences listed in §2r (card E narration, mission 1 opening at 11950, hash logs of the jobs with the player aircraft, the x = 409 column), nothing new.

### GD-3. Speed (`renderer.md` §2.7)
Same six runs as step 1 (§5 there), same conditions (windows not focused); CPU = game tick + GS processing + recording per presented frame.

| Scene | CPU mean | Worst frame | Frames with CPU > 16 ms |
|---|---|---|---|
| No-save intro 1× | 9.0 → 6.6 ms | 91.5 → 21.2 ms | 200 → 4 |
| No-save intro 8× | 10.0 → 7.4 ms | 93.0 → 30.6 ms | 286 → 173 |
| Hangar/shop route | 4.0 → 3.6 ms | 36.2 → 30.8 ms | 579 → 60 |
| Mission 1 opening | 9.5 → 8.1 ms | 30.9 → 26.5 ms | 86 → 80 |
| Mission 1 | 9.0 → 8.4 ms | 34.0 → 35.2 ms | 310 → 300 |
| Mission 4 replay | 10.7 → 8.2 ms | 39.3 → 20.9 ms | 621 → 245 |

- The intro stretch still open in §2r IN-2 (seagull shot after the second cloud transition, vblank 2390–2460): CPU 14.4 → 9.8 ms per frame on average, worst 41.2 → 12.5 ms.
- Hangar viewer (vblank 4160–4650): the game cycles several textures through the same memory every frame; without the content hash each return is a new GPU decode (about 158 per frame, before 6 CPU decodes). GS processing 9.1 → 6.5 ms, recording 1.8 → 2.5 ms (per-decode descriptor set and dispatch), CPU 16.8 → 15.3 ms per frame.
- Remaining frames over 16 ms in the missions and at 8× carry two game ticks (the unfocused windows presented at ~50 Hz, so many frames ran two ticks): game tick 6–13 ms plus GS processing about 7 ms. Not texture work.

## 2t. Release 0.9 preparation (2026-10-22)

- **Development parameters in release builds**: every development-only parameter (23 branches of `command_line.cpp`, among them `--vk-validation` and `--replay-input`) is rejected in release builds and enables nothing. Their values are now skipped (`dev_only`), so `--replay-input D:/in.ac4i` gives one error instead of two; the two release-build test failures of §2s GD-1 are fixed by this. Development and release tests: 87 test cases pass in both.
- **Version 0.9.0** (`CMakeLists.txt`); `--version`: `ProjectStonehenge 0.9.0 (<commit>, development build)`.
- **File properties** (`src/platform/win32/version.rc.in`, configured to `build/.../generated/version.rc`): read back from the release executable: CompanyName and ProductName `ProjectStonehenge`, FileDescription `Unofficial native PC port of Ace Combat 04: Shattered Skies`, FileVersion and ProductVersion `0.9.0`, LegalCopyright `Copyright (C) ProjectStonehenge. Licensed under the GNU GPL v3.`, OriginalFilename `ProjectStonehenge.exe`.
- **Startup image**: until the GS renderer has a frame, release builds show black; development builds keep the test pattern (`Renderer::UploadTestPattern`).
- **No game data in the executable** (release build, 7.5 MB): no VAG, TIM2, GIM, PS2I or IOPRP signatures and no embedded PS2 ELF; 0 of about 26,000 sampled 64-byte runs of every section of `SLUS_201.52` (code, `.data`, `.rodata`, VU code and data) occur in it. Two 4-byte MPEG start codes in `.rdata` are inside constant tables (not followed by a sequence header). The executable reads all game data from the user's disc image at runtime.
- **Release folder**: the build writes `ProjectStonehenge.exe` next to build leftovers (`ac04_core.lib`, `ac04_platform.lib`) and a stale `ac04port.exe` from before the rename (2026-10-22 00:50); there is no packaging step yet, so a release archive must be put together by hand from `ProjectStonehenge.exe` (plus `LICENSE`, `THIRD_PARTY_NOTICES.md`, `README.md`).
- **Packaging step** (User request, same day): `cmake --build --preset release --target package_release` (`cmake/PackageRelease.cmake`) writes `out/release/ProjectStonehenge-0.9.0-win64.zip` with `ProjectStonehenge.exe`, `LICENSE`, `README.md` and `THIRD_PARTY_NOTICES.md` in a `ProjectStonehenge-0.9.0-win64/` folder (3.2 MB; checked by listing the archive). The script refuses other configurations (checked with `--preset dev`) and stops on game-data file names, the VAG, TIM2 and IOPRP signatures and embedded PS2 ELF headers (checked with a test file holding `VAGp`). The stale `ac04port.exe` and every other build leftover under the old name (its intermediate folder, project files, the old solution) were deleted from `build/` (User approval).

## 3. Recommendations

1. Treat effect draw notes as orientation only. Translate packet code from the disassembly (already the rule in the `port-function` skill).
2. Before translating any function, check the appendix of `../game/random.md` for random reads. The translation must keep their order.
3. Rerun `tools/research/validate.py` after each research pass and before each commit of `coverage-functions.csv`.

## Samples

### First sample (seed 20261011)

| Address | Name | Subsystem | Group | Score | Comment |
|---|---|---|---|---|---|
| 0x00131DC8 | `func_00131DC8` | ai | understood | agree | ground vehicle move/turret AI incl. class-4 proximity hold and NextRandomFloat speed/offset |
| 0x0010D6A0 | `func_0010D6A0` | camera | understood | agree | view slot reset (0x440 bytes; two camera records +0xB0/+0x270); note terse |
| 0x00144DC8 | `func_00144DC8` | collision | understood | agree | segment vs static objects of one cell via 0x00143EF8 |
| 0x00105778 | `ApplyVibrationOptions` | core | understood | agree | vibration option bit 8 per port; CSV note empty (name only) |
| 0x00105178 | `func_00105178` | core | understood | minor | doc omitted the sign-extension word left in the matrix for negative scales (V-3) |
| 0x00100DC8 | `NextRandomFloat` | core | understood | agree | table 0x00411998 cursor 0x00411780 |
| 0x00100AC8 | `RequestSubState` | core | understood | agree | ctx req 2 + next sub |
| 0x0015E778 | `thunk_FUN_00155ed8` | effects | understood | agree | thunk to pool reset |
| 0x001E0700 | `func_001E0700` | effects | understood | agree | lowest-free bitmap allocator |
| 0x00155ED8 | `func_00155ED8` | effects | understood | agree | pool reset wrapper |
| 0x001558C8 | `func_001558C8` | effects | understood | agree | effect state packet (0x00155188) + store at +0x6249C |
| 0x0015E740 | `thunk_FUN_00156290` | effects | understood | agree | thunk to 256-entry pool reset |
| 0x001270B8 | `func_001270B8` | flight | understood | agree | player per-frame engine sound block 0x420610 + warning loops |
| 0x0023C8B8 | `func_0023C8B8` | frontend | understood | agree | save page message id by state and card status |
| 0x00239E10 | `func_00239E10` | frontend | understood | agree | cursor column x = base + n*0x140 |
| 0x00236F28 | `func_00236F28` | frontend | understood | agree | orbit 90 s period + easing |
| 0x00245B88 | `func_00245B88` | frontend | understood | agree | help text by state with typewriter reveal (HDD page context not checkable blind) |
| 0x00228EB8 | `func_00228EB8` | frontend | understood | minor | doc omitted the first stage black -> 0x765834 |
| 0x00235580 | `func_00235580` | frontend | understood | agree | tail call to hangar panel reset 0x00234078 |
| 0x0014B3F8 | `func_0014B3F8` | frontend | understood | major | doc said light/colour constants; code uploads camera world->screen/world->clip matrices + one vector (V-4) |
| 0x00105448 | `func_00105448` | frontend | understood | agree | save statistics clamps identical |
| 0x0025C410 | `func_0025C410` | hdd | understood | agree | partition create + sub-partitions (doc terse) |
| 0x00161890 | `func_00161890` | hud | understood | minor | doc omitted target marker and direction sprite |
| 0x0015FEE0 | `DrawHudSprite` | hud | understood | agree | sprite with 16:9 width 3/4 and half-texel offsets |
| 0x0010FB30 | `func_0010FB30` | mission | understood | major | doc said mission end kill bookkeeping; code is the phase-change unit activation/retire (V-5) |
| 0x00179998 | `func_00179998` | mission | understood | minor | doc omitted restore of other units' flags from 0x002B7828 |
| 0x0017BAF0 | `func_0017BAF0` | mission | understood | agree | 8-entry proximity queue |
| 0x00213A38 | `func_00213A38` | movie | understood | minor | doc did not say which register (TEX0_1) |
| 0x002507F0 | `func_002507F0` | radio | understood | agree | bounded table lookup |
| 0x001118C8 | `func_001118C8` | render | understood | agree | sky block init + GIF upload |
| 0x001152D8 | `func_001152D8` | render | understood | agree | cloud puffs up to 400 sorted |
| 0x00138C28 | `func_00138C28` | render | understood | minor | REF address is header+0x10 (0x00298AD0) |
| 0x00171460 | `func_00171460` | replay | understood | agree | segment vs terrain/objects for camera clamp |
| 0x0024B248 | `func_0024B248` | results | understood | agree | results text page |
| 0x00220BF0 | `func_00220BF0` | sequence | understood | agree | create/clear character set +0xE4 |
| 0x00219B48 | `func_00219B48` | sound | understood | agree | bank loader state reset base/base+0x30000 |
| 0x00218D58 | `func_00218D58` | sound | understood | agree | getter (bank count not verifiable blind) |
| 0x0021AAC8 | `func_0021AAC8` | sound | understood | minor | doc omitted the addition before the wrap |
| 0x0014F398 | `func_0014F398` | weapons | understood | agree | shell init radius 500 speed 510 |
| 0x0014BB40 | `UpdateProjectiles` | weapons | understood | agree | 7 pools in order |
| 0x001A5DA8 | `func_001A5DA8` | effects | partly | agree | plume sprite cluster with wind vector and table 0x002B91C0 |
| 0x001FF818 | `func_001FF818` | effects | partly | major | doc said trail parameters/colour table; code builds the frustum clip structure 0x002BB990 (V-6) |
| 0x00189B88 | `func_00189B88` | effects | partly | agree | record init with two NextRandomFloat reads |
| 0x002034A8 | `func_002034A8` | effects | partly | minor | labels: +0x624E0 is the clip matrix (not light) and +0x62494/98 near/far depths (not frame range) (V-6) |
| 0x001E88C0 | `func_001E88C0` | effects | partly | major | doc said plain afterburner sprite; code is a two-pass heat-haze distortion sampling the framebuffer with dest-alpha blend (V-7) |
| 0x001E81F0 | `func_001E81F0` | effects | partly | minor | afterburner draw is a heat-haze distortion (label) |
| 0x001EB728 | `func_001EB728` | effects | partly | agree | empty |
| 0x001DDAC0 | `func_001DDAC0` | effects | partly | agree | 12-entry trail ring every second frame |
| 0x001E24C8 | `func_001E24C8` | effects | partly | minor | LCG-placed sprite cluster agrees; doc omits ballistic motion and keyframe curves |
| 0x00202290 | `func_00202290` | effects | partly | agree | smoke trail ribbon pool B |
| 0x001CD6D8 | `func_001CD6D8` | effects | partly | minor | doc omits ground-plane intersection; puffs from table 0x002B91C0 agree |
| 0x00254340 | `func_00254340` | radio | partly | minor | note had contradictory queue number (3 vs 4); behaviour agrees |
| 0x00138350 | `func_00138350` | render | partly | agree | sea tiles with scrolling UV and fog alpha |
| 0x00171CD0 | `func_00171CD0` | replay | partly | minor | wrong offset constants in doc (style 8 too) |
| 0x00223B80 | `func_00223B80` | sequence | partly | minor | headline said texture/CLUT upload; body (strip mesh with ADC) agrees |
| 0x00259C40 | `func_00259C40` | hdd | dead | agree | getter +0x4380 |
| 0x002208E0 | `func_002208E0` | sequence | dead | agree | empty |
| 0x0021E200 | `func_0021E200` | sound | dead | agree | SetSoundPitch wrapper |
| 0x0024D7A8 | `func_0024D7A8` | unreached | dead | minor | note was empty |
| 0x0017C180 | `func_0017C180` | unreached | dead | minor | note was empty |

### Wider re-review (seed 20261012)

| Address | Name | Subsystem | Group | Score | Comment |
|---|---|---|---|---|---|
| 0x001F69A0 | `func_001F69A0` | effects | wide | minor | kind dispatch imprecise (kinds 1/3 -> 0x001F7EA8 |
| 0x0017F760 | `func_0017F760` | effects | wide | agree | scratchpad near/far range |
| 0x00185C48 | `func_00185C48` | effects | wide | minor | doc says up to 8 puffs; code draws 16 segments with a 16-entry jitter table; brightness from 0x001152A8 cloud function |
| 0x0015DDA8 | `func_0015DDA8` | effects | wide | agree | collect pass for 64-slot pool |
| 0x001859D8 | `func_001859D8` | effects | wide | agree | age = 0 |
| 0x00189BF8 | `func_00189BF8` | effects | wide | agree | 108-frame lifetime |
| 0x001CFAF8 | `func_001CFAF8` | effects | wide | agree | mission 7 colour switch |
| 0x001F3C08 | `func_001F3C08` | effects | wide | agree | matrices + near/far / 4 + size factor by view mode |
| 0x001B1BD8 | `func_001B1BD8` | effects | wide | agree | special object 0x15 fire/smoke column (doc thin) |
| 0x001D0BB0 | `func_001D0BB0` | effects | wide | agree | scratchpad parameters |
| 0x0015E5F0 | `func_0015E5F0` | effects | wide | agree | draw wrapper with depth sort |
| 0x001FF218 | `func_001FF218` | effects | wide | agree | allocator |
| 0x001FF020 | `func_001FF020` | effects | wide | agree | allocator |
| 0x00181BB0 | `func_00181BB0` | effects | wide | agree | spawn/refresh; random read in init 0x0017F6D8 documented |
| 0x00157728 | `func_00157728` | effects | wide | agree | update wrapper frees expired records |
| 0x00155FE8 | `func_00155FE8` | effects | wide | agree | reset wrapper |
| 0x00188A50 | `func_00188A50` | effects | wide | minor | two NextRandomFloat reads in the spawn not documented (V-8) |
| 0x0015E198 | `func_0015E198` | effects | wide | agree | draw loop over 8-slot pool |
| 0x0023D0E0 | `func_0023D0E0` | frontend | wide | agree | load slot into 0x002A0318 |
| 0x00230080 | `SetAircraftViewerAircraft` | frontend | wide | agree | viewer request gated by state 2/3 |
| 0x00234078 | `func_00234078` | frontend | wide | agree | hangar panel reset by category table 0x0039E128 |
| 0x002285D8 | `func_002285D8` | frontend | wide | agree | 6 hint icons with pulse colours |
| 0x00241708 | `func_00241708` | frontend | wide | agree | records text page |
| 0x00238380 | `func_00238380` | frontend | wide | agree | briefing movie sub-state |
| 0x002294F8 | `func_002294F8` | frontend | wide | agree | list widget frames (colour fade 0x00228EB8) |
| 0x00226F98 | `func_00226F98` | frontend | wide | agree | hint bar easing 0.88/0.9 |
| 0x002309C0 | `func_002309C0` | frontend | wide | minor | doc says leave to MenuGoTo; this function only enters state 2 with a fade |
| 0x002468B0 | `func_002468B0` | frontend | wide | agree | HDD install page draw |
| 0x0022E580 | `func_0022E580` | frontend | wide | agree | viewer instance allocation |
| 0x0023A208 | `func_0023A208` | frontend | wide | minor | s16 rect is lerped linearly by t; only the two floats use the eased value |
| 0x00238FE0 | `func_00238FE0` | frontend | wide | agree | movie page draw + send + swap |
| 0x002409D0 | `func_002409D0` | frontend | wide | agree | white/grey/black patch grid |
| 0x0023CF80 | `func_0023CF80` | frontend | wide | agree | store campaign block into slot |
| 0x00234A08 | `func_00234A08` | frontend | wide | agree | special weapon cycling among up to 3 owned options |
| 0x0010A898 | `FlightSceneEnter` | mission | wide | agree | subs 0/1 create scene + mission kind + double buffer |
| 0x00135C68 | `func_00135C68` | mission | wide | agree | running minimum vs line +0x1C |
| 0x0010B500 | `func_0010B500` | mission | wide | agree | mid-mission sequences by mission id and part |
| 0x0010B038 | `StoreFlightDataPointers` | mission | wide | minor | CSV note empty (name only); behaviour per subs 2-5 confirmed |
| 0x0017B740 | `func_0017B740` | mission | wide | minor | doc omits the libc rand() call per fired trigger (rand() % 1 via 0x00252FB8) |
| 0x0017BFB0 | `func_0017BFB0` | mission | wide | minor | requests (ORs the mask) rather than draws the tutorial hints |
| 0x0017E460 | `func_0017E460` | mission | wide | agree | rearm menu draw + special-weapon panel |
| 0x00176BA0 | `func_00176BA0` | mission | wide | agree | clear secondary lock |
| 0x0010AA50 | `CreateFlightSystems` | mission | wide | agree | sub 7 create subsystems + heap mark |
| 0x0010A3E0 | `func_0010A3E0` | mission | wide | minor | doc omits the fade level +0x1670 = 1.0 |
| 0x0010A130 | `func_0010A130` | mission | wide | agree | fade target/time + current level |
| 0x0010BCD8 | `func_0010BCD8` | mission | wide | agree | sub 0x1D frame without the sim step |
| 0x0017B890 | `func_0017B890` | mission | wide | agree | reset list fields |
| 0x001769C0 | `func_001769C0` | mission | wide | agree | drop stale locks |
| 0x0017EB30 | `func_0017EB30` | mission | wide | agree | menu item animation |
| 0x0017E1D0 | `func_0017E1D0` | mission | wide | agree | game-over menu reset/layout |
