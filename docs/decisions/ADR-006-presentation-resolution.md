# ADR-006: Presentation resolution and output scaling

Status: **Accepted** (owner decision, 2026-10-10; §3 and §4 decided 2026-10-11). No open sub-decisions.

## Context (confirmed facts)

- `InitGraphics` 0x001044A8:
  - `sceGsResetGraph(0, 1, 2, 1)`, i.e. NTSC, interlaced, field mode;
  - a **512×448 draw buffer** and a **512×224 display buffer** (`../game/main-context.md`: `draw_w/h`, `disp_w/h`).
- Every frame, `UpdateDisplayFrame` 0x00104950 calls `DrawDisplayTransfer` (H) 0x001049B0.
  - It copies or blends the 448-line draw buffer into the 224-line display buffer for the current field, in 32-pixel strips.
  - It uses `LerpRgba` with the factors at `ctx+0x80..0x88`, which looks like a flicker filter (H).
  - Field parity comes from `sceGsSyncV`. `disp_flags` 0x2000/0x8000 select the copy mode, and 0x4000 skips it.
- Movies switch to frame mode (`disp_h` = 448) and show 640×448 (SS cutscenes 640×336, letterboxed inside 448).
- The PS2 shows the 512-pixel-wide image across a 4:3 screen, so pixels are not square. Pixel aspect = (4/3) / (512/448) ≈ 1.167.
- Options `ApplyDisplayOffset` (±12 steps on DISPLAY DX/DY) move the picture on a TV; `ApplyBrightness` 0x00105988 (option level 0–6) scales the colours of the display copy.

## Decision (accepted parts)

1. The game renders at its **native internal resolution**, as on the PS2: 512×448 for the game and menus, 640×448 for movies.
   - Game logic and the renderer's coordinate handling (ADR-003) are unchanged.
2. A **final output pass** scales the finished image to the host window or screen.
   - It **preserves the original 4:3 aspect ratio**, with pillarboxing on wider screens and letterboxing on taller ones.
   - It works the same for 512- and 640-wide sources, since both map to the same 4:3 picture.
3. Interpolation (ADR-002) happens inside the native-resolution rendering. The output pass only scales.
4. The output pass is presentation only. It never changes what the game draws or reads back (render-to-texture, field copy inputs).
5. Screen-position and brightness options (`ApplyDisplayOffset`, `ApplyBrightness`) still run unchanged in game code, so the GS DISPLAY register values match the PS2.
   - Whether the output pass mirrors that offset on the PC screen is part of sub-decision §4.
   - Default proposal: ignore it, since there is no TV overscan to correct.

## 3. Scaling filter (decided 2026-10-11)

**Decision:** the port implements **two filters**, selected in `settings.json` (`"presentation": {"filter": "sharp" | "smooth"}`):
- `sharp` = sharp bilinear (A below): integer nearest pre-scale, then bilinear to the final size;
- `smooth` = plain bilinear (C below).

Default: `sharp` (owner decision, 2026-10-11). There is no settings menu in the skeleton; the settings code is structured so one can be added later (see "Settings architecture").

Options considered:

| Filter | How | Pros | Cons |
|---|---|---|---|
| **A. Sharp bilinear** (integer pre-scale with nearest, then bilinear to the final size) | upscale 512×448 by the largest integer factor that fits, then bilinear the rest | crisp pixels without shimmer, handles the non-square 1.167 horizontal factor and any screen size; cheap | slight softness at the edges of pixels; still not "pure" pixels |
| B. Integer scaling (nearest, whole multiples only) | vertical ×N, horizontal chosen to keep 4:3 | perfectly uniform pixels vertically | **not possible on both axes at 4:3** because the pixels are not square: the horizontal factor is never an integer when the vertical one is. So the result is either not 4:3, or it falls back to nearest horizontally (uneven column widths). It also leaves large black borders (1080p: ×2 = 896 lines) |
| C. Plain bilinear | one bilinear pass | simplest, no borders, smooth | blurry at 2–5× scale |
| D. Nearest neighbour | one nearest pass | sharpest | uneven pixel widths (shimmer when things move), most visible with the 1.167 aspect |
| E. Area / Lanczos / bicubic | higher-order resampling | good detail on downscale or odd ratios | ringing (Lanczos), more shader work, softer than A at large factors |

