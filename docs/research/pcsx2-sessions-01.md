# PCSX2 debugging sessions — run 01

- Emulator: PCSX2 2.8.2, original disc image (NTSC-U, SLUS_201.52)
- Register values: only the low 32 bits are shown. EE registers are 128-bit; the upper words are irrelevant here.
- "Observed" sections are raw data. "Interpretation" sections are hypotheses to confirm in Ghidra.

## Session 1 — current scene id per screen

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

## Session 2 — writes to the requested-scene field

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

### Manual pause snapshots (not breakpoint hits)

Taken while the game was waiting for input; nothing was written to `0x0029F38C`.

| Situation | pc | ra |
|---|---|---|
| After the story movie: NEXT / RETRY / SAVE prompt | `0x00264530` | `0x00261C4C` |
| No memory card: "Cannot save without memory card (PS2) in MEMORY CARD slot 1 (75KB necessary)" | `0x00264530` | `0x00261C4C` |
| Save prompt at the end of briefings/debriefings (card present) | `0x00264530` | `0x00261C4C` |

## Session 3 — startup with and without a memory card

Without a memory card in slot 1:
1. Boot writes 0, 0, then 6 (same as Session 2 rows 1–3).
2. The game stops on the "Cannot save without memory card" message (scene stays 6). See the pause snapshot above.
3. After a card is inserted, the game continues from the same call site (pc `0x00100ABC`, ra `0x002613E0`):
   - card with an AC04 save: request 4 (title screen);
   - empty card: request 2 (in-engine game intro), as on a normal boot with no save.

With a memory card: the startup path is Session 2 row 4a (no AC04 save) or 4b (save present).

## Session 4 — execution breakpoint at 0x0010AF10

Breakpoint hits after the aircraft selection screen.

| Mission | Hit | Shown afterwards | a0 | a1 | ra |
|---|---|---|---|---|---|
| 1 | 1 | Mission title | `0x0010A54C` | `0x00000001` | `0x0010A040` |
| 1 | 2 | Pre-gameplay cutscene | `0x0010A554` | `0x00000001` | `0x0010A040` |
| 1 | 3 | Loading screen with text | `0x0010A54C` | `0x00000001` | `0x0010A040` |
| 1 | 4 | Gameplay | `0x0010A554` | `0x00000001` | `0x0010A040` |
| 2 | 1 | Mission title | `0x0010A54C` | `0x00000001` | `0x0010A040` |
| 2 | 2 | Gameplay | `0x0010A554` | `0x00000001` | `0x0010A040` |

## Interpretation (hypotheses — confirm in Ghidra)

1. `0x00100ABC` is the store inside RequestScene (0x00100AB0). Every scene change goes through it; `ra` identifies the requester.
2. Rows 1–2 are not scene requests: `0x8000EBE0` is kernel code (memory clear at boot) and `0x0010067C` is inside InitMainContext (0x00100650), called from main.
3. Scene 2 is the in-engine mission/flight scene, also used for the game intro on first boot (Claude's "attract demo" hypothesis for RequestScene(2) in scene 6 fits this).
4. Scene 4 handles all front-end screens, including story movie playback. Scene 3 (Phase 2: "full-screen movie player") was not used for story movies; its use is still unknown (candidates: idle/attract movie on the title screen, ending/credits, theater).
5. Scene 6 exits to both 2 and 4 from the same call site (ra `0x002613E0`, near handler 0x002613D0): the target scene is computed, not two separate calls. Phase 2's description of two exits in scenes.md needs correcting. The choice is consistent in every run: no AC04 save on the card → 2 (intro), save present → 4 (title), including when the card is inserted after the missing-card message.
6. Scene 2 and scene 4 re-request their own scene id for internal transitions (rows 5–9, 11).
7. The pause snapshots all sit in the syscall stub block (pc `0x00264530`) called from `0x00261C4C`: the main thread is blocked in a kernel wait inside a shared message/prompt routine, used for both the missing-card error and the save prompts.
8. 0x0010AF10 is probably not a loader taking a mission number: a1 is always 1, ra is always the scene 2 handler, and a0 alternates between two .text addresses (0x0010A54C / 0x0010A554), one per sub-state change. It looks like a sub-state runner. The mission number is probably read from a global.

## Corrections (added 2026-10-10, from static analysis)

1. **Scene 6 has two separate exits, not one computed call site** (interpretation 5). `StartupSplashUpdate` 0x00260980 ends with either `j RequestScene` at 0x00260B54 (scene 2, first-boot intro) or `j EnterMenu(0)` at 0x00260B78 (scene 4, title). `EnterMenu` itself tail-jumps to `RequestScene`. Because both are tail jumps, `ra` still holds the return address of the `jal 0x00260980` in scene 6 sub 9 (0x002613E0), so both exits show the same `ra`. The choice is bit 0x4000 of the scene work flags (AC04 save found). Details: `scenes.md` (scene 6 section).
2. **The pause snapshots are the vblank wait, not a shared prompt routine** (interpretation 7). `ra` 0x00261C4C is inside `sceGsSyncV`; pc 0x00264530 is inside `PollVblankStart` 0x00264520, a libgraph helper that busy-polls `INTC_STAT` bit 2. It is not a syscall stub. Any pause while the game idles lands in the main loop's per-frame wait. Details: `scenes.md` (transition table), `threads-timing.md`.
3. **0x0010AF10 takes no arguments** (session 4, interpretation 8). It is `LoadFlightDataEntry`, which picks the DATA index from the scene 2 sub-state and the game-state block. `a0` held the jump-table target just used by `EnterFlightSubState` (0x0010A54C = sub-state 2 case, 0x0010A554 = sub-state 3 case, both `j 0x0010AF10`), and `a1` was a leftover. Details: `scenes.md` (scene 2, "Mission data loading").
