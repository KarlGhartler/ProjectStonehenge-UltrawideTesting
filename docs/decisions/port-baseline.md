# Modern-port baseline features

The port distinguishes three categories (CLAUDE.md, Goal):
1. **Original behaviour.** Game logic, simulation, rules and content reproduce the original exactly.
2. **Modern-port baseline.** Features intrinsic to a PC port, built into the port. They may change **presentation only**, never game logic or simulation results.
3. **Everything else** that departs from the original goes only into the optional modding layer.

This file lists category 2. A feature is added here only after the owner approves it.

## Approved

| Feature | Status | Related ADR | Date approved |
|---|---|---|---|
| Output scaled to the host's screen resolution, with the internal rendering resolution kept native (4:3 preserved, pillarboxing) | approved; filter and interlace handling are open sub-decisions | ADR-006 | 2026-10-10 |
| Frame rate decoupled from the simulation, with interpolation and whatever else is needed for smooth presentation at any refresh rate | approved; design is in ADR-002 (Proposed) | ADR-002 | 2026-10-10 |

## Candidates (proposed by Claude, not approved)

None is built without the owner's approval. Each one is presentation-only unless noted.

| Candidate | Reasoning | Risk to "original behaviour" |
|---|---|---|
| Windowed / borderless / exclusive fullscreen, window resizing | basic PC expectation; only affects the output pass (ADR-006) | none |
| Input rebinding for keyboard and host controllers (SDL3 gamepad mapping onto the PS2 pad bytes) | PC players have no DualShock 2; the game still receives the exact `scePadRead` byte layout. The analog/pressure values are synthesised from host input | low: only the mapping, the game's own `ConvertPadInput` is unchanged. Pressure-sensitive buttons need a defined mapping for digital keys |
| Higher internal rendering resolution (integer multiples of 512×448) | sharper 3D on modern screens | **medium**: render-to-texture, small render targets (64/128 px), the field copy and any VRAM readback must scale consistently, or effects break. It needs the GS dumps and rendering-path research (priority 2, step 9) first |
| Widescreen (Hor+) | common request for PS2 ports | **high**: it changes the projection and the HUD layout, and possibly culling and what is visible to the player. Culling decisions could feed back into game logic. More likely category 3 (modding layer) unless research shows it is presentation-only |
| Texture filtering / anisotropic filtering / MSAA | image quality | low to medium: GS bilinear rules are part of the look; MSAA interacts with the field copy and AA1 lines |
| Vsync on/off and a frame-rate cap | follows from ADR-002 | none (presentation timing only) |
| Audio output device selection and latency setting | PC expectation | none |
| Pause on focus loss | PC expectation | low: must freeze the simulation step and the audio clock together, never skip steps |
| Memory card management: both ports mapped to `saves/` folders | PCSX2 interoperability already decided (ADR-005) | none if it maps onto the same `sceMc*` behaviour |
| On-screen messages from the port (ISO check failure, save path) outside game rendering | needed for errors | none |
