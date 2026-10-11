# Ace Combat 04 (SLUS_201.52) — native PC port

<!-- Maintainers: keep this file under 200 lines. Path-specific rules live in .claude/rules/,
     the step-by-step porting procedure in .claude/skills/port-function/. -->

## Goal

- Build a native PC (x86-64) version of Ace Combat 04: Shattered Skies (NTSC-U, `SLUS_201.52`) by translating the PS2 game code into C++.
- Game logic must behave exactly like the original. Instruction order, register use and control flow may change.
- This is NOT a matching decompilation. Never spend effort reproducing the original compiler's output.
- Reference behaviour = PCSX2 running the original disc. When unsure what "correct" is, check it in PCSX2.
- Three categories of behaviour:
  1. **Original behaviour**: game logic, simulation, rules and content reproduce the original exactly.
  2. **Modern-port baseline**: features intrinsic to a PC port, built into the port. They may change presentation only, never game logic or simulation results. Only features approved by the owner and listed in `docs/decisions/port-baseline.md`.
  3. **Everything else** that departs from the original goes only into the optional modding/debug layer.
- **All original game code is kept and translated**, including dead, unreachable and debug code (e.g. scenes 1 and 3). In the default build it stays unreachable exactly as on the PS2. It may only be exposed through the optional debug/modding layer (e.g. a debug menu to explore cut content). Catalogue: `docs/research/dead-code.md`; keep it updated.
- This applies to **game code only** (0x001000D8–0x002616E7). Sony SDK and C runtime code is not translated: `src/sce/` implements what the game calls. SDK functions reached only from dead game code (e.g. the HDD path) get stubs that log the call and return an error.

## Platform (ADR-005, Accepted)

- Windows first; Linux is deferred until the port is complete. Do not choose libraries or APIs that would clearly be hard to make work on Linux later.
- OS-specific code stays in `src/platform/`, behind interfaces.
- Never rely on case-insensitive file lookup: match ISO file names exactly as stored (uppercase, e.g. `DATA.CDP`).
- Saves: folder `saves/` in the executable's directory, in PCSX2 Folder Memory Card layout (`docs/formats/folder-memcard.md`). Each save folder holds the raw files exactly as the PS2 writes them, so folders can be copied between the port and PCSX2 both ways.
- Logs: folder `logs/` in the executable's directory; `last_session.log` (replaced at each start) and `previous_session.log`. Content: build version, OS/GPU info, ISO verification result, settings, warnings, errors, unimplemented `sce*` calls (once each). No game data in logs.
- If the executable's directory is not writable, the fallback location is the owner's decision (open, ADR-005): never decide it in code.
- Presentation (ADR-006, Accepted): render at the native internal resolution; a final output pass scales to the host screen, keeping 4:3 (pillarbox).

## Working rules

These replace any older per-phase stop conditions where they conflict.
- **Work autonomously on**:
  - decompilation and analysis;
  - renaming, and creating functions and data types in Ghidra;
  - research documents;
  - research tools in `tools/`;
  - importing files into the Ghidra project.
- **Ask first before**: writing any code for the port skeleton or in `src/`; any architecture decision (including marking an ADR `Accepted`); adding, fetching or downloading any dependency.
- **Always ask before destructive Ghidra operations**: clearing analysis, deleting functions or programs, re-importing a program that already exists.
- **PCSX2 questions**: give the owner exact steps (breakpoints, registers, memory, what to do in the game) and continue with other work while waiting, unless the answer blocks you.
- **No git commits.** The owner reviews and commits.
- Log every Ghidra rename and comment in `docs/research/rename-log.csv`, and write findings to the docs as you go, not only at the end.

## Game files and local paths

- Absolute paths to the ISO, the extracted disc files and the Ghidra project are in `CLAUDE.local.md` (machine-specific, not committed).
- Treat every folder outside this repo as read-only.
- **The port reads all game data at runtime from the user's own original disc image** (ISO 9660, path from configuration). That includes the executable's initialised data (`.data`, `.rodata`, `.vutext`, `.vudata`): translate code, never copy tables into `src/`.
- The port accepts only the supported image and verifies its SHA-1 at startup (`docs/research/supported-version.md`).
- The extracted-disc folder is for research tools (`tools/`) only. The port never reads it.
- Never copy or commit game data: the ISO, `SLUS_201.52`, `*.IRX`, `IOPRP23.IMG`, `*.CDP`, `*.TBL`, `*.SMV`, `*.VPK`, PCSX2 save states (`*.p2s`, they contain game memory; read them with `tools/p2s_read.py`), or anything extracted or converted from them (textures, models, audio, video, archives, tables). Generated output goes in `build/` or `out/` (both gitignored).
- Enforcement: `tools/hooks/pre-commit` (game-data names, files > 1 MiB outside `third_party/`, known format signatures). Enable per clone with `git config core.hooksPath tools/hooks`.

## Repo layout

