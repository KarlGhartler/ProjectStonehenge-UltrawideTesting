# PCSX2 reference sessions (runs 01-12)

Results of every PCSX2 session the User ran, oldest first: one section per run (formerly `sessions.md` runs 01-12). References elsewhere say "`sessions.md` run NN". Pending checks with exact steps: `procedures.md`.

## Index of answered checks

Answered 2026-10-17 (run 12): S5-A run 2 missions 3 and 4 (states, GS dumps, screenshots; 0 unexplained RAM ranges, `../port/validation.md` S5-A3). Answered 2026-10-17 (run 11): S5-A run 1 (calibration recording, frames 1002/1723), run 2 mission 1 (state, GS dump, 2 screenshots), S2b-B (BGM player state, screenshot, cleared card). Answered 2026-10-15/16: the slice S4 batch, runs A-C (13 save states, 9 screenshots; results in `../port/validation.md` S4). Answered in run 08 (2026-10-14): the slice S2b batch (9 save states, 10 screenshots, button sequence) and S2a-R (software-renderer screenshot). Answered in run 07 (2026-10-14): the slice S2a batch (4 save states, 3 screenshots, S1 splash bug report). Answered in run 06 (2026-10-14): the slice S1 batch (6 save states, 5 screenshots, card folders S and E; card messages appear only with no card). Answered in run 05 (2026-10-11): carrier takeoff after a rearm (0x19 → 0x1A → 0x1B → 0x0B, rearm menu sub 0x1D), VU0 skinning path 0x00225110 (part type 1) with before/after states. Earlier runs: runs 01-04.

## Run 01

- Emulator: PCSX2 2.8.2, original disc image (NTSC-U, SLUS_201.52)
- Register values: only the low 32 bits are shown. EE registers are 128-bit; the upper words are irrelevant here.
- "Observed" sections are raw data. "Interpretation" sections are hypotheses to confirm in Ghidra.

### Session 1 — current scene id per screen

Field: `g_MainContext + 0x8` = `0x0029F388` (u16).

| Screen | Scene id |
|---|---|
| Boot logos / startup warnings | 6 |
| Game intro (only when slot 1 has no AC04 save, see Session 3) | 2 |
| Title screen | 4 |
| Main menu | 4 |
| Intro / story movies | 4 |
| Mission title, mission intro, briefing | 4 |
| Aircraft selection | 4 |
| Mission (pre-mission cutscene, gameplay, end replay) | 2 |
| Results / debriefing | 5 |

Same pattern for missions 1 and 2.

Title screen extra values:

| Address | Bytes | Meaning |
|---|---|---|
| `0x0029F38E` (sub-state, u16) | `06 00` | 6 |
| `0x00410E7C` (menu page, size unknown) | `00 00 02 00` | u16 pair: 0, 2 — or u32 0x00020000 |

### Session 2 — writes to the requested-scene field

Breakpoint: memory write, `0x0029F38C`, size 2. Value = u16 read after the write.

| # | Transition (on screen) | Value | pc | ra |
|---|---|---|---|---|
| 1 | Boot (black screen) | 0 | `0x8000EBE0` | `0x8000E170` |
| 2 | Boot (black screen) | 0 | `0x0010067C` | `0x00100208` |
| 3 | Boot → logos | 6 | `0x00100ABC` | `0x00100A84` |
| 4a | Logos → game intro (no save in slot 1) | 2 | `0x00100ABC` | `0x002613E0` |
| 4a' | Game intro → title screen | 4 | `0x00100ABC` | `0x00178CE8` |
| 4b | Logos → title screen (save present in slot 1) | 4 | `0x00100ABC` | `0x002613E0` |
| 5 | Title/main menu (New Game) → intro/story movie | 4 | `0x00100ABC` | `0x0022D8B4` |
| 6 | Story movie → mission title | 4 | `0x00100ABC` | `0x002262FC` |
| 7 | Mission title → mission intro | 4 | `0x00100ABC` | `0x002307A8` |
| 8 | Mission intro → briefing | 4 | `0x00100ABC` | `0x002262FC` |
| 9 | Briefing (start mission) → aircraft selection | 4 | `0x00100ABC` | `0x00230FE8` |
| 10 | Aircraft selection → mission start + cutscene | 2 | `0x00100ABC` | `0x00235B38` |
| 11 | Pre-mission cutscene → gameplay (+ end replay) | 2 | `0x00100ABC` | `0x00178D64` |
| 12 | Gameplay/replay → results/debriefing | 5 | `0x00100ABC` | `0x0016E450` |
| 13 | Results/debriefing → story movie | 4 | `0x00100ABC` | `0x0024CB60` |

Scenes 1 and 3 were never requested on this route.

#### Manual pause snapshots (not breakpoint hits)

Taken while the game was waiting for input; nothing was written to `0x0029F38C`.

| Situation | pc | ra |
|---|---|---|
| After the story movie: NEXT / RETRY / SAVE prompt | `0x00264530` | `0x00261C4C` |
| No memory card: "Cannot save without memory card (PS2) in MEMORY CARD slot 1 (75KB necessary)" | `0x00264530` | `0x00261C4C` |
| Save prompt at the end of briefings/debriefings (card present) | `0x00264530` | `0x00261C4C` |

### Session 3 — startup with and without a memory card

Without a memory card in slot 1:
1. Boot writes 0, 0, then 6 (same as Session 2 rows 1–3).
2. The game stops on the "Cannot save without memory card" message (scene stays 6). See the pause snapshot above.
3. After a card is inserted, the game continues from the same call site (pc `0x00100ABC`, ra `0x002613E0`):
   - card with an AC04 save: request 4 (title screen);
   - empty card: request 2 (in-engine game intro), as on a normal boot with no save.

With a memory card: the startup path is Session 2 row 4a (no AC04 save) or 4b (save present).

### Session 4 — execution breakpoint at 0x0010AF10

Breakpoint hits after the aircraft selection screen.

| Mission | Hit | Shown afterwards | a0 | a1 | ra |
|---|---|---|---|---|---|
| 1 | 1 | Mission title | `0x0010A54C` | `0x00000001` | `0x0010A040` |
| 1 | 2 | Pre-gameplay cutscene | `0x0010A554` | `0x00000001` | `0x0010A040` |
| 1 | 3 | Loading screen with text | `0x0010A54C` | `0x00000001` | `0x0010A040` |
| 1 | 4 | Gameplay | `0x0010A554` | `0x00000001` | `0x0010A040` |
| 2 | 1 | Mission title | `0x0010A54C` | `0x00000001` | `0x0010A040` |
| 2 | 2 | Gameplay | `0x0010A554` | `0x00000001` | `0x0010A040` |

### Interpretation (hypotheses — confirm in Ghidra)

