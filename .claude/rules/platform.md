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
- Verify the ISO at startup against `docs/research/supported-version.md` (size and volume descriptor, then SHA-1) before any other access.
- Never write game data, extracted or converted assets (textures, models, audio, video, archives) into the repo; caches belong in a user data directory outside the repo.

## Subsystem mapping
| PS2 subsystem | PC replacement |
|---|---|
| GS / GIF / VIF / DMA packets | Renderer backend |
| VU1 microcode | Renderer backend (see `vu-microcode.md`) |
| CDVD (`sceCd*`) | Sector reads from the ISO image (ISO 9660, 2048-byte sectors); `sceCdSearchFile` resolves paths in the ISO directory tree; `sceCdReadIOPm` targets the audio backend's IOP buffer pool |
| Memory card (`sceMc*`) | Save files on disk |
| Pad (`scePad*`) and `USBJOY.IRX` | Input backend |
| `libsd`, `SOUND.IRX`, `VAG.IRX` (via SIF RPC) | Audio backend, at the RPC boundary (`docs/research/iop-interface.md`, ADR-004 proposed) |
| EE kernel threads, semaphores, interrupts/vsync handlers | Ask before choosing between OS threads and a cooperative scheduler (ADR-002 proposes a single game thread with a fixed 59.94 Hz step) |