| Path | Contents |
|---|---|
| `src/game/` | Game logic translated from EE code |
| `src/sce/` | PC implementations of the Sony SDK functions the game calls (`sce*`), same names and signatures |
| `src/platform/` | OS/backend layer: window, renderer, audio, input, file I/O, timing |
| `src/common/` | `types.h`, `ee_math.h` (EE float behaviour), `vu0_math.h` (VU0 macro-mode vector math) |
| `tools/` | Python 3 extractors and format parsers (research only) |
| `tools/hooks/` | Git hooks (`pre-commit` blocks game data) |
| `third_party/` | Only third-party code we modify (rules in `.claude/rules/third-party.md`); listed in `THIRD_PARTY_NOTICES.md` |
| `tests/` | Behaviour tests using values captured in PCSX2 |
| `docs/research/` | Findings about the executable, VU code, open questions |
| `docs/formats/` | One file per reverse-engineered data format |
| `docs/decisions/` | Architecture decision records (`ADR-NNN-*.md`) and the proposed dependency list |
| `docs/symbols.csv` | `address,name,kind,status` for every symbol we name |

Folders and files in this table that don't exist yet are created when first needed.

## Build and test

- Build system: **CMake ≥ 3.24**. Language standard: **C++17**.
- Rendering: **Vulkan** (minimum version set in ADR-003, proposed 1.2). Window, input and audio output: **SDL3**.
- Targets: Windows (MSVC) first. Linux (GCC, Clang) after the port is complete (ADR-005); macOS via MoltenVK is best effort.
- Dependencies are decided at the skeleton phase, within the platform requirements (ADR-005).
- Every third-party dependency needs the owner's approval and a GPL-3.0 compatibility check before it is added. Proposed list with licenses: `docs/decisions/dependencies.md`.
- Third-party code rules (details: `.claude/rules/third-party.md`):
  - unmodified dependencies are fetched by CMake `FetchContent`, pinned to an exact version/commit with a checksum, never copied;
  - only code we modify is copied, into `third_party/<name>/`, with its original license, an `UPSTREAM.md`, and the original headers plus a "Modified by the AC04 port project" note;
  - never edit third-party code outside `third_party/`, never copy it into `src/`;
  - `THIRD_PARTY_NOTICES.md` is updated in the same change; `python tools/check_notices.py` must pass.
- When a new tool or project is used as a reference, add it to the README "Acknowledgements" in the same change.
- Architecture decisions: `docs/decisions/ADR-*.md`. Status `Proposed` means not decided: do not build on a proposed ADR without asking.
- The build is not set up yet. When it is, record the exact build and test commands in this section.

## Facts about SLUS_201.52

Full section table: `docs/research/elf-sections.md`.

- Fully stripped: no `.symtab`, `.mdebug` or `.comment`. No original names exist; every name in this project is ours.
- Sony SDK 2.3.0 (libmc 2.3.2). Compiler: GCC 2.9x `ee-gcc` with g++ (exact version is a hypothesis; not needed for the port).
- Game code is C++ written as "C with classes": no vtables, no `new`/`delete`, very few indirect calls. Details: `docs/research/toolchain.md`.
- No `.lit8`: game math is single-precision `float`.
- `.text` 0x00100000–0x00297B33 (~1.6 MB) also contains statically linked Sony SDK, libc and libgcc code.
- `.vutext` 0x00297C00–0x0029C8FF, `.vudata` 0x004021F0–0x004036EF and the 17 `.DVP.overlay.*` sections are VU microcode/data, not R5900 code.
- `.data` 0x0029C980–0x004021E7 (~1.4 MB) is unexplored; likely contains game tables.
- `gp` = 0x004159F0 (from `.reginfo` and crt0), used for `.sdata`/`.sbss` access. Set in Ghidra as one register-context range over `.text`.
- Boot and main loop (`docs/research/boot.md`): `main` 0x001001F0; main loop 0x00100290–0x001003BC inside `main`; scene table 0x0029FDA0; game bump allocator 0x00101000.
- Architecture facts for the port: `docs/research/memory-model.md`, `threads-timing.md` (single game thread, frame-locked 60 Hz NTSC), `render-boundary.md`, `iop-interface.md`.
- Ghidra memory blocks `registers.io`, `vu0.*`, `vu1.*`, `registers.gs`, `iop_ram`, `scratchpad`, `RAM(uncached*)` and `BIOS/*` are added by the Emotion Engine plugin, not by the ELF.

## Disc files

All "read" entries below mean: read from the ISO image at runtime, never from extracted files.

