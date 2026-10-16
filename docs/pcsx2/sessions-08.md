# PCSX2 debugging sessions: run 08 (slice S2b batch, S2a-R)

- Emulator: PCSX2 2.8.2, original disc image (NTSC-U, SLUS_201.52).
- Run by the owner on 2026-10-14, following `procedures.md` items S2b and S2a-R (now removed from there).
- All files stay outside the repo (folders `Slices/Slice2b/` and `Slices/Slice2a/`, paths in `CLAUDE.local.md`). Only derived values are recorded here.

## S2a-R

- Owner's graphics settings for all screenshots so far: renderer **Automatic** (default; hardware), blending accuracy **Basic**, deinterlacing **Automatic** (default).
- `s2a_menu_sw.png`: `s2a_menu_active.p2s` resumed with the **software** renderer, about 2 s later.
- Result: `../port/validation.md` S2a-4 (software and hardware agree; the port is the outlier, radial pattern).

## S2b files

Cold boot with **card S** in slot 1, one session. Buttons as on a PS2 pad (the owner used an Xbox pad: A = X, Y = Triangle).

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

Screenshots (640×448): `s2b_ctrl`, `s2b_snd`, `s2b_rec`, `s2b_rec_2` (PERSONAL DATA after scrolling; the owner reports 4 views, all looking like the first), `s2b_load`, `s2b_menu_loaded` (main menu with CONTINUE), `s2b_opt`, `s2b_opt_changed` (SCREEN RATIO 16:9), `s2b_menu_169` (options list in 16:9), `s2b_ctrl_169` (CONTROLLER SETUP in 16:9).

## Owner's button sequence

1. Title: Start. Main menu → OPTIONS: Down ×4, X.
2. OPTIONS → CONTROLLER SETUP: X. Inside: Down, Down, Down, Left, X (VIBRATION ON; leaves the page).
3. → SOUND SETTINGS: Down, Down, X. Inside: Down, Left, Left, Up, Left, X (BGM level −2, output MONO; "the sound is lower").
4. → PERSONAL DATA: Down, X. Views: Down ×3.
5. Triangle, Triangle, Up ×3, X (LOAD). Load slot 1: X, X. CONTINUE appeared in the main menu.
6. Down ×3, X (OPTIONS), Up, Up, X (SCREEN SETTINGS). BRIGHTNESS: Down, X, X (already at the maximum in this save). SCREEN RATIO: Down, X, Down, X. Triangle (leaves).
7. Up, X (CONTROLLER SETUP again; "nothing seems changed apart from the background").

## Owner notes

- Brightness of the loaded save is at the maximum. Before LOAD both sides have brightness 3 (0x002A15CD); after LOAD the screenshots on both sides are taken with the save's value.
- Between LOAD entry and LOAD exit the owner opened the LOAD page several times to see why breakpoint 0x0023D4FC did not fire (confirmed by the owner; visible in RAM, `../port/validation.md` S2b-1, "owner sequence").

## Values used in the comparison

- Records page allocation pad: PCSX2 object at 0x00CCFFF0 (`frame_count & 0xF` = 6 at creation); the port replay is aligned to the same value.
- Records page data-load timer at entry: 16/60 s on PCSX2 (0x3E888888), 4/60 s in the port.
- SE request counter 0x00410D00: LOAD entry 399, exit 480 (port 371 / 408).
- Menu BGM record 0x0042D1D0 at LOAD exit: slot 0, tracks {4, 4}, states {0, 0} (port {3, 4}); from SCREEN SETTINGS entry on equal: tracks {4, 4}, state 2 on slot 0.
