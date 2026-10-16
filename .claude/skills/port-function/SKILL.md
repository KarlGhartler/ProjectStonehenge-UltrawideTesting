---
name: port-function
description: Translate one function from SLUS_201.52 (PS2 EE MIPS R5900) into C++ for the PC port. Use when asked to port, translate or decompile a specific function or address.
---

# Port one function

Functions are translated slice by slice (ADR-007, `docs/port/slices.md`); the function should belong to the current slice (`python tools/research/slices.py --list <slice>`). Validation method: `docs/port/validation-strategy.md`.

Before the first function of a slice is written, everything the slice uses from the game files must already be decompiled and documented (owner rule 2026-10-15, CLAUDE.md Workflow step 1): all reached game functions, the VU microcode programs it runs, and the data formats it reads.

1. Get the function from Ghidra via MCP: disassembly, decompiler output, callers and callees.
2. Check it is not inside `.vutext`, `.vudata` or a `.DVP.overlay.*` section. If it is, stop and follow `.claude/rules/vu-microcode.md`.
3. Decide whether it is Sony SDK, libc or libgcc code. If yes: name it, add it to `docs/symbols.csv` with kind `sdk`, and implement or stub it in `src/sce/` instead of `src/game/`.
4. Determine parameter and return types from callers and register use. Check `gp`-relative globals resolve to named addresses.
5. Compare decompiler output with the disassembly wherever MMI, `lq`/`sq`, VU0 macro mode or COP0 instructions appear.
6. Check the appendix of `docs/game/random.md` for random reads (`NextRandomFloat`, libc `rand`, VU0 R, effect LCG, sound LFSR) in this function and its callees: the translation keeps every read in the original order, including draw-phase reads.
7. Write the C++ in the matching `src/game/` file with `// EE: 0x00XXXXXX` above it, following `.claude/rules/game-code.md`. Every float `add.s`/`sub.s`/`adda`/`suba`/`madd`/`msub` becomes `fpu::Add`/`fpu::Sub`/`fpu::MulAdd`, never C++ `+`/`-` on floats. Count those instructions in the disassembly and check that each one has its helper call.
8. Replace every hardware access with a `src/sce/` or `src/platform/` call.
9. Rename the function, globals and structs in Ghidra and update `docs/symbols.csv`; log every rename and comment in `docs/rename-log.csv`. If the translation shows that a doc, CSV note or Ghidra comment is wrong, fix it there too (and log it).
10. Tests:
    - pure functions (math, parsing, state update without hardware): a test in `tests/` with arguments and results captured from PCSX2 (breakpoint at entry for arguments, at return for `v0`/`f0` and touched memory);
    - **risky functions** (random sources, flight model, AI, weapons, effects, slot rules): test vectors from PCSX2 save states at function entry and exit (`docs/port/validation-strategy.md` §4). Small scalar vectors go into the test as literals; large ones are read at test time from `AC04_TEST_STATES` and the test is skipped without it. Add the entry/exit captures to the slice's PCSX2 batch instead of asking one at a time.
11. Build and run the tests; run `python tools/research/translated.py` to refresh `docs/port/translated-functions.csv`.
12. Report: what the function does, which names are confirmed vs. `HYPOTHESIS`, and anything left unresolved.
