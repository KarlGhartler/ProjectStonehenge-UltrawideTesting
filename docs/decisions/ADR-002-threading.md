# ADR-002: Threading and timing model

Status: **Proposed** (rev. 2026-10-10 b: owner's working assumptions and interpolation design added; decision before the skeleton).

## Context (confirmed facts)

From `docs/research/threads-timing.md`, `pcsx2-sessions-02.md` (sessions 5 and 6) and `boot.md`:
- **Single game thread.** All game logic runs on the EE main thread in one main loop, in this order:
  1. DMA kick;
  2. pad;
  3. sound RPC;
  4. stream RPC;
  5. `SceneStep`;
  6. vsync wait;
  7. display flip;
  8. CD queue.
- **No game interrupt handlers.** Vsync is a busy-poll inside `sceGsSyncV` (`PollVblankStart`).
- **Fixed step of one NTSC field.** Game logic advances once per main-loop iteration (1/60 s constants, frame-count timers, fades in frames). There is no variable timestep anywhere. **Measured: 59.94 Hz in all gameplay and in-engine cutscenes, heavy scenes included; no 30 Hz mode exists** (session 5).
- **SMV movies run at 29.97 fps on the 59.94 Hz loop:** each picture is held for 2 vblanks (session 5).
- **Threads:** in normal play (memory card, no HDD) the only `CreateThread` call is the SDK helper at boot (session 6). The save/HDD module's job worker and power-off handler are created only on the HDD path, which the port does not support (`dead-code.md`, "Unsupported paths").
- I/O is asynchronous but polled from the main loop: CD request tickets, `sceSifCheckStatRpc`, memory-card status.
- Rendering runs one frame behind (the main loop kicks last frame's DMA lists first).

## Options

### A. Single game thread, fixed step paced by an accumulator (recommended)
- **Loop:** the translated main loop runs unchanged on one thread. A real-time accumulator advances the game by exactly one PS2 frame each time 1/59.94 s of wall time has accumulated. `sceGsSyncV` is the point where the loop waits for the next step.
- **Presentation:** the window presents the latest completed frame at the monitor's refresh rate (vsync of the host swapchain), independent of the game step.
- **Platform work** (disc reads, audio mixing, save files) runs on platform threads behind the same polled interfaces the game uses.

### B. Couple the game step to the host vsync
- \+ Simpler (no accumulator).
- − Runs the game too fast on 120/144 Hz monitors and too slow on 50 Hz, so game speed would depend on the display. Rejected: the game must always advance at 59.94 Hz.

### C. Emulate EE threads (cooperative scheduler or OS threads)
- − Not needed: no game-owned thread exists on the supported path. OS threads would also add races the original never had.

## Decision proposal (option A; owner's working assumption 2026-10-10)

1. **Fixed step.** The simulation advances exactly one PS2 frame per step (1/59.94 s). Game code never receives a variable `dt`. Simulation results are identical to the original.
2. **Pacing.** A real-time accumulator.
   - Each step consumes 1/59.94 s.
   - If the host falls behind, the simulation **catches up with at most N steps per presented frame** (N = 4 proposed). Beyond that, the excess time is dropped: the game then runs slower for that moment, but it never runs permanently slow or fast.
3. **Presentation is decoupled.** It runs at the monitor's refresh rate (any rate, vsync on or off).
   - Between simulation steps the renderer **interpolates visually** (section "Interpolation design").
   - Interpolation is presentation only. It is an approved baseline feature (`port-baseline.md`).
4. **Field parity** alternates exactly as `sceGsSyncV` returns it on the PS2: once per simulation step, never per presented frame.
5. **SMV movies:** each decoded picture is held for **exactly 2 game ticks**, paced by the tick count, not by the video's own clock. Video is never interpolated. Movie PCM is fed in step with the same tick count (SOUND.IRX block transfer, ADR-004).
6. **No EE thread emulation on the default (memory card) path.**
   - `sceMc*` are file operations behind the same polled API.
   - The HDD-only threads belong to unsupported paths (`dead-code.md`). Their SDK calls are stubs that log the call and return an error.
7. **RPC and I/O latency.**
   - SOUND, VAG and USBJOY calls go to platform services that report completion on the next poll, matching the original one-call-in-flight rule (ADR-004).
   - CD requests complete asynchronously, with the same tickets and the 3600-frame timeout semantics.
   - Audio, file and save work run on platform threads.

## Interpolation design (presentation only)

The approach follows the N64 recompilation ports: record what each simulation step draws, match the draws between the last two steps, and redraw at the presentation time with interpolated transforms.

### Timing
- Presentation time `t` lies between step N−1 (state A) and step N (state B), with `alpha = (t − t_A) / step`.
- The renderer always shows a blend of the last two completed steps. That adds **one step of latency (16.7 ms)** compared with presenting step N directly. This is the usual cost of interpolation; it is not a game-logic change.
- At exactly 59.94 Hz with vsync, `alpha` is always 1, so the output equals the original frame by frame.

### What is recorded per step
The renderer (ADR-003) already walks the DMA/VIF/GIF stream of each step. It records a **draw list** instead of drawing immediately:
- **3D draws** (VU1 path): VU1 program id, the input buffers (DMA `ref` source addresses in guest RAM), and the **matrices uploaded to VU1 memory** for that draw (model/view/projection, as the VU1 program reads them).
  - Where each matrix comes from is a priority-2 research item (step 9: per draw path, matrix source, object identity, 2D/3D).
- **2D draws** (GIF PATH3 sprites, HUD, menus): the GIF packet and its source address, plus the screen coordinates.
- **State changes** (render targets, textures/CLUT uploads, blend/test state) in order. Render-to-texture passes are recorded as part of the list.

### Matching between steps
A draw in step N is matched with a draw in step N−1 by a **stable key**:
- 3D: `(VU1 program id, guest address of the object's model/mesh data, ordinal of that address within the step)`. The ordinal separates instances of the same model.
  - If research finds an object id or slot index in the data that builds the draw (e.g. the actor table index), that index is used instead, because it is stabler than ordering.
- 2D: `(packet source address, primitive ordinal within the packet)`.
- Unmatched draws (new or removed this step) are drawn with step N's values, without interpolation.

### Interpolating
- **Object matrices:**
  - decompose into translation, rotation (quaternion) and scale;
  - translation and scale are lerped, rotation is slerped;
  - the result is recomposed.
  - Matrices that do not decompose cleanly (skew, projection-like) are lerped element by element, or not interpolated if that fails.
- **Camera:** the view matrix is interpolated separately (position lerp, orientation slerp). The projection is taken from step N.
- **2D / HUD:** screen-space positions of matched 2D primitives are lerped, for example a target box that follows a 3D object.
  - Text and static HUD elements whose position does not change are drawn unchanged.
  - HUD values (numbers, gauges) are never interpolated: step N's values are shown.
- **Particles and effects:** interpolated only if a stable key exists. Otherwise they are drawn from step N.
- **Render-to-texture:** passes are replayed with the interpolated matrices in their recorded order, so effects stay consistent.

### Never interpolated (snap to step N)
- **Camera cuts.** Detected when:
  - the camera position or orientation changes beyond a threshold (e.g. > 50 world units or > 30° in one step);
  - or the game switches camera mode, which is to be identified in priority 2, step 6 (camera).
- **Teleports.** Per object, a translation or rotation delta beyond a threshold.
- **Scene and sub-state changes** (`SceneStep` request bits), loading screens, menus (scene 4 menu pages), pause, results.
- **Movies (SMV)** and any frame where the field copy is skipped (`disp_flags & 0x4000`).
- Any step where the matching ratio falls below a threshold (most draws unmatched).

### Guarantees
- Interpolation only reads the recorded draw lists. It never writes guest memory, never calls game code and never affects the next simulation step.
- With interpolation off, or at exactly 59.94 Hz, the output is the step-N frame, the same as the original.
- Thresholds and keys are tuned against PCSX2 captures and replays. A debug overlay showing matched/unmatched/snapped draws is a modding-layer tool.

## Consequences

- `src/platform/` owns: the frame clock (accumulator), presentation and interpolation, file/audio/save worker threads.
- ADR-003: the renderer records per-step draw lists and runs the VU1 HLE at presentation time with interpolated matrices.
- `src/sce/` thread/semaphore functions are needed only to satisfy calls on unsupported paths (log once, return an error, per the platform rules).
- Determinism: game logic depends only on the step count and on I/O completion frames. Behaviour tests can pin the I/O latency ("completes after N steps") and replay inputs frame by frame against PCSX2.

## Research finding (2026-10-10, front end): in-menu movies are paced by timer 0

- The in-menu movie player (briefing movies, story interludes) does not wait in `sceGsSyncV`. `StartMenuMovie` sets `RCNT0_MODE = 0x83` (timer 0 counts H-blanks). Each movie frame resets `RCNT0_COUNT` at the start, and `MovieFrameSync` busy-waits until the counter reaches **0x118 (280 H-blanks, about 17.8 ms)**, then increments `ctx->frame_count` itself.
- The port's T0 must count H-blanks at the NTSC rate on this path. Alternatively, the wait can be mapped to "one step" if PCSX2 confirms that each loop iteration takes exactly one field (open question).
- Still compatible with decision 5 (a picture held for 2 ticks), but the tick here is produced by the T0 wait, not by the main loop's vsync.

## Research finding (2026-10-11, HUD): game state changed in the draw phase

- `DrawHud` 0x0015EAE0 reads the pad and changes the radar mode byte (HUD state +0x72) while it builds the draw lists. The draw-list phase is therefore not purely read-only.
- Consequence for the interpolation design: the game's own draw-list build must run exactly once per simulation step (as now). Extra presented frames must re-use the recorded draw lists (with interpolated matrices), never call `BuildFlightDrawLists`/`DrawHud` again.

## Research finding (2026-10-11, rendering): randomness and matrices in the draw phase

- The draw phase consumes random numbers (`render.md`, randomness table):
  - `rand()` is called by the sky (`func_00110630`), the sun/moon camera jitter (`func_001145E8`) and effect draws (`func_001A1FA8`, `func_001CFCA8`, `func_001D3950`). `rand()` also drives the radio chatter choices, so drawing changes later radio messages.
  - The random table (`func_00100DC8`) is read by draw code too. It is reset to `frame & 0x1FF` at the start of every gameplay frame, so draw-time reads do not leak into the next frame's gameplay.
- Effects are updated inside the simulation step and read the random table before the AI and weapons of the same frame. Effects therefore cannot be skipped or thinned without changing gameplay.
- This confirms the HUD finding above: the game's draw-list build must run **exactly once per simulation step, in the original order**. Extra presented frames re-use the recorded draw lists.
- Matrix sources for interpolation: the per-node world matrices live in the model instances (instance +0x10/+0x14, rebuilt each step by `UpdateModelHierarchy` 0x0013FE58). The final camera × node products exist only inside the packet built by `EmitModelInstance` 0x001404A8. Object identity: unit index + model state + LOD for units, player index for aircraft, pool slot for projectiles and effects (`render.md`, "Matrix sources"). An interpolating renderer must record node world matrices and the camera per step and redo the products at presentation time, as the Consequences section above already assumes (ADR-003: VU1 HLE at presentation time with interpolated matrices).
