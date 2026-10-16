# PCSX2 debugging sessions: run 07 (slice S2a batch)

- Emulator: PCSX2 2.8.2, original disc image (NTSC-U, SLUS_201.52).
- Run by the owner on 2026-10-14, following `procedures.md` item S2a (now removed from there).
- All files stay outside the repo (folder `Slices/Slice2a/`, path in `CLAUDE.local.md`). Only derived values are recorded here.

## Files

Cold boot with **card S** in slot 1 (the S1 card with the AC04 save).

| Breakpoint | Event | Save state | `frame_count` (0x0029F3A8) |
|---|---|---|---|
| 0x0022DF00 | title sub 6, first hit | `s2a_title_sub6` | 597 |
| 0x0022D748 | Start accepted | `s2a_start` | 1153 |
| 0x0022D84C | menu tree active, first hit | `s2a_menu_active` | 1185 |
| 0x0022D880 | confirm in the menu | `s2a_menu_confirm` | 1829 |

Screenshots (640×448): `s2a_title` (title faded in), `s2a_menu` (main menu about 2 s after it became active), `s2a_menu_open` (about 2 s after the confirm: NEW GAME opened its difficulty column with VERY EASY / EASY / NORMAL / HARD, cursor on NORMAL, help "Level of difficulty suitable for average players").

Extra file: `s1_bug_1.png`, a copy of `../Slices/Slice1/s1_S_splash1.png` (the AC04 PROJECT splash), sent with the report of the S1 bug below.

## Owner notes

- Before the confirm the owner moved the cursor down to the second item (LOAD) and immediately back to NEW GAME. The RAM shows it: the SE request counter 0x00410D00 is 17 at the confirm (1 at "menu active"), voice 24 last played sound 1 (cursor tick) as request 14, and the help line was re-typed (extra sound-9 ticks).
- **S1 bug report:** in PCSX2 the AC04 PROJECT logo screen is all blue; the port showed a green area in the lower part of the screen. Cause and fix: `../port/validation.md` S2a-5 (24-bit image transfers split over GIF packets lost the bytes of the split pixel).
- Music: no comment on the title/menu music by ear yet.

## Values used in the comparison

- `s2a_title_sub6`: menu fade object `g_MenuFade` +4 (time) = 0x3EFFFFF8, +0xC (value) = 0x3F7F4C7E: 30 fade steps of 1/60, with PCSX2's add.s model (`../port/validation.md` S2a-3). The port enters sub 6 after 10 steps (DATA 0xB0 loads about 20 frames faster).
- Title time (`g_TitleMenu` 0x00CCFF80 +0x68C, f32 s): 9.283 at Start, 9.817 at menu active, 20.55 at the confirm.
- SE request counter 0x00410D00: 0 at Start, 1 at menu active, 17 at the confirm.

Comparison results: `../port/validation.md` §2f (S2a-3 to S2a-5).
