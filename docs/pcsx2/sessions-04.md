# PCSX2 debugging sessions: run 04 (GS dumps, timing, labels)

- Emulator: PCSX2 2.8.2, original disc image (NTSC-U, SLUS_201.52).
- Inputs from the owner: eight single-frame GS dumps, save states taken at breakpoints or in specific views, two screenshots, and values read by hand. All files stay outside the repo (paths in `CLAUDE.local.md`): they contain game data. Values were read from the save states with `tools/p2s_read.py` and from the dumps with `tools/research/gsdump.py`; only derived values are recorded here.
- Checks refer to the numbering of `procedures.md` at the time of the run.

## Check 1: GS dumps

Dumps: title screen, main menu, first briefing, story intro movie, first-boot in-engine cutscene, in-engine demonstration, hangar (F-4E), gameplay (attacking bombers, explosions, HUD). Each holds 4 vsyncs.

Result: the full feature table is in `../platform/gs-subset.md`, "Measured in GS dumps". The points that change earlier assumptions:
- **trilinear mipmapping** is used (TEX1 0x36C: MXL 3, MMIN 5, MTBA 1, K = 0xF28) on 512×512 fogged triangle fans;
- **CT24 textures** (TCC 0) are used;
- triangle fans and strips with STQ and fog carry the 3D geometry;
- all transfers are host → local: no local → local copy, no readback.

## Check 2: in-menu movie frame pacing

Save states `bp_00238870_a` (hit 1) and `bp_00238870_b` (hit 11), breakpoint at `MovieFrameSync` 0x00238870, briefing of campaign mission 2.

| Field | Hit 1 | Hit 11 | Difference over 10 iterations |
|---|---|---|---|
| `frame_count` (0x0029F3A8) | 0x33E2 | 0x33F6 | **20** |
| `frame_count_unpaused` (0x0029F3AC) | 0x32A8 | 0x32B2 | 10 |
| `scene_frame` (0x0029F3B0) | 0x0E | 0x18 | 10 |

Interpretation:
- One movie loop iteration is **one main-loop iteration** (`scene_frame` and `frame_count_unpaused` +1 each) in which `frame_count` advances by 2: once in the main loop and once in `MovieFrameSync`.
- Each iteration takes **two fields**. The main loop resets `RCNT0_COUNT` after its vsync wait; the movie sub-state resets it again; `MovieFrameSync` waits for 0x118 = 280 H-blanks, longer than one NTSC field (262.5 lines), so the next main-loop vsync wait ends at the second field. This matches the 29.97 fps measured in session 5 (`sessions-02.md`) and keeps `frame_count` counting fields.
- Port consequence: an in-menu movie iteration is two ticks of the 59.94 Hz clock, and `frame_count` gets +1 at each tick. Timer 0 need not be emulated line by line: "wait until the next field" is equivalent, as long as decoding stays within one field (ADR-002).

## Check 4: half-resolution pass `func_00142460`

- Owner: the breakpoint hits in mission 3 in the external (third-person) view, aircraft F-4E. Skipping one call (`pc = ra`) shows no visible difference in a screenshot.
- GS dump of the in-engine demonstration (the gameplay dump has no such pass):
  - untextured, alpha-blended Gouraud triangles and strips are drawn into a **256-pixel-wide target at block 0x2500** (FBP 0x128, FBW 4) with Z24 at block 0x2A00 (FBP 0x150), the two addresses `func_001483A0` passes to `func_00142460`;
  - sprite passes copy between 0x2500 and 0x2A00 with `(0 − Cs)·FIX + Cd` and FIX blends (blur/decay);
  - the result is **added** to the frame in 256×16 strips with `(Cs − 0)·As + Cd`.
- Interpretation: an additive glow of the aircraft, rendered at half resolution. A single skipped frame changes only a faint glow, which is why the screenshots look the same. Status: glow pass, confirmed as additive; what lights it (H: specular highlights) is open.

## Check 5: CONTROL TYPE label

Option word 0x002A15D0 after changing CONTROL TYPE in OPTIONS: **EASY = 0x0009**, **NORMAL = 0x0008**. Bit 0 set = EASY (Euler flight path), clear = NORMAL (matrix path); bit 3 is set in both (vibration on).

## Check 6: player view byte

Save states `hud_only_view`, `cockpit_view`, `external_view` (mission 1 gameplay, scene 2 sub 0xB). Read through the unit pointer table: unit 0 = u32 at 0x002A1F70 (0x01165D30), player state = unit +0x44 (0x012463B0), view byte = state +0x149.

| State | +0x149 |
|---|---|
| `hud_only_view` | 0 |
| `cockpit_view` | 1 |
| `external_view` | 2 |

So 0 = first-person HUD-only view, 1 = cockpit view, 2 = external view. The procedure text read 0x002A1FB4 as if 0x002A1F70 were an array of structs; it is an array of 255 pointers to 0xA0-byte unit structs (corrected in `../subsystems/flight.md` and `../subsystems/mission-flow.md`).

## Check 9: scripted flight segment, scene 2 sub-states 0x14–0x1B

- Save state and screenshot `mission3_surpass_return_line`: breakpoint 0x00179BC0 hit when the player crossed the return line in campaign mission 3. Scene 2, sub 0x14, mission id 3, difficulty 2. Screen: AWACS "Approaching return line. Follow the controller's orders for landing.", HUD "RETURN TO CARRIER", landing ladder.
- So subs 0x14–0x18 are the **carrier landing**.
- 0x0017ABD8 (sub 0x19 init) did not hit in that run. Code review after the run: sub 0x19 is requested by the rearm-menu item TAKEOFF (`func_0017DBC0`). The rearm menu (sub 0x1D) opens when the player presses START during the approach (subs 0x14/0x15) **before** the mission is complete (mission flag 4 clear); with the mission complete START skips to the results. Subs 0x19–0x1B are therefore the carrier **takeoff** after a rearm and are reachable. Still to observe in PCSX2 (`procedures.md`).

## Check 13: pause-menu item for ctx flag 0x20000

- Owner: memory at 0x0029F380 while paused: `00 01 60 00 …` → `flags` = 0x00600100 (0x100 pause, 0x200000 and 0x400000 from the startup scene). No pause item flipped 0x00020000.
- Code review after the run: the normal pause menu (`func_0017DCC0`, ctx flag 0x100) offers the HUD ON/OFF item (callbacks 0x0017DB28/0x0017DAF0, which toggle 0x20000) **only when 0x002A0318 bit 0 is set (game cleared once)**; otherwise the list is one item shorter. The observation (no item flips the flag before a clear) matches the code. Item labels: RETURN TO GAME, REPLAY, RETRY, QUIT MISSION, CAPTIONS, HUD.
