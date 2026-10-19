# PCSX2 debugging sessions: run 11 (slice S5 block A batch, S2b-B)

- Emulator: PCSX2 2.8.2, original disc image (NTSC-U, SLUS_201.52), settings as in `sessions-10.md`.
- Run by the owner on 2026-10-16/17, following `procedures.md` items S5-A (run 1, run 2 mission 1) and S2b-B (now removed from there).
- All files stay outside the repo (folders `Slices/Slice5/`, `Slices/Slice2b/`, `Memory Card Folder/`; paths in `CLAUDE.local.md`). Only derived values are recorded here.

## S5-A run 1: calibration recording (cold boot)

Files: `s5_cal.p2m2`, `s5_cal_start.p2s`, `s5_cal_confirm.p2s`.

| Breakpoint | Recording frame |
|---|---|
| 0x0022D748 (Start accepted on the title) | 1002 |
| 0x0022D880 (confirm in the main menu, NEW GAME) | 1723 |

Use: the port replays `s5_cal.p2m2` with `--replay-p2m2`; the port tick at which the same checkpoints fire, minus these frame numbers, gives `--p2m2-offset` (`../port/s5-plan.md` P2). Port result (2026-10-17): offset **153**, same `frame_count` (837 / 1558) at both breakpoints, 0 unexplained RAM ranges (`../port/validation.md` S5-P2).

## S5-A run 2: mission 1 start

Files: `s5_m1_loading.png`, `s5_m1_start.p2s` (0x0010A230), GS dump `s5_m1_frame.zst`, `s5_m1_frame.png`.

- Sequence seen by the owner: hangar (default aircraft F-4E) → special weapon chosen → **Start** needed to start the mission (the "01 SITTING DUCK" objectives page with PRESS START BUTTON) → an in-engine **cutscene on an aircraft carrier** → loading screen → mission 1.
- The loading screen (`s5_m1_loading.png`) is not a mission card: black screen with two lines of blue narration text in the centre ("Amidst the blue skies, a link from past to future. / The sheltering wings of the protector...").
- The first gameplay picture (`s5_m1_frame.png`): overcast sky over cloud/sea with the full HUD (PTS/TGT, TIME 09'58"86, AWACS radio "AWACS here. Callsign SkyEye. Do you read?", pitch ladder, SPEED 258, ALT 6666, radar, weapons GUN 0650 / MSSL 052 / UGBM 08, DMG 000%, "MISSION START" box).
- Missions 3 and 4 of run 2: not run yet (next session).

## S2b-B: BGM player

Files: `s2b_bgm_enter.p2s` (0x00244EA0), `s2b_bgm.png`; card `Memory Card Folder/Mcd001_converted.ps2` (slot 3 of its save: campaign fully cleared).

- Route: title → **Start** → **Down** → **X** (LOAD) → save slot 3 → **Down** ×5 → **X** → **Down** ×5 → **X** (MUSIC PLAYER; 0x00244EA0 fires). Inside, **X** plays the first track "ISAF".
- Screenshot: the player page (640 wide): button legend row (up, down, left, right, X, triangle), "BGM NUMBER > #01 / BGM TITLE > ISAF" box, a 2×18 grid of track cells (first one selected), text "Use Left Analog Stick to select music tracks / X Button: PLAY / Triangle Button: CANCEL".
- For port runs, copy the card under `out/` first (`--memcard`), never use the original folder.
