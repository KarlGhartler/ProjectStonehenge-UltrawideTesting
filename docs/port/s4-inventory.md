# S4 inventory (decompile-first, owner rule 2026-10-15)

Everything slice S4 uses from the game files, decompiled and described before the rest of S4 is implemented (CLAUDE.md Workflow step 1). Function list: `python tools/research/slices.py --list S4` (197 left on 2026-10-15; the title card, briefing, camera library, campaign menu and aircraft-data accessors are already translated). Status per line: **D** decompiled and described here, **T** translated. **All lines are T since 2026-10-16** (the table column still shows D), except the effect drawers and the glow pass listed under "In the static closure but not run in S4", deferred to S5f. Names in *italics* are working names, not yet renamed in Ghidra.

## 1. Hangar (scene 4 subs 27–36; pages 6–10, 13–16)

### Hangar state object (0x170 bytes, `*0x00411048`)
- +0x40..+0x6F: copy of the purchase state at 0x002A0320 (6 × u64; per aircraft 2 bits, in **save order**, see `0x00233720`/`0x00233790`).
- +0x70..+0x9F: the same 6 × u64 in **list order** (2 bits per aircraft: 0 not available, 2 owned, 3 for sale; rows 0–2 colours, rows 3–5 special weapons).
- +0xA0 player (0/1), +0xA2 page (0 aircraft, 1–4 shop columns, 5–7 special weapon, 8/9 confirm, 10/11 leave), +0xA4 next page, +0xA6 return page, +0xA8/+0xAA MenuGoTo target on accept, +0xAC/+0xAE on cancel.
- +0xB0..+0xDF: 6 × u64 availability masks (bit per aircraft) from the 2-bit rows (`0x00233950` by shop mode +0xE0).
- +0xE0 shop mode (0 select, 1 buy, 2 sell, 3/4 special weapons buy/sell), +0xE8 aircraft id, +0xEC list index (aircraft − 1), +0xEE colour (0–2), +0xF0 special weapon slot, +0xF2 panel count, +0xF4 = 0x15, +0xF6 special weapon count, +0xF8 panel flags, +0xFA animation flags (bits 0–2 phase: 1 shown, 2 opening, 4 closing; 0x10 sliding, 0x20 texts, 0x80 settled, 0x100 input, 0x200 crosshair, 0x400, 0x1000 SE played, 0x2000 tick SE).
- +0xFC..+0x108 animation times, +0x10C 6 approach values, +0x124 6 panel levels, +0x144 panel list (0x80 bytes, 16-byte entries from 0x0039E148), +0x148 credits, +0x168 credits ^ 0x5963 (checksum; the commit is skipped when it does not match), +0x14C purchase confirmation pending, +0x150, +0x154 nothing available, +0x158 page fade level, +0x15C viewer delay, +0x160/+0x164 viewer camera preset and time.

