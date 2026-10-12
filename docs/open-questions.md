# Open research questions

Questions that are still open. Checks that need PCSX2 have their steps in `pcsx2/procedures.md` (numbers in brackets). Answered questions are at the end with a pointer to the doc that holds the answer.

## Need PCSX2

| Question | Doc | Procedure |
|---|---|---|
| VU0 micro skinning test vectors for part types 0 and 3 (type 1 captured, `pcsx2/sessions-05.md`; feeds STATUS decision 14) | `subsystems/vu-programs.md` | [1] |
| Folder memory card: format against PCSX2 v2.8.2, round trip of a save, file times | `formats/folder-memcard.md` | needs the port's `sceMc*` layer first |

## Static analysis (Ghidra, tools)

- **Formats:** VPK container layout; Namco sound-bank header format (DATA 0x164/0x165) and its pairing with BD.CDP bodies; terrain blocks (MISSION 5–18, 31), unit placement records (22) and message system (34/35); what the shared mission packets 0 and 26 draw; VIF vertex layout inside model node packets; model animation key format and vertex-animation block; AIRCRAFT block 6 fields and which item of block 2 is the cockpit.
- **SMV audio:** the exact sample layout of the 0x4000-byte PCM blocks (H: SPU2 core-0 input, L/R alternating in 0x200-byte blocks; `subsystems/iop-sound.md`). Can be checked offline on the audio blocks of one SMV file.
- **Weapons:** weapon id → in-game weapon name (special-weapon tables in AIRCRAFT data); whether any aircraft carries special-weapon id 15.
- **AI:** identity of the class 8/9 cannons (H: Stonehenge) and of the class 10 scripted objects (type 0x4C objects react to the strike event zones).
- **Rendering:** what each VU1 program computes; whether the first pool table of the flight frame receives packets from outside `BuildFlightDrawLists`.
- **Effects:** names of the effect types marked (H).
- **Sound and radio:** mapping of radio event ids to the probability table 0x00409DA8; VAG.IRX mode 4 vs. mode 8 and `VagMain` ring hand-off; SOUND `delay_timer` / `spu_register_timer`; VAG fno 1 command fields.
- **Flight:** meaning of the constants at 0x0040DF68–0x0040DFBC.
- **Main context:** `unk_310` (0x310–0xA0F) and the pad sub-structs; whether the struct really ends at 0xA20; result record +0x0E (u16 unit count at creation vs. byte "players" in the results code).
- **Memory:** game code that relies on `lq`/`sq` ignoring the low 4 address bits; absolute addresses inside the remaining packet sub-blocks.
- **Executable:** contents of `.data`; whether the 34 unreferenced `bad_alloc` strings mark g++ translation units (module boundaries, `platform/toolchain.md`); a full list of the libgcc double-precision call sites (13–15 per routine; known: flight damping, `func_00137930`, `func_00215D20`, `func_00216D88`, `func_00224848`, `func_001FD398`; `port/coverage.md`).
- **SDK names still hypothesis:** libmpeg `sceMpegSetImageBuff` 0x00270B58 is called as `(mp, bitstream, size)` (AddBs-like) and 0x00270CA8 is polled as "bitstream consumed", so both names need rechecking; libkernl internals `_InitSys`, `scePrintf`, `sceFsInit`, `sceFsReset`, `_lf_bind`, `_SifLoadModule`, `sceSifExitRpc`; libpkt variant (Vif1/Gif/Vif0) of the 5 functions at 0x00262E08–0x00262E98; syscall numbers 0xFD/−0xFE of two uncalled alarm stubs (ps2sdk numbers differ).
- **IRX:** import the `.mdebug` STABS info of the IRX modules (needs `stdump`); re-import the other 7 IRX as R3000 if needed.
- **Ghidra:** rename the `MainContext` and `CdFileEntry` fields to snake_case by hand (the ghidra-mcp plugin re-adds type prefixes, `decisions/naming-convention.md`).

## Answered

