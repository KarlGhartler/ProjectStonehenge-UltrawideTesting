# Feasibility: higher internal rendering resolution

Status: **study, not approved.** Higher internal resolution is a "Later" baseline candidate (`../decisions/port-baseline.md`). ADR-006 keeps native resolution; this document says what a scale factor N (2×, 3×, 4×) would take, and how to design the ADR-003 renderer now so N exists from day one, fixed at 1×.

Sources: `../subsystems/rendering.md`, `../platform/gs-subset.md` (static scan + eight GS dumps), `../subsystems/camera-hud.md`, `../subsystems/effects.md`, ADR-003 / ADR-006.

## 1. Why it cannot change game logic

- The game **never reads GS memory back**: the dumps show host → local uploads only, no local → host transfer and no local → local copy (`gs-subset.md`, "Measured in GS dumps"). No game function touches GS privileged registers or VU memory (`rendering.md`).
- So nothing the renderer produces flows back into guest RAM. The packets, GS register values, camera records (+0x160 screen size, +0x178 centre, +0x188 viewport) and every HUD/lock-on projection stay in native 512×448 coordinates in guest memory.
- Scaling is applied **inside the renderer only**, after it has parsed the guest packets. Game code, `src/sce/` and the guest GS state are unchanged at any N. The random sequence and draw-list build are unaffected (they run before the renderer sees anything).

## 2. How scaling works in a GS-replay renderer

A GS coordinate is `(XYZ − XYOFFSET) / 16` pixels (12.4 fixed point). With scale N the renderer maps a GS pixel (x, y) in a render target to host pixel (N·x, N·y) of a surface allocated N× larger. Everything the GS addresses in **pixels of a render target** scales; everything addressed in **texels of an uploaded texture** does not.

## 3. Draw paths that work unchanged

| Path | Why it scales cleanly |
|---|---|
| 3D models through the VU1 programs (A–E): triangle strips/fans, Gouraud, STQ, fog | VU1 HLE outputs GS vertices; only the final XY is multiplied by N. Perspective UV (STQ) is resolution-independent. Mip selection (trilinear, LCM 0 + K) must use the native-resolution LOD formula or textures get sharper/blurrier per level (it uses Q, not screen derivatives, so it is unchanged) |
| Terrain and sea (`DrawTerrain`, sea tiles clipped on the CPU) | same as above; CPU clipping is in 3D, not screen space |
| Sky, weather, world objects, projectiles | same VU1/GIF triangle paths |
| Effects drawn as textured triangles/sprites with STQ or with UV from uploaded textures | geometry scales; texture lookup unchanged |
| Screen fade (bucket 2 of the overlay table), full-screen tints | full-screen sprites: rectangle scales, flat colour |
| Scissor / viewport (`SCISSOR_1/2`, camera +0x188) | multiply by N (inclusive max: `N·(max+1) − 1`) |
| Depth test / Z buffers (Z24, Z32), DATE, alpha test, blending | per-pixel operations: identical at any N |
| Movies (640×448 MPEG frames; 640×336 for SS) | uploaded as textures and drawn as sprites: the sprite scales. At N > 1 the frame texture is replaced by the output of the movie upscaling stage (FSR 1, 640N×448N), owner decision 2026-10-13 (`../decisions/ADR-006-presentation-resolution.md` §5); at 1× the 640×448 texture is used as on the PS2. The stage, the decoder side path and the draw identification (`DrawCommand::texture_guest_source`) exist since S3. Still to do with the scaled GS backend: sampling the stage output in those draws |

## 4. Paths that break, and the fix for each