1. `0x00100ABC` is the store inside RequestScene (0x00100AB0). Every scene change goes through it; `ra` identifies the requester.
2. Rows 1–2 are not scene requests: `0x8000EBE0` is kernel code (memory clear at boot) and `0x0010067C` is inside InitMainContext (0x00100650), called from main.
3. Scene 2 is the in-engine mission/flight scene, also used for the game intro on first boot (Claude's "attract demo" hypothesis for RequestScene(2) in scene 6 fits this).
4. Scene 4 handles all front-end screens, including story movie playback. Scene 3 (Phase 2: "full-screen movie player") was not used for story movies; its use is still unknown (candidates: idle/attract movie on the title screen, ending/credits, theater).
5. Scene 6 exits to both 2 and 4 from the same call site (ra `0x002613E0`, near handler 0x002613D0): the target scene is computed, not two separate calls. Phase 2's description of two exits in boot.md needs correcting. The choice is consistent in every run: no AC04 save on the card → 2 (intro), save present → 4 (title), including when the card is inserted after the missing-card message.
6. Scene 2 and scene 4 re-request their own scene id for internal transitions (rows 5–9, 11).
7. The pause snapshots all sit in the syscall stub block (pc `0x00264530`) called from `0x00261C4C`: the main thread is blocked in a kernel wait inside a shared message/prompt routine, used for both the missing-card error and the save prompts.
8. 0x0010AF10 is probably not a loader taking a mission number: a1 is always 1, ra is always the scene 2 handler, and a0 alternates between two .text addresses (0x0010A54C / 0x0010A554), one per sub-state change. It looks like a sub-state runner. The mission number is probably read from a global.

### Corrections (added 2026-10-10, from static analysis)

1. **Scene 6 has two separate exits, not one computed call site** (interpretation 5). `StartupSplashUpdate` 0x00260980 ends with either `j RequestScene` at 0x00260B54 (scene 2, first-boot intro) or `j EnterMenu(0)` at 0x00260B78 (scene 4, title). `EnterMenu` itself tail-jumps to `RequestScene`. Because both are tail jumps, `ra` still holds the return address of the `jal 0x00260980` in scene 6 sub 9 (0x002613E0), so both exits show the same `ra`. The choice is bit 0x4000 of the scene work flags (AC04 save found). Details: `../game/boot.md` (scene 6 section).
2. **The pause snapshots are the vblank wait, not a shared prompt routine** (interpretation 7). `ra` 0x00261C4C is inside `sceGsSyncV`; pc 0x00264530 is inside `PollVblankStart` 0x00264520, a libgraph helper that busy-polls `INTC_STAT` bit 2. It is not a syscall stub. Any pause while the game idles lands in the main loop's per-frame wait. Details: `../game/boot.md` (transition table), `../game/threads-timing.md`.
3. **0x0010AF10 takes no arguments** (session 4, interpretation 8). It is `LoadFlightDataEntry`, which picks the DATA index from the scene 2 sub-state and the game-state block. `a0` held the jump-table target just used by `EnterFlightSubState` (0x0010A54C = sub-state 2 case, 0x0010A554 = sub-state 3 case, both `j 0x0010AF10`), and `a1` was a leftover. Details: `../game/boot.md` (scene 2, "Mission data loading").

## Run 02

- Emulator: PCSX2 2.8.2, original disc image (NTSC-U, SLUS_201.52)
- Register values: low 32 bits only (upper words of the 128-bit EE registers are leftovers and irrelevant).
- "Observed" data is raw. "Interpretation" items are hypotheses to confirm in Ghidra.

### Session 5 — frame rate

#### Observed

| Situation | FPS | VPS |
|---|---|---|
| Video cutscenes (SMV) | 29.97 | 59.94 |
| In-engine cutscenes | 59.94 | 59.94 |
| Heavy gameplay scene | 59.94 | 59.94 |
| Light gameplay scene | 59.94 | 59.94 |

Frame counter at `0x0029F3A8` (only the low 2 bytes were recorded), 60 Frame Advance presses each:

| Scene | Before | After | Difference |
|---|---|---|---|
| Heavy | `F6 67` = 0x67F6 | `32 68` = 0x6832 | 0x3C = 60 |
| Light | `C0 E5` = 0xE5C0 | `FC 5E` (as noted) | see note |

Note: the light-scene "after" value is probably a transcription slip for `FC E5` (= 0xE5FC, difference 60). As written (0x5EFC) it would be lower than "before". To be re-measured if it matters.

#### Interpretation

- Game logic and in-engine rendering run at the full 59.94 Hz, in heavy and light scenes alike. No 30 Hz mode was observed.
- Video cutscenes are shown at 29.97 fps (each MPEG frame held for 2 vblanks), the standard NTSC video rate.

### Session 6 — thread creation

#### Observed

Execute breakpoint on CreateThread (`0x00263EA0`):

| When | ra |
|---|---|
| Boot | `0x0026B444` |
| Saving and loading (memory card) | no hits |

#### Interpretation

- `0x0026B444` is the expected SDK helper: on MIPS, `ra` = call address + 8 (call + delay slot), so the call is at `0x0026B43C`, as predicted.
- The same +8 applies to the game's two thread-creation sites: watch for ra `0x0025C034` / `0x0025C20C` if `0x0025C02C` / `0x0025C204` are call addresses.
- Neither game-owned thread was created during memory card saves or loads in this configuration (no HDD attached in PCSX2).

### Session 7 — DATA entries loaded per mission

Execute breakpoint at `0x0010B020`. a2 = DATA index.

#### Observed

| Mission | Hit | a2 (DATA) | Sub-state `0x0029F38E` | Mission id `0x0029F377` | Campaign no. `0x002A15B8` | On screen afterwards |
|---|---|---|---|---|---|---|
| 1 | 1 | 0x26 (38) | 2 | 0x25 | 1 | Black screen after aircraft selection |
| 1 | 2 | 0x7C (124) | 3 | 0x25 | 1 | Mission title and objective |
| 1 | 3 | 0x53 (83) | 2 | 0x01 | 1 | End of in-engine cutscene |
| 1 | 4 | 0x67 (103) | 3 | 0x01 | 1 | Text on black background, then gameplay |
| 2 | 1 | 0x54 (84) | 2 | 0x02 | 2 | Black screen after aircraft selection |
| 2 | 2 | 0x67 (103) | 3 | 0x02 | 2 | Mission title and objective, then gameplay |
| 3 | 1 | 0x55 (85) | 2 | 0x03 | 3 | Black screen after aircraft selection |
| 3 | 2 | 0x67 (103) | 3 | 0x03 | 3 | Mission title and objective, then gameplay |
| 4 | 1 | 0x62 (98) | 2 | 0x10 | 4 | Black screen after aircraft selection |
| 4 | 2 | 0x67 (103) | 3 | 0x10 | 4 | Mission title and objective, then gameplay |

#### Interpretation

- Sub-state 2 loads mission data, sub-state 3 the aircraft data (as Phase 2.5 found).
- Campaign missions use DATA = mission id + 0x52: ids 1, 2, 3, 0x10 → 83, 84, 85, 98. The other formula, id + 1, was used only for id 0x25 (DATA 38), an in-engine opening sequence played before mission 1 with its own aircraft entry (0x7C). The first-boot intro (id 0x23) presumably uses id + 1 as well.
- The campaign → mission id lookup is confirmed for mission 4 (campaign 4 → id 0x10 = 16), but the predicted DATA 17 was wrong: campaign missions take the + 0x52 branch, giving 98.
- With ids 1–18 for the campaign, + 0x52 covers DATA 83–100, the end of the mission-shaped range 1–100. Entries 1–82 would then be id + 1 for ids 0–81 (in-engine sequences and other non-campaign content); to confirm.
- Aircraft entry 0x67 (103) in missions 1–4 is probably the aircraft selected by the player (101–172 = 24 aircraft × 3 variants).

### Corrections (added 2026-10-10, from static analysis)

1. **The `+1` / `+0x52` choice does not depend on campaign vs. non-campaign content** (session 7, interpretation bullets 2 and 4). In `LoadFlightDataEntry` (branch 0x0010AF48–0x0010AF70) the game uses `DATA = id + 0x52` when the mission id is below 0x13 **and** the player's aircraft id `game[0x00]` has its bit set in `g_AltMissionAircraftMask` (gp 0x004104C8, a never-written constant 0x00204114 = aircraft 2, 4, 8, 14, 21). Otherwise it uses `id + 1`. The runs in session 7 flew aircraft 2 (aircraft entry 0x67), which is in the mask, so missions 1–4 used `+0x52`. With an aircraft outside the mask the same missions are expected to use DATA 2, 3, 4, 17 (not yet checked). Id 0x25 (≥ 0x13) always uses `+1`. The two variants share the map and differ in units, unit models and message data. Details: `../game/boot.md` (scene 2), `../game/file-loading.md`, `docs/formats/MISSION.md`.

## Run 03 (skeleton checks)

- Emulator: PCSX2 2.8.2, original disc image (NTSC-U, SLUS_201.52), USB ports = None, Fast Boot.
- Method: an Execute breakpoint, then a **save state** taken while stopped at the breakpoint (save state compression = Uncompressed). The values below were read from the save states with `tools/p2s_read.py`.
- The save states stay outside the repo (path in `CLAUDE.local.md`), because they contain game memory. Only the derived values below are recorded here.
- **Timing check:** the User copied the first 16 bytes at 0x00429800 by hand at breakpoint 0x00100130 (`02 00 00 00`, then 12 zero bytes). The save state holds the same bytes. So a state saved while stopped at a breakpoint shows the memory at that breakpoint.

States taken (2026-10-10):

| File | Taken at |
|---|---|
| `bp_00100220.p2s` | `main`, right after `InitWorkHeap` returns |
| `bp_00100130.p2s` | `InitSubsystems`, after `InitSoundRpc` returned 1 |
| `bp_00100138.p2s` | `InitSubsystems`, after the `InitVagStream` loop |
| `bp_001097F8.p2s` | `ReadUsbJoy`, after the `sceSifCheckStatRpc` loop (first hit, frame 1) |
| `mainMenu.p2s` | main menu, no breakpoint |
| `inGame_mission1.p2s` | flying in mission 1, no breakpoint |

### Check 1: work heap

Observed:

| State | size 0x00411784 | base 0x00411788 | offset 0x0041178C | mark 0x00411790 |
|---|---|---|---|---|
| bp_00100220 | 0x01A318A4 | 0x004CC760 | 0 | 0 |
| bp_00100130, bp_00100138 | same | same | 0 | 0 |
| bp_001097F8 | same | same | 0x3820 | 0x3820 |
| mainMenu | same | same | 0x0083D6E0 | 0x3820 |
| inGame_mission1 | same | same | 0x019C5180 | 0x3820 |

Interpretation:
- The work heap is **0x01A318A4 bytes (27,465,892, about 26.2 MiB)**, at 0x004CC760–0x01EFE004. The port must give `InitWorkHeap` exactly this base and size (ADR-001).
- The boot allocates 0x3820 bytes before `SetWorkHeapMark`. Those blocks survive every scene change.
- In mission 1, the heap is **98.4 % used**. Only 0x6C724 bytes (about 434 KiB) are free. `WorkHeapAlloc` returns 0 when the heap is exhausted, and callers do not check for 0. So the port's heap cannot be smaller than the PS2's, and allocation sizes must match exactly. An allocation that grows on the host (e.g. a host-sized struct) would break this.

### Check 2: first SOUND and VAG replies

SOUND (reply buffer 0 at 0x00429800, after `InitSoundRpc` returned 1):
- Bytes 0–3 = `02 00 00 00`. The remaining 0x5FC bytes are zero. Reply buffer 1 (0x00429E00) is all zero.
- In frame 1 (`bp_001097F8`) the buffer is unchanged: no fno 1 reply had arrived there yet.

Interpretation (H):
- `word 0` is the record count, so the count is 2. That matches "voice records + 2" with no voice records.
- Both record ids are 0, so `UpdateSoundRpc` skips both records.
- The HLE answers fno 0 with this exact layout: count 2, then zeros.

VAG (reply buffer 0 at 0x0042CC90, after the `InitVagStream` loop):

| Offset | Value | Meaning (`../subsystems/sound.md`, `VagAllInit`) |
|---|---|---|
| +0x00 | 0x0008DA40 | IOP address of the stream area `vag_buf` (`rbuf[0]`) |
| +0x24..+0x30 | 1, 1, 1, 1 | ring index per channel 0–3 (`rbuf[9..12]`) |
| +0x50 | 0x200 | `rbuf[20]`, meaning not yet known |
| +0x60 | 0x200 | `rbuf[24]`, meaning not yet known |
| rest of 0x100 | 0 | |

Buffer 1 (0x0042CD90) is zero. The EE buffer pointers at 0x00353F78 are cmd 0x0042CA90/0x0042CB90 and reply 0x0042CC90/0x0042CD90, and the current index 0x00353F88 = 1 after the swap.

Interpretation:
- `InitVagStream` computes `rbuf[0] + 0x180` = 0x0008DBC0 as the first ring segment:
  - slots 0 and 1: 3 × 0x20000 each, from 0x0008DBC0;
  - slots 2 to 5: 3 × 0x10000 each, from the same base 0x0008DBC0.
- Both layouts end at 0x0014DBC0, inside the 0xC01C0-byte `vag_buf`. The two layouts overlap, so slots 0/1 and slots 2–5 cannot stream at the same time without sharing IOP memory. That is behaviour the HLE must keep: the EE only passes these addresses back, and the HLE owns the memory.
- In `mainMenu`, IOP RAM at 0x0008DBC0 holds SPU-ADPCM-looking data (menu music streaming), which agrees.
- The HLE can return any IOP address here, as long as the EE-side arithmetic stays consistent. The EE never dereferences it; it only passes the addresses back in VAG commands (to be confirmed when `0x0021F448` is fully understood). Using 0x0008DA40 keeps RAM comparable with PCSX2.

### Check 3: USB joystick reply, no USB device

0x004204C0, 0x80 bytes, read as u32 words (only the low byte is used by `ReadUsbJoy`):

| Offset | Value | Field (from `ReadUsbJoy`) |
|---|---|---|
| +0x00 | 0 | connection bits: bit 0 = joystick 0, bit 1 = joystick 1 (0 = none) |
| +0x04 | 0 | copied to output byte 0 |
| +0x08..+0x1C | 0x80 ×6 | joystick 0 axes (centred) |
| +0x20, +0x24 | 0, 0 | joystick 0 hat / extra axes |
| +0x28, +0x2C | 0xFF, 0xFF | joystick 0 button bytes (all released, active low) |
| +0x30..+0x44 | 0x80 ×6 | joystick 1 axes |
| +0x48, +0x4C | 0, 0 | joystick 1 hat / extra axes |
| +0x50, +0x54 | 0xFF, 0xFF | joystick 1 button bytes |
| +0x58..+0x7F | 0 | |

Interpretation:
- With no device, `ReadUsbJoy` sets output bytes 1 and 0x15 to 0xFF (`(bits & 1) - 1`). `UpdatePads` then ignores the USB path.
- The HLE returns exactly this block every frame.

### Effect on the docs

- `../port/skeleton-readiness.md` risks 1, 4 and 5 are closed with these values.
- `../open-questions.md`: the skeleton PCSX2 checks item is closed.
- ADR-001 working assumption ("exact work heap size the PS2 gets") now has the number. The high fill level in mission 1 is recorded there as a constraint.

## Run 04 (GS dumps, timing, labels)

- Emulator: PCSX2 2.8.2, original disc image (NTSC-U, SLUS_201.52).
- Inputs from the User: eight single-frame GS dumps, save states taken at breakpoints or in specific views, two screenshots, and values read by hand. All files stay outside the repo (paths in `CLAUDE.local.md`): they contain game data. Values were read from the save states with `tools/p2s_read.py` and from the dumps with `tools/research/gsdump.py`; only derived values are recorded here.
- Checks refer to the numbering of `procedures.md` at the time of the run.

### Check 1: GS dumps

Dumps: title screen, main menu, first briefing, story intro movie, first-boot in-engine cutscene, in-engine demonstration, hangar (F-4E), gameplay (attacking bombers, explosions, HUD). Each holds 4 vsyncs.

Result: the full feature table is in `../platform/gs-subset.md`, "Measured in GS dumps". The points that change earlier assumptions:
- **trilinear mipmapping** is used (TEX1 0x36C: MXL 3, MMIN 5, MTBA 1, K = 0xF28) on 512×512 fogged triangle fans;
- **CT24 textures** (TCC 0) are used;
- triangle fans and strips with STQ and fog carry the 3D geometry;
- all transfers are host → local: no local → local copy, no readback.

### Check 2: in-menu movie frame pacing

Save states `bp_00238870_a` (hit 1) and `bp_00238870_b` (hit 11), breakpoint at `MovieFrameSync` 0x00238870, briefing of campaign mission 2.

| Field | Hit 1 | Hit 11 | Difference over 10 iterations |
|---|---|---|---|
| `frame_count` (0x0029F3A8) | 0x33E2 | 0x33F6 | **20** |
| `frame_count_unpaused` (0x0029F3AC) | 0x32A8 | 0x32B2 | 10 |
| `scene_frame` (0x0029F3B0) | 0x0E | 0x18 | 10 |

Interpretation:
- One movie loop iteration is **one main-loop iteration** (`scene_frame` and `frame_count_unpaused` +1 each) in which `frame_count` advances by 2: once in the main loop and once in `MovieFrameSync`.
- Each iteration takes **two fields**. The main loop resets `RCNT0_COUNT` after its vsync wait; the movie sub-state resets it again; `MovieFrameSync` waits for 0x118 = 280 H-blanks, longer than one NTSC field (262.5 lines), so the next main-loop vsync wait ends at the second field. This matches the 29.97 fps measured in session 5 (`sessions.md` run 02) and keeps `frame_count` counting fields.
- Port consequence: an in-menu movie iteration is two ticks of the 59.94 Hz clock, and `frame_count` gets +1 at each tick. Timer 0 need not be emulated line by line: "wait until the next field" is equivalent, as long as decoding stays within one field (ADR-002).

### Check 4: half-resolution pass `func_00142460`

- User: the breakpoint hits in mission 3 in the external (third-person) view, aircraft F-4E. Skipping one call (`pc = ra`) shows no visible difference in a screenshot.
- GS dump of the in-engine demonstration (the gameplay dump has no such pass):
  - untextured, alpha-blended Gouraud triangles and strips are drawn into a **256-pixel-wide target at block 0x2500** (FBP 0x128, FBW 4) with Z24 at block 0x2A00 (FBP 0x150), the two addresses `func_001483A0` passes to `func_00142460`;
  - sprite passes copy between 0x2500 and 0x2A00 with `(0 − Cs)·FIX + Cd` and FIX blends (blur/decay);
  - the result is **added** to the frame in 256×16 strips with `(Cs − 0)·As + Cd`.
- Interpretation: an additive glow of the aircraft, rendered at half resolution. A single skipped frame changes only a faint glow, which is why the screenshots look the same. Status: glow pass, confirmed as additive; what lights it (H: specular highlights) is open.

### Check 5: CONTROL TYPE label

Option word 0x002A15D0 after changing CONTROL TYPE in OPTIONS: **EASY = 0x0009**, **NORMAL = 0x0008**. Bit 0 set = EASY (Euler flight path), clear = NORMAL (matrix path); bit 3 is set in both (vibration on).

### Check 6: player view byte

Save states `hud_only_view`, `cockpit_view`, `external_view` (mission 1 gameplay, scene 2 sub 0xB). Read through the unit pointer table: unit 0 = u32 at 0x002A1F70 (0x01165D30), player state = unit +0x44 (0x012463B0), view byte = state +0x149.

| State | +0x149 |
|---|---|
| `hud_only_view` | 0 |
| `cockpit_view` | 1 |
| `external_view` | 2 |

So 0 = first-person HUD-only view, 1 = cockpit view, 2 = external view. The procedure text read 0x002A1FB4 as if 0x002A1F70 were an array of structs; it is an array of 255 pointers to 0xA0-byte unit structs (corrected in `../subsystems/flight.md` and `../subsystems/mission-flow.md`).

### Check 9: scripted flight segment, scene 2 sub-states 0x14–0x1B

- Save state and screenshot `mission3_surpass_return_line`: breakpoint 0x00179BC0 hit when the player crossed the return line in campaign mission 3. Scene 2, sub 0x14, mission id 3, difficulty 2. Screen: AWACS "Approaching return line. Follow the controller's orders for landing.", HUD "RETURN TO CARRIER", landing ladder.
- So subs 0x14–0x18 are the **carrier landing**.
- 0x0017ABD8 (sub 0x19 init) did not hit in that run. Code review after the run: sub 0x19 is requested by the rearm-menu item TAKEOFF (`func_0017DBC0`). The rearm menu (sub 0x1D) opens when the player presses START during the approach (subs 0x14/0x15) **before** the mission is complete (mission flag 4 clear); with the mission complete START skips to the results. Subs 0x19–0x1B are therefore the carrier **takeoff** after a rearm and are reachable. Still to observe in PCSX2 (`procedures.md`).

### Check 13: pause-menu item for ctx flag 0x20000

- User: memory at 0x0029F380 while paused: `00 01 60 00 …` → `flags` = 0x00600100 (0x100 pause, 0x200000 and 0x400000 from the startup scene). No pause item flipped 0x00020000.
- Code review after the run: the normal pause menu (`func_0017DCC0`, ctx flag 0x100) offers the HUD ON/OFF item (callbacks 0x0017DB28/0x0017DAF0, which toggle 0x20000) **only when 0x002A0318 bit 0 is set (game cleared once)**; otherwise the list is one item shorter. The observation (no item flips the flag before a clear) matches the code. Item labels: RETURN TO GAME, REPLAY, RETRY, QUIT MISSION, CAPTIONS, HUD.

## Run 05 (VU0 skinning vectors, carrier takeoff)

- Emulator: PCSX2 2.8.2, original disc image (NTSC-U, SLUS_201.52).
- Inputs from the User: breakpoint register values, debugger screenshots, save states and in-game screenshots. All files stay outside the repo (paths in `CLAUDE.local.md`, folders `Breakpoints/00225110/` and `SaveStates/mission3_landing_and_takeoff/`): they contain game data. Only derived values are recorded here.
- Checks refer to the numbering of `procedures.md` at the time of the run (2026-10-11).

### Check 1: VU0 micro skinning test vectors (partial)

Run: new campaign game, the in-engine opening sequence before mission 1 (mission id 0x25, `../subsystems/mission-flow.md`). Breakpoints at the three skinning loops.

| Breakpoint | Hit | Notes |
|---|---|---|
| 0x002250E8 (entry 0x00, part type 0) | no | |
| 0x00225110 (entry 0x10, part type 1 and rigid path) | **yes** | registers below; `vu0_00225110_before.p2s`, `vu0_00225110_after.p2s`, screenshots `VU0f_before.png`, `VU0i_before.png` |
| 0x00225140 (entry 0x30, part type 3, morphing) | no | |

Registers at the hit (low word of each 128-bit GPR):

| Reg | Value | Meaning |
|---|---|---|
| a0 | 0x0036C240 | output buffer, one quadword (VF01) stored per call; static buffer that `FUN_002236A0` passes for every part |
| a1 | 0x00000000 | → VI01 (`ctc2 a1, vi1`): VU0 data-memory input pointer |
| a2 | 0x00000080 | → VI03 (`ctc2 a2, vi3`): second VU0 data-memory pointer |
| a3 | 0x0000000F | loop count: 15 `vcallms 0x10` calls, a0 += 0x10 each |
| ra | 0x002246DC | return into `FUN_00224410` (call at 0x002246D4) = the **part type 1** skinning function |

Findings:
- The hit came through `FUN_002236A0` → `FUN_00224410`, which `FUN_002236A0` calls for parts whose type field (+0x140) is **1**. The earlier labels in `procedures.md` and in three `coverage-functions.csv` notes had types 0 and 1 swapped; corrected (`../port/validation.md`, finding V-1).
- Part type 0 comes from the character data (`FUN_00220308` copies it from the part table). Type 3 is set at run time by `FUN_00223F58` when a morph track is active and reset to 1 by `FUN_00223E20`. So the opening sequence 0x25 has no type-0 parts and no active morph track.
- VU0 registers in the save state: PCSX2 2.8.2 stores VU0's `VURegs` in `PCSX2 Internal Structures.dat` at offset **0x36FE** (VF00–VF31, 16 bytes each, then VI00–VI15, 16 bytes each). The values match the `VU0f_before`/`VU0i_before` screenshots exactly. `tools/p2s_read.py STATE vu0` now prints them, so the missing "after" VU0f screenshot is not needed.
- Before → after: VF01–VF06 change (VF01/VF03/VF04 hold the last vertex's integer-converted output, VF05/VF06 the last transformed position); VF07–VF31 and VI unchanged. VF16–VF31 hold the bone matrices loaded by the caller (VF28–VF31 = VF16–VF19 in this batch).
- The full input set (VU0 registers, `vu0Memory.bin`, scratchpad 0x70002200…, input vertex arrays) and output (`0x0036C240` buffer) are all in the two states. Test vectors are extracted from them at test time (`tests/` will read the User's states by path from a local config, never from the repo).

Still needed: hits on 0x002250E8 and 0x00225140, from a sequence that has type-0 parts or an active morph track. New procedure in `procedures.md` (check 1).

### Check 2: carrier takeoff after a rearm

Run: mission 3, state `mission3_surpass_return_line`, return line crossed, START pressed on the approach.

| Event | Sub-state (u16 at 0x0029F38E) |
|---|---|
| bp 0x0017A7C8 (rearm menu opens) | 0x1D |
| bp 0x0017ABD8 (takeoff starts) | 0x19 |
| sequence watched | 0x19 → 0x1A → 0x1B → 0x0B |

- Menu: TAKEOFF and CHANGE WEAPON, cockpit view; CHANGE WEAPON shows the owned special weapon as "UGBM / 08" (the "NAME / ammo" template, `../subsystems/mission-flow.md`).
- After the takeoff, flight controls work normally.
- State `mission3_takeoff.p2s` saved at 0x0017ABD8; screenshots `takeoff_menu.png`, `change_weapon.png`, `takeoff_start.png`, `takeoff_completed.png`.

Result: the code reading of subs 0x19–0x1B and 0x1D is confirmed. Nothing to change in the docs except marking it confirmed.

## Run 06 (slice S1 batch)

- Emulator: PCSX2 2.8.2, original disc image (NTSC-U, SLUS_201.52).
- Run by the User on 2026-10-14, following `procedures.md` item S1 (now removed from there).
- All files stay outside the repo (folder `Slices/Slice1/`, path in `CLAUDE.local.md`): save states, screenshots and the two folder memory cards contain game or save data. Only derived values are recorded here.

### Files

| Run | Card in slot 1 | Save states | Screenshots |
|---|---|---|---|
| 1 | card S (folder card with the AC04 save `BASLUS-20152AC04`) | `s1_S_sub7`, `s1_S_sub9`, `s1_S_exit` | `s1_S_splash0`, `s1_S_splash1` |
| 2 | card E (folder card with an empty `_pcsx2_superblock`: PCSX2 reports it **unformatted**) | `s1_E_sub7`, `s1_E_exit` | `s1_E_splash0` |
| 3 | none (slot 1 unchecked) | `s1_N_sub7` | `s1_N_msg_1`, `s1_N_msg_2` |

Screenshots are 640×448 (the scene 6 draw buffer size, `../subsystems/results-startup.md` sub 0). Card folders: `card S.ps2`, `card E.ps2`.

### `frame_count` at each state (u32 at 0x0029F3A8, read with `tools/p2s_read.py`)

| State | Breakpoint | frame_count |
|---|---|---|
| `s1_S_sub7` | 0x00261378 (sub 7, first hit) | 0x4F (79) |
| `s1_S_sub9` | 0x002613D0 (sub 9, first hit) | 0x51 (81) |
| `s1_S_exit` | 0x00260B78 (`EnterMenu(0)` exit) | 0x236 (566) |
| `s1_E_sub7` | 0x00261378 | 0x3B (59) |
| `s1_E_exit` | 0x00260B54 (`RequestScene(2)` exit) | 0x12F (303) |
| `s1_N_sub7` | 0x00261378 | 0x3A (58) |

- Card S: sub 7 → sub 9 in 2 frames, so no card message was shown.
- The sub-7 frame differs by about 20 frames between card S and cards E/none: the probe takes longer when the AC04 directory exists (second `sceMcGetDir` of the save file, `../subsystems/results-startup.md` sub 6). The exact count is the calibration target of the port's libmc timing model.

### Observations

- **Card message windows appear only with no card** (run 3). Two windows:
  1. "Cannot save without memory card (PS2) in MEMORY CARD slot 1 (75KB necessary)";
  2. "Start game anyway? X Button: YES / Δ Button: NO". The User pressed **X** (Cross), which led to the splash image.
- Card E has an empty `_pcsx2_superblock`, so PCSX2 reports an **unformatted** card (`sceMcGetInfo` result -2, format 0, free 0). It shows **no** message (result -2 suppresses it in `EvaluateCardResult`) and goes to the no-save intro after splash image 0.
- Card S shows no message; both splash images, then the title.

## Run 07 (slice S2a batch)

- Emulator: PCSX2 2.8.2, original disc image (NTSC-U, SLUS_201.52).
- Run by the User on 2026-10-14, following `procedures.md` item S2a (now removed from there).
- All files stay outside the repo (folder `Slices/Slice2a/`, path in `CLAUDE.local.md`). Only derived values are recorded here.

### Files

Cold boot with **card S** in slot 1 (the S1 card with the AC04 save).

| Breakpoint | Event | Save state | `frame_count` (0x0029F3A8) |
|---|---|---|---|
| 0x0022DF00 | title sub 6, first hit | `s2a_title_sub6` | 597 |
| 0x0022D748 | Start accepted | `s2a_start` | 1153 |
| 0x0022D84C | menu tree active, first hit | `s2a_menu_active` | 1185 |
| 0x0022D880 | confirm in the menu | `s2a_menu_confirm` | 1829 |

Screenshots (640×448): `s2a_title` (title faded in), `s2a_menu` (main menu about 2 s after it became active), `s2a_menu_open` (about 2 s after the confirm: NEW GAME opened its difficulty column with VERY EASY / EASY / NORMAL / HARD, cursor on NORMAL, help "Level of difficulty suitable for average players").

Extra file: `s1_bug_1.png`, a copy of `../Slices/Slice1/s1_S_splash1.png` (the AC04 PROJECT splash), sent with the report of the S1 bug below.

### User notes

- Before the confirm the User moved the cursor down to the second item (LOAD) and immediately back to NEW GAME. The RAM shows it: the SE request counter 0x00410D00 is 17 at the confirm (1 at "menu active"), voice 24 last played sound 1 (cursor tick) as request 14, and the help line was re-typed (extra sound-9 ticks).
- **S1 bug report:** in PCSX2 the AC04 PROJECT logo screen is all blue; the port showed a green area in the lower part of the screen. Cause and fix: `../port/validation.md` S2a-5 (24-bit image transfers split over GIF packets lost the bytes of the split pixel).
- Music: no comment on the title/menu music by ear yet.

### Values used in the comparison

- `s2a_title_sub6`: menu fade object `g_MenuFade` +4 (time) = 0x3EFFFFF8, +0xC (value) = 0x3F7F4C7E: 30 fade steps of 1/60, with PCSX2's add.s model (`../port/validation.md` S2a-3). The port enters sub 6 after 10 steps (DATA 0xB0 loads about 20 frames faster).
- Title time (`g_TitleMenu` 0x00CCFF80 +0x68C, f32 s): 9.283 at Start, 9.817 at menu active, 20.55 at the confirm.
- SE request counter 0x00410D00: 0 at Start, 1 at menu active, 17 at the confirm.

Comparison results: `../port/validation.md` §2f (S2a-3 to S2a-5).

## Run 08 (slice S2b batch, S2a-R)

- Emulator: PCSX2 2.8.2, original disc image (NTSC-U, SLUS_201.52).
- Run by the User on 2026-10-14, following `procedures.md` items S2b and S2a-R (now removed from there).
- All files stay outside the repo (folders `Slices/Slice2b/` and `Slices/Slice2a/`, paths in `CLAUDE.local.md`). Only derived values are recorded here.

### S2a-R

- User's graphics settings for all screenshots so far: renderer **Automatic** (default; hardware), blending accuracy **Basic**, deinterlacing **Automatic** (default).
- `s2a_menu_sw.png`: `s2a_menu_active.p2s` resumed with the **software** renderer, about 2 s later.
- Result: `../port/validation.md` S2a-4 (software and hardware agree; the port is the outlier, radial pattern).

### S2b files

Cold boot with **card S** in slot 1, one session. Buttons as on a PS2 pad (the User used an Xbox pad: A = X, Y = Triangle).

| Breakpoint | Event | Save state | `frame_count` (0x0029F3A8) |
|---|---|---|---|
| 0x002437C8 | CONTROLLER SETUP, first hit | `s2b_ctrl_enter` | 1939 |
| 0x00226450 | CONTROLLER SETUP left | `s2b_ctrl_exit` | 2976 |
| 0x00244410 | SOUND SETTINGS, first hit | `s2b_snd_enter` | 3506 |
| 0x00226450 | SOUND SETTINGS left | `s2b_snd_exit` | 4853 |
| 0x002423A0 | PERSONAL DATA, first hit | `s2b_rec_enter` | 5192 |
| 0x0023EB68 | LOAD, first hit | `s2b_load_enter` | 8397 |
| 0x00226450 | LOAD left after loading slot 1 | `s2b_load_exit` | 10230 |
| 0x00240F68 | SCREEN SETTINGS, first hit | `s2b_opt_enter` | 11452 |
| 0x00226450 | SCREEN SETTINGS left (16:9 set) | `s2b_opt_exit` | 13766 |

Not taken: `s2b_load_done` (breakpoint 0x0023D4FC did not fire: it is in the read of the save image when the LOAD page opens, which had already happened when the breakpoint was added; procedure error, `../port/validation.md` S2b-1). `s2b_bgm_enter`/`s2b_bgm.png` (optional; no card with a cleared campaign).

Screenshots (640×448): `s2b_ctrl`, `s2b_snd`, `s2b_rec`, `s2b_rec_2` (PERSONAL DATA after scrolling; the User reports 4 views, all looking like the first), `s2b_load`, `s2b_menu_loaded` (main menu with CONTINUE), `s2b_opt`, `s2b_opt_changed` (SCREEN RATIO 16:9), `s2b_menu_169` (options list in 16:9), `s2b_ctrl_169` (CONTROLLER SETUP in 16:9).

### User's button sequence

1. Title: Start. Main menu → OPTIONS: Down ×4, X.
2. OPTIONS → CONTROLLER SETUP: X. Inside: Down, Down, Down, Left, X (VIBRATION ON; leaves the page).
3. → SOUND SETTINGS: Down, Down, X. Inside: Down, Left, Left, Up, Left, X (BGM level −2, output MONO; "the sound is lower").
4. → PERSONAL DATA: Down, X. Views: Down ×3.
5. Triangle, Triangle, Up ×3, X (LOAD). Load slot 1: X, X. CONTINUE appeared in the main menu.
6. Down ×3, X (OPTIONS), Up, Up, X (SCREEN SETTINGS). BRIGHTNESS: Down, X, X (already at the maximum in this save). SCREEN RATIO: Down, X, Down, X. Triangle (leaves).
7. Up, X (CONTROLLER SETUP again; "nothing seems changed apart from the background").

### User notes

- Brightness of the loaded save is at the maximum. Before LOAD both sides have brightness 3 (0x002A15CD); after LOAD the screenshots on both sides are taken with the save's value.
- Between LOAD entry and LOAD exit the User opened the LOAD page several times to see why breakpoint 0x0023D4FC did not fire (confirmed by the User; visible in RAM, `../port/validation.md` S2b-1, "User sequence").

### Values used in the comparison

- Records page allocation pad: PCSX2 object at 0x00CCFFF0 (`frame_count & 0xF` = 6 at creation); the port replay is aligned to the same value.
- Records page data-load timer at entry: 16/60 s on PCSX2 (0x3E888888), 4/60 s in the port.
- SE request counter 0x00410D00: LOAD entry 399, exit 480 (port 371 / 408).
- Menu BGM record 0x0042D1D0 at LOAD exit: slot 0, tracks {4, 4}, states {0, 0} (port {3, 4}); from SCREEN SETTINGS entry on equal: tracks {4, 4}, state 2 on slot 0.

## Run 09 (slice S3 batch)

- Emulator: PCSX2 2.8.2, original disc image (NTSC-U, SLUS_201.52).
- Run by the User on 2026-10-15, following `procedures.md` item S3 (now removed from there).
- All files stay outside the repo (folder `Slices/Slice3/`, paths in `CLAUDE.local.md`). Only derived values are recorded here.

### Files

Cold boot with **card S** in slot 1, one session. Buttons: on the title **Start**, then **X** (NEW GAME), then **X** (NORMAL), as in the port's pad script.

| Breakpoint | Event | Save state | `frame_count` (0x0029F3A8) | Movie page (+0x288 iterations, +0x290 time) |
|---|---|---|---|---|
| 0x00239248 | `MenuMoviePlay`, first hit | `s3_ss01_start` | 12310 | 0, 0.0 s |
| 0x00238870 | `MovieFrameSync`, first hit after `s3_ss01_b` | `s3_ss01_mid` | 13686 | 689, 22.03 s (SMV reader: 662 pictures) |
| 0x002392A8 | `MenuMovieEnd` | `s3_ss01_end` | 20714 | 0, 139.0 s (reader already freed) |

- Screenshots: `s3_ss01_a.png` (about 5 s in: star field with a trail), `s3_ss01_b.png` (about 20 s: the TV with the "LIVE" report, subtitle "And in turn how that cannon brought war upon us.").
- Sound: `s3_ss01_audio.mp4`, Media Capture with audio only, codec **FLAC** in an MP4 container, 48 kHz, stereo, 16-bit, 42.0 s from just after the first picture. Decoded with `tools/research/flac_mp4.py`.
- `s3_m1_movie.png`: after the mission 1 title card and X, a second movie starts. About 5 s in, it shows a **projector**: the briefing movie of mission 1 (slice S4).
- User's notes: X and Start can be pressed during SS01 to skip straight to the second movie (`../subsystems/frontend.md`: skip state +0x274, skippable after 30 frames).

### Results

Port comparison: `../port/validation.md` S3-3 to S3-5.
- RAM: 0 unexplained ranges at all three checkpoints with `../port/ramdiff-expected-s3.csv`.
- Pacing: from the mid checkpoint to `MenuMovieEnd`, 7028 frames in PCSX2 and in the port. Before the first picture the port is 42 frames earlier (subtitle font load and VAG stop latency; `../open-questions.md`, I/O latency).
- Answered: IPU alpha 0x80 (every pixel of the image buffer), PCM layout (0x200-byte L/R blocks), the audio half reported by SOUND (reader +0x70 equal mid-movie). Still open, without game effect: `sceMpeg.frameCount` (PCSX2 46, port 47 in the same chunk) and the `flags` word (PCSX2 0x19B).

## Run 10 (slice S4 batch, runs A-C)

- Emulator: PCSX2 2.8.2, original disc image (NTSC-U, SLUS_201.52). User's settings (`inis/PCSX2.ini`): FPU, VU0 and VU1 rounding mode 3 (chop), DAZ on; the GameDB entry sets the EE to round to nearest (`eeRoundMode: 0`).
- Run by the User on 2026-10-15 (run A) and 2026-10-16 (runs B and C), following `procedures.md` item S4 (now removed from there).
- All files stay outside the repo (folder `Slices/Slice4/`, paths in `CLAUDE.local.md`). Only derived values are recorded here.

### Run A: mission title to hangar (2026-10-15, card S, New Game)

States `s4_title`, `s4_brief`, `s4_map`, `s4_campaign`, `s4_hangar`, `s4_scene2`; screenshots `s4_title`, `s4_brief`, `s4_map`, `s4_campaign`, `s4_hangar_a`. User's notes: X did not skip SS01 (Start did); Start (not X) left the briefing map; only the F-4E was available in the hangar (no `s4_hangar_b`); 0x00100AB0 fired after X on the aircraft and X on UGBM.

### Run B: hangar purchase (2026-10-16, card S)

Buttons: on the title **Start**, **Down**, **X** (LOAD), **X**, **X** (slot 1). After the load the main menu shows CONTINUE. **Up**, **X** (CONTINUE). **Start** x3 (title card, briefing movie, briefing map). **Down**, **Down**, **X** (BUY), **X** (AIRCRAFT). **X**, **X** on the F-5E (the shop preselects the second aircraft).

| Item | Value |
|---|---|
| Item bought | F-5E, price 52,000 |
| Credits before / after | 161,000 / 109,000 |
| 0x00233F10 (`CommitHangarSelection`) | fired on **Triangle** (leaving the hangar), not on the purchase: state `s4_commit_before` |
| Step Out | stopped at 0x00235C00 (ra; the cancel path of `AircraftSelectRun`): state `s4_commit_after` |
| Screenshot | `s4_shop.png`: the campaign menu after leaving, BUY highlighted |

### Run C: briefing map frames, VS mission select (2026-10-16, card S)

- As run A, skipping the first cutscene, the mission title and the second cutscene with Start, Start, Start, up to the briefing movie. When the movie ended, 0x00238440 fired for the first time; 2nd hit `s4_map2`, 3rd hit `s4_map3`.
- Reset; on the title **Start**, **Down** x3 (VS MODE), **X**: 0x0023C708 → `s4_vs`; screenshots `s4_vs.png` (about 3 s later), `s4_vs_right.png` (after **Right**, VS STAGE 02).

### Results

Port comparison: `../port/validation.md` S4-1 to S4-5.
- Map frames 2/3: the 1/16-pixel sprite differences came from the VU0 rounding. PCSX2 runs COP2 macro instructions under the EE's MXCSR (round to nearest), not with the VU0 chop setting; the port now does the same (`src/common/vu0_math.h`). Map camera and terrain sprites match.
- `CommitHangarSelection`: byte-exact against the run B states (test `tests/test_vectors_s4.cpp`); the port's replay of run B reaches the commit with identical purchase state, credits and selection.
- VS mission select: 0 unexplained after one input-timing entry; both screens match.

## Run 11 (slice S5 block A batch, S2b-B)

- Emulator: PCSX2 2.8.2, original disc image (NTSC-U, SLUS_201.52), settings as in `sessions.md` run 10.
- Run by the User on 2026-10-16/17, following `procedures.md` items S5-A (run 1, run 2 mission 1) and S2b-B (now removed from there).
- All files stay outside the repo (folders `Slices/Slice5/`, `Slices/Slice2b/`, `Memory Card Folder/`; paths in `CLAUDE.local.md`). Only derived values are recorded here.

### S5-A run 1: calibration recording (cold boot)

Files: `s5_cal.p2m2`, `s5_cal_start.p2s`, `s5_cal_confirm.p2s`.

| Breakpoint | Recording frame |
|---|---|
| 0x0022D748 (Start accepted on the title) | 1002 |
| 0x0022D880 (confirm in the main menu, NEW GAME) | 1723 |

Use: the port replays `s5_cal.p2m2` with `--replay-p2m2`; the port tick at which the same checkpoints fire, minus these frame numbers, gives `--p2m2-offset` (`../port/slices.md` §B P2). Port result (2026-10-17): offset **153**, same `frame_count` (837 / 1558) at both breakpoints, 0 unexplained RAM ranges (`../port/validation.md` S5-P2).

### S5-A run 2: mission 1 start

Files: `s5_m1_loading.png`, `s5_m1_start.p2s` (0x0010A230), GS dump `s5_m1_frame.zst`, `s5_m1_frame.png`.

- Sequence seen by the User: hangar (default aircraft F-4E) → special weapon chosen → **Start** needed to start the mission (the "01 SITTING DUCK" objectives page with PRESS START BUTTON) → an in-engine **cutscene on an aircraft carrier** → loading screen → mission 1.
- The loading screen (`s5_m1_loading.png`) is not a mission card: black screen with two lines of blue narration text in the centre ("Amidst the blue skies, a link from past to future. / The sheltering wings of the protector...").
- The first gameplay picture (`s5_m1_frame.png`): overcast sky over cloud/sea with the full HUD (PTS/TGT, TIME 09'58"86, AWACS radio "AWACS here. Callsign SkyEye. Do you read?", pitch ladder, SPEED 258, ALT 6666, radar, weapons GUN 0650 / MSSL 052 / UGBM 08, DMG 000%, "MISSION START" box).
- Missions 3 and 4 of run 2: not run yet (next session).

### S2b-B: BGM player

Files: `s2b_bgm_enter.p2s` (0x00244EA0), `s2b_bgm.png`; card `Memory Card Folder/Mcd001_converted.ps2` (slot 3 of its save: campaign fully cleared).

- Route: title → **Start** → **Down** → **X** (LOAD) → save slot 3 → **Down** ×5 → **X** → **Down** ×5 → **X** (MUSIC PLAYER; 0x00244EA0 fires). Inside, **X** plays the first track "ISAF".
- Screenshot: the player page (640 wide): button legend row (up, down, left, right, X, triangle), "BGM NUMBER > #01 / BGM TITLE > ISAF" box, a 2×18 grid of track cells (first one selected), text "Use Left Analog Stick to select music tracks / X Button: PLAY / Triangle Button: CANCEL".
- For port runs, copy the card under `out/` first (`--memcard`), never use the original folder.

## Run 12 (slice S5 block A batch, run 2 missions 3 and 4)

- Emulator: PCSX2 2.8.2, original disc image (NTSC-U, SLUS_201.52), settings as in `sessions.md` run 10.
- Run by the User on 2026-10-17, following `procedures.md` item S5-A run 2 (missions 3 and 4). With this the S5-A batch is complete and removed from `procedures.md`.
- All files stay outside the repo (folder `Slices/Slice5/`; paths in `CLAUDE.local.md`). Only derived values are recorded here.

### Mission 3

Files: `s5_m3_loading.png`, `s5_m3_loading_2.png`, `s5_m3_start.p2s` (0x0010A230), GS dump `s5_m3_frame.zst`, `s5_m3_frame.png`.

- Sequence: LOAD file 1, CONTINUE, title card and briefing skipped, START MISSION, default aircraft (state: aircraft 2 = F-4E, colour 0, weapon 0; mission id 3, mode 1, difficulty 3).
- `s5_m3_loading.png`: the loading screen "03 / "THE NORTHERN EYE" / MISSION OBJECTIVES: DESTROY ALL INDICATED TARGETS AND RETURN TO BASE. / NOW LOADING...".
- `s5_m3_loading_2.png`: a fully black frame. The User saw a black screen between the loading screen and gameplay. Explained in the port (`../port/validation.md` S5-A3): after Start the loading screen fades to black, the breakpoint 0x0010A230 runs on the black frame, then the mission fades in; the screenshot was taken while paused there.
- `s5_m3_frame.png`: snowy mountains, HUD with TIME 15'00"00, AWACS "This combat area is within enemy airspace. Keep an eye out for bandits.", SPEED 261, ALT 4300.

### Mission 4

Files: `s5_m4_loading.png`, `s5_m4_loading_2.png` (black), `s5_m4_start.p2s`, GS dump `s5_m4_frame.zst`, `s5_m4_frame.png`.

- Sequence: LOAD file 4 (MISSION 04 / BLOCKADE), then as mission 3 (state: mission id 0x10, aircraft 2, colour 0, weapon 0, mode 1, difficulty 3).
- `s5_m4_frame.png`: high altitude over a hazy mountain range, HUD TIME 12'00"00, SPEED 260, ALT 13333, AWACS "Mobius 1, SkyEye here. You should be getting visual on the transports soon.", a wingman to the right.

### Notes

- The GS dumps came zstd-compressed again (PCSX2 setting "GS dump compression"); unpacked for research as before.
- Effect clock: the word at effect block +0x6249C read by `InitEffects` before it is written in the scene held 0x80000000 (mission 3), 0x80312B1F (mission 1, run 11) and 0 (mission 4) in these states: memory left from the briefing (`../port/validation.md` S5-A3).
- New card from the User: `Memory Card Folder/Mcd001_converted.ps2` with a fully cleared campaign (already used for S2b-B in run 11). It gives access to the BGM player, the interlude viewer and all missions in free mission.

## Run 13 (check M6: mission 6 track stream)

- Emulator: PCSX2 2.8.2, original disc image. Run by the User on 2026-10-25, following `procedures.md` M6 (now removed from the list).
- Files (outside the repo, `TestSessions/M6/`): `0x00411584.p2s` (mission frame 4,364, after the step 3 read), `0x004BA300_01_54_40.p2s` (frame 47,138, 1'54"40 left on the mission timer).
- Result: mission 6 played to 1'54" before the end with no problem; the write breakpoint on 0x004BA300 never fired. The radio header still holds 0x0131EE40 (as after the mission load in the port).
- Track stream (parsed from RAM): 786 blocks, stream position 0x00471C48 (227 KB used of 512 KB). Units recorded per second: 7 in second 0 (the player and units 44-49), 22-37 later. The port recorded 110 in second 0 and about 90 later: the units it recorded were exactly the ones the original skips. Cause and fix: `../port/validation.md` §2ah.

