<div align="center">

# ProjectStonehenge

**A native x86_64 PC port of *Ace Combat 04: Shattered Skies***

![Latest release](https://img.shields.io/github/v/release/Onaga00/ProjectStonehenge?style=flat-square)
![License](https://img.shields.io/github/license/Onaga00/ProjectStonehenge?style=flat-square)
![Platform](https://img.shields.io/badge/platform-Windows%20x64-blue?style=flat-square)

<!-- Gameplay GIF: add docs/gameplay.gif (recorded from the port, no game assets in the repo besides it) and uncomment:
<img src="docs/gameplay.gif" alt="Gameplay" width="800">
-->

</div>

---

## About

**ProjectStonehenge** is a native x86_64 PC port of *Ace Combat 04: Shattered Skies*, based on the North American release (NTSC-U, SLUS-20152).

The port is built from a decompilation of the original PS2 executable (ELF). It is **not** an emulator and **not** a static recompilation: the game code has been translated function by function into C++ (with AI assistance, see [AI Disclosure](#ai-disclosure)) and runs natively on PC. Game logic, physics, AI and mission rules behave exactly like the original, with PCSX2 running the original disc as the reference. Rendering (a Vulkan renderer for the PS2 GS), audio (the game's IOP sound drivers re-implemented) and saves are rebuilt for the PC.

> [!IMPORTANT]
> Only the **North American release (NTSC-U, SLUS-20152)** is currently supported. Other regions and revisions will not work. The port checks the disc image at startup (SHA-1 `aaa707214aea7f8f9c2904c4b5e00c372c943533`, 3,554,705,408 bytes).

## Legal Notice

This repository **does not contain any game assets, files or copyrighted data** from *Ace Combat 04: Shattered Skies*. All assets, including the executable's own data tables, are loaded at runtime from a disc image that you must provide yourself.

To play, you need a dump of **your own legally purchased copy** of the game. This project does not support piracy in any form: please do not open issues asking for game files or links to ISOs, they will be closed.

This is an unofficial fan project and is **not affiliated with, endorsed by or sponsored by Bandai Namco Entertainment**. *Ace Combat* and all related names, logos and trademarks are the property of their respective owners.

## AI Disclosure

This project is developed with extensive use of generative AI. Most of the code, the reverse-engineering notes, the research tools and the documentation in this repository were written by an AI coding assistant (Claude, by Anthropic), working under the direction of the project's maintainer, who decides what is built, reviews the changes and makes every commit.

What this means in practice:
- The translated game code is checked against the original game running in PCSX2 (RAM comparisons, screenshots, input recordings; see [`docs/port/validation-strategy.md`](docs/port/validation-strategy.md)), not taken on trust. Differences that are still open are listed in [`docs/port/validation.md`](docs/port/validation.md) and [`docs/open-questions.md`](docs/open-questions.md).
- AI-written code can still contain mistakes that no test has caught yet. If something behaves differently from the original game, please [open an issue](https://github.com/Onaga00/ProjectStonehenge/issues).
- The assistant works from the game executable of the maintainer's own disc (analysed in Ghidra), from PCSX2 as the behaviour reference and from public documentation; the tools and references used are listed under [Credits](#-credits). No game data is committed to this repository.

## Features

**Graphics and performance**
- **High frame rates** (frame cap 60 / 120 / 144 / 240 / 360 FPS): rendering is decoupled from game logic and interpolated, while physics and AI still run at the original 59.94 Hz
- **Higher internal resolution** with a multiplier from 1x to 8x (native 512×448 up to 4096×3584), including 4K output
- **Screen** windowed or borderless fullscreen
- **16:9 by default on wide monitors**: the game's own SCREEN RATIO option starts at 16:9
- **Graphics defaults matched to your monitor**: the first start uses v-sync, FXAA and the internal resolution closest to the monitor's (for example 2× at 1080p, 3× at 1440p, 5× at 4K)
- **Anti-aliasing**: FXAA and SMAA support
- **V-Sync**
- **FSR 1 upscaled cutscenes**: the FMV movies are upscaled with AMD FidelityFX FSR 1 to match internal resolution
- **QUIT GAME in the main menu**: the last item of the main menu closes the port
- **Save messages for a PC**: "memory card (PS2)" reads "memory card", and "switch off power" reads "switch off the computer"
- **Dedicated in-game graphics settings**: a GRAPHICS in-game page under OPTIONS → SCREEN SETTINGS

**Controls**
- Keyboard and gamepad support through SDL3, with bindings in `settings.json`
- **Vibration** on gamepads with rumble, switched by the game's own VIBRATION option
- **Modern control types**: two extra CONTROL TYPE choices on CONTROLLER SETUP with L1/L2 and R1/R2 swapped (throttle on the triggers); the original types are still there

**Saves**
- **PCSX2-compatible save system** using PCSX2's folder memory card format, so saves can be moved between the port and the emulator in both directions (copy the `BASLUS-20152AC04` folder)

> [!WARNING]
> Save compatibility is still in **beta**. Always back up your original save before moving it.

**Preservation**
- **Built-in debugger** for testing (see [Debugger](#debugger))
- **Cut content and dead code preserved**: code that was unreachable in the final release (272 functions, including two unused scenes and the HDD support) has been ported along with the rest of the game. In the default build it stays unreachable exactly as on the PS2

## Roadmap

- [ ] VS mode (and a surprise 👀)
- [ ] Performance optimizations
- [ ] Modding support
- [ ] A full catalog and map of the cut content, and a debug menu to explore it
- [ ] Linux support

## 💻 Requirements

- Windows 10/11 (64-bit)
- GPU: any graphics card with **Vulkan 1.2** support and an up-to-date driver. Higher internal resolutions (4x–8x) need a correspondingly faster GPU
- A dump of your own copy of *Ace Combat 04: Shattered Skies* (NTSC-U, SLUS-20152) as an `.iso` file (ISO 9660, 2048-byte sectors; raw `.bin`/`.cue` dumps are not supported)

## 📥 Download and Installation

1. Download the latest version from the [Releases page](https://github.com/Onaga00/ProjectStonehenge/releases).
2. Extract the archive to a folder of your choice. The port is a single executable: no installer, no other DLLs and no Visual C++ Redistributable needed.
3. Dump your own copy of the game to an ISO (for example with ImgBurn on PC, or by following PCSX2's [dumping guide](https://pcsx2.net/docs/setup/dumping/)).
4. Launch `ProjectStonehenge.exe`. On the first start the port asks for the disc image: click **Select ISO...** and pick your ISO. The path is saved in `settings.json`. You can also start the port with `ProjectStonehenge.exe --iso "C:\path\to\game.iso"`.
5. Play. Everything the port writes goes to `%LOCALAPPDATA%\ProjectStonehenge\`:
   - `settings.json`: port settings (graphics, input bindings, debug options)
   - `saves\`: the memory card (PCSX2 folder card format)
   - `logs\`: the log of the last session (attach it to bug reports)

### Command-line options (release builds)

| Option | Effect |
|---|---|
| `--iso <path>` | use this disc image instead of the one in `settings.json` |
| `--user-data <dir>` | use another folder instead of `%LOCALAPPDATA%\ProjectStonehenge\` |
| `--internal-scale <1-8>` | internal resolution for this session |
| `--antialiasing <off\|fxaa\|smaa>` | anti-aliasing for this session |
| `--fps-overlay` / `--no-fps-overlay` | show or hide the frame-rate counter |
| `--debug-overlay` / `--no-debug-overlay` | enable or disable the debugger for this session |
| `--verify-iso` | check the disc image again instead of using the cached result |
| `--version`, `--help` | show the version or the usage text |

Command-line values apply to the current session only and are never written to `settings.json`. Development builds accept many more options (start at a mission, input recording and replay, screenshots, RAM dumps, ...): see [`docs/port/command-line.md`](docs/port/command-line.md).

## 🔧 How to Build

The port builds with CMake and MSVC. All dependencies are fetched automatically at the first configure, pinned to exact versions with checksums. No Vulkan SDK is needed: shaders are compiled with a fetched copy of glslang.

**Prerequisites**
- Windows 10/11 (64-bit)
- Visual Studio 2026 with the **Desktop development with C++** workload (it includes CMake ≥ 3.24)
- Git

**Build**

From a **Developer PowerShell for VS 2026**:

```powershell
git clone https://github.com/Onaga00/ProjectStonehenge.git
cd ProjectStonehenge
cmake --preset windows
cmake --build --preset release     # or: --preset dev (RelWithDebInfo), --preset debug
ctest --preset release             # unit and behaviour tests
```

The executable is written to `build\windows\src\<Config>\ProjectStonehenge.exe` (`Release`, `RelWithDebInfo` or `Debug`).

Release archive: `cmake --build --preset release --target package_release` writes `out\release\ProjectStonehenge-<version>-win64.zip` with the executable, `LICENSE`, `THIRD_PARTY_NOTICES.md` and `README.md`, and nothing else. It packages only the Release configuration and stops if a file looks like game data.

**Build types**
- `release`: the build for players.
- `dev` and `debug` (development builds): console window, debugger on by default, **F8** screenshot, and the development command-line options.

Research tools (Python 3, in `tools/`) are only needed to work on the decompilation, not to build or play.

## Debugger

The game contains a large amount of dead code that was never reachable in the final release.

The debugger is **disabled by default** in release builds. To enable it, open the configuration file `%LOCALAPPDATA%\ProjectStonehenge\settings.json` and set `overlay_enabled` to `true` in the `debug` section:

```json
"debug": {
  "overlay_enabled": true,
  "overlay_toggle": "\\"
}
```

Keep the rest of the file as it is: a file that is not valid JSON, or that contains an unknown key or value, is replaced with the defaults. You can also enable it for one session with `--debug-overlay`. Changes to the file apply while the port runs.

**Controls**
- **`\`** opens and closes the panels. Change it with `overlay_toggle`: another character, or a key name such as `"key:F12"`.
- While a panel has keyboard focus, the keyboard does not reach the game; gamepads still do.

**Panels** (read-only: they never change the game)
- **Performance**: frame rate and frame-time graph, simulation steps and dropped steps, renderer timings and texture statistics, and the frame-rate overlay checkbox.
- **Scene**: current scene and sub-state, frame counters, mission id, and the last 64 scene changes.
- **Memory**: a viewer for the PS2 memory (32 MB RAM and scratchpad) as bytes, u32 or f32, with jump to an address or symbol name.
- **Session log**: the last 400 log lines, with a filter.

A menu to enter the cut content is planned; see the [Roadmap](#roadmap) and [`docs/port/dead-code.md`](docs/port/dead-code.md).

## 🤝 Contributing

Feedback is very welcome! If you find a bug, or a feature that doesn't behave like the original game, please [open an issue](https://github.com/Onaga00/ProjectStonehenge/issues).

When reporting a bug, it helps to include:
- what happened and what you expected to happen (a screenshot from PCSX2 of the same moment helps a lot)
- the mission or menu where it occurred
- your system specs and the port version (`ProjectStonehenge.exe --version`)
- the log of that session (`%LOCALAPPDATA%\ProjectStonehenge\logs\`)

Pull requests and forks are welcome too. Project documentation (decompilation notes, data formats, architecture decisions, validation) starts at [`docs/README.md`](docs/README.md). Never commit game data: enable the repository's pre-commit hook with `git config core.hooksPath tools/hooks`.

## 📜 License

The source code in this repository is licensed under the [GNU General Public License v3.0](LICENSE).

This license applies **only** to the code in this repository. It does not cover *Ace Combat 04: Shattered Skies* or any of its original assets, which remain the property of Bandai Namco Entertainment. Third-party libraries keep their own licenses: [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).

## 🙏 Credits

### Acknowledgements

These projects are used as tools or references. No code from them is copied into this repository (third-party code that is included is listed in `THIRD_PARTY_NOTICES.md`).

- **[Ghidra](https://github.com/NationalSecurityAgency/ghidra)**: the reverse-engineering framework used as the research database.
- **[ghidra-mcp](https://github.com/bethington/ghidra-mcp)** by bethington: the MCP server used to drive Ghidra during analysis.
- **[ghidra-emotionengine-reloaded](https://github.com/chaoticgd/ghidra-emotionengine-reloaded)** by chaoticgd: the Emotion Engine (R5900) processor plugin for Ghidra.
- **[PCSX2](https://github.com/PCSX2/pcsx2)**: debugger and behaviour reference for the original game; its source was read (not copied) to document the Folder Memory Card layout for save compatibility, the input-recording (`.p2m2`) format used to replay PCSX2 input in validation, and the IPU colour-conversion method that the port's movie decoding follows.
- **[ps2sdk](https://github.com/ps2dev/ps2sdk)**: syscall tables and SDK references used to identify library functions.

### Libraries

Fetched at build time, unmodified ([`docs/decisions/dependencies.md`](docs/decisions/dependencies.md)):
[SDL3](https://github.com/libsdl-org/SDL) ·
[Vulkan-Headers](https://github.com/KhronosGroup/Vulkan-Headers) ·
[volk](https://github.com/zeux/volk) ·
[VulkanMemoryAllocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator) ·
[glslang](https://github.com/KhronosGroup/glslang) ·
[libmpeg2](https://libmpeg2.sourceforge.io/) ·
[nlohmann/json](https://github.com/nlohmann/json) ·
[Dear ImGui](https://github.com/ocornut/imgui) ·
[AMD FidelityFX FSR 1](https://github.com/GPUOpen-Effects/FidelityFX-FSR) ·
[SMAA](https://github.com/iryoku/smaa) ·
NVIDIA FXAA 3.11 ·
[doctest](https://github.com/doctest/doctest)
