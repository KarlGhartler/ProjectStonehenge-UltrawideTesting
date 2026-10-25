---
paths:
  - "src/platform/**/*"
  - "src/sce/**/*"
---

# Rules for the SDK shim and platform layer

- `src/sce/` implements only the Sony SDK functions the game actually calls, with the original names and signatures, on top of `src/platform/`.
- Before implementing an `sce*` function, write its observed PS2 behaviour (arguments, return values, side effects) as a comment above it.
- Unimplemented `sce*` functions log once and return an error code. Never make them silently succeed.
- No game logic in `src/platform/` or `src/sce/`.
- No hardcoded game data paths. The port reads game data only from the user's original ISO image, whose path comes from configuration. Never read the extracted-disc folder (it is for research tools only).
- Verify the ISO at startup against `docs/platform/executable.md` (size and volume descriptor, then SHA-1) before any other access.
- Never write game data, extracted or converted assets (textures, models, audio, video, archives) into the repo; caches belong in a user data directory outside the repo.

## User data, settings, logs (ADR-005, ADR-006)
- Every path the port writes comes from the platform paths interface (user data folder `%LOCALAPPDATA%\ProjectStonehenge\` on Windows: `saves\`, `settings.json`, `logs\`). No other code builds these paths, and nothing is written next to the executable.
- Settings: one `Settings` struct with defaults, JSON load/save, change notification. Invalid or missing values fall back to defaults and are logged. `src/game/` never reads settings.
- Log: last session only (older logs deleted at start). Log errors and problems of startup, ISO check, renderer, audio, file I/O, saves and IOP stand-ins; unimplemented `sce*` calls once each; crashes through the crash handler in `src/platform/`. Never log per call or per frame, never log game data.

## Subsystem mapping
| PS2 subsystem | PC replacement |
|---|---|
| GS / GIF / VIF / DMA packets | Renderer backend |
| VU1 microcode | Renderer backend (see `vu-microcode.md`) |
| CDVD (`sceCd*`) | Sector reads from the ISO image (ISO 9660, 2048-byte sectors); `sceCdSearchFile` resolves paths in the ISO directory tree; `sceCdReadIOPm` targets the audio backend's IOP buffer pool |
| Memory card (`sceMc*`) | Save files on disk |
| Pad (`scePad*`) and `USBJOY.IRX` | Input backend |
| `libsd`, `SOUND.IRX`, `VAG.IRX` (via SIF RPC) | Audio backend, HLE at the RPC boundary with own SPU2-subset mixer (`docs/game/iop-interface.md`, `docs/subsystems/sound.md`, ADR-004 Accepted) |
| EE kernel threads, semaphores, interrupts/vsync handlers | Single game thread with a fixed 59.94 Hz step (ADR-002 Accepted); no EE thread emulation on the memory-card path; HDD-only thread calls are logging stubs |
