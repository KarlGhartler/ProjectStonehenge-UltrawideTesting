# Proposed third-party dependencies

Status: **Proposed, none approved.** Each dependency needs the project owner's approval before it is added (CLAUDE.md, Build section).

Repo license: **GPL-3.0** (`LICENSE`). A dependency is compatible if its license can be combined into a GPL-3.0 work: GPL-3.0(-or-later), GPL-2.0-**or-later**, LGPL-2.1+/3, Apache-2.0, MIT, BSD, zlib, BSL-1.0. Not compatible: GPL-2.0-**only**, proprietary, "non-commercial" licenses.

## Runtime

| Dependency | Purpose | License | GPL-3.0 compatible | Notes |
|---|---|---|---|---|
| **SDL3** | window, input (pads, joysticks), audio output, timing | zlib | yes | required by the project direction; use the stable 3.2.x line |
| **Vulkan-Headers** | Vulkan API headers | Apache-2.0 (+MIT) | yes (Apache-2.0 is GPL-3.0 compatible, not GPL-2.0-only) | header only |
| Vulkan loader | runtime | system-provided on Windows/Linux (Apache-2.0) | yes | not vendored |
| **volk** | function loader, avoids link-time dependency on the loader | MIT | yes | optional; SDL3 can also provide `vkGetInstanceProcAddr` |
| **VulkanMemoryAllocator** (VMA) | GPU memory allocation | MIT | yes | single header |
| **MoltenVK** | Vulkan on macOS (best effort target) | Apache-2.0 | yes | runtime library, not vendored |
| **MPEG-2 video decoder** (for SMV, see below) | decode MIS/SS/INTRO/STAFF movies | see options | — | must handle MPEG-2 MP@ML 640×448 elementary streams |

### MPEG-2 decoder options

| Option | License | Compatible | Pros | Cons |
|---|---|---|---|---|
| **libmpeg2** (0.5.1) | GPL-2.0-or-later | yes | small C library; does exactly MPEG-2 video elementary streams, which is what the PS2 IPU decodes; low CPU | unmaintained since 2008 (stable, but no fixes); needs a local build |
| **FFmpeg** libavcodec, minimal build (`mpeg2video` decoder only) | LGPL-2.1-or-later (GPL if built with `--enable-gpl`) | yes | maintained, fast, well tested | large build system; easier as a system or prebuilt package; licence bookkeeping for the shipped binaries |
| pl_mpeg | MIT | yes | single header, trivial to integrate | **MPEG-1 only**: cannot decode the MPEG-2 streams in SMV files, so it is not usable |
| In-house decoder | GPL-3.0 (ours) | yes | no dependency | a correct MPEG-2 decoder is a lot of work |

Recommendation: **libmpeg2**, fetched by CMake FetchContent pinned with a checksum (copied into `third_party/` only if we have to modify it; `.claude/rules/third-party.md`), because it matches the IPU feature set and is small. FFmpeg is the fallback if libmpeg2 turns out to be insufficient (e.g. a stream feature it does not handle).

## Build and test only

| Dependency | Purpose | License | Compatible | Notes |
|---|---|---|---|---|
| **CMake ≥ 3.24** | build system | BSD-3 | n/a (tool) | chosen by the project owner |
| **glslang** (or **shaderc**) | GLSL → SPIR-V at build time | BSD-3 / Apache-2.0 / MIT mix (shaderc: Apache-2.0) | yes | build tool only; output SPIR-V is ours |
| **doctest** (or GoogleTest BSD-3, Catch2 BSL-1.0) | unit and behaviour tests | MIT | yes | doctest is a single header and fast to compile |

## Written in-house (no dependency proposed)

| Component | Why in-house |
|---|---|
| ISO 9660 reader | Only the primary volume descriptor, the directory records and 2048-byte sector reads are needed (the game looks files up by path, like `sceCdSearchFile`). About 300 lines. libcdio (GPL-3.0+) would be compatible but is far larger than needed |
| SHA-1 for the startup check | about 150 lines, standard algorithm (`supported-version.md`) |
| SPU ADPCM decoder, ADSR, voice mixer | small, documented, and must match the game's SOUND/VAG semantics (ADR-004) |
| Config file | a simple INI/TOML subset; toml++ (MIT) would be the dependency if a full parser is wanted |

## Not allowed

- Any Sony SDK code or headers. `src/sce/` is written from observed behaviour only.
- PCSX2 source code is not proposed. Its GPL-3.0+ license would be compatible, but copying emulator internals is a separate decision for the project owner. Using PCSX2 as a behaviour reference is the existing practice.
