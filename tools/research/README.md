# Research tools

Read-only helpers for analysing `SLUS_201.52` and the Ghidra database. They are research aids only: the port never uses them, and they never write game data into the repo.

- Inputs: the extracted-disc folder (`--extracted DIR`, the `AC04_EXTRACTED` environment variable, or the "Extracted disc files" line of `CLAUDE.local.md`), and the ghidra-mcp HTTP plugin (`GHIDRA_MCP_URL`, default `http://127.0.0.1:8089`).
- Caches go to `out/research/` (gitignored).
- Data written to the repo: only `docs/port/coverage-functions.csv`, `docs/port/translated-functions.csv`, `docs/symbols.csv` and `docs/rename-log.csv`.
- Run from the repository root: `python tools/research/<tool>.py …`. Each file starts with its usage.

| Tool | Purpose | Needs |
|---|---|---|
| `common.py` | shared paths, ELF reader (library) | — |
| `scan.py` | shared instruction scan state for the scanners (library) | ELF |
| `ramdiff.py` | compare a port guest-RAM dump with a PCSX2 save state, by section and symbol (run with `python -I`). `--expected [slice CSV ...]` hides the bytes explained in `docs/port/ramdiff-expected.csv` (plus slice files such as `ramdiff-expected-s1.csv`) and lists the rest as unexplained; `--range START END` limits the area (END exclusive); `--values` also prints 16 bytes per range from both sides (game memory: terminal only, never commit) | port dump (`--dump-ram-after-load`, `--dump-ram-at-first-scenestep`), `.p2s` |
| `runmany.py` | runs many port instances in parallel from a job file (own user-data folder and card copy per job), then summarises exit codes, distinct log warnings/errors, RAM checkpoints and screenshots; `--baseline DIR` compares screenshots pixel for pixel (`--tolerance N` allows N levels), RAM checkpoints byte for byte and hash logs with a saved set (`--save-baseline DIR` saves all three), `same_hashes_as` compares two `--hash-log` runs, `known` marks an expected failure; `--extra-args "..."` appends arguments to every job (e.g. `--internal-scale 8`), `--shots-only` skips the screenshot comparison, `--suite` names the output folder; `--jobs N`, `--only` (run with `python -I`; ISO from `AC04_ISO`) | job file, the port, cards under `out/cards/` |
| `full.json` | full regression suite (no `--freeze-sim`): S1-S4 front-end screens and checkpoints, the movie, the mission 3/4 routes, a replayed gameplay run and the mission 1 opening with hash logs, all 18 missions; the logic oracle for refactors and renderer changes (`docs/port/validation.md` IR-1) | — |
| `pcsx2_compare.py` | compares the User's PCSX2 screenshots of the slice batches (Slices folder) with port screenshots: one port run per entry with screenshots over a vblank window, best match per reference (imgdiff metric), `--play-gsdump` entries for GS dumps, `--extra-args` for other internal scales (run with `python -I`) | `pcsx2_compare.json`, the Slices folder |
| `pcsx2_compare.json` | the comparison list: pad scripts from `docs/port/validation.md`, windows per reference, `--blend-fields` where PCSX2 deinterlaced | — |
| `regress.json` | regression suite for `runmany.py`: front-end route screenshots, mission 1/3/4 routes with checkpoints, calm vs `--stress-host` hash logs, all 18 missions through `--mission` | — |
| `hashdiff.py` | compare two RAM hash logs of the port (`--hash-log`): first differing vblank and the 64 KiB blocks (run with `python -I`) | two `hashes.ac4h` |
| `dmacap.py` | summary of a port step capture (`--dump-dma-at`): VIF1 codes, GIF tags, primitives, A+D registers (`--vif`, `--regs`) | `vif1_<n>.bin`, `gif_<n>.bin` (keep them outside the repo) |
| `sample_profile.py` | sampling CPU profiler for `ProjectStonehenge.exe` (Windows, ctypes + dbghelp, no dependencies): starts the port with its arguments, samples every thread after `--wait` s for `--time` s, prints self/total functions; `--waits 1` keeps blocked threads | a build with its PDB |
| `gsvram.py` | compare the port's GS memory (`--dump-vram`) with a PCSX2 VRAM image (save state `GS.bin` or `.gs` dump) block by block, with the GS column tables of the 32/16/8/4-bit formats; finds the VRAM offset in the GS state (run with `python -I`) | `vram_<n>.bin`, `.p2s` or `.gs` |
| `gsdraws.py` | every draw of a PCSX2 `.gs` dump (or port GIF capture) in order with its GS state (PRIM, FRAME, TEX0, ALPHA, TEST, ZBUF) and screen box; `--region X0 Y0 X1 Y1` (draws covering a screen area), `--date`, `--from/--count`, `--verts` (each vertex: position and UV) | `.gs` dump (uncompressed) |
| `heapdiff.py` | work-heap differences per allocation between a port dump and a PCSX2 state: live allocations from the `--log-allocs` log, caller names from the PDB (dbghelp), `--by-caller`, `--detail CALLER [--values]`, `--mask DUMP2` (second dump with another `--heap-fill` byte: leftover bytes ignored) (run with `python -I`) | `allocs.txt`, port dump(s), `.p2s`, the port executable with its PDB |
| `imgdiff.py` | compare a port native screenshot with a PCSX2 screenshot: mean/max difference per channel, pixels over a threshold, difference image, `--offset` (run with `python -I`; `docs/port/validation-strategy.md` §2.2) | two PNG files (keep them outside the repo) |
| `validate.py` | integrity checks: names vs Ghidra, coverage totals vs STATUS.md, classification, dead-code reachability | `funcs.json`, `reach.json`, Ghidra |
| `ghidra_dump.py` | function list with bodies, callers, callees → `out/research/funcs.json` | Ghidra |
| `decomp_dump.py` | decompiler output cache → `out/research/decomp/` | Ghidra, `funcs.json` |
| `elfmem.py` | words, pointers and strings by virtual address | ELF |
| `dis.py` | minimal R5900 disassembler for quick reads (COP1 single precision, lwc1/swc1, lqc2/sqc2 decoded; MMI and VU0 macro raw) | ELF |
| `fpucheck.py` | float add/sub rule check: COP1 add-type instructions vs `fpu::Add/Sub/MulAdd/UnsignedToFloat` calls per translated function; `// fpucheck: verified N` marks a hand-checked function | ELF, `funcs.json` |
| `vu_disasm.py` | VU1 microcode listings of the `.vutext` programs | ELF |
| `model_dump.py` | SM model data dump (nodes, mesh DMA lists, vertices) | `out/cdp/` |
| `gifstrips.py` | 3D primitive packets of one frame per texture (counts, screen/Z ranges, mean colour) from a port GIF capture or a PCSX2 .gs dump | capture or dump (keep them outside the repo) |
| `show.py` | print cached decompiled functions (by address or range/subsystem) | `decomp/` |
| `brief.py` | short view of the not-yet-understood functions in a range | `funcs.json`, `decomp/` |
| `calltree.py` | callee tree with coverage status | `funcs.json` |
| `rawcalls.py` | raw call graph (jal/j) and data pointers → `out/research/rawcalls.json` | ELF, `funcs.json` |
| `callers.py` | callers and data references of functions | `rawcalls.json` |
| `reach.py` | reachability from `main`; compares with the coverage `dead` status | ELF, `funcs.json` |
| `fdeaudit.py` | function boundaries vs `.eh_frame` FDEs | ELF, `funcs.json` |
| `gapscan.py` | code outside any Ghidra function | ELF, `funcs.json` |
| `closure.py` | call closure from roots | ELF |
| `hwscan.py`, `hwregs.py` (+ `hwreg_names.py`) | hardware register and scratchpad access scans | ELF |
| `gs_scan.py` | GS register usage (A+D writes) | ELF |
| `coverage.py` | coverage table; `--refresh` syncs names from `funcs.json` | `funcs.json` for `--refresh` |
| `coverage_mark.py` | set status/subsystem/note rows from stdin | — |
| `slices.py` | assign every game function to one translation slice (`docs/port/slices.md`); `--list S`, `--vs` | `reach.json`, ELF, `decomp/` for `--vs` |
| `translated.py` | list the functions translated into `src/game` (`// EE:` lines) into `docs/port/translated-functions.csv` | — |
| `log_renames.py` | log Ghidra renames into `symbols.csv`, `rename-log.csv`, coverage | — |
| `smv_walk.py` | walk all SMV movie containers | extracted folder |
| `flac_mp4.py` | decode the FLAC audio track of a PCSX2 media capture (.mp4) to WAV, pure Python | capture file, output WAV in `out/` |
| `cdp_sub.py`, `cdp_blocks.py` | DATA.CDP container statistics | `out/cdp/` from `tools/cdp_extract.py` |
| `terrain_check.py` | check the terrain block layout of the mission entries (`docs/formats/MISSION.md`) | `out/cdp/` from `tools/cdp_extract.py` |
| `decl_section.py` | append a `src/game/game.h` section declaring every `// EE:` function of a source file (translation helper) | none |

Typical refresh after Ghidra work: `ghidra_dump.py` → `decomp_dump.py` → `rawcalls.py` → `reach.py` → `coverage.py --refresh`.
