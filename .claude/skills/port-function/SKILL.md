---
name: port-function
description: Translate one function from SLUS_201.52 (PS2 EE MIPS R5900) into C++ for the PC port. Use when asked to port, translate or decompile a specific function or address.
---

# Port one function

1. Get the function from Ghidra via MCP: disassembly, decompiler output, callers and callees.
2. Check it is not inside `.vutext`, `.vudata` or a `.DVP.overlay.*` section. If it is, stop and follow `.claude/rules/vu-microcode.md`.
3. Decide whether it is Sony SDK, libc or libgcc code. If yes: name it, add it to `docs/symbols.csv` with kind `sdk`, and implement or stub it in `src/sce/` instead of `src/game/`.
4. Determine parameter and return types from callers and register use. Check `gp`-relative globals resolve to named addresses.
5. Compare decompiler output with the disassembly wherever MMI, `lq`/`sq`, VU0 macro mode or COP0 instructions appear.
6. Write the C++ in the matching `src/game/` file with `// EE: 0x00XXXXXX` above it, following `.claude/rules/game-code.md`.
7. Replace every hardware access with a `src/sce/` or `src/platform/` call.
8. Rename the function, globals and structs in Ghidra and update `docs/symbols.csv`.
9. If the function is pure (math, parsing, state update without hardware), add a test in `tests/` using arguments and results captured from PCSX2 (breakpoint at entry for arguments, at return for `v0`/`f0` and touched memory).
10. Build and run the tests.
11. Report: what the function does, which names are confirmed vs. `HYPOTHESIS`, and anything left unresolved.