### Functions
| Address | Working name | Role | St |
|---|---|---|---|
| 0x002356D8 | HangarEnter | subs 27–35: work heap reset; state object (0x170) and its panel list (0x80); `InitHangarState` from game slot 0 or 1 (sub 0x1F = player 2); `CreateAircraftViewer(aircraft, colour)`; viewer mode +0x9B8 by page (1 for pages 1/3, 2 for 2/4); camera preset 0 at once; BGM (`0x00235540`); sub 36 | D |
| 0x00235540 | *PlayHangarBgm* | sub 0x1B: `GetProgressBgmTrack(2, mission)`, else track 9 | D |
| 0x00235588 | InitHangarState | clears the object (keeps +0x144); aircraft 1–23 (else 2), colour 0–2; list index via `0x00226400`; +0xF4 = 0x15; special count (`0x00233EF0`); credits from 0x002A0D30 and checksum; page/targets by sub (`0x00233808`); purchase state copy and list-order conversion; masks; first available aircraft (`0x00233BA0`); panel set-up (`0x00235580`); fade level 1 | D |
| 0x00233808 | *SetHangarPageForSub* | sub 0x1B: page 0, accept → page 3 (briefing), game[0x27] = mission id, game[0x25] = game[0x29] = 1; 0x1C–0x1F: page 0, cancel target 0x1C/0x1D/0x1E; 0x20–0x23: shop pages (buy/sell aircraft/specials) with mode +0xE0 | D |
| 0x00233720 / 0x00233790 | *PurchaseStateToList* / *PurchaseStateToSave* | reorder the 2-bit fields of 6 u64 rows between save order and list order (table 0x0039DA80 +8 per 0x3C-byte aircraft entry / s16 table 0x0039DA50) | D |
| 0x00233908 | *GetStateMask* | bit mask of the 21 aircraft whose 2-bit state equals `n` | D |
| 0x00233950 | *BuildAvailabilityMasks* | masks +0xB0.. from the rows by shop mode (0/2: state 3; 1: state 2; 3/4: mixed) | D |
| 0x00233B48 | *ListSetBits* | writes the indices of the set bits (below n) to a byte list; returns the count | D |
| 0x00233BA0 | *SelectFirstAvailable* | moves aircraft/colour/special to an available one; returns change flags (1 aircraft, 2 colour, 4 special) | D |
| 0x00233D48 | *SelectAvailableSpecial* | special slot to the first one available for the aircraft (returns 4 on change) | D |
| 0x00233DF0 | *MoveHangarCrosshair* | crosshair target by page (aircraft list row/colour column, special slot) | D |
| 0x00233E58 / 0x00233E88 | *ClearValues* / *ApproachValues* | n floats zeroed / `ApproachValue` towards a table (step k) | D |
| 0x00233EF0 | *GetSpecialWeaponCount* | `*GetAircraftSpecialWeapons(a)` | D |
| 0x00233F10 | CommitHangarSelection | writes aircraft/colour/special to game slot; slot 0 also to 0x002A15BE.. and `ValidateCampaignSelection`; when the checksum matches: credits to 0x002A0D30 and the purchase state back to save order at 0x002A0320 | D |
| 0x00234000 | *BuildPanelList* | copies the 16-byte panel entries (table 0x0039E148, 8) selected by flag bits (always the first two) | D |
| 0x00234078 | *OpenHangarPage* | page set-up: crosshair, values cleared, panel flags from 0x0039E128 by page, panel list, crosshair target | D |
| 0x00234150 | *SetHangarPhase* | phase 2 → `OpenHangarPage`; 4 → closing (times reset, crosshair reset) | D |
| 0x00234190 | *AnimateHangarPanels* | opening: panel levels (time − start) × 6, SE 0xD at 0.75, flags at 1.0; closing: 1 − t × 6, at 0 next page (10/11: fades and viewer state 4/5 = leave); tick SE 9 while texts type | D |
| 0x00234588 / 0x00234628 | *GetAircraftArrows* / *GetSpecialArrows* | tab/arrow flags for the menu fade (0x30/0x3C, +3 or +0xC) from the counts | D |
| 0x002346B0 | ApplyHangarChange | after a selection change: texts restart, special count, slot 0; a colour change sets camera preset 4 (+0x160; corrected 2026-10-16: not the viewer delay) | D |
| 0x00234740 | *HangarAircraftInput* | left/right: next available aircraft (`0x00226430` list index); up/down: colour among available; SE 4; viewer delay 5 on left/right | D |
| 0x00234A08 | *HangarSpecialInput* | left/right through the available special weapons | D |
| 0x00234C50 | *GetPurchasePrice* | price/ownership change for page `kind` (prices from table 0x0039DA80, 0x3C bytes per aircraft: name, code, ..., prices at +0x24.. and +0x30..); with `apply` sets the 2-bit state (3 → 2 buy, 2 → 3 sell); returns −price (buy) or +price (sell), 0 when not possible | D |
| 0x00234EB8 | *HangarAccept* | X/Circle: pages with a price: first press asks (SE 2, +0x14C), second applies (SE 0xF/0x10), credits and checksum, masks, reselect; other pages: next page from 0x0039E1C8 (or SE 5 when not available) | D |
| 0x002351B0 | *HangarCancel* | Triangle: SE 3; cancels the pending purchase or goes to the page from 0x0039E1E8 | D |
| 0x00235238 | HangarInput | pad of the player (player 2 also takes player 1's Triangle); accept/cancel/Start (Start on pages 0/5: confirm page 8/9); aircraft and special input by panel flags | D |
| 0x002353E8 | *HangarConfirmInput* | pages 8/9: Start → back to +0xA6 | D |
| 0x00235438 | HangarUpdate | panel animation; pages 8/9 fade the page out; else fade in, crosshair, approach values (0x0039DA8C + 0x3C × index), input | D |
| 0x00235580 | InitHangarPanels | tail call `SetHangarPhase(h, 2)` (opens the first page; corrected 2026-10-16: not an empty stub) | D |
| 0x00235828 | *SyncHangarViewer* | viewer colour, aircraft (`SetAircraftViewerAircraft`) after the delay; highlight mask +0x9AC of parts that are for sale (state 3, or 2 on pages 3/6) | D |
| 0x00235988 | *UpdateHangar* | from viewer state 2 on: help line, `HangarUpdate`, viewer sync, camera (`0x00231CE0`); `UpdateAircraftViewer` | D |
| 0x002359F0 | *DrawHangar* | buckets (7), viewer, page fade bar (`0x002281E0`), panels (`0x00233138`), help line, tabs, black fade | D |
| 0x00235AB8 | AircraftSelectRun | sub 36: update, draw; viewer state 3/4 after the menu fade: commit and MenuGoTo(+0xA8/+0xAA) (clear colour 0; mission 1 with the default aircraft → game[0x27] 0x25, game[0] 0x17; mission 18 → game[0x27] 0x24); state 5: commit and MenuGoTo(+0xAC/+0xAE) | D |
| 0x002334F8 | *UpdateHangarHelp* | help line by page (tables 0x0039E038/68/98, special weapon help 0x0039E0C8/E8/108) | D |
| 0x00233450 | *HasOtherColourForSale* | another colour or special of the aircraft is for sale | D |
| 0x00233138 | *DrawHangarPanels* | frame outlines, price box (`0x00232E70`), crosshair, gauges (`0x00231ED0`), special icons (`0x00231DE0`), texts (`0x00232F80`), pulse boxes | D |
| 0x00232F80 | *DrawHangarTexts* | text layer: name (`0x00232260`), statistics (`0x00232408`), weapon (`0x002325A8`), colours (`0x002327A0`), special (`0x00231FB8`), price (`0x00232BE8`), ownership marks (`0x00232930`, `0x00232AD0`); returns "finished" flags | D |
| 0x00232260, 0x00232408, 0x002325A8, 0x002327A0, 0x00231FB8, 0x00232BE8, 0x00232930, 0x00232AD0 | *DrawHangar…Text* | the panel texts sliding in by level × 80 (sprintf formats at 0x00410FB8..0x00411040) | D |
| 0x00232E70 | *DrawPriceBox* | frame outline of the price box (0x0039E018), red when unaffordable; inner box `0x0022AB20` | D |
| 0x00231ED0 / 0x00231DE0 | *DrawHangarGauges* / *DrawSpecialIcons* | `BuildGauge` × n; icon boxes `0x0022A868` | D |
| 0x00231CE0 | *UpdateHangarCamera* | viewer camera preset by page (3 aircraft, 4–6 special slots, 7 cockpit, 8 special view) and time | D |
| 0x002318F8 | UpdateViewerCamera | look-at eye/target per preset: 0 slow orbit (fmodf), 3 orbit by colour (floorf, cosf, sinf), 4–6 special slot orbits, 7/8 pad-controlled (`0x00231420` + limits `0x002316D0`/`0x00231728`); either jump or `sceVu0InterVector` blend | D |
| 0x00231420 | *UpdateFreeCamera* | analog sticks → cubic speed (VU0: x³ × k1 × k2), `SetCameraLookAt`, distance limits (`0x00104F50`), rotation about Y, blend | D |
| 0x002316D0 / 0x00231728 | *ClampCameraBox* | box limits (`0x001051F0` twice), floor heights 740/400 | D |
| 0x002313B8 | *BuildModelMatrices* | `0x001096A8`, `0x00108C68`, `sceVu0TransMatrix` | D |
| 0x002312E0 | *BuildShadowMatrix* | `sceVu0ScaleVector`, direction angles, RotZ/RotX | D |
| 0x00231238 / 0x002311B8 | *SetLightSet* | light set (`0x00136F48` ambient, `0x00136DA0` × 3 directions; one variant transforms the directions) | D |
| 0x00231100 | *SetLightDirection* | light n colour and direction (−Z rotated by X then Y) | D |
| 0x002310C8 | *InitLights* | ambient colour, 3 directions cleared | D |
| 0x00231000 | *CreateMenuObject* | container reader on a model file: items 0 and 1 (or n), `0x0014AE60` / `0x0014AEF8` / `0x0014B078` (menu 3D object API) | D |

## 2. Aircraft viewer (0x0022DF70–0x002301F0)

Viewer object (0x9D0 bytes, `*0x00410EDC`): +0x10 data load queue, +0x220 DATA 0xCD (font), +0x224/+0x228/+0x22C hangar models, +0x234 aircraft model (DATA 0xCD + aircraft), +0x238.., +0x23C.. special weapon models (DATA 0xE4 + id, 8 bytes each), +0x260 fade (+0x26C level), +0x270 camera, +0x430 look-at, +0x460 light set, +0x4B0/+0x580 menu objects (hangar), +0x650/+0x750/+0x850 light sets, +0x950 aircraft record, +0x954 special weapon records, +0x958 help line message box, +0x97C leave state, +0x980 load state (0 queue, 1 loading, 2 shown, 3 change pending, 4 reload, 5 loading, 6 building), +0x984 aircraft, +0x988 colour, +0x98C/+0x990 special list/count, +0x994, +0x998 build step, +0x99C.., +0x9A4 fog/alpha value, +0x9AC/+0x9AE highlight masks, +0x9B0 pending colour, +0x9B4, +0x9B8 mode, +0x9BC/+0x9C0.

| Address | Working name | Role | St |
|---|---|---|---|
| 0x002300F8 | CreateAircraftViewer | clear colour (16,16,16,0x80), menu draw mode, object (0x9D0), init (`0x0022DFA8`), state 0 | D |
| 0x0022DFA8 | *InitAircraftViewer* | fades, 2D camera with a 640 × 480 (or 16:9) projection (distance 920, +0x16C 50, +0x170 16384), look-at defaults (`0x002300A8`), aircraft/colour, special list, light set (ambient 0x101210, two lights 0x707070/0x282828) | D |
| 0x002300A8 | *ResetViewerLookAt* | default eye/target | D |
| 0x0022DF70 | *SetViewerSpecialList* | special weapon list of the aircraft | D |
| 0x0022FF78 | UpdateAircraftViewer | fade, load state machine (`0x0022E2B0`), fade-in when the menu fade is done, camera (`0x0022F750`) | D |
| 0x0022E2B0 | *UpdateViewerLoad* | states 0–6: font, models (`0x0022E1F0`, `0x0022E218`), hangar objects (`0x0022E450`), aircraft record (`0x0022E580`), colour variants (`0x0022E660` × 3), specials (`0x0022E800`) | D |
| 0x0022E1A8 / 0x0022E1F0 / 0x0022E218 | *QueueViewer…* | DATA 0xCD (5 items), 0xCD + aircraft (2), 0xE4 + special id per special | D |
| 0x0022E450 | *CreateHangarObjects* | two hangar objects (items of +0x224/+0x228), three light sets, `0x00137068` | D |
| 0x0022E580 | *CreateAircraftRecord* | record (0x30) + object block (0x5B0); shadow object | D |
| 0x0022E660 | *CreateAircraftColour* | per colour: model matrices, two objects (normal and mirrored by −Y: the floor reflection), part flags (`0x0022E978`), `0x0022EBD0` | D |
| 0x0022E978 | *SetAircraftParts* | switch on the mode (jump table 0x00407070, 8 cases): part values (`0x0014B0A0` part types 0x1F/0x13) | D |
| 0x0022EBD0 | *FinishAircraftObjects* | `0x0014B168` on the three objects | D |
| 0x0022E800 | *CreateSpecialRecords* | per special weapon: record, three objects (positions 0x0039D870) | D |
| 0x0022F750 | *UpdateViewerCamera3D* | `SetCameraLookAt`, `UpdateCameraMatrices`, scale ×10 (`0x00105178`) into the screen/clip matrices | D |
| 0x00230048 | DrawAircraftViewer | state 2: everything (0xF); 3–6: hangar only (3) | D |
| 0x00230080 | SetAircraftViewerAircraft | in state 2/3: new aircraft and colour, state 4 (reload) | D |
| 0x0022FDA0 | *DrawViewerScene* | `0x0022FCD0`, five render-state packets (`0x0022EC28`: FRAME/ZBUF/TEST/ALPHA by pass), hangar (`0x0022ED70`, `0x0022EE30`, lights `0x0022F810`), aircraft passes (`0x0022F048` shadow, `0x0022F130` reflection, `0x0022EEF0`), specials (`0x0022F3D8`, `0x0022F5B0`, `0x0022F200`), black fade | D |
| 0x0022EC28 | *WriteViewerPassState* | GIF A+D: PRMODECONT, TEXA?, FBA, TEST (by pass), ZBUF, CLAMP/ALPHA | D |
| 0x0022ED70 / 0x0022EE30 | *DrawHangarObject* | hangar object pass: VU program upload (`0x0014B280`), light set, view matrices (`0x0014B3F8`), texture/context (`0x0014B2A8`), model (`0x0014B5A0`) | D |
| 0x0022EEF0 / 0x0022F048 / 0x0022F130 | *DrawAircraftPass* | aircraft: with its light set transformed into the model (`0x002312E0`, `0x00231238`), mirrored floor reflection (light set +0x750), shadow/floor object | D |
| 0x0022F200 / 0x0022F3D8 / 0x0022F5B0 | *DrawSpecialPass* | special weapon objects (mask bit per weapon), same three passes | D |
| 0x0022F810 | *DrawCeilingLights* | 5 rows × 8/9 ceiling light glows: VU0 projection (`vmula/vmadda` with the camera, Q divide, ftoi4), MMI packing/visibility, size by distance, colour `LerpColor`, sprites via `0x00112A10` (needs disassembly check: MMI + VU0) | D |
| 0x0022FCD0 | *ClearViewerBackground* | z clear sprite (`BuildSpriteStrips`) | D |

## 3. Menu 3D object API and model library

Menu object (0xD0 bytes; written by `0x00231000`): +0 radius, +4/+8, +0x10 world→view part matrix, +0x50 world matrix, +0x90 model instance, +0x94 second instance (glow model, mode 2), +0x98 VU1 program block (`.vutext` packet: 0x00298AC0 mode 0, 0x00297C00 mode 1, 0x00299820 mode 2), +0x9C mode, +0xA0 texture (GIM), +0xA4/+0xA8 effect list (always empty in S4: the only creator passes 0), +0xAC alpha, +0xB0 flags, +0xB4..+0xC0 frame/z formats.

| Address | Working name | Role | St |
|---|---|---|---|
| 0x0014AE60 | *InitMenuObject* | defaults, VU program by mode | D |
| 0x0014AEF8 | *BindMenuObject* | model data → `BindModelData` (0x28) + `CreateModelInstance` (0x4C); texture (`InitGimTexture`); glow model; effect list | D |
| 0x0014B078 | *SetMenuObjectTexturePos* | texture TBP/CBP words | D |
| 0x0014B0A0 | *SetMenuObjectPart* | part value on both instances (`0x00140F68`) | D |
| 0x0014B168 | *UpdateMenuObject* | `UpdateModelHierarchy` from identity on both instances | D |
| 0x0014B1D0 / 0x0014B238 | *SetMenuObjectPose* / *SetMenuObjectMatrices* | rotation (`0x00108C68`) + translation; or copy two matrices | D |
| 0x0014B280 | *UploadMenuObjectProgram* | DMA `ref` to the VU1 program packet (`0x001042D0`) | D |
| 0x0014B2A8 | *WriteMenuObjectTexture* | texture upload (`BuildTextureUpload`) and TEX0/TEX1 via VIF UNPACK to VU address 0x1C | D |
| 0x0014B3F8 | *WriteMenuObjectCamera* | VIF UNPACK to VU 0x1E (1 qword) and 0x00 (8 qwords: world→screen and world→clip of the camera) | D |
| 0x0014B5A0 | *DrawMenuObject* | GIF A+D (TEST/ALPHA), `EmitModelInstance` (mode 2: three passes with alpha +0xAC), effect list (`DrawSpecialObject`; empty in S4), glow pass `0x00142460` for mode 2 | D |
| 0x00144618 | BindModelData | model header: +0x10 node table, +0x14 root node; node count, max depth, part index table | D |
| 0x00144238 / 0x00144340 | *CountModelNodes* / *BindModelNodes* | recursive node walk (format version at data+4: ≥ 0xB4 uses relative child lists; ≥ 0xBF part indices) | D |
| 0x0013F018 | CreateModelInstance | per-node matrices (+0x10 local, +0x14 world), vectors, flags, part values (0.5), morph buffers (node flag 4), mesh VIF list counts (tag 0x60000000 terminated) | D |
| 0x0013FE58 | UpdateModelHierarchy | recursive: toggles visited flag, local transform from node T/R (+0x10/+0) with part keyframes (`0x0013F4D0`), rotation matrix (`0x001089B0`), multiply with parent (`0x00108F00`), morph (`0x0013F6C0`), node origins | D |
| 0x0013F4D0 | *InterpolatePartPose* | part value × 100 → keyframe pair, VU0 lerp of translation/rotation | D |
| 0x0013F6C0 | *BuildMorphMesh* | morph targets: lerp of positions (s16), colours (u8), normals (f32), UVs (s16) between two keyframes into the instance's VIF buffer | D |
| 0x00140F68 | *SetModelPartValue* | part value change marks the nodes dirty (flag 4) | D |
| 0x001404A8 | EmitModelInstance | per visible node: VIF UNPACK of 13 qwords at VU 0 (world→screen × node, world→clip × node, light matrix), GIF tags at VU 0x15/0x16/0x19/0x1F (PRIM by flags: Gouraud, texture, fog, alpha), DMA `call` to the node's mesh packet | D |
| 0x00142460 | *DrawGlowPass* | VU1 program D (0x0029A940): half-resolution copy of the frame (`0x00141EE0`), model glow geometry twice, blur/add passes with `BuildScaledCameraMatrices` | D |
| 0x00141EE0 | *WriteHalfResCopy* | GS sprites copying the frame buffer to a half-size buffer | D |
| 0x001420F0 | BuildScaledCameraMatrices | camera matrices with the viewport scaled by k (0.5 for the glow pass) | D |
| 0x00136AA0 / 0x00136DA0 / 0x00136F48 / 0x00137068 | *InitLightSet* / *SetLight* / *SetAmbient* / *SetLightTexture* | light set (directions as matrix rows at +0x50, colours at +0x90, ambient +0xD0, VIF header +0xE0/+0xF0); colours /128 | D |
| 0x001370D8 | WriteLightSetPacket | VIF UNPACK 9 qwords at VU 0x0C (light matrix, colours, ambient); optional environment texture upload and A+D state | D |
| 0x001041E8 / 0x001042D0 | *AllocBucketPacket* / *AddBucketRef* | DMA packet in a bucket (`cnt` + data) / `ref` tag to external data (VU programs) | D |
| 0x001066D8 | *WriteTex0Tex1* | GIF A+D TEX0/TEX1 from a GIM texture record | D |
| 0x00112A10 | *WriteSpriteStrip* | sprite strips with UV interpolation, clipped to the scissor | D |
| 0x00104F50 | *GetDistanceXZ* | √(dx² + dz²) | D |
| 0x00105178 | *BuildScaleMatrix* | diagonal matrix (MMI pextlw/pcpyld) | D |
| 0x001051A8 | *EvalHermite* | (VU0) cubic of 1 − t | D |
| 0x001051F0 | *ClampVector* | VU0 vmax/vmini | D |
| 0x001089B0 / 0x00108C68 | *BuildRotationMatrix* / *RotateMatrix* | VU0 sin/cos polynomials (constants at 0x002A15E0..0x002A1610) for rotations X, Y, Z; Ghidra drops the broadcast fields: translate from the disassembly | D |
| 0x00108F00 | *MulMatrixVu0* | 4×4 multiply (vmula/vmadda order) | D |
| 0x001096A8 | *SetUnitMatrixVu0* | identity | D |

### In the static closure but not run in S4
The slice tool assigns these to S4 through `DrawMenuObject` → `DrawSpecialObject`, but the menu object's effect list is always empty in S4 (`0x00231000` passes 0 to `0x0014AEF8`). They draw mission effects and are deferred to S5f (effect drawing): `DrawSpecialObject` 0x001E0A98 and the 20 drawers it dispatches (0x001A41F8, 0x001A5DA8, 0x001A7AE8, 0x001A9940, 0x001AB5D8, 0x001AD510, 0x001AFDF8, 0x001B1BD8, 0x001B3C58, 0x001B5AC8, 0x001B9C90, 0x001E0B60, 0x001E1270, 0x001E1838, 0x001E2238, 0x001E24C8, 0x00211278, and the constant getters 0x001E0A40/58/78), with their helpers `0x00108FC0` (VU0 triangle clipper using VU0 data memory through `vlqi`/`vsqi`), `0x00109500` (VU0 vertex projection), `0x0013E338`/`0x0013D590` (terrain height lookup), `0x001456F8` (direction angles).

### libc and SDK used by S4 (src/sce)
- `fmodf`, `floorf` (exact on the host), `sinf`, `cosf`, `atan2f` (newlib: **not** bit-exact on the host; translate the newlib routines before the hangar camera depends on them; `risks.md`).
- libvu0: `sceVu0ScaleVector`, `CopyVector`, `CopyMatrix`, `InterVector` (have), `AddVector`, `SubVector`, `OuterProduct`, `InnerProduct`, `Normalize` (effects only).

## 4. Mission select (scene 4 subs 0x42–0x49; free mission, VS, records)

Page object (0x770 bytes, `*0x0041182C`): +0x10 data load queue, +0x220 DATA 0x110 (font block, +0x224 image), +0x240 fade (+0x24C level), +0x250 panel tab, +0x290 animated frame {x, y, w, h, sx, sy}, +0x2A0 panel (5 rows, 0x003AF070), +0x2F8/+0x2FA crosshair target, +0x300 crosshair, +0x304 menu tree (4 columns 0x003AEFD0, connections 0x003AF030, parents 0x00411190), +0x70C help line, +0x730 mode (0 campaign free mission, 1 extra, 2 VS, 3 VS team), +0x734 state (0–5 mission list: 1 browsing, 2 confirm, 3/4 leaving; 6–10 settings menu), +0x738/+0x73A MenuGoTo target, +0x73C mission index, +0x740 shown index, +0x744 available count, +0x748 preview level, +0x74C row count, +0x74E row x, +0x750 thumbnail table, +0x754 frame animation step, +0x758 frame time, +0x75C VS flag, +0x760/+0x764 text times.

| Address | Working name | Role | St |
|---|---|---|---|
| 0x0023C600 | *EnterMissionSelect* | sub 0x42..0x47 entry: menu draw mode, page (`CreateMissionSelectPage`), DATA 0x110, BGM 4, sub 0x48 | D |
| 0x0023C6A8 | *LoadMissionSelect* | sub 0x48: font block, help line, sub 0x49 | D |
| 0x0023C708 | *MissionSelectRun* | sub 0x49: fade, help line; states < 6: list (`0x0023A8B8`) and its draw (`0x0023BD78`); else settings menu (`MissionSelectUpdate`, `0x0023C4C0`) | D |
| 0x0023A118 | CreateMissionSelectPage | by sub: mode 0–3, list (`0x00239E58`) or settings (`0x00239F58`) | D |
| 0x00239E58 | *InitMissionList* | fade, mode, counts (`0x00239D50`), row layout, frame, panel (`InitPanel`), crosshair | D |
| 0x00239F58 | *InitMissionSettings* | settings menu tree with hidden items by mode; VS team: weather/time strings into 0x0042D1E0, game[0x2E] | D |
| 0x00239D50 | *GetMissionCounts* | rows per mode (0x00410E80); extra missions from the unlock bits 0x002A031C; VS needs bit 0 of 0x002A0318; last index 0x00411198 | D |
| 0x00239E10 | *SelectMissionRow* | index, crosshair to the row | D |
| 0x00239C58 | *UpdateMissionSelectHelp* | help message by state (0x003AEEB8 per mode, 0x003AEEC8 per settings item, 0x2C VS) | D |
| 0x0023A208 / 0x0023A380 | *LerpFrame* / *AnimateMissionFrame* | frame between key rectangles (0x003AF190.., times 0x003AF1D0), eased by `0x001051A8` | D |
| 0x0023A360 | *ResetMissionFrame* | first key rectangle | D |
| 0x0023A550 | *AcceptMission* | VS: confirm then game[0x25] = 1, game[0x29] = 2, mission id (`GetMissionId(2, n)`), slot 0 aircraft 2; else go to the settings menu | D |
| 0x0023A668 | *MissionListInput* | X/Circle accept; left/right through the available rows (SE 4) | D |
| 0x0023A770 | *UpdateMissionPreview* | preview fade after the cursor rests (SE 5 at start) | D |
| 0x0023A8B8 | *UpdateMissionList* | frame animation (SE 0xE), panel, preview; switch on state (jump table 0x00409150): 0 wait fade, 1 browse (Triangle → state 5), 2 confirm, 3 → settings, 4 → MenuGoTo, 5 → back | D |
| 0x0023AB48 / 0x0023ABA0 | *LeaveMissionSelect* / *LeaveMissionSelectMenu* | state 8/9 with target | D |
| 0x0023AC00 | *MissionSettingsCancel* | close column or back to the list | D |
| 0x0023AC70 | MissionSelectAction | settings item 0–18: start (game[0x25]/[0x29], mission id by kind), hangar pages (0x1C/0x1D...), records, save/load sub-pages (0x4D/0x4E/0x54/0x55; HDD columns with ctx flag 0x80000), VS weather 0x10–0x12 | D |
| 0x0023AF78 | MissionSelectUpdate | settings states 6–10 | D |
| 0x0023B108 | DrawMissionRecords | mission name, difficulty/rank (records 0x002A045C + difficulty × 0x168 + mission × 0x14, extras 0x002A0CCC), best times (h'm"s) and points, row tabs | D |
| 0x0023B930 | *DrawMissionThumbnail* | thumbnail sprite from the page image (sinf wobble) | D |
| 0x0023BBA0 | *DrawMissionFrame* | frame sprite from the animated rectangle | D |
| 0x0023BD78 | *DrawMissionList* | buckets, frame, fades, panel, thumbnail, records, help line | D |
| 0x0023BE88 | *DrawMissionSettingsTexts* | per player: aircraft code, special weapon; VS team texts; tick SE 9 | D |
| 0x0023C230 | *DrawTitleBar* | bar sliding in with a sinf pulse (`LerpColor`) | D |
| 0x0023C4C0 | *DrawMissionSettings* | menu tree, texts, title bar, help line, fades | D |

## 5. VU programs, formats and libc (analysed 2026-10-15)
- **VU1 programs B (0x00298AC0), C (0x00299820), D (0x0029A930)**: disassembled (`tools/research/vu_disasm.py`) and documented in `../subsystems/vu-programs.md` (inputs, clipping, lighting, reflection pass, backface/ADC, glow fins). Program A (0x00297C00) is used by menu mode 1, which S4 never selects; it stays with S5b.
- **Model data format**: `../formats/SM-model.md` (header, nodes, keyframes, morph table, mesh DMA list and its UNPACK layout); research dump `tools/research/model_dump.py`.
- **newlib float maths** (bit-exact translation into `src/sce/libc.cpp` with the EE float helpers; currently host libm): `sinf` 0x00284830, `cosf` 0x00284660 → `__ieee754_rem_pio2f` 0x00286DE0 (medium range: 3-part π/2 constants; large: `__kernel_rem_pio2f` 0x00287450 with `scalbnf` 0x002886B8, `floorf`, tables 0x0040C960/0x0040C970 and the 2/π digits from 0x0040C578), `__kernel_sinf` 0x00287DA0, `__kernel_cosf` 0x002872F8; `atan2f` 0x00284CB8 (wrapper: `_LIB_VERSION` 0x0040CA0C, `isnanf` 0x00288690, matherr path) → `__ieee754_atan2f` 0x00286018 → `atanf` 0x002883A0 (tables 0x0040CA20 atanhi, 0x0040CA30 atanlo, 0x0040CA40 aT[11]); `fabsf` 0x00288648; `fmodf`, `floorf` (exact on the host).

## 6. Disassembly checks to do while translating
Where Ghidra drops VU0 broadcast fields or MMI: `0x001089B0`, `0x00108C68`, `0x0022F810`, `0x00231420`, `0x0013F4D0`, `0x001051A8`, `0x00105178`; jump tables `0x0022E978` (0x00407070, 8 cases) and `0x0023A8B8` (0x00409150, 6 cases).

## 7. Port pieces S4 needs (beyond game code)
- **VIF1 + VU1 model**: VU1 data memory (16 KiB), VIF1 UNPACK (formats used: V4-32, V4-16, V4-8, with STCYCL/FLG/USN), BASE/OFFSET/TOPS double buffering, ITOP, MPG (program identity by source address), MSCAL/MSCNT → CPU HLE of programs B, C, D producing GIF packets (PATH1), in `src/sce/libgraph.cpp` (DMA intake) and `src/platform/render/` (ADR-003).
- **GS features**: local→local transfers and texture-from-frame-buffer at half resolution (glow pass), alpha/blend modes of the viewer passes, the ADC bit, fog.
