# Dead, unreachable and debug code

Catalogue of code that the shipped game never runs. Per the project principles (CLAUDE.md, Goal), **all game code in it is kept and translated** (SDK/library code: section 7). In the default build it stays unreachable exactly as on the PS2; it may only be exposed through the optional debug/modding layer.

Keep this file updated whenever more is found. Status words:
- **unreferenced**: no `jal`/`j`, no data pointer, no `lui/addiu` address build anywhere;
- **unreachable**: referenced, but no path from `main` reaches it in normal play;
- **debug**: development-only behaviour (host file I/O, profiler, printf);
- **runtime-unreachable**: complete code behind flags that nothing sets (the HDD layer).

## Method

- Ghidra: functions with zero references (`find_functions max_xrefs=0`): 387 in total, about 120 in game code (0x001000D8–0x002616E7).
- Offline filter: a function counts as referenced if any `.data` word, `jal`/`j` or `lui/addiu` pair points at it. 49 of the 120 are reached through data tables Ghidra had not typed, so they are **not** dead. **63 game functions have no reference of any kind.**
- Caveat: a function reached only through a computed address (base + offset arithmetic) would be counted as dead. None is known, but the list is "unreferenced (static)", not proven unreachable.
- **Full reachability pass** (`tools/research/reach.py`, `../../tools/research/rawcalls.py`, `../../tools/research/fdeaudit.py`, `../../tools/research/gapscan.py`):
  - **graph**: every `jal`/`j` site in `.text`, attributed to the nearest preceding function entry (so switch-case code outside a Ghidra body still counts), plus `lui`+`addiu`/`ori` function-address builds;
  - **roots**: the entry point, `main`, and every data pointer to a function, except the `pc_begin` fields of `.eh_frame` FDEs;
  - **FDEs**: the executable carries C++ unwind tables (FDE = length, CIE pointer, function start, size; 867 found). They give exact function boundaries. All 380 FDE functions Ghidra knew match exactly. **39 game functions had an FDE but no Ghidra function**; they were created (`../rename-log.csv`) and none of them has a caller;
  - scene-table rule (validation 2026-10-11, `validation.md` V-2): the scene table 0x0029FDA0 is a data root, so scenes 1 and 3 count as reachable in the raw graph, although no code ever requests them. Their handlers, sub-states and helpers used only by them are marked dead: scene 1 (0x00211C58) and scene 3 (0x00211CA8 + 4 sub-states, `SetRequestFlags` 0x00100AF8). `tools/research/validate.py` recomputes this split;
  - result: **272 dead game functions** of 2,884 (coverage status `dead` in `coverage-functions.csv`, which is the full list). Functions reached only through tables or switch code are live: for example the replay camera styles (table 0x002B77B0), the sequence Euler-order matrices (called from a switch) and the options page handlers.

## 1. Scenes and scene helpers

| Address | Name (H) | Status | What it does |
|---|---|---|---|
| 0x00211C58 | `SceneMenuEntry`, scene 1 | unreachable (only in the scene table) | sets mission id = `GetMissionId(0, 1)`, `EnterMenu(4)`. Probably a debug shortcut into the mission-1 menu flow |
| 0x00211CA8 (+ sub-states 0x00211D48, 0x00211E68, 0x00211FB0, 0x002120D8) | `SceneMovie`, scene 3 | unreachable | stand-alone full-screen SMV player; returns via `SetRequestFlags(0x40)` (0x00100AF8, used only here) to `ctx->ret_scene`. Story movies use the in-menu player instead (scene 4 subs 7–13) |
| 0x00100A98 | (request helper) | unreferenced | `ctx->req = a2; next_scene = a0; next_sub = a1` |
| 0x00100AE0 | (request helper) | unreferenced | `ctx->req = 7; next_scene = a0; next_sub = a1` |
| 0x00100B08 | `SetReturnScene` | unreferenced | `ctx->ret_scene = a0; ctx->ret_sub = a1`. **The only writer of `ret_scene`**, which scene 3 needs to return |

Evidence: `../game/scenes.md`, "Who requests scenes 1 and 3".

## 2. Debug / development code

