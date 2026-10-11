# PCSX2 debugging sessions: run 03 (skeleton checks)

- Emulator: PCSX2 2.8.2, original disc image (NTSC-U, SLUS_201.52), USB ports = None, Fast Boot.
- Method: an Execute breakpoint, then a **save state** taken while stopped at the breakpoint (save state compression = Uncompressed). The values below were read from the save states with `tools/p2s_read.py`.
- The save states stay outside the repo (path in `CLAUDE.local.md`), because they contain game memory. Only the derived values below are recorded here.
- **Timing check:** the owner copied the first 16 bytes at 0x00429800 by hand at breakpoint 0x00100130 (`02 00 00 00`, then 12 zero bytes). The save state holds the same bytes. So a state saved while stopped at a breakpoint shows the memory at that breakpoint.

States taken (2026-10-10):

| File | Taken at |
|---|---|
| `bp_00100220.p2s` | `main`, right after `InitWorkHeap` returns |
| `bp_00100130.p2s` | `InitSubsystems`, after `InitSoundRpc` returned 1 |
| `bp_00100138.p2s` | `InitSubsystems`, after the `InitVagStream` loop |
| `bp_001097F8.p2s` | `ReadUsbJoy`, after the `sceSifCheckStatRpc` loop (first hit, frame 1) |
| `mainMenu.p2s` | main menu, no breakpoint |
| `inGame_mission1.p2s` | flying in mission 1, no breakpoint |

## Check 1: work heap

Observed:

| State | size 0x00411784 | base 0x00411788 | offset 0x0041178C | mark 0x00411790 |
|---|---|---|---|---|
| bp_00100220 | 0x01A318A4 | 0x004CC760 | 0 | 0 |
| bp_00100130, bp_00100138 | same | same | 0 | 0 |
| bp_001097F8 | same | same | 0x3820 | 0x3820 |
| mainMenu | same | same | 0x0083D6E0 | 0x3820 |
| inGame_mission1 | same | same | 0x019C5180 | 0x3820 |

Interpretation:
- The work heap is **0x01A318A4 bytes (27,465,892, about 26.2 MiB)**, at 0x004CC760–0x01EFE004. The port must give `InitWorkHeap` exactly this base and size (ADR-001).
- The boot allocates 0x3820 bytes before `SetWorkHeapMark`. Those blocks survive every scene change.
- In mission 1, the heap is **98.4 % used**. Only 0x6C724 bytes (about 434 KiB) are free. `WorkHeapAlloc` returns 0 when the heap is exhausted, and callers do not check for 0. So the port's heap cannot be smaller than the PS2's, and allocation sizes must match exactly. An allocation that grows on the host (e.g. a host-sized struct) would break this.

## Check 2: first SOUND and VAG replies

SOUND (reply buffer 0 at 0x00429800, after `InitSoundRpc` returned 1):
- Bytes 0–3 = `02 00 00 00`. The remaining 0x5FC bytes are zero. Reply buffer 1 (0x00429E00) is all zero.
- In frame 1 (`bp_001097F8`) the buffer is unchanged: no fno 1 reply had arrived there yet.

Interpretation (H):
- `word 0` is the record count, so the count is 2. That matches "voice records + 2" with no voice records.
- Both record ids are 0, so `UpdateSoundRpc` skips both records.
- The HLE answers fno 0 with this exact layout: count 2, then zeros.

VAG (reply buffer 0 at 0x0042CC90, after the `InitVagStream` loop):

| Offset | Value | Meaning (`iop-sound.md`, `VagAllInit`) |
|---|---|---|
| +0x00 | 0x0008DA40 | IOP address of the stream area `vag_buf` (`rbuf[0]`) |
| +0x24..+0x30 | 1, 1, 1, 1 | ring index per channel 0–3 (`rbuf[9..12]`) |
| +0x50 | 0x200 | `rbuf[20]`, meaning not yet known |
| +0x60 | 0x200 | `rbuf[24]`, meaning not yet known |
| rest of 0x100 | 0 | |

Buffer 1 (0x0042CD90) is zero. The EE buffer pointers at 0x00353F78 are cmd 0x0042CA90/0x0042CB90 and reply 0x0042CC90/0x0042CD90, and the current index 0x00353F88 = 1 after the swap.

Interpretation:
- `InitVagStream` computes `rbuf[0] + 0x180` = 0x0008DBC0 as the first ring segment:
  - slots 0 and 1: 3 × 0x20000 each, from 0x0008DBC0;
  - slots 2 to 5: 3 × 0x10000 each, from the same base 0x0008DBC0.
- Both layouts end at 0x0014DBC0, inside the 0xC01C0-byte `vag_buf`. The two layouts overlap, so slots 0/1 and slots 2–5 cannot stream at the same time without sharing IOP memory. That is behaviour the HLE must keep: the EE only passes these addresses back, and the HLE owns the memory.
- In `mainMenu`, IOP RAM at 0x0008DBC0 holds SPU-ADPCM-looking data (menu music streaming), which agrees.
- The HLE can return any IOP address here, as long as the EE-side arithmetic stays consistent. The EE never dereferences it; it only passes the addresses back in VAG commands (to be confirmed when `0x0021F448` is fully understood). Using 0x0008DA40 keeps RAM comparable with PCSX2.

## Check 3: USB joystick reply, no USB device

0x004204C0, 0x80 bytes, read as u32 words (only the low byte is used by `ReadUsbJoy`):

| Offset | Value | Field (from `ReadUsbJoy`) |
|---|---|---|
| +0x00 | 0 | connection bits: bit 0 = joystick 0, bit 1 = joystick 1 (0 = none) |
| +0x04 | 0 | copied to output byte 0 |
| +0x08..+0x1C | 0x80 ×6 | joystick 0 axes (centred) |
| +0x20, +0x24 | 0, 0 | joystick 0 hat / extra axes |
| +0x28, +0x2C | 0xFF, 0xFF | joystick 0 button bytes (all released, active low) |
| +0x30..+0x44 | 0x80 ×6 | joystick 1 axes |
| +0x48, +0x4C | 0, 0 | joystick 1 hat / extra axes |
| +0x50, +0x54 | 0xFF, 0xFF | joystick 1 button bytes |
| +0x58..+0x7F | 0 | |

Interpretation:
- With no device, `ReadUsbJoy` sets output bytes 1 and 0x15 to 0xFF (`(bits & 1) - 1`). `UpdatePads` then ignores the USB path.
- The HLE returns exactly this block every frame.

## Effect on the docs

- `skeleton-readiness.md` risks 1, 4 and 5 are closed with these values.
- `open-questions.md`: the skeleton PCSX2 checks item is closed.
- ADR-001 working assumption ("exact work heap size the PS2 gets") now has the number. The high fill level in mission 1 is recorded there as a constraint.
