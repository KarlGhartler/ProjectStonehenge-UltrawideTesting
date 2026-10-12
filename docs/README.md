# Documentation index

Reference documentation for the Ace Combat 04 (SLUS_201.52) PC port. Project rules: `../CLAUDE.md`. Short status: `STATUS.md`.

## Top level

| File | Contents |
|---|---|
| `STATUS.md` | decompilation status, pending PCSX2 checks, owner decisions, risks, readiness |
| `open-questions.md` | open research questions (and answered ones with pointers) |
| `symbols.csv` | `address,name,kind,status` for every named symbol |
| `rename-log.csv` | every Ghidra rename, comment and created function |

## `game/`: core game structure

| File | Contents |
|---|---|
| `game/boot.md` | crt0, `main` initialisation, main loop, static constructors |
| `game/scenes.md` | scene dispatch, the 8 scenes, request helpers, transition graph |
| `game/main-context.md` | `MainContext` layout and flags, game-state block `g_GameState` |
| `game/memory.md` | work-heap allocator, pointers and 32-bit dependencies, alignment, scratchpad |
| `game/threads-timing.md` | threads, interrupts, frame pacing, frame count vs. time |
| `game/file-loading.md` | file table, CD request queues, DATA/BD archive access |
| `game/iop-interface.md` | EE side of the SOUND, VAG and USBJOY RPCs |
| `game/random.md` | the five random sources and their users |

## `subsystems/`: game systems

| File | Contents |
|---|---|
| `subsystems/frontend.md` | scene 4: title, menus, options, briefing, in-menu movies, loading screen |
| `subsystems/mission-flow.md` | scene 2: flight scene sub-states, mission setup, rules and event script, cutscenes, replay and demo, pause menus |
| `subsystems/flight.md` | player aircraft: controls, flight model, landing gear, collision response |
| `subsystems/ai.md` | enemy and friendly units |
| `subsystems/weapons.md` | projectiles, damage, terrain and unit collision |
| `subsystems/camera-hud.md` | cameras and the HUD |
| `subsystems/results-startup.md` | scene 5 results, scene 6 startup, scene 7 HDD error |
| `subsystems/save.md` | memory-card and HDD save layers |
| `subsystems/sound.md` | EE-side sound, radio chatter, subtitles, text engine, rumble |
| `subsystems/iop-sound.md` | SOUND.IRX and VAG.IRX internals and command tables |
| `subsystems/rendering.md` | packet transport, flight frame structure, cameras, models, lighting, terrain, shadows, cutscene characters |
| `subsystems/effects.md` | effect pools and special objects |
| `subsystems/vu-programs.md` | VU1/VU0 microcode blocks and their upload |

## `formats/`: data formats

| File | Contents |
|---|---|
| `formats/CDP.md` | DATA/BD archives and the DATA index map |
| `formats/MISSION.md` | mission entries (DATA 1–100) |
| `formats/AIRCRAFT.md` | aircraft entries (DATA 101–172) |
| `formats/MODEL.md` | `SM` model format |
| `formats/GIM.md` | Namco GIM textures |
| `formats/SMV.md` | movie container |
| `formats/SAVE.md` | memory-card save image |
| `formats/folder-memcard.md` | PCSX2 folder memory card layout used by the port |

## `platform/`: executable and SDK

| File | Contents |
|---|---|
| `platform/elf-sections.md` | ELF section table |
| `platform/toolchain.md` | compiler, SDK version, `gp` |
| `platform/sdk-functions.md` | SDK and libc functions identified in the executable |
| `platform/gs-subset.md` | GS features used by the EE-side packet code |
| `platform/supported-version.md` | the supported disc image and its checksums |

## `decisions/`: decision records

| File | Contents |
|---|---|
| `decisions/ADR-001-memory-model.md` … `decisions/ADR-006-presentation-resolution.md` | architecture decisions (status in each file) |
| `decisions/port-baseline.md` | approved modern-port features and candidates |
| `decisions/dependencies.md` | proposed third-party dependencies (none approved yet) |
| `decisions/naming-convention.md` | naming rules and the analysis behind them |

## `port/`: port planning

| File | Contents |
|---|---|
| `port/coverage.md`, `port/coverage-functions.csv` | decompilation status per function and subsystem |
| `port/skeleton-readiness.md` | functions and SDK calls the port skeleton needs |
| `port/dead-code.md` | dead, debug and unreachable code |
| `port/risks.md` | port risks per subsystem |
| `port/command-line.md` | command-line parameters and `settings.json` keys of the executable |
| `port/internal-resolution.md` | feasibility of a higher internal rendering resolution (study) |
| `port/debug-overlay.md` | debug overlay proposal (features, dependency, rules) |
| `port/validation.md` | integrity checks and blind re-review of the decompilation |

## `pcsx2/`: reference runs

| File | Contents |
|---|---|
| `pcsx2/procedures.md` | pending PCSX2 checks with exact steps |
| `pcsx2/sessions-01.md` … `pcsx2/sessions-05.md` | results of past PCSX2 sessions (historical record) |

Research tools: `../tools/research/README.md`.