| Address | Status | What it does |
|---|---|---|
| 0x00108458 / 0x001084F8 / 0x001085A8 / 0x00108678 | unreferenced | `C_PCCON` performance-counter profiler: `scePcStart`/`scePcStop` + `printf` of the results (`C_PCCON` strings) |
| 0x00106810 | unreferenced | **writes a GIM image to the host PC**: `sceOpen("host0:%s")`, header `GIM`, w/h, then w·h·4 bytes (32-bit framebuffer/texture dump; devkit only) |
| 0x00225308 | unreferenced | **loads a file from `host0:`** (also checks a `sim` prefix with `strncmp`); `printf` on error. Devkit file loader |
| 0x002254F8, 0x00222050, 0x00222100 | unreferenced | `printf`-based debug output (menu/3D model area) |
| 0x00100CB8 | unreferenced | uploads an image to GS memory (`sceGsSetDefLoadImage` + `sceGsExecLoadImage`) (H: debug texture upload) |
| `SetupMapFromMissionData` (H) 0x00139B70 warning path | reachable only with old data | `printf("WARNING : MTXT version old! …")` when a mission's MTXT header is below version 0x6F |
| 0x002386D8 | reachable | `printf("CH0:%08x  CH2:%08x")` when stream slots 0/2 are not ready before an in-menu movie |
| `func_00250C30` | reachable | ends with `printf(" %x %x %x %x\n", …)` (debug print left in shipping code; output goes to the devkit TTY, invisible on retail) |

Shipped code calls `printf` in several more places; on retail hardware the output goes nowhere. The port must keep these calls but route them to a debug log that is off by default.

## 3. Unreferenced game functions (static list, 63)

Grouped by area; names are not assigned yet (`func_XXXXXXXX`).

| Area | Addresses | Notes |
|---|---|---|
| core / math helpers | 0x00100DF0, 0x00102460, 0x00102798 (`atan2f`, `sqrtf`), 0x00103150, 0x001034A0, 0x00103660, 0x00103AF0, 0x00103B50, 0x00103BA8 (`acosf`), 0x00109598 (matrix build) | unused utility functions of the core library |
| display/DMA | 0x00103CC8 (calls `GetSprWorkBuffer` 0x00103D80 and `FlushSprWorkBuffer` 0x00103D98, the scratchpad → GIF ring path), 0x001368C0 (`sceDmaSend`), 0x00110070 | unused packet/DMA variants; the SPR → GIF ring is therefore unused (`../subsystems/rendering.md`) |
| debug | 0x00106810, 0x00108458, 0x001084F8, 0x001085A8, 0x00108678, 0x00100CB8 | see section 2 |
| flight/render | 0x00108F90, 0x00137778, 0x00137868, 0x001400A0, 0x00140998, 0x001410D0, 0x0014B110, 0x0016EA90, 0x001796E0, 0x0017C130, 0x0017C180 | unused renderer/physics variants (H) |
| 3D effects (0x001D8–0x0020) | 0x001D8910, 0x001D8A98, 0x001D8E88, 0x001D9278, 0x001D95A8, 0x001D98D8 (leaf functions 650–1,000 bytes), 0x001E95E8, 0x001F5280 (5.4 KB), 0x001FAD90 (6.4 KB), 0x002068E0, 0x00207268 (5 KB) | large unused drawing routines (H: cut or replaced effects) |
| menus/other | 0x00210868, 0x00220BA0, 0x00221978, 0x002219D8, 0x00221A40, 0x00221BE8, 0x00222720, 0x00222750, 0x002227A8, 0x00222950, 0x00223D08, 0x00223D60, 0x00226F48 (`powf`), 0x0022AED0, 0x0024D7A8, 0x0024DA30, 0x0024F360 (twin of 0x0024DA30), 0x00252070 | unused menu/text/model helpers |
| debug I/O | 0x00225308, 0x002254F8, 0x00222050, 0x00222100 | see section 2 |

### 3b. Found by the full reachability pass

