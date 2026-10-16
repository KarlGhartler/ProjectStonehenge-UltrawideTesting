# PCSX2 debugging sessions: run 10 (slice S4 batch, runs A-C)

- Emulator: PCSX2 2.8.2, original disc image (NTSC-U, SLUS_201.52). Owner's settings (`inis/PCSX2.ini`): FPU, VU0 and VU1 rounding mode 3 (chop), DAZ on; the GameDB entry sets the EE to round to nearest (`eeRoundMode: 0`).
- Run by the owner on 2026-10-15 (run A) and 2026-10-16 (runs B and C), following `procedures.md` item S4 (now removed from there).
- All files stay outside the repo (folder `Slices/Slice4/`, paths in `CLAUDE.local.md`). Only derived values are recorded here.

## Run A: mission title to hangar (2026-10-15, card S, New Game)

States `s4_title`, `s4_brief`, `s4_map`, `s4_campaign`, `s4_hangar`, `s4_scene2`; screenshots `s4_title`, `s4_brief`, `s4_map`, `s4_campaign`, `s4_hangar_a`. Owner's notes: X did not skip SS01 (Start did); Start (not X) left the briefing map; only the F-4E was available in the hangar (no `s4_hangar_b`); 0x00100AB0 fired after X on the aircraft and X on UGBM.

## Run B: hangar purchase (2026-10-16, card S)

Buttons: on the title **Start**, **Down**, **X** (LOAD), **X**, **X** (slot 1). After the load the main menu shows CONTINUE. **Up**, **X** (CONTINUE). **Start** x3 (title card, briefing movie, briefing map). **Down**, **Down**, **X** (BUY), **X** (AIRCRAFT). **X**, **X** on the F-5E (the shop preselects the second aircraft).

| Item | Value |
|---|---|
| Item bought | F-5E, price 52,000 |
| Credits before / after | 161,000 / 109,000 |
| 0x00233F10 (`CommitHangarSelection`) | fired on **Triangle** (leaving the hangar), not on the purchase: state `s4_commit_before` |
| Step Out | stopped at 0x00235C00 (ra; the cancel path of `AircraftSelectRun`): state `s4_commit_after` |
| Screenshot | `s4_shop.png`: the campaign menu after leaving, BUY highlighted |

## Run C: briefing map frames, VS mission select (2026-10-16, card S)

- As run A, skipping the first cutscene, the mission title and the second cutscene with Start, Start, Start, up to the briefing movie. When the movie ended, 0x00238440 fired for the first time; 2nd hit `s4_map2`, 3rd hit `s4_map3`.
- Reset; on the title **Start**, **Down** x3 (VS MODE), **X**: 0x0023C708 → `s4_vs`; screenshots `s4_vs.png` (about 3 s later), `s4_vs_right.png` (after **Right**, VS STAGE 02).

## Results

Port comparison: `../port/validation.md` S4-1 to S4-5.
- Map frames 2/3: the 1/16-pixel sprite differences came from the VU0 rounding. PCSX2 runs COP2 macro instructions under the EE's MXCSR (round to nearest), not with the VU0 chop setting; the port now does the same (`src/common/vu0_math.h`). Map camera and terrain sprites match.
- `CommitHangarSelection`: byte-exact against the run B states (test `tests/test_vectors_s4.cpp`); the port's replay of run B reaches the commit with identical purchase state, credits and selection.
- VS mission select: 0 unexplained after one input-timing entry; both screens match.
