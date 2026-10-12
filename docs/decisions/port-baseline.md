# Modern-port baseline features

The port distinguishes three categories (CLAUDE.md, Goal):
1. **Original behaviour.** Game logic, simulation, rules and content reproduce the original exactly.
2. **Modern-port baseline.** Features intrinsic to a PC port, built into the port. They may change **presentation only**, never game logic or simulation results.
3. **Everything else** that departs from the original goes only into the optional modding layer.

This file lists category 2. A feature is added here only after the owner approves it.

## Approved

| Feature | Details | Related | Date approved |
|---|---|---|---|
| Output scaled to the host's screen, internal rendering resolution native (4:3 preserved, pillarboxing; 16:9 when the game's own SCREEN RATIO option is on) | two filters, **sharp** (sharp bilinear) and **smooth** (bilinear), chosen in `settings.json`, default **sharp**; the full 448-line draw buffer is presented (ADR-006 §4) | ADR-006 | 2026-10-10, filters 2026-10-11 |
| Frame rate decoupled from the simulation, with interpolation | fixed 59.94 Hz step, interpolated presentation | ADR-002 | 2026-10-10 |
| Frame cap | 60 / 120 / 144 / 240; **360 is the default upper limit** | ADR-002 | 2026-10-11 |
| V-sync on/off | presentation timing only | ADR-002 | 2026-10-11 |
| Window modes | **windowed** and **borderless** (exclusive fullscreen not included) | ADR-006 | 2026-10-11 |
| Settings in `settings.json` | `%LOCALAPPDATA%\AC04Port\settings.json`; settings struct + JSON load/save + change notification; no menu in the skeleton | ADR-005, ADR-006 | 2026-10-11 |
| Input rebinding | keyboard and host controllers (SDL3) mapped onto the exact PS2 pad bytes; bindings in `settings.json`; the game's `ConvertPadInput` is unchanged | ADR-005 | 2026-10-11 |
| Audio on focus loss | setting `audio.on_focus_loss`: `keep_playing` (default) or `mute`; never pauses the simulation | ADR-005 | 2026-10-11 |

Interpolation is feasible with the node matrices and the camera recorded per step (ADR-002 research findings). Effects and particles use their pool slot as identity and must snap when a slot is reused.

## Pending (owner's decision)

None.

## Later

| Candidate | Reasoning | Risk |
|---|---|---|
| Higher internal rendering resolution | sharper 3D; feasibility in `../port/internal-resolution.md`. The renderer has a scale factor from day one, fixed at 1× until validated (owner 2026-10-11) | medium: render targets, field copy and the half-resolution pass must scale together |
| Audio output device selection and latency setting | the port uses the system default device until then (owner 2026-10-11) | none |
| MSAA, texture/anisotropic filtering | image quality | low to medium: GS bilinear rules are part of the look; MSAA interacts with the field copy and AA1 lines |

## Modding layer only (category 3)

| Feature | Why not baseline |
|---|---|
| Widescreen (Hor+) | changes the projection and HUD layout, possibly culling and what the player sees. Not checked yet: whether any gameplay rule uses on-screen visibility (e.g. lock-on through the camera projection). Draw-phase randomness (sky, sun jitter) must run identically either way |
| Effects detail / "reduce effects" | effects read the random table inside the simulation step, so changing them changes gameplay randomness; must be labelled gameplay-altering (`../subsystems/effects.md`) |
| Debug overlay and its tools | optional debug layer (`../port/debug-overlay.md`); opens only when `debug.overlay_enabled` is true (default off; on by default in development builds); nothing changes unless it is opened |

## Not features (follow from other decisions)

| Item | Note |
|---|---|
| Memory card mapping onto `saves\` | PCSX2 interoperability (ADR-005); port 2 reports empty |
| On-screen messages from the port (ISO check failure, save path) outside game rendering | needed for errors |