| Area | Addresses | Notes |
|---|---|---|
| sound API never called | 0x00215978, 0x002159C0, 0x00217748, 0x00217788, 0x002177A0, 0x00217B28, 0x00217BC0, 0x00218118, 0x00218768, 0x00218BF8, 0x00218C80, 0x00218C98, 0x00218D68, 0x00218EB0, 0x00218EE0, 0x00219640, 0x00219658, 0x00219758, 0x002197C0, 0x0021A080 (second sound LFSR), 0x0021A0A8, 0x0021A5C0, 0x0021A6E8, 0x0021AD88, 0x0021ADC8, 0x0021CEA0, 0x0021D048, 0x0021D208, 0x0021D4B8, 0x0021DA68, 0x0021DC88, 0x0021DD18, 0x0021DDA8, 0x0021DED8, 0x0021E0A0, 0x0021E120, 0x0021E150, 0x0021E188, 0x0021E1C8, 0x0021E200, 0x0021EF10, 0x0021EF58, 0x0021EF78, 0x0021EF98, 0x0021EFB8, 0x0021F108, 0x0021F160, 0x0021F238, 0x0021F278, 0x0021F928, 0x0021F948, 0x0021FB28, 0x0021FB40 | unused parts of the sound library (`../subsystems/sound.md`): play/volume variants, voice/bank/stream accessors |
| CD / host file helpers | 0x00214460, 0x002144A8, 0x002144B8, 0x002144E0, 0x002146B8, 0x00214840, 0x00214D48, 0x00214D90, 0x00214E18 ("BAD ALIGNMENT 64"), 0x00214F00, 0x00215320 (`sceSifLoadIopHeap` loader), 0x002153F8 | unused file-layer helpers (`../game/file-loading.md`); only unwind-table entries point at them |
| sequence character engine | 0x002222C0, 0x002224E8, 0x00223E20, 0x00223E70, 0x00223F58 (vertex morph), 0x00225648, 0x00225970, 0x00225A88, 0x00225C38 (MMI matrix), 0x002260D0, 0x00226130 | called only from the already-dead 0x00222050, 0x00222100, 0x00223D08, 0x00220BA0, 0x002219D8 (`../subsystems/rendering.md`) |
| core / render | 0x00103330, 0x00103420, 0x00103A60 (called only by dead 0x00109598, 0x001034A0/0x00103660, 0x00103AF0/0x00103BA8), 0x00137608 (noise, called only by dead 0x00137778/0x00137868) | |
| text | 0x00251FF8 (string pixel width) | called only by dead 0x0024DA30 / 0x0024F360 / 0x00252070 |
| save | 0x0025EB58, 0x0025F170, 0x0025F820 (unformat operation, found by FDE) | see section 5 |
| HDD module | 0x00259810…0x0025C300 (23 functions, see coverage list) | see section 5 |

## 4. Code outside Ghidra functions (gaps)

Scan of every stretch longer than 16 bytes between Ghidra function bodies in game `.text` (`tools/research/gapscan.py`, 125 gaps):
- **Switch case code** (not dead): 0x0014FC24, 0x001541E8 (`../subsystems/weapons.md`), 0x00222874, 0x0022E9D4, 0x0023A9A0, 0x0025DC88, 0x0025E010, 0x0017D938–0x0017DBD7. Each is the target of a jump table in `.rodata`.
- **Undefined functions with an FDE**: 39 in game code, now created. None has a caller (section 3b). Two more were missing: 0x001759E8 (replay camera style 12, reached through table 0x002B77B0, **live**) and 0x002124F8 (static constructor, **live**, `.ctors` list 0x0029F308).
- **C++ library template instances**: 32 of the 208/212-byte gaps (33 counting 0x001DAA48, found after the first pass) are the same pair of functions, g++ `lexicographical_compare` / `__lexicographical_compare_3way` for `char` ranges. g++ 2.9x emits them into every object file that includes the STL header. **Nothing references any copy.** They are C++ library code, not game code. Under the owner decision of section 7 they are not translated, and the port's C++ runtime provides its own. Addresses (start of each pair): 0x00109F60, 0x0010AE40, 0x0010C770, 0x0011B770, 0x0011E7A8, 0x00127AD8, 0x0012A3B8, 0x0012FB38, 0x00135358, 0x00135B80, 0x0013EB38, 0x001469E0, 0x00149EE8, 0x00154488, 0x00155AF8, 0x0016EBF8, 0x00170E98, 0x00179808, 0x00184758, 0x00184D20, 0x00186AA8, 0x0019F448, 0x001A4028, 0x001BDA08, 0x001D7ED0, 0x001E0538, 0x001E42B0, 0x001E91F0, 0x001FED58, 0x002105A0, 0x002111A8, 0x00211AB8, 0x001DAA48.
- **Functions that Ghidra never defined (88, now created).** Each starts right after a return in the preceding code, and none is reached by a branch, jump, call or data pointer, except:
  - **8 live callbacks**: 0x0017D938, 0x0017D958, 0x0017D9A8, 0x0017DAA8, 0x0017DAD8, 0x0017DAF0, 0x0017DB28, 0x0017DBC0. They are referenced from the in-flight pause-menu and rearm-menu item table at 0x002B8130 (CAPTIONS toggle, ctx flag 0x20000 HUD toggle, retry/quit actions, fades).
  - **80 dead functions**: 0x00100A98, 0x00100AE0, 0x00100B08, 0x00100FB8, 0x00102578, 0x00102870, 0x00102880, 0x00103AA8, 0x00103C50, 0x00104418, 0x00105070, 0x00105210, 0x00105278, 0x001052A8, 0x00106468, 0x00107238, 0x00107668, 0x001083A0, 0x00108400, 0x001086D8, 0x00108718, 0x00108F78, 0x0010D590, 0x00113010, 0x00136968, 0x00138C40, 0x0013B068, 0x001422E8, 0x001446F0, 0x00144710, 0x00144D68, 0x001467C8, 0x00149D08, 0x00149D20, 0x00149E10, 0x0014B148, 0x0014B568, 0x0014B570, 0x0017B690, 0x001DA7E0, 0x001DA998, 0x001E94E0, 0x001FF668, 0x001FF680, 0x001FF6A0, 0x00210F60, 0x00210F78, 0x00210F98, 0x00211C88, 0x002124B8, 0x002124E8, 0x002208E0, 0x002208E8, 0x00220918, 0x00220968, 0x00220AF0, 0x00220B80, 0x002213B0, 0x002218C0, 0x00221BB0, 0x002228E0, 0x002229F0, 0x00223680, 0x00223D50, 0x00225750, 0x00225908, 0x00226348, 0x0022A4D0, 0x0022D030, 0x0022DF38, 0x0022F7B8, 0x002301C8, 0x00234BC8, 0x0023ECD8, 0x0024D828, 0x0024D8E0, 0x00250820, 0x00255690, 0x002557D0, 0x00260058. They include unused DMA helpers with direct hardware access (fromSPR 0x00105210, 0x00105278; toSPR 0x001052A8, 0x00144D68; GIF ring start 0x00103C50), unused C_PCCON profiler parts (0x00108400, 0x001086D8), an unused VU0 micro call (0x00136968), the request/return-scene helpers of section 1, and copies of effect constants.
  - Translated in S4 (unreachable as on the PS2): 0x0022F7B8 `ClampViewerLookAt`, 0x002301C8 `SetAircraftViewerSelection`, 0x00234BC8 `CountOwnedFields` (`src/game/aircraft_viewer.cpp`, `src/game/hangar.cpp`).
