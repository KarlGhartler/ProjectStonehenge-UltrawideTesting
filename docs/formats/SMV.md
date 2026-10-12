# SMV movie container (`MIS/*.SMV`, `SMV/*.SMV`)

Layout derived from the movie stream reader (0x00212630–0x00212960) and checked against all 31 files with `tools/research/smv_walk.py`: every file walks cleanly.

## Identification

No magic. Files are opened by file id from `g_CdFileTable` (ids 6–37), through the movie table at 0x002BBA10. A raw MPEG-2 sequence header `00 00 01 B3` sits at file offset 0x810.

## Layout — confirmed

```
0x000  file header sector (0x800 bytes)
         u32 next_size      size in bytes of chunk 0 (sector multiple)
         0x7FC zero bytes
0x800  chunk 0  (next_size bytes)
       chunk 1
       ...      a chunk whose header next_size == 0 is the last one
```

Chunk header (16 bytes at the start of every chunk):

| Off | Type | Field | Used by |
|---|---|---|---|
| 0x0 | u32 | `next_size`: byte size of the **following** chunk (multiple of 0x800), 0 = this is the last chunk | 0x002126C0, 0x00212788, 0x00212960: `QueueCdRead(…, next_size >> 11 sectors)` |
| 0x4 | u32 | `video_bytes`: MPEG-2 video elementary-stream bytes, starting at chunk + 0x10 | `sceMpegSetImageBuff(mpeg, chunk + 0x10, video_bytes)` (H) 0x00270B58 (adds the bitstream) |
| 0x8 | u32 | `audio_off`: offset of the audio data from the chunk start (= `0x10 + video_bytes` rounded up to 16) | `audio = chunk + ((audio_off - 0x10) & ~0xF) + 0x10` |
| 0xC | u32 | `audio_bytes`: audio size, a multiple of 0x4000 (0 in MIS files) | `audio_bytes >> 14` = number of 0x4000-byte audio blocks |

- Reading is sector-based. The file's own header sector gives chunk 0's size. Each chunk header then gives the size of the next chunk, so the reader always knows how many sectors to request for the read-ahead.
- **The last chunk's size is rounded up to a whole sector, but the file is not padded.** The final read therefore runs up to 0x7FF bytes past EOF (onto the disc's sector padding). All data that is actually used (`0x10 + video_bytes`, `audio_off + audio_bytes`) lies inside the file. A PC port must clamp the last read or zero-fill.
- Every chunk is ≤ 0x180000 bytes (the size of each reader buffer).
- Chunk 0's video begins with a sequence header. Video continues across chunks as one elementary stream.

Measured (walker):

| File group | Chunks | Video | Audio |
|---|---|---|---|
| MIS01–MIS18 | 12–27 | 640×448, 30 fps, MPEG-2 (sequence extension present), 4.6 Mbit/s | none (`audio_bytes = 0`) |
| INTRO | 31 | 640×448, 29.97 fps | 11.5 MB |
| SS01–SS12 | 18–75 | 640×336 (letterboxed), 30 fps | in every chunk |
| STAFF | 131 | 640×448, 29.97 fps | 50 MB |

Audio rate: SS01 has 26.7 MB of audio for 79.9 MB of video at 4.6 Mbit/s ≈ 139 s, i.e. ≈ 192,000 B/s = **48 kHz, 16-bit, stereo PCM** (H). The audio does not decode as SPU ADPCM (84 % of 16-byte blocks are invalid ADPCM). Playback path (`../subsystems/iop-sound.md`): the blocks are streamed by SOUND.IRX with `sceSdBlockTrans` into the SPU2 core-0 sound-data input. That input expects 48 kHz 16-bit stereo with left/right alternating in 0x200-byte blocks (standard SPU2 behaviour, H until checked against a PCSX2 SPU2 capture).

## Player (code)

Movie table `0x002BBA10`, 32 × 0x14 bytes:

| Off | Field |
|---|---|
| 0x00 | file id (`GetCdFileLsn`) |
| 0x04 | 1 = video only (MIS files) → the player's audio feed is disabled (`player+0x84 = flag & 1`) |
| 0x08 | stream-audio track for slot `flags & 1` (`RequestStreamPlay` (H) 0x0021FD40), −1 = none |
| 0x0C | stream-audio track for slot 2/4 (`func_0021FC80`) — MIS movies use tracks 2..0x13 |
| 0x10 | argument for that track, −1 = none |

| Id | File |
|---|---|
| 0 | INTRO |
| 1–12 | SS01–SS12 |
| 13–30 | MIS01–MIS18 (with a separate stream-audio track); briefing of campaign mission 1–18 (`StartMenuMovie(n − 1 + 0xD)`, `../subsystems/frontend.md`) |
| 31 | STAFF |

`MovieStart` (H) 0x00212150: starts the stream-audio tracks, then `func_00214130(buf, 0x600000, lsn, 0x2300, desc)`. That allocates the reader state (`func_00214058`) and initialises it (0x00212508), which carves two 0x180000-byte chunk buffers, a 0x13B800-byte libmpeg work area, a 0x118000-byte image buffer and a 0x20000-byte buffer out of the 0x600000-byte block.

Reader state machine (`player+0x90`):

| State | Function | Action |
|---|---|---|
| 0 | 0x00212630 | read sector 0 (header) into buffer A; `sceMpegInit` (H) 0x00270870 |
| 1 | 0x002126C0 | when done: read chunk 0 (`hdr.next_size` bytes) into buffer B |
| 2 | 0x00212788 | when done: `sceMpegCreate(mp, work, 0x13B800)`; add video of chunk B; queue chunk 1 into A. If audio: init the audio feeder (`func_00213CE8(0x002BBCB8, 0x4000)`), DMA the first block, set volume, DMA the second block |
| 3 | 0x00212960 | per frame: feed audio blocks (ping-pong IOP slots, `sceSifSetDma` to IOP ring at `*0x00353BF8`, slot 1 at +0x4000) and swap chunk buffers when the next chunk has arrived. When libmpeg reports the bitstream used up (`sceMpegIsEnd` (H) 0x00270CA8), `sceMpegReset` (H) 0x00270CC8 + add the next chunk's video, and queue the following read. `next_size == 0` → state 4 |
| 4/5/7 | | end / fade-out / error |

Decode and display per frame: `MovieDecodeFrame` (H) 0x002122D0 → `func_002141E8` → `func_00212F88` (decode) + `func_00213048` (draw).

All disc reads go through **queue 0** of the CD request system (`../game/file-loading.md`) with `chunk = -1`.

## Port notes

- Timing (PCSX2 session 5): movies are shown at 29.97 fps while the main loop runs at 59.94 Hz, so each picture is held for 2 vblanks. Pace frames by game ticks.

- Demux: walk the chunks; concatenate `chunk[0x10 : 0x10 + video_bytes]` for the video ES and `chunk[audio_off : audio_off + audio_bytes]` for the audio.
- MIS briefing videos have no audio track of their own. Their sound comes from the VPK stream track given in the movie table.
- Audio path: SOUND.IRX block transfer into the SPU2 core-0 input (`../subsystems/iop-sound.md`). Confirm the 0x200-byte L/R interleave with a PCSX2 SPU2 capture before writing the audio path.
