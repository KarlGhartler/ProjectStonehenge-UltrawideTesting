# PCSX2 debugging sessions: run 05 (VU0 skinning vectors, carrier takeoff)

- Emulator: PCSX2 2.8.2, original disc image (NTSC-U, SLUS_201.52).
- Inputs from the owner: breakpoint register values, debugger screenshots, save states and in-game screenshots. All files stay outside the repo (paths in `CLAUDE.local.md`, folders `Breakpoints/00225110/` and `SaveStates/mission3_landing_and_takeoff/`): they contain game data. Only derived values are recorded here.
- Checks refer to the numbering of `procedures.md` at the time of the run (2026-10-11).

## Check 1: VU0 micro skinning test vectors (partial)

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
- The full input set (VU0 registers, `vu0Memory.bin`, scratchpad 0x70002200…, input vertex arrays) and output (`0x0036C240` buffer) are all in the two states. Test vectors are extracted from them at test time (`tests/` will read the owner's states by path from a local config, never from the repo).

Still needed: hits on 0x002250E8 and 0x00225140, from a sequence that has type-0 parts or an active morph track. New procedure in `procedures.md` (check 1).

## Check 2: carrier takeoff after a rearm

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
