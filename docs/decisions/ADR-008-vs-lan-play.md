# ADR-008: LAN play for VS mode

Status: **Accepted: option A** (User decision 2026-10-13, after the analysis of the same day). Lockstep input sharing, category 3 (optional layer), built after the VS slice S6 and after the gameplay slices have passed input-recording validation (`../port/slices.md`, slice S9). No networking code and no dependency has been added yet; a networking dependency, if one turns out to be needed, still needs the User's approval.

## Context

- The original VS mode is two players on one PS2, split screen (`../subsystems/vs-mode.md`): both pads are read by one game, which simulates both aircraft and draws two 256×448 views side by side.
- The question: can two PCs on a local network play VS mode, each player on their own PC?
- Constraints: game logic unchanged (CLAUDE.md, Goal); determinism rules of the port (`../port/validation-strategy.md` §3.5); ADR-002 (one game thread, fixed 59.94 Hz step, presentation decoupled); ADR-005 (OS code in `src/platform/`).

## Options

### A. Lockstep input sharing (delay-based)

Both PCs run the full game from boot. Each step, each PC sends its local pad bytes (18 bytes, the `scePadRead` data) for step `s + D` and runs step `s` only when both pads for `s` are known. One PC is pad port 1, the other pad port 2. Both simulations are identical, so both show the same frame.

- **Outside game code:** a netplay layer in `src/platform/` between the input backend and `scePadRead` (input queue per port, step gate in the game-thread loop of ADR-002), session setup (discovery, handshake), desync detection, UI messages outside game rendering. The presentation decoupling of ADR-002 keeps rendering smooth while the game thread waits.
- **Inside game code:** none. The game already reads two pads.
- **Rollback:** not needed on a LAN. RTT on wired Ethernet is < 1 ms and on Wi-Fi typically 2–10 ms, i.e. < 1 step (16.7 ms). A fixed input delay of **D = 1–2 steps** (17–33 ms) hides it. Rollback would need a per-step snapshot of guest RAM (32 MB + scratchpad) **and** all service state (CD queue, SOUND/VAG stand-ins, pads, timers) plus re-simulation of several steps in one frame; that is the save-state feature of `../port/port-features.md` §8.4.3 (effort L) and is only worth it over the internet.
- **Determinism requirements** (everything that feeds the simulation must be bit-identical on both PCs):
  - identical disc image (SHA-1 already verified at startup, `../platform/executable.md`), identical port build (build hash exchanged in the handshake);
  - identical starting state: both PCs boot from power-on in lockstep, and the **host's memory card image** is sent to the client and used for the session (VS offers the aircraft owned in the save; options such as SCREEN RATIO and vibration are save data too). The client's own card is not touched;
  - EE float arithmetic only through `src/common/ee_math.h`; no FMA contraction (MSVC `/fp:precise` without `/fp:contract`; GCC/Clang `-ffp-contract=off` when Linux comes); `/arch` level fixed in the build (no runtime dispatch in game math); libm functions translated (`sinf`/`cosf` bit-exact, `../port/coverage.md`);
  - the five random sources in guest memory, read in the original order (`../game/random.md`); the draw-list build runs exactly once per step on both PCs (ADR-002), since it consumes `rand`;
  - service state visible to the game advances per step, never by wall time or the audio device: SOUND voice status, VAG stream positions, CD completions, RPC replies (`../port/validation-strategy.md` §3.5). This is the same requirement as input-recording replay;
  - frame pacing: irrelevant to the result (the step gate decides when a step runs); a slower PC slows both.
  - Settings never reach game code (ADR-006), so host settings differences (filter, frame cap, bindings) are safe.
- **Desync detection:** the determinism checker's per-step hash (`../port/validation-strategy.md` §3.4) of the game RAM regions, exchanged every step (8 bytes) or every N steps. On mismatch: stop the session, write both hash streams and the step number to the log, show a message. Optional debug: dump RAM on both sides at the first bad step for `ramdiff.py`.
- **Own view full screen:** the game always draws both halves. The only lockstep-safe way to show one player's view alone is a **presentation crop**: the output pass shows only the local player's 256×448 half, scaled to the window with its own aspect (2:3 at 4:3 settings, 8:9 with SCREEN RATIO). Forcing a single full-width view in game terms changes `game[0x26]`/the view count, which changes the draw-list build and its `rand` reads → desync. A crop at native resolution is soft (256 pixels wide); a higher internal resolution (`../port/renderer.md` §1) would help. The original split (both halves) stays the default.
- **Latency:** D steps of input delay (17–33 ms), plus the normal presentation latency. Both players get the same delay.
- **Network:** LAN only: UDP, one port, host/join by IP or by LAN discovery (UDP broadcast on the subnet). No NAT traversal. Windows Defender Firewall asks once to allow the executable (inbound UDP); document it. Packet loss handled by resending the last D+few inputs in every packet (redundancy), no separate reliability layer needed.
- **Dependencies:** none required. Options:
  - plain BSD/Winsock UDP sockets in `src/platform/` (Winsock on Windows, POSIX sockets on Linux later; about 300–500 lines);
  - ENet (MIT): reliable/unreliable UDP channels, small, cross-platform;
  - SDL3_net (zlib): thin socket wrapper of the SDL family.
  Recommendation within A: plain sockets (the protocol is tiny), ENet if reliability for the handshake/card transfer becomes tedious. Either needs the User's approval as usual.