B (integer) and D (nearest) are not implemented: B cannot keep 4:3 with integer factors on both axes, and D shimmers with the 1.167 pixel aspect. E is not needed at these scale factors.

## 4. Interlaced / field output → output image (decided 2026-10-11)

**Decision: option A**, present the full 448-line draw buffer. The field copy still executes in guest terms; its output is not presented. Movies (frame mode) are presented at 640×448.

Options considered:

On the PS2, the game draws a full 448-line frame every 1/59.94 s. The field copy then sends only one 224-line field (copied or blended from that frame) to the TV per vblank. Options for the port:

| Option | How | Pros | Cons |
|---|---|---|---|
| **A. Present the full 448-line draw buffer** (progressive) | the output pass reads the 512×448 draw buffer after the frame is drawn, before or instead of the field copy | full vertical resolution every frame; no combing, no flicker; the field copy still runs in guest terms (the renderer still executes its GIF packet, so the GS state stays exact) | skips the flicker filter's blend, so the image is slightly sharper than on a TV (H: the filter is mild) |
| B. Weave the two most recent fields | rebuild a 448-line frame from the field-copy outputs of the last two steps | closest to "what the GS output" | combing on motion, since the two fields come from different frames |
| C. Bob (line-double the current field) | scale the 224-line field to 448 | exactly what one field showed, simple | half vertical resolution; flicker of thin lines between fields |
| D. Field output + blend/motion-adaptive deinterlacing | CRT-like processing | closest to a TV look | complexity; it is a look, which belongs to the modding layer |

Reason for A: the game already renders a full progressive 448-line frame every tick. Movies (frame mode) are presented directly at 640×448.
- Confirmation needed: a PCSX2 GS dump showing what the field-copy packet does in each `disp_flags` mode, so we know the draw buffer is complete when the copy starts.
- B/C stay available for comparison testing against PCSX2.

## Consequences

- `src/platform/render/` gets an output pass:
  - input is the native framebuffer (512×448 or 640×448);
  - output is the swapchain image, with the selected filter, 4:3 and black bars.
- The renderer's GS emulation stays at native resolution. ADR-003 option C is unaffected.
- A higher internal resolution is studied in `../port/internal-resolution.md` (not approved; the renderer keeps a scale factor fixed at 1×). Widescreen stays in the modding layer (`port-baseline.md`).

### Settings architecture
- One `Settings` struct (plain data, defaults in code) owned by `src/platform/`; `settings.json` under `%LOCALAPPDATA%\AC04Port\` (ADR-005).
- JSON load at startup and save on change; missing or invalid keys fall back to defaults and are logged; unknown keys are kept.
- A change notification (subscribers per section: presentation, window, input, audio, debug) so a later settings menu or the debug overlay can change values at run time without restarting. The output pass re-reads the filter on notification.
- No settings menu in the skeleton. Game code never sees settings.

## Research findings

### The game has its own 16:9 mode

- The options page has **SCREEN RATIO** (option byte 0x002A15CC bit 0). Turning it on sets ctx flag 0x10000.
- Menu layouts, the aircraft-viewer camera aspect, text boxes and the briefing map all branch on that flag (`../subsystems/frontend.md`). On the PS2 this produces an **anamorphic 16:9** picture for widescreen TVs: the same 512/640-wide frame, drawn for a 16:9 display.
- Consequence for this ADR: the output pass must use **16:9 when the in-game SCREEN RATIO option is on**, and 4:3 otherwise. That is original behaviour (category 1), not a new feature. The 4:3 rule above applies to the default setting.

### 16:9 mode in the flight scene

- In 16:9 mode (ctx flag 0x10000) every HUD sprite is drawn 3/4 as wide, fonts switch to narrower tables, and the view cameras use a 16:9-aware width (`../subsystems/camera-hud.md`). The picture is anamorphic: the same 512-pixel frame is meant to be shown at 16:9.
- So the rule above applies to the flight scene too: the output pass presents at 16:9 when SCREEN RATIO is on.
