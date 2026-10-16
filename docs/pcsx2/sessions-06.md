# PCSX2 debugging sessions: run 06 (slice S1 batch)

- Emulator: PCSX2 2.8.2, original disc image (NTSC-U, SLUS_201.52).
- Run by the owner on 2026-10-14, following `procedures.md` item S1 (now removed from there).
- All files stay outside the repo (folder `Slices/Slice1/`, path in `CLAUDE.local.md`): save states, screenshots and the two folder memory cards contain game or save data. Only derived values are recorded here.

## Files

| Run | Card in slot 1 | Save states | Screenshots |
|---|---|---|---|
| 1 | card S (folder card with the AC04 save `BASLUS-20152AC04`) | `s1_S_sub7`, `s1_S_sub9`, `s1_S_exit` | `s1_S_splash0`, `s1_S_splash1` |
| 2 | card E (folder card with an empty `_pcsx2_superblock`: PCSX2 reports it **unformatted**) | `s1_E_sub7`, `s1_E_exit` | `s1_E_splash0` |
| 3 | none (slot 1 unchecked) | `s1_N_sub7` | `s1_N_msg_1`, `s1_N_msg_2` |

Screenshots are 640×448 (the scene 6 draw buffer size, `../subsystems/results-startup.md` sub 0). Card folders: `card S.ps2`, `card E.ps2`.

## `frame_count` at each state (u32 at 0x0029F3A8, read with `tools/p2s_read.py`)

| State | Breakpoint | frame_count |
|---|---|---|
| `s1_S_sub7` | 0x00261378 (sub 7, first hit) | 0x4F (79) |
| `s1_S_sub9` | 0x002613D0 (sub 9, first hit) | 0x51 (81) |
| `s1_S_exit` | 0x00260B78 (`EnterMenu(0)` exit) | 0x236 (566) |
| `s1_E_sub7` | 0x00261378 | 0x3B (59) |
| `s1_E_exit` | 0x00260B54 (`RequestScene(2)` exit) | 0x12F (303) |
| `s1_N_sub7` | 0x00261378 | 0x3A (58) |

- Card S: sub 7 → sub 9 in 2 frames, so no card message was shown.
- The sub-7 frame differs by about 20 frames between card S and cards E/none: the probe takes longer when the AC04 directory exists (second `sceMcGetDir` of the save file, `results-startup.md` sub 6). The exact count is the calibration target of the port's libmc timing model.

## Observations

- **Card message windows appear only with no card** (run 3). Two windows:
  1. "Cannot save without memory card (PS2) in MEMORY CARD slot 1 (75KB necessary)";
  2. "Start game anyway? X Button: YES / Δ Button: NO". The owner pressed **X** (Cross), which led to the splash image.
- Card E has an empty `_pcsx2_superblock`, so PCSX2 reports an **unformatted** card (`sceMcGetInfo` result -2, format 0, free 0). It shows **no** message (result -2 suppresses it in `EvaluateCardResult`) and goes to the no-save intro after splash image 0.
- Card S shows no message; both splash images, then the title.
