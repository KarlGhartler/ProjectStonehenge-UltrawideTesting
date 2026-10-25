# Validation strategy for translated slices

How each slice is checked against PCSX2 (ADR-007). The record of checks already run is `validation.md`; this file is the method. Reference: PCSX2 2.8.2 running the original disc with its GameDB settings for SLUS-20152 (`eeRoundMode: 0`, the three COP2 patches; `validation.md` B-2, E-2).

Three tools, used together:

| Tool | Used for | Where it runs |
|---|---|---|
| RAM checkpoint + screenshots (§2) | every slice | `tools/research/ramdiff.py`, native screenshot (debug overlay) |
| Input recording replay + determinism checker (§3) | gameplay slices (scene 2 sub-slices, VS mode, title demos) | debug layer of the port |
| Function test vectors (§4) | risky functions: random sources, flight model, AI, weapons, effects, slot rules | `tests/` (doctest) |

No game data enters the repo: save states, screenshots, dumps and recordings stay in the User's folders (`CLAUDE.local.md`) or in `%LOCALAPPDATA%\ProjectStonehenge\`; the repo keeps only addresses, ranges, reasons and small scalar values.

## 1. What every slice defines

In its plan (`slices.md` and the slice's PCSX2 batch in `../pcsx2/procedures.md`):
- **Checkpoints:** a game-defined point where both sides are compared, e.g. "Execute breakpoint at `SceneStep` 0x00100B18 with `scene == 4` and `sub == X`, first hit" or "first step of scene 2 sub 0xB". Points are chosen where no CD request or IOP reply is in flight, so timing differences do not leak into the comparison.
- **Screens:** the list of screens to compare and how to reach them.
- **Test vectors:** the risky functions of the slice and their entry/exit breakpoints.
- **Recordings** (gameplay slices): the input recordings and their anchor (§3.2).

## 2. RAM checkpoint and screenshots (every slice)

### 2.1 RAM
- PCSX2 side: a save state taken at the checkpoint (Execute breakpoint, first hit, then save state).
- Port side: a guest RAM dump at the same checkpoint: `--dump-ram-at <addr>[,<hit>]` (development builds, added in S1, `command-line.md`). The translated code reports the breakpoint address with `debug::OnGuestCheckpoint` (function entry, or right before the instruction for call sites such as tail calls).
- Compare: `python -I tools/research/ramdiff.py <dump> <state.p2s> --expected [docs/port/ramdiff-expected-<slice>.csv]`. Every difference is either fixed or classified with a reason (category, address range, why): in `ramdiff-expected.csv` when it holds for every checkpoint, in the slice file when it is specific to that slice's checkpoints (S1: frame-timing knock-ons). Ranges may span several entries; only the differing bytes outside all entries count. A slice passes with **0 unexplained ranges**.
- Frame timing: the port's CD reads (and PCSX2's own I/O timing, which varies by a frame between cold boots, `../pcsx2/sessions.md` run 06) do not match frame for frame, so frame counters and what follows from the frame parity (double-buffer halves, field parity, SOUND batch buffers) differ at checkpoints. They are classified `timing`; the state of the code under test must match exactly.
- Categories added per slice when needed, besides the existing ones (`kernel`, `sdk`, `stack`, `timing`, `pcsx2-patch`, `stack-garbage`): `pending-slice` (state owned by code that a later slice translates; must disappear by the end of scene 2), `host-service` (stand-in state of `src/sce/` / IOP that the game never reads).
- Scene 2 rule: until weapons, effect simulation and effect drawing are translated (sub-slices 2c–2e, `slices.md`), the random-table cursor and everything downstream of it can differ; such slices compare their own regions and list the rest as `pending-slice`. From the end of sub-slice 2e on, the full RAM must match.