| Question | Answer | Doc |
|---|---|---|
| `gp` value | 0x004159F0, matches crt0 and Ghidra; the only gp write is crt0 | `platform/toolchain.md` |
| SDK version, source file names, type-info strings | SDK 2.3.0 (libmc 2.3.2); no source names; only libstdc++ classes | `platform/toolchain.md` |
| `main`, main loop, scene table | `main` 0x001001F0, loop 0x00100290–0x001003BC, 8 scenes; scenes 1 and 3 have no requester | `game/boot.md`, `game/scenes.md` |
| VU microcode upload | inline every frame via DMA `ref` tags; 5 VU1 programs + 1 VU0 program | `subsystems/vu-programs.md` |
| TBL/CDP, SMV formats | — | `formats/CDP.md`, `formats/SMV.md` |
| MISxx.SMV ↔ mission | MISnn is the briefing movie of campaign mission nn (`StartMenuMovie(n − 1 + 0xD)`) | `subsystems/frontend.md` |
| Mission entries `id + 1` / `id + 0x52` | chosen by the player's aircraft (mask 0x00204114); variants share the map and differ in units, unit models and messages | `formats/MISSION.md` |
| EE↔IOP RPC, IOP side of SOUND/VAG | — | `game/iop-interface.md`, `subsystems/iop-sound.md` |
| Threading, frame rate | single game thread, 59.94 Hz everywhere, SMV 29.97 fps; HDD threads never created | `game/threads-timing.md` |
| HDD save module | complete but unreachable on any hardware (init has no caller) | `subsystems/save.md` |
| Main context | `MainContext` 0xA20; floats at 0x80 are the brightness scale | `game/main-context.md` |
| Code gaps, dead code | switch cases, created functions, STL copies, `jr ra` stubs | `port/dead-code.md` |
| Skeleton PCSX2 values (heap, first replies, USB joystick) | — | `pcsx2/sessions-03.md` |
| EE FPU rounding mode for the port | round to nearest, as PCSX2 runs AC04 (GameDB `eeRoundMode: 0`); owner decision 2026-10-13 | `port/validation.md` E-2 |
| Carrier takeoff after a rearm (subs 0x19–0x1B, 0x1D) | confirmed on screen: 0x19 → 0x1A → 0x1B → 0x0B, control normal afterwards | `pcsx2/sessions-05.md` |
| Does the in-engine intro play only on the first boot? | no: it plays at every boot where card slot 1 has no `BASLUS-20152AC04` directory (sceMcGetDir count at save+0x4D8) | `subsystems/results-startup.md` |
| VU0 skinning part types | type 0 → 0x002250E8, type 1 → 0x00225110, type 3 (morph) → 0x00225140; earlier labels had 0 and 1 swapped | `subsystems/vu-programs.md` |
| `sceGsSyncV` return value | `CSR.FIELD` when interlaced, else 1 | `port/skeleton-readiness.md` |
| Scratchpad → GIF ring users | none in live code | `subsystems/rendering.md` |
| SOUND command 0xC | no EE writer: dead | `subsystems/iop-sound.md` |
| `QueueCdRead` chunked path | every caller passes `chunk = −1` | `port/dead-code.md` |
| `C_PCCON` profiler | dead game code: translated, unreachable | `port/dead-code.md` |
| Naming convention | accepted (no type prefixes) | `decisions/naming-convention.md` |
| Model format, GIM, DMA-tag sub-blocks | `SM` models with relative offsets; GIM textures; pre-built DMA packets | `formats/MODEL.md`, `formats/GIM.md`, `formats/MISSION.md` |
| GS dumps of whole frames | triangle fans/strips with STQ and fog, trilinear mipmapping, CT24, Z24 as texture, destination-alpha blends | `platform/gs-subset.md`, `pcsx2/sessions-04.md` |
| In-menu movie pacing | one loop iteration = 2 fields (frame counter +2) | `game/threads-timing.md`, `pcsx2/sessions-04.md` |
| Half-resolution pass `func_00142460` | additive glow of the aircraft in external view; skipping it shows no visible change in the tested scene | `subsystems/rendering.md` |
| CONTROL TYPE option bit | 0x002A15D0 bit 0 set = EASY (Euler path), clear = NORMAL (matrix path) | `subsystems/flight.md` |
| Player view byte +0x149 | 0 HUD only, 1 cockpit, 2 external | `subsystems/flight.md` |
| Scene 2 subs 0x14–0x1D | 0x14–0x18 carrier landing, 0x1D rearm menu (START during the approach), 0x19–0x1B carrier takeoff (reachable from the rearm menu) | `subsystems/mission-flow.md` |
| Pause-menu HUD item (flag 0x20000) | shown only when the game has been cleared (0x002A0318 bit 0) | `game/main-context.md` |
| Strike event `func_00135008` consumer | class 10 scripted objects of type 0x4C (`func_00134810`) read the zone bits | `subsystems/ai.md` |
| Sea surface | flat scrolling tiles clipped on the CPU; the noise table is filled from `rand()` but read only by dead code | `subsystems/rendering.md`, `game/random.md` |
| HUD warning texts | read from the executable (tables 0x002B7450 / 0x002B73D8); ids 1 and 9–12 are never selected | `subsystems/camera-hud.md` |
| Loaders for DATA 291–332 and BD 2 | none: every DATA and BD load call site was checked; indices are constants or bounded below 291; BD uses 0, 1, 3–12 | `formats/CDP.md`, `port/dead-code.md` |
| DATA = id + 1 for aircraft outside mask 0x00204114 | yes: `LoadFlightDataEntry` takes id + 0x52 only when the id < 0x13 and the mask bit is set (disassembly 0x0010AF48–0x0010AF70 checked), else id + 1 | `subsystems/mission-flow.md` |
| `func_00226710` / `func_0017BE38` in trial missions | aircraft unlock (state 2) for the mission; best-run track for the trial HUD comparison | `subsystems/results-startup.md` |
| Result mode names | the mode has no on-screen name; it selects the title (`TRIAL MISSION nn` / `MISSION nn`), the reward lines and the background | `subsystems/results-startup.md` |
