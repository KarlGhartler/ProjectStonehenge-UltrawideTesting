# PCSX2 debugging sessions — run 02

- Emulator: PCSX2 2.8.2, original disc image (NTSC-U, SLUS_201.52)
- Register values: low 32 bits only (upper words of the 128-bit EE registers are leftovers and irrelevant).
- "Observed" data is raw. "Interpretation" items are hypotheses to confirm in Ghidra.

## Session 5 — frame rate

### Observed

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

### Interpretation

- Game logic and in-engine rendering run at the full 59.94 Hz, in heavy and light scenes alike. No 30 Hz mode was observed.
- Video cutscenes are shown at 29.97 fps (each MPEG frame held for 2 vblanks), the standard NTSC video rate.

## Session 6 — thread creation

### Observed

Execute breakpoint on CreateThread (`0x00263EA0`):

| When | ra |
|---|---|
| Boot | `0x0026B444` |
| Saving and loading (memory card) | no hits |

### Interpretation

- `0x0026B444` is the expected SDK helper: on MIPS, `ra` = call address + 8 (call + delay slot), so the call is at `0x0026B43C`, as predicted.
- The same +8 applies to the game's two thread-creation sites: watch for ra `0x0025C034` / `0x0025C20C` if `0x0025C02C` / `0x0025C204` are call addresses.
- Neither game-owned thread was created during memory card saves or loads in this configuration (no HDD attached in PCSX2).

## Session 7 — DATA entries loaded per mission

Execute breakpoint at `0x0010B020`. a2 = DATA index.

### Observed

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

### Interpretation

- Sub-state 2 loads mission data, sub-state 3 the aircraft data (as Phase 2.5 found).
- Campaign missions use DATA = mission id + 0x52: ids 1, 2, 3, 0x10 → 83, 84, 85, 98. The other formula, id + 1, was used only for id 0x25 (DATA 38), an in-engine opening sequence played before mission 1 with its own aircraft entry (0x7C). The first-boot intro (id 0x23) presumably uses id + 1 as well.
- The campaign → mission id lookup is confirmed for mission 4 (campaign 4 → id 0x10 = 16), but the predicted DATA 17 was wrong: campaign missions take the + 0x52 branch, giving 98.
- With ids 1–18 for the campaign, + 0x52 covers DATA 83–100, the end of the mission-shaped range 1–100. Entries 1–82 would then be id + 1 for ids 0–81 (in-engine sequences and other non-campaign content); to confirm.
- Aircraft entry 0x67 (103) in missions 1–4 is probably the aircraft selected by the player (101–172 = 24 aircraft × 3 variants).

## Corrections (added 2026-10-10, from static analysis)

1. **The `+1` / `+0x52` choice does not depend on campaign vs. non-campaign content** (session 7, interpretation bullets 2 and 4). In `LoadFlightDataEntry` (branch 0x0010AF48–0x0010AF70) the game uses `DATA = id + 0x52` when the mission id is below 0x13 **and** the player's aircraft id `game[0x00]` has its bit set in `g_AltMissionAircraftMask` (gp 0x004104C8, a never-written constant 0x00204114 = aircraft 2, 4, 8, 14, 21). Otherwise it uses `id + 1`. The runs in session 7 flew aircraft 2 (aircraft entry 0x67), which is in the mask, so missions 1–4 used `+0x52`. With an aircraft outside the mask the same missions are expected to use DATA 2, 3, 4, 17 (not yet checked). Id 0x25 (≥ 0x13) always uses `+1`. The two variants share the map and differ in units, unit models and message data. Details: `../game/scenes.md` (scene 2), `../game/file-loading.md`, `docs/formats/MISSION.md`.