- 8-byte `jr ra; nop` stubs that remain outside functions: 0x00103D78, 0x0010A4A0 (shared return, `j` target inside 0x0010A5D0), 0x0010C848, 0x00144720, 0x0014B278, 0x0016DA70, 0x001D8A90, 0x001E7CF0 (shared return of 0x001E46E0), 0x001ED3F8, 0x00214128, 0x00220300, 0x00220B38, 0x00220C30, 0x00223D58, 0x002252A8, plus 0x0024ED78 (a lone `j`). They are empty functions or shared epilogues. Nothing calls them except the two shared returns.
- C++ runtime above the game range: **446 FDE functions in 0x00277AC8–0x0028FB00 have no Ghidra function** (libstdc++/libgcc exception and iostream support). Not game code, not translated. Creating them in Ghidra is optional research work.

## 5. Runtime-unreachable paths (complete code that never runs)

| What | Addresses | Why unreachable |
|---|---|---|
| **HDD module init** | `func_00259910` → `InitHddModule` (H) 0x00259938 (thread/semaphore setup) → worker thread `func_0025E218` → dispatcher `func_0025BF20` | **no caller anywhere** (full reachability pass). The boot scene clears ctx flag 0x80000 ("HDD present") and writes 3 to HDD context +0x4398; nothing in game code sets 0x80000 or the "HDD usable" byte +0x4399. So the HDD features are **unreachable on any hardware in this build**, not only without an HDD (`../subsystems/save.md`) |
| HDD install / HDD data path | `HddInstallDataCdp` (H) 0x0025A090, HDD read path 0x00215070, `EnterHddErrorScene` (H) 0x002616B8 → scene 7 | HDD module never initialised; the port reads only the ISO |
| save/HDD module threads | `func_0025C090` → job worker 0x0025BF70, power-off handler 0x0025C108 | created only by the uncalled HDD module init (confirmed in PCSX2 session 6, `../pcsx2/sessions-02.md`) |
| scene 7 (HDD read error) | 0x00261680 | only after an HDD read failure (`../subsystems/results-startup.md`) |
| scene 7 message-window widget copy (resources embedded at 0x003F8730, `../subsystems/results-startup.md`) | 0x0024D470–0x0024EC30 | HDD only |
| `func_0025EB58` (`sceMcUnformat` step machine), `func_0025F170` (`sceMcChdir` step machine) | no callers | save layer helpers never used by the game (`../subsystems/save.md`); translated, unreachable |
| HDD install menu page (page 23, scene 4 subs 62–64) | 0x00245B88–0x00246A58 | the main-menu item is shown only with ctx flag 0x80000 (HDD present); installs DATA to the HDD in 10 MiB steps (`../subsystems/frontend.md`) |
| HDD save/load variants of the save page | `SavePageUpdate` modes 2/3 (sub-pages 0x51–0x57) | HDD only |
| BGM player cancel branch in `BgmPlayerInput` (SE 3, fade, `FadeOutMenuBgm(1.0)`) | 0x002445F8 (pad 0x20 branch) | `BgmPlayerInput` runs only in state 1 with panel flag 0x800; `BgmPlayerUpdate` checks cancel first and its `SetPanelState(…, 1)` clears 0x800, so the branch never sees a cancel edge (translated, unreachable) |
| Menu object effect list and glow pass (`DrawMenuObject` 0x0014B5A0: `DrawSpecialObject` 0x001E0A98, direction angles 0x001456F8, glow pass 0x00142460) | in `DrawMenuObject` | not reached in S4: its menu objects have no effect list (`CreateMenuObject` passes 0) and no glow model. Whether any later page reaches them is checked with S5f; until then the calls go through `CallGuest` (logged once if hit) (`s4-inventory.md` §3) |
| `QueueCdRead` chunked path | 0x00214870 with `chunk > 0` | every caller passes `chunk = −1` (and the path has an offset bug) |

