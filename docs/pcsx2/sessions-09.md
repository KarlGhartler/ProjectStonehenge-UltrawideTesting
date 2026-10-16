# PCSX2 debugging sessions: run 09 (slice S3 batch)

- Emulator: PCSX2 2.8.2, original disc image (NTSC-U, SLUS_201.52).
- Run by the owner on 2026-10-15, following `procedures.md` item S3 (now removed from there).
- All files stay outside the repo (folder `Slices/Slice3/`, paths in `CLAUDE.local.md`). Only derived values are recorded here.

## Files

Cold boot with **card S** in slot 1, one session. Buttons: on the title **Start**, then **X** (NEW GAME), then **X** (NORMAL), as in the port's pad script.

| Breakpoint | Event | Save state | `frame_count` (0x0029F3A8) | Movie page (+0x288 iterations, +0x290 time) |
|---|---|---|---|---|
| 0x00239248 | `MenuMoviePlay`, first hit | `s3_ss01_start` | 12310 | 0, 0.0 s |
| 0x00238870 | `MovieFrameSync`, first hit after `s3_ss01_b` | `s3_ss01_mid` | 13686 | 689, 22.03 s (SMV reader: 662 pictures) |
| 0x002392A8 | `MenuMovieEnd` | `s3_ss01_end` | 20714 | 0, 139.0 s (reader already freed) |

- Screenshots: `s3_ss01_a.png` (about 5 s in: star field with a trail), `s3_ss01_b.png` (about 20 s: the TV with the "LIVE" report, subtitle "And in turn how that cannon brought war upon us.").
- Sound: `s3_ss01_audio.mp4`, Media Capture with audio only, codec **FLAC** in an MP4 container, 48 kHz, stereo, 16-bit, 42.0 s from just after the first picture. Decoded with `tools/research/flac_mp4.py`.
- `s3_m1_movie.png`: after the mission 1 title card and X, a second movie starts. About 5 s in, it shows a **projector**: the briefing movie of mission 1 (slice S4).
- Owner's notes: X and Start can be pressed during SS01 to skip straight to the second movie (`frontend.md`: skip state +0x274, skippable after 30 frames).

## Results

Port comparison: `../port/validation.md` S3-3 to S3-5.
- RAM: 0 unexplained ranges at all three checkpoints with `../port/ramdiff-expected-s3.csv`.
- Pacing: from the mid checkpoint to `MenuMovieEnd`, 7028 frames in PCSX2 and in the port. Before the first picture the port is 42 frames earlier (subtitle font load and VAG stop latency; `../open-questions.md`, I/O latency).
- Answered: IPU alpha 0x80 (every pixel of the image buffer), PCM layout (0x200-byte L/R blocks), the audio half reported by SOUND (reader +0x70 equal mid-movie). Still open, without game effect: `sceMpeg.frameCount` (PCSX2 46, port 47 in the same chunk) and the `flags` word (PCSX2 0x19B).
