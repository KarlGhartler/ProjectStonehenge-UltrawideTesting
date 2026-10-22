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

Audio rate: SS01 has 26.7 MB of audio for 4170 pictures (139.0 s at 30 fps), i.e. ≈ 192,000 B/s = **48 kHz, 16-bit, stereo PCM**. The audio does not decode as SPU ADPCM (84 % of 16-byte blocks are invalid ADPCM). Playback path (`../subsystems/sound.md`): the blocks are streamed by SOUND.IRX with `sceSdBlockTrans` into the SPU2 core-0 sound-data input, which takes **left and right in alternating 0x200-byte blocks** (256 samples each). Checked on the data (S3): read that way, SS01 chunk 0 is twice as smooth (mean |s[i+1] − s[i]| / mean |s| = 0.06) as read as plain L/R-interleaved samples (0.12). **Confirmed** against the PCSX2 capture of the S3 batch: the port mixing it this way correlates 0.97-0.995 per channel with PCSX2, with no drift (`../port/validation.md` S3-5).

Each chunk's video is a **complete MPEG-2 sequence**: it starts with a sequence header and ends with a sequence end code (00 00 01 B7); SS01 chunk 0 holds 60 pictures in 4 GOPs. The decoder is reset per chunk (below).

**The chunks are still one stream: GOPs are open across chunk boundaries.** Only SS01 chunk 0 starts with a closed GOP. From chunk 4 on, most chunks begin `I B` or `I B B` in coding order (e.g. `I1 B0`, `I2 B0 B1`). The leading B-pictures come before the I-picture in display order and predict from the previous chunk's last reference picture. A decoder must therefore keep its reference pictures over the per-chunk reset. The port uses libmpeg2's partial reset (`mpeg2_reset(dec, 0)`, `src/sce/libmpeg.cpp`). With a full reset, those 1–2 pictures per chunk decode as garbage: flicker with yellow and green blocks about every 2 s, as in the first S3 build. Each chunk still returns exactly its own picture count (checked per chunk on SS01).

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

`StartMovie` 0x00212150: starts the stream-audio tracks, then `CreateSmvReader(buf, 0x600000, lsn, 0x2300, desc)` 0x00214130. That allocates the reader state (`AllocMovieBlock` 0x00214058 from a 0x40-aligned arena, `AllocFromMovieArena` 0x002140D0) and initialises it (`InitSmvReader` 0x00212508), which carves two 0x180000-byte chunk buffers, a 0x13B800-byte libmpeg work area, a 0x118000-byte image buffer (RGBA32 macroblocks) and a 0x20000-byte packet buffer out of the 0x600000-byte block. The arena aligns the **end** of each block, not its start (a quirk of `AllocFromMovieArena`). Field layout of the reader: `src/game/movie.cpp`.

Reader state machine (`player+0x90`):

| State | Function | Action |
|---|---|---|
| 0 | `ReadSmvHeader` 0x00212630 | read sector 0 (header) into buffer A; `sceMpegInit` 0x00270870 |
| 1 | `ReadSmvFirstChunk` 0x002126C0 | when done: read chunk 0 (`hdr.next_size` bytes) into buffer B |
| 2 | `StartSmvReader` 0x00212788 | when done and no PCM transfer runs: `sceMpegCreate(mp, work, 0x13B800)`; `sceMpegAddBs` 0x00270B58 with chunk B's video; queue chunk 1 into A. If audio: feeder ring (`SetMovieAudioBuffer(0x002BBCB8, 0x4000)`), first block into half 0, volume, `StartMovieAudio` (SOUND 0xA, loop), second block into half 1 |
| 3 | `StreamSmvReader` 0x00212960 | per frame: when the SPU2 has moved to the other ring half (`GetMovieAudioHalf`, H), refill the half it left with the next 0x4000-byte block (`sceSifSetDma` to the IOP ring at `*0x00353BF8`, half 1 at +0x4000). When the decoder has passed the chunk's sequence end (`sceMpegIsEnd` 0x00270CA8): `sceMpegReset` 0x00270CC8 + add the next chunk's video, queue the following read; `next_size == 0` → state 4. A late read or a stop request fades out (state 5) |
| 4 | `DrainSmvReader` 0x00212C78 | last chunk: audio to its end, then silence; the sequence end → state 5 |
| 5 | `FadeOutSmvReader` 0x00212DF0 | silence for more than 0.1 s (double compare), then stop the PCM transfer → 6 |
| 6 | `FinishSmvReader` 0x00212EF0 | wait for the stop (at most 2 s), release the ring → 7: returns 1, or −1 after a stop request |

Decode and display per frame: `DecodeMovieFrame` 0x002122D0 → `UpdateSmvReader` 0x002141E8 → `StepSmvReader` 0x00212F88 (states above) + `DrawMoviePicture` 0x00213048. The draw calls `sceMpegGetPicture` 0x00270B90 `(mp, image buffer, 0x460)`, which writes the next picture as RGBA32 16×16 macroblocks **column by column** (macroblock (x, y) at `(x · rows + y) · 0x400`), then sends one GIF chain: per 16-pixel column an IMAGE transfer of 16 × height pixels to VRAM block 0x2300 (REFS tags of at most 0x7FFF qwords) and a 16 × height sprite from that texture, centred in 640 × 448 plus the request offset (16:9: 0x38 lines lower).

All disc reads go through **queue 0** of the CD request system (`../game/file-loading.md`) with `chunk = -1`.

## Port notes

- Timing (PCSX2 session 5): movies are shown at 29.97 fps while the main loop runs at 59.94 Hz, so each picture is held for 2 vblanks. In the port: one picture per iteration, `MovieFrameSync`'s timer-0 busy wait lets one vblank pass (`../port/validation.md` S3-1); SS01 ends at frame 10090 after exactly 4170 pictures.
- Decoding (slice S3): `src/sce/libmpeg.cpp` implements `sceMpegInit/Create/Reset/AddBs/IsEnd/GetPicture` on libmpeg2 0.5.1 (one sequence per `AddBs`), with the IPU's integer colour conversion; 2.2 ms per picture.

- Demux: walk the chunks; concatenate `chunk[0x10 : 0x10 + video_bytes]` for the video ES and `chunk[audio_off : audio_off + audio_bytes]` for the audio.
- MIS briefing videos have no audio track of their own. Their sound comes from the VPK stream track given in the movie table.
- Audio path: SOUND.IRX block transfer into the SPU2 core-0 input (`../subsystems/sound.md`): the port mixes the ring from IOP RAM at 48 kHz with the 0x200-byte L/R blocks (confirmed against the PCSX2 capture, `../port/validation.md` S3-5). The reply's transfer status word gives the half being played (confirmed, S3-3).