| # | Path | What breaks at N > 1 | Fix |
|---|---|---|---|
| 1 | **2D sprites, HUD, text, menus** (≈40 textured SPRITE functions, `DrawHud`, fonts) | Sprites map texels 1:1 to native pixels with bilinear filtering. At N× each texel spans N pixels and bilinear sampling between texel centres softens text and shows seams where atlas sub-rectangles touch (REGION_CLAMP/REPEAT atlases) | For sprites (PRIM SPRITE with FST UV): **round UVs to the native pixel grid**, i.e. compute the native pixel the host pixel belongs to and sample at that pixel's UV ("round sprite"). Optionally switch 1:1 sprites to nearest when UV step = 1 texel per native pixel. Region clamp rules stay in texel space |
| 2 | **Lines and line strips, AA1** (≈20 functions: HUD pitch ladder, radar, lock boxes, map) | GS lines are 1 pixel wide: at N× they become 1/N as thick and AA1 coverage is computed at the wrong scale | Draw lines as quads **N pixels wide**; AA1 = coverage ramp over the outer pixel at host scale. HUD look must be checked against native screenshots |
| 3 | **Framebuffer used as a texture** (PSMCT32 1024×1024 "screen copies", field copy, glow composite, **heat-haze distortion** `func_001E88C0` with displacement offsets in pixels) | the texture is the render target itself: TEX0 TBP points at an FBP page, UVs are in GS texels of the native target | The render-target cache (§5) resolves TBP → scaled surface and divides UV by the **native** target size, so the sample lands on the same native location; bilinear at scaled resolution then reads N× more detail (desired) |
| 4 | **Field copy** `DrawDisplayTransfer` 0x001049B0 (448-line draw buffer → 224-line display buffer, 32-px strips, flicker blend) | scaled draw buffer → scaled display buffer: works, but the vertical halving and line blend operate on N×-scaled lines | With ADR-006 §4 option A (present the draw buffer), the copy still executes (GS state stays exact) but its output is not presented: no visible issue. Options B/C (weave/bob) would read the display buffer at N× and need line-pair selection at host scale. Decide together with §4 |
| 5 | **Render-to-texture chains: aircraft shadow** (page 0x2500, half-size copy, 3-offset blur, blend onto frame) and **small targets** FBP 0x120–0x1C0 (64/128/256 px wide) | If the small targets scale too, the blur's ±1 texel offsets become ±1 *native* texel = N host texels apart: the blur gets a dotted, aliased look. If they do not scale, the composite onto the scaled frame is magnified (soft, like the PS2) | **Per-target scale policy**: main framebuffer and depth at N; small off-screen targets at **1× by default** (keeps the PS2 look and the blur kernel exact), optional N later per chain after visual comparison |
| 6 | **Half-resolution glow pass** `func_00142460` (camera ×0.5 via `BuildScaledCameraMatrices`, 256-px target at block 0x2500, Z24 at 0x2A00, blur, add in 256×16 strips) | its Z buffer (0x2A00) is a separate half-size Z: fine. But if the main Z were sampled into it, sizes must agree. The strips are sampled from the half-size target | Treat as a small target (rule 5): 1× by default. Its vertices are scaled by the camera, so they land correctly in a 1× target; the composite sprite scales to the N× frame. If later scaled, scale the target **and** its Z by the same N |
| 7 | **Depth sampled as a texture** (PSMZ24 as texture, FBP 0x150) and **format reinterpretation** (CT32 target read as T8H palette index in the alpha byte, CT24 with TCC 0, CT16S target sampled as CT16S) | pixel-preserving reinterpretations work per pixel at any N. A reinterpretation that changes the pixel layout (e.g. a CT32 page read as PSMT8/PSMT4 swizzled data) would need native data | The dumps show only pixel-preserving cases. Rule: if the cache sees a layout-changing reinterpretation of a scaled target, it **downsamples to native** (one sample per native pixel, nearest) and converts; log it once |
| 8 | **Uploads into a region that is also a render target** (IMAGE transfer whose DBP overlaps an RT) | a native-size upload into an N× surface | Upload to a native staging texture, then draw it scaled (nearest) into the surface. Movies and texture uploads normally go to texture pages, so this is a corner case; detect and log |
| 9 | **Sub-pixel rules** (top-left fill convention, 12.4 snapping, sprite edge rounding) | half-pixel offsets differ at N× (thin cracks between sprites, 1-host-pixel shifts) | Compute coverage in GS 12.4 units scaled by N (exact for integer N); sprite edges rounded in native units then multiplied by N |
| 10 | **Scissor-dependent strip work** (256×16 composite strips, 32-px field copy strips) | strips meet exactly at native pixel boundaries; at N× they still meet if edges are integer multiples | integer N only (2, 3, 4). Non-integer scales are not supported |

## 5. Effort

| Work | Effort | Notes |
|---|---|---|
| Scale plumbing: one GS→host transform, surfaces "GS region × scale", scissor/viewport | 2–3 days | cheap if designed in from day one (§6) |
| Render-target cache keyed by (page, width, PSM) with per-target scale, TBP→surface resolution, UV normalisation by native size | 0 extra | **needed at 1× anyway** (render-to-texture, framebuffer-as-texture) |
| Sprite UV rounding (fix 1), line quads + AA1 (fix 2) | 4–6 days | most visible work; HUD must match native screenshots |
| Per-target scale policy (fixes 5–6), downsample path (7), RT-overlap uploads (8) | 3–5 days | |
| Validation: replay the eight GS dumps at 1× and N×, downsample N× to native and diff against the 1× image (tolerance), plus side-by-side screenshots per scene | 4–6 days | tooling reusable for the 1× renderer |
| **Total** | **≈ 13–20 days** on top of the 1× renderer | renderer-only |

Nothing outside the renderer changes: no game code, no `src/sce/`, no guest memory layout, no timing. The output pass (ADR-006) scales from N×512×448 instead of 512×448; the `sharp`/`smooth` filters still apply (at N ≥ 2 the integer pre-scale of `sharp` is usually 1).

## 6. Recommendation: design for a scale factor now, ship 1×

1. **One transform function** maps every GS coordinate to host pixels: `HostXY = (XYZ − XYOFFSET) · N / 16`. Scissor, viewport and copies use the same function. No other code multiplies by a resolution.
2. **Render targets are allocated as "GS region × scale"**, with the scale stored per target, chosen by a policy function (main framebuffer/Z: N; small targets: 1). The policy returns 1 for everything while N is fixed at 1.
3. **A page-tracked render-target cache** (needed at 1× for render-to-texture): lookups by TBP/FBP page, width and PSM; every sample from a target normalises UV by the **native** size.
4. **Sprite and line rasterisation rules in one place** (fixes 1, 2, 9), parameterised by N, so they are exact at 1× and switch on with N > 1.
5. **Internal scale is a setting** (`settings.json`, `presentation.internal_scale`, accepted value 1 only) until each draw path in §4 is validated with the dump comparison; raising the allowed values is an owner decision (baseline "Later" item).

This keeps the 1× renderer simple (N = 1 makes every rule the identity) and avoids a later rewrite of coordinate handling across the ≈233 packet-writing paths.

## Open points

- Which ADR-006 §4 interlace option is chosen (affects fix 4 only).
- Whether the shadow/glow chains look better scaled (fix 5) is a visual judgement for the owner once the renderer runs.
