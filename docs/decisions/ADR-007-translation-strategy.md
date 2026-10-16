# ADR-007: Translation strategy

Status: **Accepted** (owner decision, 2026-10-13).

## Context

- The skeleton (phase 3, milestones A–E) translated 245 game functions by hand: the boot path, the main loop and scene 0. RAM at the first `SceneStep` matches PCSX2 with 0 unexplained differences (`../port/validation.md` E-1).
- About 2,640 game functions remain (2,884 in total, 272 of them dead but still translated; `../port/coverage.md`).
- Two ways to translate the rest were considered:
  - **Option A: hand translation in vertical slices.** Claude translates each function into readable C++ from Ghidra and the disassembly, following the `port-function` skill, one slice of the game at a time.
  - **Option B: a recompiler.** A tool translates the MIPS R5900 code mechanically into C++ (static recompilation); hand-written code only for the parts the tool cannot handle.

## Decision

**Option A.** Option B is **rejected**.

Reason (owner): Claude translates the code directly. Correctness comes from validation against PCSX2 (save states, input recordings and function-level test vectors), not from mechanical translation. A recompiler would be a second large program to write and verify, and its output would not be the readable, documented code this project wants.

## How it works

### Slice
- A **slice** is a part of the game that ends in something **visible or playable** that can be validated against PCSX2.
- A slice is not necessarily a whole scene. Small scenes can be one slice; large ones are split into sub-slices (scene 2, and scene 4 if needed). Self-contained features (movies) get their own slice.
- Each slice is sized so that its PCSX2 checks fit in **one session** of the owner.
- The order and contents of the slices: `../port/slices.md`. Every game function, dead code included, belongs to exactly one slice (checked by `tools/research/slices.py`).

### Per-slice workflow
1. **Plan:** list the functions, the renderer/audio/input/`sce` features and the validation points.
2. **PCSX2 batch:** collect every save state, screenshot, input recording and test vector the slice needs into **one** batch of exact steps for the owner, early in the slice (`../pcsx2/procedures.md`). Claude continues translating while it waits.
3. **Translate** with the `port-function` skill: `// EE: 0x…` on every function, the game's random sources in the original call order, guest memory (ADR-001). Only the renderer, audio and `sce` features the slice needs are implemented.
4. **Validate** (`../port/validation-strategy.md`): tests pass; the RAM checkpoint matches PCSX2 apart from documented differences; screens match the PCSX2 screenshots.
5. **Fix the records:** errors found in the docs during translation are fixed in the docs and in Ghidra, and logged (`../rename-log.csv`).
6. **Report:** what was translated (counts against `../port/coverage.md`), what is visible or playable, validation results, documented differences, problems. Then wait for the owner's go-ahead; the owner tests and commits between slices.

## Consequences

- No recompiler tooling is written. `tools/research/` stays a set of read-only research aids.
- The Ghidra database and the docs stay the primary source; the translation reads the code itself where notes are at interface level (effect draws, packet code; `../port/validation.md` §3).
- Validation infrastructure becomes part of the work: RAM checkpoints, the determinism checker, the input-recording replayer and test vectors (`../port/validation-strategy.md`). The parts that live in the port are debug-layer features (category 3, `port-baseline.md`).
- Progress is measured per slice in `../port/translated-functions.csv` (`tools/research/translated.py`) and reported against `../port/coverage.md`.