## 6. Data that nothing references

| Data | Notes |
|---|---|
| DATA.CDP entries 42–81 (mission ids 0x29–0x50) | not produced by `GetMissionId` or any known code (`../game/scenes.md`) |
| HUD warning ids 1 and 9–12 (table 0x002B7450; texts FRIENDLY / HOLD FIRE, PULL DOWN, TURN RIGHT, TURN LEFT, RADAR CLEAR) | `SelectHudWarning` never selects them; the drawing code `DrawHudWarning` 0x0016D348 would show them (`../subsystems/camera-hud.md`). Cut content for the debug layer |
| `.DVP.overlay.*` sections | non-loaded ELF sections (file offsets only); the loaded microcode is in `.vutext` |
| DATA.CDP entries 291–332 (291: 2 sub-blocks; 292–332: 41 images of 66,832 B) | no load call site produces these indices. Every DATA load goes through `LoadDataEntryAsync` 0x002154E8 (6 callers); all index arguments are constants or bounded expressions with a maximum of 290 (`../formats/CDP.md`). Never loaded |
| BD.CDP entry 2 (1.0 MB sound bank) | `StreamBdEntryToIop` 0x00219C80 has 3 callers, which use BD 0, BD 1 and the per-aircraft table 0x002A1878 (BD 3–12 for aircraft 0–23); no data word points at the loaders except unwind records. Never loaded |

## 7. SDK and library code (for completeness)

- 92 uncalled syscall stubs (`../platform/sdk-functions.md`).
- About 265 zero-reference functions in the SDK/libc/libgcc range (0x002616E8–0x00297B33). These are normal unused library code.
- Gaps with disassembled code outside any Ghidra function in the library range (start, size in bytes): 0x002661F0 (144, libsif), 0x0026B2F4 (220), 0x0026D984 (148, libmc), 0x0026DE08 (88, libmc), 0x0026ED24 (380), 0x00271034 (136), 0x00271FD0 (96), 0x00272FC4 (3348), 0x00275E94 (180), 0x00276E80 (328), 0x00277188 (216), 0x0028C4DC (348), 0x0028E460 (72), 0x0028E570 (92), 0x0028E600 (72), 0x0028E670 (80). Not game code; not created.
- **Owner decision (2026-10-10):** the "keep all original code" principle covers **game code only** (0x001000D8–0x002616E7). Sony SDK and C runtime code is not translated: `src/sce/` (and the host C/C++ runtime) implements what the game calls.
- SDK functions reached **only from dead or runtime-unreachable game code** (e.g. the HDD path: `sceDevctl`, `sceDopen`, `sceDread`, `sceRemove`, `sceCdPowerOff`, the thread/semaphore calls of the HDD module) get **stubs that log the call once and return an error**. The game code that calls them is still translated and stays unreachable by default.
- Uncalled SDK code (the 92 syscall stubs and the ~265 zero-reference library functions above) is not implemented at all.
