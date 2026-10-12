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
