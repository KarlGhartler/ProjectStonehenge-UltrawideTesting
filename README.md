# AC4_recomp
Native PC x86-64 static recompilation of Ace Combat 04: Shattered Skies (PS2 MIPS architecture)

This repository contains source code only. It does not contain, and will never contain, any game data, copyrighted textures, audio, or game binaries. To run this software, users must provide their own legally acquired copy of Ace Combat 04: Shattered Skies.

## Acknowledgements

These projects are used as tools or references. No code from them is copied into this repository (third-party code that is included is listed in `THIRD_PARTY_NOTICES.md`).

- **[Ghidra](https://github.com/NationalSecurityAgency/ghidra)**: the reverse-engineering framework used as the research database.
- **[ghidra-mcp](https://github.com/bethington/ghidra-mcp)** by bethington: the MCP server used to drive Ghidra during analysis.
- **[ghidra-emotionengine-reloaded](https://github.com/chaoticgd/ghidra-emotionengine-reloaded)** by chaoticgd: the Emotion Engine (R5900) processor plugin for Ghidra.
- **[PCSX2](https://github.com/PCSX2/pcsx2)**: debugger and behaviour reference for the original game; its source was read (not copied) to document the Folder Memory Card layout for save compatibility.
- **[ps2sdk](https://github.com/ps2dev/ps2sdk)**: syscall tables and SDK references used to identify library functions.
