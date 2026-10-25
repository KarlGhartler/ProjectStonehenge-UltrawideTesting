# VS mode (two-player split screen)

Original content (category 1): translated like the rest of the game. Sources: the docs listed per row and Ghidra (decompiler cache `out/research/decomp/`, checked 2026-10-13). `(H)` = hypothesis. LAN play is a separate, undecided question (`../decisions/ADR-008-vs-lan-play.md`).

## 1. Summary

- Two players on one PS2, **pad port 1 and pad port 2**, in one flight scene with **two views side by side** (each view 256×448 of the 512×448 draw buffer).
- Eight VS stages (`GetMissionId` kind 3, table 0x00384800; mission ids 0x14, 0x1A–0x1F, 0x28; mission kind 2, `mission-flow.md`).
- Win rule: first player to reach the **target score** chosen in the menu; otherwise the higher score when the time runs out or both are down; leaving the area loses (§4).
- Everything else is the normal mission engine: the same flight model, weapons, AI (if the stage has units), effects, HUD, cameras, replay recording and results scene, run for two players.

## 2. Entry from the front end

| Step | Code | What happens |
|---|---|---|
| Mode menu | `MissionSelectAction` 0x0023AC70 | the mission-select page (scene 4 pages 24–30, `frontend.md`) has four modes; mode 3 = **VS Stage** |
| Stage list input | `func_0023A668` | reads `ctx->pad_buttons[0][1..2]` (port 1); **in VS mode it ORs in port 2's buttons** (`pad_buttons[1][1..2]`): either player can move the cursor and confirm |
| Target score | `MissionSelectAction` cases 0x10–0x12 | three options per stage; the choice (0..2) is stored per stage in 0x003AF180 and the target score (u16 points) is copied from table 0x003AEEF0 `[stage·3 + option]` into **`game[0x2E]`** (u16) |
| Start | `MissionSelectAction` case 8 | `game[0x25] = 2` (players), `game[0x29] = 2` (free-mission path), `game[0x27] = GetMissionId(3, n + 1)`; then the hangar/aircraft select |
| Aircraft per player | `CommitHangarSelection` 0x00233F10 | writes `game[slot·0x10 + 0/1/3]` (aircraft, colour, weapon) for player slot `slot` (+0xA0 of the hangar page): player 2 = `game[0x10/0x11/0x13]`. Only aircraft owned in the save are offered (shared by both players) |
| Flight entry | `MenuGoTo` kind 2/3 → `RequestScene(2)` | as for any mission (`boot.md` #10) |

`func_0022D030` (dead, no reference) sets a fixed two-player state (aircraft 2/0 for both): a debug VS setup (H), catalogued in `../port/dead-code.md`.

## 3. Flight scene with two players

| Part | Code | Behaviour |
|---|---|---|
| Split flag | `func_0010A668` (scene 2 setup) | `game[0x26] = 1` when the mission data says so (field at `game+0x20`, mask 0xFFFF0000000000 == 0x20000000000); forced to 0 when `game[0x25] == 1`. Clears `game[0x2D]` (winner) |
| Data load | `StoreFlightDataPointers` 0x0010B038 sub 4 | second aircraft entry from `game[0x10]`/`[0x11]` (`mission-flow.md`) |
| Views | `func_0010C9D0` | view count +0x1650 = `game[0x25]`. With `game[0x26] == 1`, view *i* gets projection width 320 (426.67 when SCREEN RATIO 16:9 is on; 640 / 853.33 halved), height 480, distance 571.26, and viewport **x = i·256, y = 0, w = 256, h = 448** (`draw_w/2`, full `draw_h`) |
| Cameras | `UpdateViewCameras` 0x0010CB80 | one camera per player (cockpit / HUD / third person per player) |
| Simulation | `FlightSimStep` 0x0010C320 | `UpdateHud` 0x0015E928 runs once per player; the radio (`func_00251050`) runs only for mission kinds 0 and 3, so **VS has no radio chatter** |
| Rules (sub 0xB) | `func_00118CC0` via 0x0011A208 | §4 |
| End (sub 0xC) | `func_00119000` via 0x0011A340 | per-player centre messages (`ShowCenterMessage` 0x0016DDB8 ids 0x11/0x12/0x13: win / lose / draw (H on which id is which)), fade each view separately (+0x1670, +0x1674), then sub 0x10 |
| HUD | `func_00167D68` | blinking indicator in the view of the player who is behind (the other player's score +0x5C is higher); x position depends on the 16:9 flag |
| Engine sound | `func_001279B0` | start/stop per player; both players share one voice (`flight.md`) |
| Pause | pause menu table 0x002B8410 | VS variant: RETURN TO GAME, RETRY, QUIT MISSION (`mission-flow.md`) |
| Results | `func_0024CBE0` | result record mode 2 (`results-startup.md`) |

The flight scene object has 5 view slots of 0x440 bytes (`rendering.md`); VS uses slots 0 and 1. Each view has its own camera, scissor (= viewport) and HUD flags, so the frame is built as two complete passes into two halves of the same draw buffer.

## 4. VS rules (`func_00118CC0`, every step of sub 0xB)

Players are units 0 and 1 (pointer table 0x002A1F70/0x002A1F74). Score = u16 at unit +0x5C. Rules work area = gp 0x004104F4 (flags +0x20, end cause +0x7D, timer +0x24); limits = gp 0x004104F0 (+0x14 time limit, +0x20 area half-size).

Checked in this order:
1. **Target reached:** P1 score ≥ `game[0x2E]` → P1 wins (P2 also at target → draw); else P2 ≥ target → P2 wins. End cause bit 0x01.
2. **Time out:** timer +0x24 ≥ limit → higher score wins, equal = draw. Cause bit 0x02.
3. **Both down:** both players' alive word (+0x158 of the unit's sub-record) is 0 → higher score wins / draw. Cause bit 0x10.
4. **Out of area:** a player whose x or z is outside ±limit loses; P1 is tested first. Cause bit 0x08.

Winner in `game[0x2D]`: 0 = player 1, 1 = player 2, 2 = draw; the flags +0x20 bits 0x10/0x20/0x30 carry the same result to the end sequence. Then `RequestSubState(0xC)`.

## 5. Shared with missions

Flight model, collision, weapons and projectile pools, effects (and their random-table reads), the AI of any stage units, cameras, HUD drawing, replay recording (`RecordReplayFrame`), sound, results scene. VS-specific code is small: the functions in §2–§4 plus the `game[0x25]`/`[0x26]` branches inside shared functions (66 functions read these bytes in the decompiler cache; `tools/research/slices.py --vs` lists them).

## 6. What the port needs

| Area | Need | Status |
|---|---|---|
| Input | pad port 2 delivered through `scePadRead(1, 0, …)` | already there: the skeleton opens both ports (`PadSystemInit` 0x00102050, `src/game/pad.cpp`), the input backend has two `PadState`s, port 2 = second gamepad (`../port/command-line.md`). Keyboard stays on port 1 |
| Renderer | scissor and viewport per view (two views per frame, `SCISSOR_1/2` and XYOFFSET as packet data) | part of the GS subset already required for missions; nothing VS-specific beyond drawing two passes into halves of one target |
| Presentation | the frame is still one 512×448 image; the ADR-006 output pass shows it at 4:3 (or anamorphic 16:9 when SCREEN RATIO is on), unchanged. On a wide host screen the picture is pillarboxed like any other screen | no change |
| Sound | the shared engine voice and per-player SE positions work through the normal SOUND stand-in | no change |
| Validation | a VS run in PCSX2 with two pads (the User needs a second controller or a keyboard mapping for pad 2) | VS slice batch |

Each half shows a 2:3 picture on a 4:3 screen (8:9 with SCREEN RATIO on); that is the original look. Showing one player's view full screen is not an original feature (only relevant to LAN play, ADR-008 §5).

## 7. Open points

- Which centre-message id is "win", "lose" and "draw" (0x11–0x13): check the text in the VS slice.
- Field at `game+0x20` that enables split screen: which byte of the mission/stage data sets it (VS slice).
- Whether any VS stage spawns AI units (stage data, VS slice).