- **Effort:** M–L. Netplay layer and step gate ~1 week; session setup, card transfer, handshake, UI ~1 week; desync tooling reuses the determinism checker. Most of the risk is in determinism, which the validation strategy needs anyway.
- **Risks:** a determinism gap in a rarely used path (double-precision soft-float, `cfc2` flag reads, an uninitialised stack read such as `validation.md` E-1 `stack-garbage`) shows up as a desync mid-match; mitigated by the per-step hash and by the input-recording validation of the mission slices. A PC that cannot hold 59.94 Hz slows both players.

### B. Host-authoritative with video streaming

The host runs the game with both pads (the client's pad bytes come over the network). The host encodes its output and streams it to the client, which only decodes and presents.

- **Outside game code:** capture of the host's presented image, video encoder, network transport, client decoder and player; client input sent to the host.
- **Inside game code:** none.
- **Determinism:** not required between PCs (only one simulation).
- **Own view full screen:** the host can crop and stream only the client's half (same presentation crop as A).
- **Latency:** capture + encode + network + decode + present: typically 20–50 ms with a hardware encoder, more in software; the client always lags the host (asymmetric).
- **Bandwidth:** 512×448 at 60 fps raw = ~55 MB/s (440 Mbit/s): too much for Wi-Fi, so an encoder is required.
- **Dependencies:** a video codec, e.g. x264 (GPL-2.0-or-later, compatible) or FFmpeg (LGPL/GPL), or the platform's hardware encoders (vendor SDKs, licensing to check). Large and platform-specific.
- **Effort:** L. **Risks:** encoder integration and latency tuning; audio must be streamed too; image quality loss.

### C. Host-authoritative with draw-packet streaming

The host runs the game; each step it sends the GIF/VIF packet lists the renderer consumes (plus texture uploads and display registers) to the client, whose renderer replays them. Client input goes to the host.

- **Outside game code:** serialisation of the renderer's per-step intake, transport, a client mode that runs only the renderer and audio; audio commands (SOUND/VAG stand-in output) streamed as well.
- **Inside game code:** none.
- **Determinism:** not required between PCs.
- **Bandwidth:** the per-step DMA intake (shown in the overlay's performance panel) plus texture uploads; estimated 0.5–2 MB per step in missions, i.e. 30–120 MB/s before compression: feasible on gigabit Ethernet, doubtful on Wi-Fi.
- **Own view:** presentation crop, as in A.
- **Effort:** L (renderer state must be reproducible on the client from the stream alone; streaming audio). **Risks:** bandwidth spikes on texture-heavy steps; tight coupling to renderer internals.

### Not viable

- Each PC simulates only its own aircraft and exchanges positions: changes game logic (AI targets, collisions, damage, random order), excluded by the project principles.

## Classification

LAN play is **not original behaviour** and **not intrinsic to a PC port** (category 2 is limited to presentation features of a PC port: scaling, frame rate, windows, settings, rebinding). Option A also changes when the game receives input (D steps of delay), which affects the feel of gameplay. So it belongs in **category 3, the optional modding/extras layer**, off by default and opt-in per session, with the original local split-screen VS mode unchanged as the default. The presentation crop to one's own view is part of that same optional feature.

## Recommendation

- If LAN play is wanted: **Option A, lockstep input sharing with a fixed 1–2 step delay, no rollback**, plain UDP sockets in `src/platform/` (no new dependency), host's memory card image used for the session, per-step RAM hash for desync detection, optional crop to the local player's half. It needs no game-code change, and its hard part (determinism) is already required by the validation strategy.
- Timing: after the VS slice (`../port/slices.md` S5) and after the gameplay sub-slices have passed input-recording validation, because those prove determinism first.
- B and C are not recommended: more work, dependencies or bandwidth, and worse latency for the client, with no benefit on a LAN.

**Decision (User, 2026-10-13): option A**, as recommended above: lockstep, fixed 1–2 step delay, no rollback, host's memory card image for the session, per-step RAM hash, optional crop to the local player's half; optional layer (category 3), off by default.