| File | Role | Port handling |
|---|---|---|
| `SLUS_201.52` | Main EE executable | Translate code into `src/game/`; load its initialised data sections from the ISO at runtime |
| `SYSTEM.CNF` | Boot config | Reference only |
| `IOPRP23.IMG` | Sony IOP kernel update image | Not used |
| `IRX/LIBSD, MCMAN, MCSERV, PADMAN, SIO2MAN, USBD` | Stock Sony IOP modules | Not used; replaced by `src/sce/` + `src/platform/` |
| `IRX/SOUND.IRX`, `IRX/VAG.IRX` | Namco sound drivers: SOUND = RPC 0x81000101 (SFX command batches), VAG = RPC 0x81000102 (6 streaming slots) (`docs/research/iop-interface.md`) | Not used; their RPC interface is reimplemented in the audio backend (ADR-004) |
| `IRX/USBJOY.IRX` | USB joystick driver, RPC 0x80000210 | Not used; replaced by the input backend |
| `BIN/DATA.CDP+TBL`, `BIN/BD.CDP+TBL` | Packed archives: TBL = `{u32 sector, u32 size}[]`, CDP = sector-aligned entries, loaded by index (`docs/formats/CDP.md`). DATA = 360 `count+offsets` containers (missions, aircraft, menus, sound headers); BD = 13 raw SPU ADPCM banks | Read by index like the game (`tools/cdp_extract.py` is for research only) |
| `MIS/MIS01–18.SMV` | 18 files for the game's 18 missions (confirmed count) | The 18 MIS/*.SMV files are not mission data. They contain MPEG-2 video in a Namco chunked container, the same format as the cutscenes in SMV/ (which also carry audio). Read and demux from the ISO at runtime; decode MPEG-2 |
| `SMV/*.SMV` | INTRO, SS01–SS12 cutscenes, STAFF roll. Namco chunk container (0x800 header sector, then chunks `{next_size, video_bytes, audio_off, audio_bytes}`), MPEG-2 video (640×448, SS 640×336) plus 0x4000-byte PCM audio blocks (48 kHz s16 stereo, layout unconfirmed); `docs/formats/SMV.md` | Read and demux at runtime; decode MPEG-2; PCM goes to VAG slot 0 in the audio backend |
| `VAG/*.VPK` | Packs of VAG (Sony ADPCM) audio, streamed through VAG.IRX slots | Stream from the ISO at runtime; decode in the audio backend |

Statements marked "probably", "suggest" or "unverified" are hypotheses. Confirm them from code before building on them, then update this table.

## Working with Ghidra (MCP)

- Ghidra is the research database. When you name a function, global or struct, rename it in Ghidra AND add or update the row in `docs/symbols.csv`.
- Ghidra decompiler output is a draft. Check the disassembly whenever the function uses MMI instructions, 128-bit `lq`/`sq`, VU0 macro mode (`COP2`, `qmfc2`, `qmtc2`, `v*`) or COP0.
- If globals appear as `gp + offset` instead of named addresses, the `gp` register value is wrong: compare it with `.reginfo` before continuing.
- Never create functions or run disassembly inside `.vutext`, `.vudata` or `.DVP.overlay.*`.
- Destructive operations (clearing analysis, deleting functions or programs, re-importing an existing program) need the owner's OK (see Working rules).
- Rename policy: rename in Ghidra only when the behaviour is confirmed. Otherwise add a `HYPOTHESIS:` plate comment and a `status=hypothesis` row in `docs/symbols.csv`. Log every rename and comment in `docs/research/rename-log.csv`.

## Naming

One convention for Ghidra, docs and C++ code. **No type prefixes** (no Hungarian: no `dw`, `w`, `p`, `a`, `fl`, `b`… in names).
- Game functions: PascalCase, verb first (`RequestScene`, `LoadBootData`); scene handlers `Scene<Name>`.
- SDK and libc functions: their original names, never changed (`sceGsSyncPath`, `sceCdRead`, `memcpy`).
- Globals: `g_` + PascalCase (`g_MainContext`, `g_MissionNo`). Constant tables: `k` + PascalCase (`kMissionIdTable`). Only exception: `g_p` for a pointer global whose pointee is also a named global (`g_pMainContext` → `g_MainContext`).
- Struct fields: lower snake_case (`next_scene`, `cur_buf`).
- Types (structs, enums): PascalCase (`MainContext`); enum values `kPascalCase`.
- Unnamed: `func_XXXXXXXX` (functions), `D_XXXXXXXX` (globals), `unk_XX` (struct fields at hex offset XX). Inside Ghidra the defaults `FUN_`/`DAT_` stay.
- When ghidra-mcp suggests or enforces a Hungarian prefix, override it (`strict_mode=false`) and rename auto-prefixed struct fields after creating a struct.
- Rename only when behaviour is confirmed.
- Put `// EE: 0x00XXXXXX` on the line above every translated function, with the original start address.
- Unconfirmed interpretations in code get a `// HYPOTHESIS:` comment.

## Workflow

- To translate a function, follow the `port-function` skill.
- Prioritise: (1) label Sony SDK and runtime functions, (2) boot path from the entry point, (3) main loop, (4) file loading, (5) gameplay systems.
- No commits by Claude (see Working rules). Changes should still be grouped so the owner can commit one subsystem or one source file at a time.
- Ask before large refactors, renaming files, or changing the repo layout above.
- Open research questions are tracked in `docs/research/open-questions.md`; update it when one is answered.
