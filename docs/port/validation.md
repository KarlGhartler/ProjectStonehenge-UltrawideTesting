# Validation of the decompilation records (2026-10-11)

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
| The owner's figure 2,884 = 2,229 + 389 + 266 | Correct for the committed version (HEAD). The last research pass promoted all 389 "partly" functions to understood. The 15 "partly" functions of the blind sample come from those 389 |
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
- **Ghidra stayed read-only.** One exception to check: a single `disassemble_bytes` call on existing code at 0x001F3910. It changes nothing; the owner can confirm when saving the program.
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
| V-7 | `func_001E88C0` (effects, **major**) | The note said "afterburner sprite writer". It is a two-pass **heat-haze distortion**. Pass 1 writes a mask into framebuffer alpha. Pass 2 draws displaced strips of the **framebuffer sampled as a texture** (CT32 1024×1024 at TBP 0), blended with destination alpha | Notes of 0x001E88C0 / 0x001E81F0; added to `../platform/gs-subset.md` and `internal-resolution.md`, because the renderer must handle this framebuffer feedback read |
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

Loader facts confirmed: one PT_LOAD segment at 0x00100000 (0x31172C bytes from the file, 0x3CC74C in memory, ending exactly at the end of `.bss` 0x004CC74C), NOBITS `.sbss` 0x00411780 (+0x1BC) and `.bss` 0x00411980 (+0xBADCC) zeroed, entry 0x00100008, gp 0x004159F0, executable SHA-1 as in `../platform/supported-version.md`.

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

`ac04port --iso <image>` (development build, RelWithDebInfo) runs `Entry` → `main` on the game thread. The log shows the nine IOP modules replaced, the first vsync wait reached during `InitSubsystems`/`LoadBootData`, then `entered scene 6 (SceneStartup)` about 0.55 s (≈33 vblanks) after start. The main loop then idles in the scene 6 stub: 318 vblanks in 5.3 s, 0 ticks dropped. Scene 0 (`SceneBootLoader`) runs translated: BD.TBL, DATA 0x163/0x164 (sound data), the BD entry 0 transfer to the IOP, `RequestScene(6)`.

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
- **Port side:** `ac04port --dump-ram-at-first-scenestep` (development build). The dump is taken at the entry of the first `SceneStep`, on the game thread.
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
- At 10 s (`scenestep_first_2.p2s`) the SOUND voice-status bytes 0x00424FFA–0x00424FFF (voices 42–47) are 4, meaning "released, envelope still sounding". These are probably the 6 VAG.IRX streaming voices, which SOUND sees as not keyed on but audible. HYPOTHESIS until the audio backend exists; the stand-in reports 0 for them. Noted in `../subsystems/iop-sound.md`.
- The GameDB entry for SLUS-20152 also sets `eeRoundMode: 0` ("Fixes autopilot being silly and crashing the plane"), which E-2 depends on.

### E-2. EE FPU rounding: PCSX2 runs AC04 with round-to-nearest (decided 2026-10-13: nearest)

All 1-ULP float differences have one cause:
- **Random table 0x00411998** (512 × `rand() * 2^-31`; the multiplier 0x30000000 is an exact power of two). `cvt.s.w` rounds the 31-bit integer: PCSX2 matches round-to-nearest in 512 of 512 entries. The port's `fpu::IntToFloat` truncates, so 247 entries are 1 ULP lower.
- **Sound category volumes 0x0042CA50** (`x / 255.0f`): PCSX2 matches round-to-nearest division; `fpu::Div` truncates.

Cause: PCSX2's GameDB sets `roundModes: eeRoundMode: 0` (Nearest) for SLUS-20152. The reason given is that the autopilot crashes the plane under PCSX2's default Chop mode. So the reference behaviour (PCSX2) is round-to-nearest for the whole EE FPU in this game, while `ee_math.h` and `.claude/rules/game-code.md` model round-toward-zero. Hardware is not IEEE in either mode, so neither is bit-exact by construction. The values that matter for the port are PCSX2's, and those are nearest.

**Decision (owner, 2026-10-13): round to nearest.** `ee_math.h` now rounds add/sub/mul/div/sqrt/`cvt.s.w` results to nearest (ties to even), keeping the EE rules: no NaN/Inf, denormals flushed to zero, overflow and x/0 clamped to ±FLT_MAX. `cvt.w.s` still truncates. The rule in `.claude/rules/game-code.md` is updated. Rerun of E-1: the random table and the sound volumes match PCSX2 byte for byte (796 ranges, 0 unexplained). Unit test "EE FPU: round to nearest…".

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
