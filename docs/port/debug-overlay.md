# Proposal: debug overlay

Status: **first version implemented (skeleton milestone D, 2026-10-12)**, see section 7. The rest of this file is the approved proposal. Part of the optional debug layer (CLAUDE.md, category 3). Nothing changes in the default experience unless the overlay is opened, and features that can change simulation results are marked.

## 1. Opening and closing

- **Toggle key: `\`**, matched by the **typed character**, not the physical key: the overlay listens to SDL3 text input (`SDL_EVENT_TEXT_INPUT`) and toggles when the text is `"\"`. This works on any layout: on an Italian keyboard `\` is the key left of `1`; on US/UK it is next to Enter/Shift.
  - Layouts that produce `\` only with AltGr still work, because the produced character is what counts.
  - Text input must stay enabled while the game runs; the game itself reads only pad state, so this has no side effect.
- **Rebindable** in `settings.json`: `"debug": {"overlay_toggle": "\\"}` (a character) or a key name (`"F12"`) for players who prefer a scancode.
- While the overlay has keyboard focus, keys are **not** forwarded to the game's pad mapping (no accidental inputs). Gamepad input still goes to the game unless "capture pad" is ticked.

**Proposal: also require a settings flag.** `"debug": {"overlay_enabled": false}` by default. With it off, `\` does nothing and the overlay code is never initialised (no ImGui context, no GPU resources). Reasons: a stray `\` (e.g. while typing a pilot name in the game's own name entry, which uses the pad, or alt-tabbing) cannot open it; players who never touch the debug layer get exactly the default experience; support logs record whether it was enabled. The flag is also settable by a command-line switch (`--debug-overlay`) for development.

## 2. Look

- **Square UI, no rounded corners anywhere.** Dear ImGui style: `WindowRounding`, `ChildRounding`, `FrameRounding`, `PopupRounding`, `ScrollbarRounding`, `GrabRounding`, `TabRounding` all **0**; also `WindowBorderSize = 1`, no anti-aliased rounded fill. Applied once at context creation.
- Drawn by the **output pass, after the 4:3 game image** (ADR-006), directly into the swapchain image at host resolution. It never draws into guest render targets, so GS state, framebuffer contents and any GS-dump comparison are unaffected.
- Semi-transparent panels over the game; a "game in a window" mode that shrinks the 4:3 image into a panel is a later option.

## 3. Dependency

| | |
|---|---|
| Library | **Dear ImGui** (Omar Cornut and contributors) |
| License | **MIT**: compatible with GPL-3.0 (`../decisions/dependencies.md`) |
| Backends | `imgui_impl_sdl3` + `imgui_impl_vulkan`, both in the upstream repo, so no third-party glue |
| Integration | FetchContent pinned to a release tag with checksum, unmodified (no `third_party/` copy). Memory editor widget: write our own (the popular `imgui_club` memory editor is MIT too, but ours must show PS2 addresses and symbol names anyway) |
| Build | always compiled (one binary), initialised only when `overlay_enabled` is true |

Alternatives: Nuklear (MIT, no maintained Vulkan backend, fewer widgets), RmlUi (MIT, HTML/CSS, aimed at game UI, heavy). Recommendation: **Dear ImGui**. Added to `../decisions/dependencies.md` as proposed.

## 4. Features

Effort: S ≤ 2 days, M 3–6 days, L > 1 week (each assumes the subsystem it reads already exists). "Sim" = can it change simulation results: **no** (read-only, cannot), **guarded** (changes only presentation or is safe by construction, rules given), **yes** (changes game state on purpose; such actions mark the session as "modified" in the log and in the overlay title bar).

### 4.1 Read-only

| Feature | What it shows | Source | Effort | Sim |
|---|---|---|---|---|
| Performance and frame timing | presented FPS, simulation steps per presented frame, catch-up count and dropped time (ADR-002 accumulator), CPU time per phase (game step, draw-list build, renderer, present), GPU time per bucket | platform timers, Vulkan timestamp queries | S | no |
| Scene inspector | current scene / sub-state (`ctx` scene fields, scene-2 sub-state 0x0029F38E), frame counters (`frame_count`, `frame_count_unpaused`, `scene_frame`), mission id (`game[0x27]`), `RequestScene`/`RequestSubState` log with frame numbers | guest memory + a hook in the translated `RequestScene`/`RequestSubState` | S | no |
| Memory viewer | hex/float/u16/u32 view at PS2 addresses (RAM, scratchpad), labels from `docs/symbols.csv` loaded at start, go-to by name | `GuestMemory` | M | no |
| Watch list | user-chosen globals and `g_MainContext` fields (typed from the struct definitions shared with `static_assert`s), change highlighting | guest memory + struct metadata | M | no |
| Work heap meter | used / free of the game bump allocator (0x00101000) and the work heap (98.4 % used in mission 1, `../game/memory.md`), high-water mark, allocation list by caller address | allocator hook | S | no |
| Random sources | the five sources (`../game/random.md`): table cursor and reads this frame (with caller address), libc `rand` state and call count, VU0 R, effect LCG words, sound LFSR | guest memory + counters in the translated helpers | S | no |
| File / CD queue | pending CD requests (ticket, DATA/BD index, sector, size, age vs. the 3600-frame timeout), ISO read throughput | `sceCd*` shim | S | no |
| Sound | SOUND.IRX voices (state, ADSR phase, pitch, volume), VAG slots 0–5 (stream position, buffer fill), RPC log (command batches, replies) | audio backend (ADR-004) | M | no |
| Input viewer | host input → PS2 pad bytes (`scePadRead` buffer), analog/pressure values, `ConvertPadInput` result | input backend + guest memory | S | no |
| Renderer statistics | per ordering-table bucket: packets, primitives, GS state changes, draw calls, time; VU1 program used | renderer | M | no |
| Framebuffer / GS VRAM viewer | render targets (main, shadow 0x2500, glow, small targets FBP 0x120–0x1C0), uploaded textures with CLUT, by page/PSM | renderer RT cache | M | no |
| Live session log | the session log (ADR-005) with filters (module, level) | log sink | S | no |
| *Extra:* game TTY | output of the game's own `printf` calls (shipping debug prints, e.g. `func_00250C30`, the movie-stream "CH0/CH2" warning, the MTXT version warning), invisible on a retail PS2 | `printf` in the libc shim | S | no |
| *Extra:* `sce*` call counters | per SDK function: calls this frame/total, unimplemented hits | `src/sce/` | S | no |
| *Extra:* object inspector | flight units (pointer table 0x002A1F70: position, speed, HP, AI state), projectile and effect pools with slot occupancy | guest memory + struct metadata | M | no |
| *Extra:* save image inspector | the save image (`../formats/SAVE.md`) decoded field by field, before/after a save, checksum check | `saves\` + format code | M | no |
| *Extra:* determinism checker | hash of guest RAM regions after each simulation step, written to a file; two runs (or port vs. replay) compared step by step to find the first divergence | `GuestMemory` | M | no |
| *Extra:* native screenshot | saves the native 512×448 (or N×) frame before the output pass, for comparison with PCSX2 screenshots; output goes to `%LOCALAPPDATA%\AC04Port\screenshots\`, never the repo | renderer | S | no |

### 4.2 Control

| Feature | What it does | Effort | Sim |
|---|---|---|---|
| Pause and single-step | pauses the **simulation step and the audio clock together**; "step" runs exactly one 59.94 Hz step (one PS2 frame). Presentation keeps running so the overlay stays usable | S | **guarded**: no step is skipped or repeated, so results equal an uninterrupted run. Wall-clock-dependent paths (none found: the game counts frames) would be the only risk |
| Toggle render buckets | hide a bucket / table (terrain, effects, HUD, sky…) in the renderer | S | **guarded**: only the GPU replay skips it. The draw-list build still runs (it consumes `rand` and the random table and changes state, `../subsystems/effects.md`, ADR-002). Never implemented by skipping game draw functions |
| Jump to scene | calls the translated `RequestScene(n, sub)` at the start of the next step, including the dead **scenes 1 and 3** (`SceneMenuEntry`, `SceneMovie`) to explore cut content (`dead-code.md`) | M | **yes**: changes game state; scene 3 needs `ret_scene` set first (dead `SetReturnScene` 0x00100B08) |
| RAM diff against a PCSX2 save state | loads `eeMemory.bin` from a `.p2s` (uncompressed, as `tools/p2s_read.py`) and diffs it with guest RAM, region by region, with symbol names; only reads the state file the user picks | M | no (read-only) |
| *Extra:* memory poke | edit a value in the memory viewer / watch list | S | **yes** |
| *Extra:* breakpoint on guest write | stop the simulation when a watched guest address is written (debug builds have write-watch, ADR-001) | M | **guarded** (pauses only) |
| *Extra:* random-read trace | log every `NextRandomFloat` read of one frame with caller address, to compare with a PCSX2 trace | S | no |

### 4.3 Later

| Feature | Notes | Effort | Sim |
|---|---|---|---|
| Save states | snapshot of guest RAM + scratchpad + all `src/sce/`/platform service state (CD queue, audio voices, VAG streams, pad, timers). Straightforward with guest memory (ADR-001); the service state is the work | L | **yes** on load |
| Mission select | sets the mission id and enters the mission through the game's own menu path (`MissionSelectAction` 0x0023AC70) | M | **yes** |
| Free camera | a **presentation-side** camera: the renderer replays the step's draw lists with its own view matrix (ADR-002 records the per-node world matrices). It never writes the game camera, so lock-on and HUD projection are unchanged | L | **guarded** |
| *Extra:* input recording/playback | record pad bytes per step and play them back (with the determinism checker, a regression test) | M | **yes** on playback (replaces input) |
| *Extra:* GS frame capture | write the frame's GIF/VIF packets in PCSX2 `.gs` dump format, to compare with PCSX2 GS dumps in the same tools | M | no |

## 5. Rules

- Part of the debug layer: compiled in, **inactive** unless `overlay_enabled` is true and the overlay is opened.
- Reading never mutates guest memory or service state (no read side effects: e.g. reading the random cursor must not call `NextRandomFloat`).
- Every "yes" action is logged and marks the session as modified.
- No game data leaves the process: no dumps into the repo, screenshots and traces go to the user data folder.

## 6. Owner decisions

1. Approve Dear ImGui as a dependency (`../decisions/dependencies.md`).
2. Require `overlay_enabled` in `settings.json` (proposed: **yes**, default false).
3. Which features go in the first version (proposed: the S items of 4.1, pause/step, bucket toggles, memory viewer, RAM diff).

## 7. Implemented (milestone D)

Code: `src/platform/debug/overlay.{h,cpp}`; hook for scene requests: `src/common/debug_hooks.{h,cpp}` (called by the translated `RequestScene`/`RequestSubState`, records only). Dear ImGui v1.92.9b (`../decisions/dependencies.md`).

- **Enable:** `settings.json` `"debug": {"overlay_enabled": true}` (default on in development builds, off in release), or `--debug-overlay` / `--no-debug-overlay` for one session. With it off, no ImGui context or GPU resource is created. Changing the setting while the port runs creates or destroys the overlay.
- **Toggle:** the typed character in `"overlay_toggle"` (default `"\"`, matched on SDL text input, so any keyboard layout works), or `"key:<SDL scancode name>"` (e.g. `"key:F12"`).
- **Input:** while an overlay window has keyboard focus, the keyboard is not mapped to the game's pad; gamepads still reach the game.
- **Look:** square style (every rounding 0, border 1), drawn in the output pass after the game image at host resolution. `imgui.ini` is not written (`IniFilename = nullptr`).
- **Panels** (all read-only, "Sim: no"):
  - *Performance:* presented fps and frame-time graph (240 frames), steps this frame, total steps, dropped steps (catch-up limit 4), interpolation alpha, DMA intake per step (GIF/VIF1 qwords, sends, bad chains), last DISPFB/DISPLAY.
  - *Scene:* scene and name, sub-state, previous/next scene, `req`, flags, `frame_count`/`scene_frame` (and unpaused), mission id, and the last 64 `RequestScene`/`RequestSubState` calls with step, `frame_count` and requesting scene.
  - *Memory:* 32 rows of 16 bytes at a PS2 address, as bytes+ASCII, u32 or f32; go to a hex address or an exact symbol name; each row labelled with the symbol it belongs to. Only RAM and scratchpad are read (never hardware registers). Symbols: `docs/symbols.csv` of the source tree in development builds, or an optional `symbols.csv` next to the executable (read only).
  - *Session log:* the last 400 log lines with a text filter; errors and warnings coloured.
- **Not yet** (later items of section 4): pause/single-step, bucket toggles, RAM diff against a `.p2s`, the other read-only panels.