### 2.2 Screenshots
- PCSX2 side: screenshots of the listed screens at **native resolution** (PCSX2 *Internal resolution: Native*, no texture filtering or enhancements, PCSX2's default deinterlacing; the port presents the full 448-line buffer, ADR-006 §4, so automatic/weave on a still screen is the closest).
- Port side: the debug overlay's native screenshot (512×448 or 640×448 before the output pass, `port-features.md` §8.4.1), saved under `%LOCALAPPDATA%\ProjectStonehenge\screenshots\`.
- Comparison: `python -I tools/research/imgdiff.py <port.png> <pcsx2.png> --blend-fields [--diff out/....png] [--offset dx dy]` (mean and maximum difference per channel, share of pixels over a threshold, difference image). PCSX2 shows the 224-line field output deinterlaced, the port presents the full 448-line draw buffer (ADR-006 §4). `--blend-fields` averages each port row with the row above, which matches PCSX2's output (S2a: pixels over 16 drop from 1–2.5 % to 0–0.1 %; `validation.md` S2a-4). Tolerances: **best match at offset 0, ≤ 0.5 % of pixels over 16, mean difference ≤ 1 per channel**. Known exception: frame-buffer feedback effects (menu frame filter) keep a mean bias of up to 2–3 levels while the blend rounding question is open (`validation.md` S2a-4).
- Also look at the **difference image and the maximum per region**, not only the mean: a faint but structural error (S1 splash: one band with rotated colour channels, values 0–5) can stay under the mean tolerance (`validation.md` S2a-5). Anything structural (missing primitive, wrong texture, wrong position, wrong blend, wrong colour channel) is a bug.
- Screens are compared on a **still frame** where possible (paused menus, first frame after a fade), because the two sides are not frame-aligned outside recordings.

## 3. Gameplay: input recordings and the determinism checker

### 3.1 Recording format
PCSX2 2.8.2 input recordings (`.p2m2`), documented in `../formats/p2m2.md`: one record per vsync with 18 bytes per port, identical to `scePadRead` bytes 2–19. The port's replayer copies them into its `scePadRead` answer for port 1 and port 2.

### 3.2 Alignment (anchors)
A recording cannot be replayed frame for frame from power-on: PCSX2's recording starts with a VM reset (BIOS frames included), and the port's boot and CD reads take a different number of frames (`validation.md` C-1). Method:
1. Recordings are made **from a save state** taken at the slice's anchor, or from power-on with the anchor noted.
2. The **anchor** is a game event both sides can detect: normally the first step of a scene/sub-state after a load has completed (e.g. scene 2 sub 0xB, first step; the mission timer at rules +0x24 still 0). The User notes the recording frame number of the anchor (PCSX2 shows the recording frame counter in the recording toolbar/OSD).
3. The port runs to the anchor with neutral input (or with the recorded menu inputs for the front-end part, if the slice needs them), then feeds record `anchor + k + offset` at step `anchor + k`.
4. `offset` (PS2 pad latency: emulated pad → IOP libpad → EE `scePadRead`) is calibrated once in the first gameplay slice: record a single button press at a known frame and look for the first step whose pad state (`ctx->pad_buttons`) shows it on both sides. Done 2026-10-17 (`validation.md` S5-P2): records and port ticks line up with offset 153 for power-on recordings (same `frame_count` at the same press); after an anchor the pad path adds no frame on either side.
5. After the anchor, no loads with variable latency may affect the simulation. Mid-mission CD reads (streamed radio, BGM, mid-mission sequences `func_0010B500`) are checked per slice; where the game waits on them, the port's completion model must reproduce the PS2 frame count or the checkpoint must be placed before them.

### 3.3 Checkpoint frames
- The User takes PCSX2 save states at given recording frames (e.g. anchor + 600, + 1800, + 3600) while replaying.
- The port dumps RAM at the same steps; compared as in §2.1.
- A mismatch is bisected with the determinism checker (§3.4) and with extra save states between the two nearest matching checkpoints.

### 3.4 Determinism checker (debug overlay, `port-features.md` §8.4.1 *Extra*)
- After every simulation step: a hash per RAM region (game `.data`/`.bss`, work heap, scratchpad; SDK/kernel and stack regions excluded as in `ramdiff-expected.csv`) written to a file under `%LOCALAPPDATA%\ProjectStonehenge\`.
- Uses: **port vs. port** (two runs of the same recording must give identical hash streams; the first differing step and region show any host-dependent behaviour) and **port vs. checkpoint** (the step of the first difference between two PCSX2 checkpoints).
- The same per-step hash is the desync detector of LAN play, if the User adopts it (ADR-008).

### 3.5 Determinism prerequisites
The replay is only meaningful if the port is deterministic for a given input stream:
- Game state visible to the game from the IOP stand-ins (SOUND voice-status bytes 0x00424FD0…, VAG stream positions and slot states, RPC reply timing) advances **per simulation step**, never by the host audio device clock or wall time. The audio backend (ADR-004) runs its mixer from those step-clocked commands.
- CD request completion follows a fixed model (today: done on the next poll), never host I/O timing.
- EE float behaviour only through `src/common/ee_math.h` (round to nearest, `validation.md` E-2); no FMA contraction (MSVC `/fp:contract` off — the default under `/fp:precise` — and later `-ffp-contract=off` for GCC/Clang); libm functions the game calls (`sinf`, `cosf`, …) translated, never the host's.
- The five random sources (`../game/random.md`) live in guest memory and are read in the original order.
- The pause/single-step control of the overlay never skips or repeats a step.

## 4. Function test vectors (risky functions)

Risky functions: the random sources and their callers, the flight model (incl. double-precision paths, `coverage.md`), AI decisions, weapons and projectile/effect slot rules, collision, effects that read the random table.

### 4.1 Capture (PCSX2)
For each function the User sets two Execute breakpoints and takes two save states:
- **entry:** breakpoint at the function's first instruction → save state `<addr>_entry_<n>.p2s`;
- **exit:** breakpoint at the instruction after the call site (`ra` from the entry state), same activation → save state `<addr>_exit_<n>.p2s`.
Each state holds guest RAM and the EE registers (`tools/p2s_read.py`; register file at offset 0x162 of `PCSX2 Internal Structures.dat`, `validation.md` B-1).

### 4.2 Turning captures into tests
- The test loads the entry state's RAM and registers into `GuestMemory`, calls the translated function with the argument registers, and compares `v0`/`f0`, the callee-saved registers and the touched memory with the exit state (memory compared over the regions the function may write, from its notes).
- **Small vectors** (a few scalars: arguments, results, a random cursor value) are written into `tests/` as literals. Numbers measured from a function are not game data, as in the existing tests.
- **Large vectors** (whole structs, pools, matrices) are not copied: the test reads the `.p2s` at run time from a local folder given by the environment variable `AC04_TEST_STATES` (or the CMake cache entry of the same name). Without it the test is **skipped** and reported as skipped, never failed. Implemented so far (`tests/p2s_state.h`, first user `tests/test_vectors_s4.cpp`): the environment variable only, and a missing state ends the case early with a MESSAGE (doctest counts it as passed).
- Each vector is listed in the slice's report with the function, the activation and the result.

### Determinism checker (S5 P1, 2026-10-16)
- Each game tick reads exactly one pad state, latched by the main thread before it releases the tick (ticks are released one at a time; live devices are sampled between them). With `--pad-script` or `--replay-input` only that input is used.
- `--hash-log` writes a RAM hash per vblank; `tools/research/hashdiff.py A B` prints the first differing vblank and the 64 KiB blocks. Check: the same input calm and with `--stress-host` must give identical logs; a `--record-input` run and its `--replay-input` must give identical logs.
- Memory cards for validation runs are copied with their file times kept (`cp -rp`): the folder card falls back to the host file times for directory entries the game can read.

## 5. Per-slice pass criteria

A slice is done when:
1. `ctest --preset dev` passes (skipped vector tests are listed);
2. every RAM checkpoint of the slice has 0 unexplained ranges;
3. every listed screen matches its PCSX2 screenshot within the recorded tolerance;
4. gameplay slices: the recordings replay to their last checkpoint without a difference, and two port runs give identical determinism hashes;
5. differences that remain are documented (`ramdiff-expected.csv`, the slice report).
