# Command-line parameters of `ac04port.exe`

Every parameter the executable accepts. Keep this file in sync with `src/platform/command_line.cpp`.

Command-line values override `settings.json` **for the current session only**; they are never written back to the file. Unknown parameters or a missing value stop the program with an error and the usage text (exit code 2).

| Parameter | Value | Since | Effect |
|---|---|---|---|
| `--iso <path>` | path of the disc image (UTF-8) | A (used from B) | Uses this image instead of `paths.iso` in `settings.json` |
| `--user-data <dir>` | folder | A | Uses this folder for `settings.json`, `saves\` and `logs\` instead of `%LOCALAPPDATA%\AC04Port\`. Meant for testing; the folder is created if needed |
| `--debug-overlay` | — | A (overlay in D) | Enables the debug overlay for this session, as if `debug.overlay_enabled` were true |
| `--no-debug-overlay` | — | A (overlay in D) | Disables the debug overlay for this session |
| `--verify-iso` | — | B | Recomputes the disc image SHA-1 instead of using the cached result (`<user data>\iso_cache.json`, keyed by path, size and modification time) |
| `--allow-unverified-iso` | — | B, **development builds only** | Continues when the image fails the size/volume check or the SHA-1 check (and when `SLUS_201.52` differs). Logged as a warning; the window title shows `[UNVERIFIED DISC IMAGE]`. Release builds reject the parameter |
| `--dump-ram-after-load` | — | B, **development builds only** | After loading `SLUS_201.52`, writes guest RAM (32 MiB) followed by the scratchpad (16 KiB) to `<user data>\dumps\ram_after_load.bin` for `tools/research/ramdiff.py`. The file contains game data: never copy it into the repo |
| `--dump-ram-at-first-scenestep` | — | E, **development builds only** | At the entry of the first `SceneStep` (0x00100B18, the PCSX2 breakpoint of `../pcsx2/procedures.md` §2), writes guest RAM (32 MiB) followed by the scratchpad (16 KiB) to `<user data>\dumps\ram_first_scenestep.bin`, for `tools/research/ramdiff.py --expected` (`validation.md` E-1). Runs once, on the game thread. The file contains game data: never copy it into the repo |
| `--dump-ram-at <addr>[,<hit>]` | hex guest address, optional hit number (default 1) | S1, **development builds only** | Writes guest RAM + scratchpad to `<user data>\dumps\ram_<ADDR>_<hit>.bin` when the game reaches a RAM checkpoint (`validation-strategy.md` §2.1). Checkpoints are the PCSX2 breakpoint addresses of a slice batch, reported by the translated code (`debug::OnGuestCheckpoint`): S1 has 0x00261378, 0x002613D0, 0x00260B54, 0x00260B78; S2a has 0x0022DF00 (title sub 6), 0x0022D748 (Start accepted), 0x0022D84C (menu tree update), 0x0022D880 (confirm in the menu); S4 has 0x00230790, 0x00238380, 0x00238440, 0x00230FC8, 0x00235AB8, 0x0023C708 and 0x00233F10 (`CommitHangarSelection` entry). The file contains game data |
| `--memcard <dir>` | folder | S1, **development builds only** | Uses this PCSX2 folder memory card in slot 1 (read-only in S1) instead of `<user data>\saves\`. For validation runs with the owner's cards; copy them under `out/` first |
| `--no-memcard` | — | S1, **development builds only** | No memory card in slot 1 (the "no card" messages of scene 6) |
| `--screenshot-at <n>[,...]` | vblank numbers | S1, **development builds only** | Writes a native screenshot (the pre-output-pass image, `debug-overlay.md` §4.1) to `<user data>\screenshots\native_<vblank>.png` after each listed vblank. **F8** takes one at any time in development builds |
| `--pad-script <v>:<button>[,...]` | vblank and button name (cross circle triangle square start select up down left right l1 r1 l2 r2 l3 r3) | S1, **development builds only** | Presses the button on pad 1 for 6 vblanks from vblank *v*. Since S5 P1 live devices are ignored when a script is given (pad 1 neutral, pad 2 not connected), so no real stick can leak into a validation run. For unattended validation runs (e.g. `--no-memcard --pad-script 320:cross` reaches the second card message) |
| `--record-input` | — | S5 P1, **development builds only** | Writes the pad state of every game tick to `<user data>\inputs\input.ac4i` (format in `src/platform/input_log.h`). The log is the run's whole external input |
| `--replay-input <file>` | path | S5 P1, **development builds only** | Takes both pads from an input log; live devices are ignored. A replay of a recorded run is identical to it (`validation.md` S5-P1) |
| `--replay-p2m2 <file>` | path | S5 P2, **development builds only** | Takes the pads from a PCSX2 input recording (`.p2m2` v1, `../formats/p2m2.md`; power-on recordings; pad 2 treated as not connected, HYPOTHESIS); live devices ignored |
| `--p2m2-offset <n>` | integer | S5 P2, **development builds only** | Game tick *t* reads recording frame *t* + *n* (calibration: `../pcsx2/procedures.md` S5-A run 1) |
| `--hash-log` | — | S5 P1, **development builds only** | Writes a hash of guest RAM after every vblank (plus one per 64 KiB block) to `<user data>\dumps\hashes.ac4h`; compare two runs with `tools/research/hashdiff.py` |
| `--stress-host` | — | S5 P1, **development builds only** | Random 0–40 ms delays on the main thread each frame, so the catch-up batches vary: a run with it must give the same hash log as a calm run |
| `--vk-validation` | — | S4, **development builds only** | Enables the Vulkan validation layer when it is installed (off by default since 2026-10-16, owner decision). The layer costs about a microsecond per Vulkan call: the hangar records ~4,000 draws per frame and drops from 60 to ~13 fps with it (`validation.md` S4-6) |
| `--dump-audio` | — | S1, **development builds only** | Keeps everything the SPU mixer produced and writes it to `<user data>\dumps\audio.wav` (48 kHz stereo) at a normal exit. Contains game audio: never copy it into the repo |
| `--exit-at <n>` | vblank number | S3, **development builds only** | Ends the session normally after vblank n (as closing the window), so `--dump-audio` and the other exit-time outputs are written. For validation runs |
| `--freeze-sim` | — | S5 block A, **development builds only** | Debug layer (category 3): in scene 2 the sub-states 0xA–0xC (`FlightPlayFrame` 0x0010B150) and 0x13 (special sequence 0x0010BA38, the opening of mission id 0x25 = mission 1 with the default aircraft) run only their block A parts: random index (0xA–0xC), terrain/world update, `UpdateViewCameras`, `BuildFlightDrawLists`. The simulation step, the sequence step 0x0010C748 and the player/sound/skip passes are not run, so the world is drawn from the start camera. Guest RAM then differs from PCSX2 by design: never use it for RAM checkpoints after sub 8 |
| `--dump-dma-at <n>` | vblank number | S5, **development builds only** | Records the step that starts at vblank *n*: every word the VIF1 receives with its guest address (`<user data>\dumps\vif1_<n>.bin`, pairs of u32) and the GIF data of that step (`gif_<n>.bin`). Summary with `tools/research/dmacap.py <dumps dir> <n> [--vif] [--regs]`. Contains game data: never copy it into the repo |
| `--movie-upscale <n>` | factor 2–4 | S3, **development builds only** | Runs the movie upscaling stage (ADR-006 §5, FSR 1 EASU) at factor n, with the game still drawn at 1×. A screenshot (`--screenshot-at`, F8) taken during a movie also writes the stage output as `screenshots\movie_x<n>_<vblank>.png`. For checking the stage; it changes nothing on screen |
| `--version` | — | A | Shows the version, commit and build type, then exits |
| `--help`, `-h`, `/?` | — | A | Shows the usage text, then exits |

In release builds (no console window) `--help` and `--version` show their text in a message box.

## Disc image at startup (milestone B)

The image path comes from `--iso` or `paths.iso`. Startup checks, in order: the file opens; it is an ISO 9660 image; size 3,554,705,408 bytes and volume ids `PLAYSTATION` / `ACECOMBAT04`; SHA-1 of the whole image (about 9 s the first time, then cached); SHA-1 of `SLUS_201.52`. Any failure shows a message box saying what to do, is logged, and the port exits (development builds: see `--allow-unverified-iso`). Closing the window during the SHA-1 check cancels it.

## `settings.json` keys (for reference)

Stored in `%LOCALAPPDATA%\AC04Port\settings.json` (or `--user-data`). Invalid or missing values fall back to the default and are logged; unknown keys are kept. The file is re-read automatically while the port runs (checked once per second).

| Key | Values | Default |
|---|---|---|
| `presentation.filter` | `sharp`, `smooth` | `sharp` |
| `presentation.vsync` | `true`, `false` | `true` |
| `presentation.frame_cap` | `60`, `120`, `144`, `240`, `360` | `360` |
| `presentation.window_mode` | `windowed`, `borderless` | `windowed` |
| `presentation.window_width` / `window_height` | windowed client size (saved at exit) | `1280` / `960` |
| `presentation.internal_scale` | `1` (only value accepted for now) | `1` |
| `paths.iso` | disc image path | empty |
| `audio.on_focus_loss` | `keep_playing`, `mute` | `keep_playing` |
| `input.bindings.<control>` | list of `key:<SDL key name>`, `pad:<button>`, `axis:<axis>+` / `axis:<axis>-` | see `DefaultInputSettings()` |
| `debug.overlay_enabled` | `true`, `false` | `true` in development builds, `false` in release |
| `debug.overlay_toggle` | a typed character, or `key:<SDL key name>` | `\` |

PS2 controls: `cross circle square triangle l1 r1 l2 r2 l3 r3 start select up down left right lx- lx+ ly- ly+ rx- rx+ ry- ry+`.
Gamepad buttons: `south east west north back guide start leftstick rightstick leftshoulder rightshoulder dpup dpdown dpleft dpright` (positions, not vendor labels). Gamepad axes: `leftx lefty rightx righty lefttrigger righttrigger`. Port 1 uses the keyboard and the first gamepad, port 2 the second gamepad.
