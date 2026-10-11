# ADR-005: Platform, saves and logging

Status: **Accepted** (owner decision, 2026-10-10). One sub-point is still open: the fallback location when the executable's directory is not writable (see "Open").

## Context

- The port is a native PC program. It needs a target platform order, a save location that interoperates with PCSX2, and a session log for support and debugging.
- The owner decided all three. This ADR records the decisions and their consequences.

## Decision

### 1. Platform
- **Windows first.** Linux support is deferred until the port is complete. macOS (MoltenVK) stays best effort.
- Do not choose libraries or APIs that would clearly be hard to make work on Linux later. Examples: Win32-only audio or input APIs in place of SDL3, Direct3D in place of Vulkan, MSVC-only language extensions in portable code.
- OS-specific code is isolated in `src/platform/`, behind interfaces. `src/game/` and `src/sce/` contain no OS calls.
- **File names are case-sensitive.** ISO file names are matched exactly as stored on the disc (uppercase, e.g. `DATA.CDP`, `SLUS_201.52`). The port never relies on case-insensitive lookup, not even on Windows.

### 2. Saves
- Location: a `saves` folder in the directory that contains the game executable.
- Format: compatible with **PCSX2's Folder Memory Card**.
  - Each save is a folder holding the raw save files exactly as the PS2 writes them to the card.
  - Save folders can be copied between the port and PCSX2 in both directions.
  - Layout and PCSX2's metadata files: `docs/formats/folder-memcard.md`, researched from the PCSX2 source for compatibility only, no code copied.
- The game's own save logic (`sceMc*` calls, file names, file contents) is reproduced exactly. The research is in priority 2, step 7. `src/sce/` maps `sceMc*` onto host files.

### 3. Logging
- Each run writes a session log to a `logs` folder in the executable's directory:
  - `logs/last_session.log`, replaced at each start;
  - the previous one is kept as `logs/previous_session.log`.
- Content:
  - build version;
  - OS and GPU information;
  - ISO verification result;
  - settings;
  - warnings and errors;
  - unimplemented `sce*` calls (logged once each, including the stubs for SDK functions reached only from dead game code).
- **No game data in logs** (no memory dumps, no file contents, no text extracted from the disc).

### 4. Non-writable executable directory
- If the executable's directory is not writable (e.g. under `C:\Program Files`), the fallback location for `saves` and `logs` is **not decided**.
- The port must not pick one on its own. See "Open".

## Consequences

- `src/platform/` provides:
  - paths: the executable directory, `saves/` and `logs/`;
  - case-exact ISO lookup;
  - the log sink;
  - the folder-card storage behind `sceMc*`.
- Portable code uses only C++17 and the approved dependencies (decided at the skeleton phase).
- Tests can compare save folders byte for byte with folders written by PCSX2.

## Open (owner's decision)

Proposed fallback options when the executable directory is not writable:

| Option | Pros | Cons |
|---|---|---|
| A. `%LOCALAPPDATA%\AC04Port\` (Linux later: `$XDG_DATA_HOME/ac04port/`) | standard per-user location, always writable, survives reinstall | saves are no longer next to the executable, so copying to PCSX2 means finding the folder (the log states the path) |
| B. Refuse to start, with a message asking to move the program or run it from a writable folder | one location only, never ambiguous | worse first-run experience under Program Files |
| C. `%USERPROFILE%\Documents\AC04Port\` | easy to find for users | Documents may be redirected or synced (OneDrive), which can corrupt partially written saves |

Recommendation: **A**. The path in use is written to the session log and shown once in a message.
