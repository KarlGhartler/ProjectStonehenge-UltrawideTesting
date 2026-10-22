// SMV movie player (docs/formats/SMV.md, slice S3): movie start and per-frame step, the SMV reader
// state machine (sector reads through CD queue 0, chunk double buffer, libmpeg bitstream per
// chunk), the picture upload (one IMAGE transfer per 16-pixel column + one sprite each), the PCM
// audio feeder (two 0x4000-byte halves in IOP RAM played by the SPU2 core-0 sound-data input) and
// the movie arena allocator.
//
// Movie request object (*(g_GameState + 0x20)): +0 movie id (table 0x002BBA10, 32 x 0x14), +4/+8
// picture offset x/y, +0xC flags (bit 0 video only, from the table; bit 1 stop requested), +0x10
// f32 volume, +0x14 f32 fade-out time, +0x18 pictures shown, +0x1C 6 MiB work buffer, +0x20 image
// buffer (0 = carve one from the work buffer).
//
// Reader (0x40C0 bytes, *0x00410C60): +0 chunk buffer A, +4 chunk buffer B, +8/+0xC the two buffers
// as an array, +0x10 sceMpeg (0x44 bytes), +0x58 libmpeg work (0x13B800), +0x5C image buffer
// (0x118000, RGBA32 macroblocks), +0x60 packet buffer (0x20000), +0x68 decoding buffer index,
// +0x6C next LSN, +0x70 audio half to fill next, +0x74 audio block in the chunk, +0x78 audio chunk
// index, +0x7C file LSN, +0x80 f32 volume, +0x84 video only, +0x88 LSN of the file start, +0x8C read
// ticket, +0x90 state, +0x94 pictures drawn, +0x98/+0x9C picture offset, +0xA0 stopping, +0xA4 f32
// fade time, +0xA8 f32 fade length, +0xAC f32 fade-out time of the request, +0xC0 0x4000 bytes of
// silence.
//
// Audio feeder (0x002BBCB8): +0 volume, +4 transfer running, +8 IOP ring address, +0xC half size,
// +0x10 SIF DMA id, +0x14 fading, +0x18 target volume, +0x1C f32 fade time, +0x20 f32 fade length,
// +0x24 SOUND ticket.
#include "game/game.h"

#include "common/ee_math.h"
#include "common/guest_stack.h"
#include "sce/ee_hw.h"
#include "sce/sce.h"

namespace ac04::game {

using guest::GRef;

namespace {

AC04_GUEST_VAR(u32, g_MovieAudioStarted, 0x00410C58)  // gp-0x4D98: 0 none, 1 started, 2 playing
AC04_GUEST_VAR(u32, g_MovieLsn, 0x00410C54)
AC04_GUEST_VAR(u32, g_SmvReader, 0x00410C60)
AC04_GUEST_VAR(u32, g_MovieImageDbp, 0x00410C64)
AC04_GUEST_VAR(u32, g_pMenuBgm, 0x00410E90)

constexpr u32 kMovieTable = 0x002BBA10;
constexpr u32 kAudio = 0x002BBCB8;
constexpr u32 kVagSlots = 0x00353BF8;  // VAG.IRX client slots, 0x94 bytes each

u32 MovieEntry(s32 id, u32 field) { return GRef<u32>(kMovieTable + static_cast<u32>(id) * 0x14 + field * 4); }
s32& SlotPlaying(u32 slot) { return GRef<s32>(kVagSlots + slot * 0x94 + 0x58); }

// Chunk buffers as an array (+8 / +0xC).
u32 Chunk(u32 r, u32 i) { return GRef<u32>(r + 8 + i * 4); }
// Start of the audio data of `chunk` (header +8 = audio offset, rounded to 16 from +0x10).
u32 AudioBlock(u32 chunk, u32 extra) { return chunk + ((GRef<u32>(chunk + 8) + extra - 0x10) & ~0xFu) + 0x10; }

}  // namespace

// ---- Scenes 1 and 3 (dead: no scene request ever names them; docs/port/dead-code.md) ------------

// EE: 0x00211C58
// Scene 1 handler: stores mission id (kind 0, entry 1) in game state +0x27 and enters menu page 4.
void SceneMenuEntry() {
    const s16 id = GetMissionId(0, 1);
    GRef<u8>(g_GameState().addr + 0x27) = static_cast<u8>(id);
    EnterMenu(4);
}

// EE: 0x00211C88
// Dead (no reference): InitMoviePlayback, returns 1.
s32 func_00211C88() {
    InitMoviePlayback();
    return 1;
}

// EE: 0x00211D48
// Scene 3 sub 0: 6 MiB movie work buffer from the work heap into the request (+0x1C, pictures shown
// +0x18 = 0); 640x448 display, draw mode 2, display flags 0x8100, frame buffer 0 shown and drawn.
void SceneMovieSetup() {
    const u32 work = WorkHeapAlloc(0x600000, 0x40);
    MainContext* ctx = Ctx();
    const u32 desc = GRef<u32>(g_GameState().addr + 0x20);
    ctx->disp_w = 0x280;
    GRef<u32>(desc + 0x1C) = work;
    GRef<u32>(desc + 0x18) = 0;
    ctx->clear_rgba[0] = 0;
    ctx->disp_h = 0x1C0;
    ctx->draw_w = 0x280;
    ctx->draw_h = 0x1C0;
    Ctx()->clear_rgba[1] = 0;
    Ctx()->clear_rgba[2] = 0;
    Ctx()->clear_rgba[3] = 0;
    Ctx()->frame_psm = 0;
    Ctx()->zbuf_psm = 0x31;
    Ctx()->ztest = 1;
    Ctx()->draw_mode = 2;
    Ctx()->disp_flags = 0x8100;
    BuildDrawEnvPackets();
    ctx = Ctx();
    const u64 frame = static_cast<u64>(ctx->draw_w >> 6) << 16 | static_cast<u64>(ctx->frame_psm) << 24;
    ctx->sub += 1;
    ctx->dispenv[2] &= 0xFFFFFFFFFFFFFE00ull;
    ctx->drawenv_pkt[2] = frame;
    ctx->drawenv_pkt[18] = frame;
    ctx->drawenv_pkt[4] |= 0x100000000ull;
    ctx->drawenv_pkt[20] |= 0x100000000ull;
}

// EE: 0x00211E68
// Scene 3 sub 1: work-heap mark, timer 0 counting H-blanks, the movie playback display (draw mode
// 0, display flags 0xC000, pictures drawn into frame buffer 0x8C while 0 is shown), starts the
// request's movie and waits for an odd field.
s32 SceneMovieStart() {
    PushWorkHeapMark();
    sce::EeHwWrite(sce::EeReg::kT0Mode, 0x83);
    MainContext* ctx = Ctx();
    ctx->clear_rgba[0] = 0;
    ctx->disp_h = 0x1C0;
    ctx->draw_h = 0x1C0;
    ctx->disp_w = 0x280;
    ctx->draw_w = 0x280;
    Ctx()->clear_rgba[1] = 0;
    Ctx()->clear_rgba[2] = 0;
    Ctx()->clear_rgba[3] = 0;
    Ctx()->frame_psm = 0;
    Ctx()->zbuf_psm = 0x31;
    Ctx()->ztest = 1;
    Ctx()->draw_mode = 0;
    Ctx()->disp_flags = 0xC000;
    BuildDrawEnvPackets();
    ctx = Ctx();
    const u64 frame = static_cast<u64>(ctx->draw_w >> 6) << 16 | static_cast<u64>(ctx->frame_psm) << 24 | 0x8C;
    ctx->dispenv[2] &= 0xFFFFFFFFFFFFFE00ull;
    ctx->drawenv_pkt[20] |= 0x100000000ull;
    ctx->drawenv_pkt[2] = frame;
    ctx->drawenv_pkt[18] = frame;
    ctx->drawenv_pkt[4] |= 0x100000000ull;
    StartMovie(GRef<u32>(g_GameState().addr + 0x20));
    while (sceGsSyncV(0) != 0) {
    }
    Ctx()->sub += 1;
    return 0;
}

// EE: 0x00211FB0
// Scene 3 sub 2: one movie iteration (timer 0 reset, decode, PCM fade), waits until timer 0 has
// counted 300 H-blanks, next sub at the end of the movie; swaps the displayed and the drawn frame
// buffer (0 <-> 0x8C).
s32 SceneMoviePlay() {
    ResetMovieTimer();
    const u32 desc = GRef<u32>(g_GameState().addr + 0x20);
    const s32 done = DecodeMovieFrame(desc, (GRef<u32>(desc + 0xC) & 2) != 0 ? 1 : 0);
    UpdateMovieAudioFade();
    MainContext* ctx = Ctx();
    while (sce::EeHwRead(sce::EeReg::kT0Count) < 300) sce::EeHwWaitTimer();
    if (done != 0) ctx->sub += 1;
    const u64 disp = ctx->dispenv[2];
    const u16 w = ctx->draw_w;
    u64 frame;
    if ((disp & 0x1FF) == 0x8C) {
        ctx->dispenv[2] = disp & 0xFFFFFFFFFFFFFE00ull;
        frame = static_cast<u64>(w >> 6) << 16 | static_cast<u64>(ctx->frame_psm) << 24 | 0x8C;
    } else {
        ctx->dispenv[2] = (disp & 0xFFFFFFFFFFFFFE00ull) | 0x8C;
        frame = static_cast<u64>(w >> 6) << 16 | static_cast<u64>(ctx->frame_psm) << 24;
    }
    ctx->drawenv_pkt[2] = frame;
    ctx->drawenv_pkt[18] = frame;
    return 0;
}

// EE: 0x002120D8
// Scene 3 sub 3: clears the request, display flags 0x200, pops the work-heap mark and sets request
// bit 0x40.
void SceneMovieEnd() {
    ClearMovieRequest(GRef<u32>(g_GameState().addr + 0x20));
    Ctx()->disp_flags = 0x200;
    PopWorkHeapMark();
    SetRequestFlags(0x40);
}

// EE: 0x00211CA8
// Scene 3 handler: pad 0 newly-pressed bit 0x800 requests the movie stop (fade 1.0); then sub 0-3.
void SceneMovie() {
    if ((GRef<u32>(g_pMainContext().addr + 0x218) & 0x800) != 0) RequestMovieStop(1.0f);
    switch (Ctx()->sub) {
        case 0: SceneMovieSetup(); break;
        case 1: SceneMovieStart(); break;
        case 2: SceneMoviePlay(); break;
        case 3: SceneMovieEnd(); break;
        default: break;
    }
}

AC04_GUEST_FUNCTION(0x00211C58, SceneMenuEntry);
AC04_GUEST_FUNCTION(0x00211CA8, SceneMovie);

// EE: 0x00212150
// Starts movie `desc`: the stream-audio tracks of its table entry (once per movie: separate audio
// for the MIS briefing movies), the file's LSN and a reader over the 6 MiB work buffer with the
// picture at VRAM block 0x2300; reverb depths off.
void StartMovie(u32 desc) {
    const s32 id = GRef<s32>(desc);
    const u32 track = MovieEntry(id, 2);
    const u32 track2 = MovieEntry(id, 3);
    const u32 video_only = MovieEntry(id, 1);
    const u32 arg2 = MovieEntry(id, 4);
    GRef<u32>(desc + 0xC) = video_only;
    GRef<u32>(desc + 0x10) = video_only == 0 ? 0x3F800000u : 0u;
    GRef<u32>(desc + 0x14) = 0;
    GRef<u32>(desc + 0x18) = 0;
    if (g_MovieAudioStarted() == 0) {
        const u32 slot = GRef<u32>(g_pMenuBgm()) & 1;
        const u32 slot2 = slot == 0 ? 4u : 2u;
        if (track != 0xFFFFFFFFu) {
            SlotPlaying(slot) = 1;
            func_0021FD40(slot, static_cast<u16>(track));
            g_MovieAudioStarted() = 1;
        }
        if (arg2 != 0xFFFFFFFFu) {
            SlotPlaying(slot2) = 1;
            RequestStreamPlayArg(slot2, static_cast<u16>(track2), static_cast<u16>(arg2));
            g_MovieAudioStarted() = 1;
        }
    }
    const u32 lsn = GetCdFileLsn(static_cast<s32>(MovieEntry(id, 0)));
    g_MovieLsn() = lsn;
    CreateSmvReader(GRef<u32>(desc + 0x1C), 0x600000, lsn, 0x2300, desc);
    ClearReverbDepths();
}

// EE: 0x002122C0
// Resets EE timer 0 (H-blank count) at the start of a movie iteration.
void ResetMovieTimer() { sce::EeHwWrite(sce::EeReg::kT0Count, 0); }

// EE: 0x002122D0
// One movie iteration for request `desc` (`stop`: a stop was requested): reader step and picture
// upload, the picture count into desc+0x18; at the end the stream-audio slots are stopped. While
// the reader streams, the stream audio's volume is set once (state 1 -> 2 -> done). Returns 0
// while playing, non-zero at the end (-1 without a reader).
s32 DecodeMovieFrame(u32 desc, s64 stop) {
    const u32 slot = GRef<u32>(g_pMenuBgm()) & 1;
    const u32 slot2 = slot == 0 ? 4u : 2u;
    if (g_SmvReader() == 0) return -1;
    GRef<u32>(g_SmvReader() + 0xAC) = GRef<u32>(desc + 0x14);
    const s32 r = UpdateSmvReader(stop);
    if (r == 0) {
        GRef<u32>(desc + 0x18) = GRef<u32>(g_SmvReader() + 0x94);
    } else {
        g_MovieAudioStarted() = 0;
        if (SlotPlaying(slot) != 0) {
            SlotPlaying(slot) = 0;
            func_0021FDF8(static_cast<u8>(1u << slot));
        }
        if (SlotPlaying(slot2) != 0) {
            SlotPlaying(slot2) = 0;
            func_0021FDF8(static_cast<u8>(1u << slot2));
        }
    }
    if (g_MovieAudioStarted() == 1 && GRef<s32>(g_SmvReader() + 0x90) == 3) {
        g_MovieAudioStarted() = 2;
    } else if (g_MovieAudioStarted() == 2) {
        const s32 id = GRef<s32>(desc);
        const u32 arg2 = MovieEntry(id, 4);
        if (MovieEntry(id, 2) != 0xFFFFFFFFu) {
            SlotPlaying(slot) = 0;
            func_0021F190(0.0f, static_cast<s32>(slot), 0xFF, 0xFF, 1);
        }
        if (arg2 != 0xFFFFFFFFu) {
            SlotPlaying(slot2) = 0;
            func_0021F190(0.0f, static_cast<s32>(slot2), 0xFF, 0xFF, 1);
        }
        g_MovieAudioStarted() = 0;
    }
    return r;
}

// EE: 0x00212498
void UpdateMovieAudioFade() { StepMovieAudioVolume(kAudio); }

// EE: 0x002124A8
// Clears the request's image buffer and flags and resets the SOUND voices (movie end).
void ClearMovieRequest(u32 desc) {
    GRef<u32>(desc + 0x20) = 0;
    GRef<u32>(desc + 0xC) = 0;
    func_0021A680();
}

// EE: 0x002124C8
// Requests the end of the current movie with a fade-out of `time` seconds.
void RequestMovieStop(f32 time) {
    const u32 desc = GRef<u32>(g_GameState().addr + 0x20);
    GRef<f32>(desc + 0x14) = time;
    GRef<u32>(desc + 0xC) |= 2;
}

// EE: 0x002124B8
// Dead (no reference): sets the movie id of the request.
void SetMovieRequestId(s32 id) { GRef<s32>(GRef<u32>(g_GameState().addr + 0x20)) = id; }

// EE: 0x002124E8
// Dead (no reference): pictures drawn by the reader (+0x94).
u32 GetMoviePicturesDrawn() { return GRef<u32>(g_SmvReader() + 0x94); }

// EE: 0x00212508
// Carves the reader's buffers out of `arena` and initialises it for the file at `lsn`.
s32 InitSmvReader(u32 r, u32 arena, u32 lsn, u32 desc) {
    GRef<u32>(r) = AllocFromMovieArena(arena, 0x180000);
    const u32 b = AllocFromMovieArena(arena, 0x180000);
    GRef<u32>(r + 0xC) = b;
    GRef<u32>(r + 8) = GRef<u32>(r);
    GRef<u32>(r + 4) = b;
    GRef<u32>(r + 0x58) = AllocMovieWork(0x13B800, arena);
    const u32 image = GRef<u32>(desc + 0x20);
    GRef<u32>(r + 0x5C) = image == 0 ? AllocFromMovieArena(arena, 0x118000) : image;
    const u32 packets = AllocFromMovieArena(arena, 0x20000);
    GRef<u32>(r + 0x90) = 0;
    GRef<u32>(r + 0x94) = 0;
    GRef<u32>(r + 0x68) = 0;
    GRef<u32>(r + 0x6C) = 0;
    GRef<u32>(r + 0x70) = 0;
    GRef<u32>(r + 0x74) = 0;
    GRef<u32>(r + 0x78) = 0;
    GRef<u32>(r + 0xA4) = 0;
    GRef<u32>(r + 0xA8) = 0;
    const u32 flags = GRef<u32>(desc + 0xC);
    const u32 volume = GRef<u32>(desc + 0x10);
    GRef<u32>(r + 0x7C) = lsn;
    GRef<u32>(r + 0xA0) = 0;
    GRef<u32>(r + 0x60) = packets;
    GRef<u32>(r + 0x84) = flags & 1;
    GRef<u32>(r + 0x80) = volume;
    ee::memset(r + 0xC0, 0, 0x4000);
    GRef<u32>(r + 0x8C) = 0;
    GRef<u32>(r + 0x88) = 0xFFFFFFFFu;
    GRef<u32>(r + 0x98) = GRef<u32>(desc + 4);
    GRef<u32>(r + 0x9C) = GRef<u32>(desc + 8);
    return 0;
}

// EE: 0x00212630
// State 0: reads the header sector into buffer A and initialises libmpeg (a stop ends at once).
s32 ReadSmvHeader(u32 r, s64 stop) {
    if (stop != 0) {
        GRef<u32>(r + 0x90) = 7;
        GRef<u32>(r + 0xA0) = 1;
        return -1;
    }
    StopMovieAudioIfRunning(kAudio);
    GRef<u32>(r + 0x88) = GRef<u32>(r + 0x7C);
    GRef<u32>(r + 0x8C) = QueueCdRead(0, GRef<u32>(r + 0x88), 0, GRef<u32>(r), 0, 1, -1);
    GRef<u32>(r + 0x6C) = GRef<u32>(r + 0x88) + 1;
    sceMpegInit();
    GRef<u32>(r + 0x90) = 1;
    return 0;
}

// EE: 0x002126C0
// State 1: when the header is in, reads chunk 0 (size from the header) into buffer B.
s32 ReadSmvFirstChunk(u32 r, s64 stop) {
    StopMovieAudioIfRunning(kAudio);
    if (stop != 0) GRef<u32>(r + 0xA0) = 1;
    if (GetCdDoneTicket(0) < GRef<u32>(r + 0x8C)) return 0;
    if (GRef<u32>(r + 0xA0) != 0) {
        GRef<u32>(r + 0x90) = 7;
        return -1;
    }
    const u32 size = GRef<u32>(Chunk(r, 0));
    GRef<u32>(r + 0x8C) = QueueCdRead(0, GRef<u32>(r + 0x88), 0, GRef<u32>(r + 4),
                                      GRef<u32>(r + 0x6C) - GRef<u32>(r + 0x88), static_cast<s32>(size >> 11), -1);
    GRef<u32>(r + 0x90) = 2;
    GRef<u32>(r + 0x6C) += GRef<u32>(Chunk(r, 0)) >> 11;
    return 0;
}

// EE: 0x00212788
// State 2: when chunk 0 is in and no PCM transfer runs: creates the decoder with chunk 0's video,
// reads chunk 1 into buffer A and (with audio) primes the feeder: the first block into half 0, the
// volume, the transfer start, the second block into half 1.
s32 StartSmvReader(u32 r, s64 stop) {
    StopMovieAudioIfRunning(kAudio);
    if (stop != 0) GRef<u32>(r + 0xA0) = 1;
    if (GetCdDoneTicket(0) < GRef<u32>(r + 0x8C)) return 0;
    if (IsMovieAudioIdle(kAudio) == 0) return 0;
    if (GRef<u32>(r + 0xA0) != 0) {
        GRef<u32>(r + 0x90) = 7;
        return -1;
    }
    const u32 chunk = GRef<u32>(r + 0xC);
    sceMpegCreate(r + 0x10, GRef<u32>(r + 0x58), 0x13B800);
    sceMpegAddBs(r + 0x10, chunk + 0x10, GRef<s32>(chunk + 4));
    GRef<u32>(r + 0x8C) = QueueCdRead(0, GRef<u32>(r + 0x88), 0, GRef<u32>(r), GRef<u32>(r + 0x6C) - GRef<u32>(r + 0x88),
                                      static_cast<s32>(GRef<u32>(chunk) >> 11), -1);
    GRef<u32>(r + 0x68) = 1;
    GRef<u32>(r + 0x6C) += GRef<u32>(GRef<u32>(r + 0xC)) >> 11;
    if (GRef<u32>(r + 0x84) == 0) {
        SetMovieAudioBuffer(kAudio, 0x4000);
        SendMovieAudioBlock(kAudio, AudioBlock(GRef<u32>(r + 0xC), 0), 0);
        GRef<u32>(r + 0x70) = 0;
        for (s32 i = 0; i <= 9999; ++i) {
            if (IsMovieAudioDmaBusy(kAudio) == 0) break;
        }
        SetMovieAudioVolume(GRef<f32>(r + 0x80), K(0x0040FF80), kAudio);
        StartMovieAudio(kAudio);
        SendMovieAudioBlock(kAudio, AudioBlock(GRef<u32>(r + 0xC), 0x4000), 1);
    }
    GRef<u32>(r + 0x90) = 3;
    GRef<u32>(r + 0x78) = 1;
    GRef<u32>(r + 0x74) = 1;
    return 0;
}

// EE: 0x00212960
// State 3, per frame: a stop request starts the fade-out (state 5 when it ends). With audio, when
// the SPU2 has moved to the other ring half, the half it left gets the next 0x4000-byte block of
// the chunk (or of the next chunk once it has arrived; if not, silence and the fade-out). When the
// decoder has reached the chunk's sequence end: if the next chunk is in, swap to it, add its video
// and read the one after (a chunk with next size 0 is the last: state 4); if not, fade out.
s32 StreamSmvReader(u32 r, s64 stop) {
    if (stop != 0 && GRef<u32>(r + 0xA0) == 0) {
        GRef<u32>(r + 0xA4) = 0;
        GRef<u32>(r + 0xA0) = 1;
        GRef<f32>(r + 0xA8) = GRef<f32>(r + 0xAC);
        SetMovieAudioVolume(GRef<f32>(r + 0xA4), GRef<f32>(r + 0xAC), kAudio);
    }
    if (GRef<u32>(r + 0xA0) != 0) {
        const f32 t = fpu::Add(GRef<f32>(r + 0xA4), K(0x0040FF84));
        GRef<f32>(r + 0xA4) = t;
        if (GRef<f32>(r + 0xA8) <= t) {
            GRef<u32>(r + 0xA4) = 0;
            GRef<u32>(r + 0x90) = 5;
            return 0;
        }
    }
    if (GRef<u32>(r + 0x84) == 0 && GetMovieAudioHalf(kAudio) >= 0 &&
        GetMovieAudioHalf(kAudio) != GRef<s32>(r + 0x70)) {
        const s32 block = GRef<s32>(r + 0x74) + 1;
        u32 chunk = Chunk(r, GRef<u32>(r + 0x78));
        GRef<s32>(r + 0x74) = block;
        u32 src;
        if (block < static_cast<s32>(GRef<u32>(chunk + 0xC) >> 14)) {
            src = AudioBlock(chunk, static_cast<u32>(block) << 14);
        } else if (GetCdDoneTicket(0) < GRef<u32>(r + 0x8C)) {
            GRef<u32>(r + 0xA4) = 0;
            GRef<u32>(r + 0x90) = 5;
            SetMovieAudioVolume(GRef<f32>(r + 0xA4), K(0x0040FF88), kAudio);
            src = r + 0xC0;
        } else {
            GRef<u32>(r + 0x74) = 0;
            const s32 next = (GRef<s32>(r + 0x78) + 1) % 2;
            GRef<s32>(r + 0x78) = next;
            chunk = Chunk(r, static_cast<u32>(next));
            src = AudioBlock(chunk, 0);
        }
        SendMovieAudioBlock(kAudio, src, GRef<s32>(r + 0x70));
        GRef<s32>(r + 0x70) = (GRef<s32>(r + 0x70) + 1) % 2;
    }
    if (sceMpegIsEnd(r + 0x10) == 0) return 0;
    if (GetCdDoneTicket(0) < GRef<u32>(r + 0x8C)) {
        GRef<u32>(r + 0xA4) = 0;
        GRef<u32>(r + 0x90) = 5;
        SetMovieAudioVolume(GRef<f32>(r + 0xA4), K(0x0040FF8C), kAudio);
        return 0;
    }
    GRef<s32>(r + 0x68) = (GRef<s32>(r + 0x68) + 1) % 2;
    sceMpegReset(r + 0x10);
    const u32 chunk = Chunk(r, GRef<u32>(r + 0x68));
    sceMpegAddBs(r + 0x10, chunk + 0x10, GRef<s32>(chunk + 4));
    const u32 next_size = GRef<u32>(Chunk(r, GRef<u32>(r + 0x68)));
    if (next_size == 0) {
        GRef<u32>(r + 0x90) = 4;
        return 0;
    }
    const u32 other = GRef<u32>(r + ((GRef<u32>(r + 0x68) + 1) % 2) * 4);
    GRef<u32>(r + 0x8C) = QueueCdRead(0, GRef<u32>(r + 0x88), 0, other, GRef<u32>(r + 0x6C) - GRef<u32>(r + 0x88),
                                      static_cast<s32>(next_size >> 11), -1);
    GRef<u32>(r + 0x6C) += GRef<u32>(Chunk(r, GRef<u32>(r + 0x68))) >> 11;
    return 0;
}

// EE: 0x00212C78
// State 4 (last chunk): as state 3 without reads; audio runs to the end of the chunk, then
// silence; the sequence end of the last chunk (or the stop fade) goes to state 5.
s32 DrainSmvReader(u32 r, s64 stop) {
    if (stop != 0 && GRef<u32>(r + 0xA0) == 0) {
        GRef<u32>(r + 0xA0) = 1;
        GRef<f32>(r + 0xA8) = GRef<f32>(r + 0xAC);
        GRef<u32>(r + 0xA4) = 0;
        if (GRef<u32>(r + 0x84) == 0) SetMovieAudioVolume(GRef<f32>(r + 0xA4), GRef<f32>(r + 0xAC), kAudio);
    }
    if (GRef<u32>(r + 0xA0) != 0) {
        const f32 t = fpu::Add(GRef<f32>(r + 0xA4), K(0x0040FF90));
        GRef<f32>(r + 0xA4) = t;
        if (GRef<f32>(r + 0xA8) <= t) {
            GRef<u32>(r + 0xA4) = 0;
            GRef<u32>(r + 0x90) = 5;
        }
    }
    if (GRef<u32>(r + 0x84) == 0 && GetMovieAudioHalf(kAudio) >= 0 &&
        GetMovieAudioHalf(kAudio) != GRef<s32>(r + 0x70)) {
        const s32 block = GRef<s32>(r + 0x74) + 1;
        const u32 chunk = Chunk(r, GRef<u32>(r + 0x78));
        GRef<s32>(r + 0x74) = block;
        if (block < static_cast<s32>(GRef<u32>(chunk + 0xC) >> 14)) {
            SendMovieAudioBlock(kAudio, AudioBlock(chunk, static_cast<u32>(block) << 14), GRef<s32>(r + 0x70));
        } else {
            SendMovieAudioBlock(kAudio, r + 0xC0, GRef<s32>(r + 0x70));
        }
        GRef<s32>(r + 0x70) = (GRef<s32>(r + 0x70) + 1) % 2;
    }
    if (sceMpegIsEnd(r + 0x10) != 0) {
        GRef<u32>(r + 0xA4) = 0;
        GRef<u32>(r + 0x90) = 5;
    }
    return 0;
}

// EE: 0x00212DF0
// State 5: silence into the ring for more than 0.1 s, then (when no read is pending) the PCM
// transfer stops: state 6. Video-only movies go to state 6 at once.
s32 FadeOutSmvReader(u32 r) {
    if (GRef<u32>(r + 0x84) != 0) {
        GRef<u32>(r + 0xA4) = 0;
        GRef<u32>(r + 0x90) = 6;
        return 0;
    }
    if (GetMovieAudioHalf(kAudio) >= 0 && GetMovieAudioHalf(kAudio) != GRef<s32>(r + 0x70)) {
        SendMovieAudioBlock(kAudio, r + 0xC0, GRef<s32>(r + 0x70));
        GRef<s32>(r + 0x70) = (GRef<s32>(r + 0x70) + 1) % 2;
    }
    const f32 t = fpu::Add(GRef<f32>(r + 0xA4), K(0x0040FF94));
    GRef<f32>(r + 0xA4) = t;
    // The original compares in double precision (__extendsfdf2 + __gtdf2 against the double 0.1 at
    // 0x00405A30); widening a float is exact, so host doubles give the same result.
    if (static_cast<double>(t) > GRef<double>(0x00405A30) && GetCdDoneTicket(0) >= GRef<u32>(r + 0x8C)) {
        StopMovieAudio(kAudio);
        GRef<u32>(r + 0xA4) = 0;
        GRef<u32>(r + 0x90) = 6;
    }
    return 0;
}

// EE: 0x00212EF0
// State 6: waits until the transfer has stopped (at most 2 s), releases the PCM ring and ends
// (state 7). Returns 1 at the end of a played movie, -1 after a stop request.
s32 FinishSmvReader(u32 r) {
    if (GRef<u32>(r + 0x84) == 0) {
        GRef<f32>(r + 0xA4) = fpu::Add(GRef<f32>(r + 0xA4), K(0x0040FF98));
        if (GetMovieAudioHalf(kAudio) >= 0 && GRef<f32>(r + 0xA4) < 2.0f) return 0;
        SetMovieAudioBuffer(kAudio, 0);
    }
    GRef<u32>(r + 0x90) = 7;
    return GRef<u32>(r + 0xA0) != 0 ? -1 : 1;
}

// EE: 0x00212F88
// Reader state dispatch (`stop`: a stop was requested). 0 while running, 1 at the end, -1 stopped.
s32 StepSmvReader(u32 r, s64 stop) {
    switch (GRef<u32>(r + 0x90)) {
        case 0: return ReadSmvHeader(r, stop);
        case 1: return ReadSmvFirstChunk(r, stop);
        case 2: return StartSmvReader(r, stop);
        case 3: return StreamSmvReader(r, stop);
        case 4: return DrainSmvReader(r, stop);
        case 5: return FadeOutSmvReader(r);
        case 6: return FinishSmvReader(r);
        case 7: return 1;
        default: return 0;
    }
}

// ---- Picture upload --------------------------------------------------------------------------
// Packet builder (0x24 bytes, on the stack of DrawMoviePicture): +0 buffer, +4 qwords since the
// open DMA tag, +8 qword index, +0xC open DMA tag, +0x10 open GIF tag, +0x18 u64 its value, +0x20
// its NLOOP (A+D registers added).

namespace {

// Adds one A+D register write `reg`; opens an A+D GIF tag first when none is open (closing the
// open IMAGE/REGLIST tag with its value as is). Returns the data qword.
u32 AddPacketReg(u32 p, u64 reg) {
    const s32 nloop = GRef<s32>(p + 0x20);
    if (nloop == 0) {
        const u32 open = GRef<u32>(p + 0x10);
        if (open != 0) GRef<u64>(open) = GRef<u64>(p + 0x18);
        const u32 idx = GRef<u32>(p + 8);
        const u32 tag = GRef<u32>(p) + idx * 16;
        GRef<u32>(p + 0x18) = 0;
        GRef<u32>(p + 0x1C) = 0x10000000;  // NREG 1 (A+D), NLOOP added on close
        GRef<u64>(tag + 8) = 0xE;
        GRef<u32>(p + 4) += 1;
        GRef<u32>(p + 8) = idx + 1;
        GRef<u32>(p + 0x10) = tag;
    }
    GRef<s32>(p + 0x20) = nloop + 1;
    const u32 idx = GRef<u32>(p + 8);
    const u32 e = GRef<u32>(p) + idx * 16;
    GRef<u32>(p + 8) = idx + 1;
    GRef<u64>(e + 8) = reg;
    GRef<u32>(p + 4) += 1;
    return e;
}

}  // namespace

// EE: 0x00213578
u32 AddPacketTest2(u32 p) { return AddPacketReg(p, 0x47); }
// EE: 0x00213610
u32 AddPacketAlpha2(u32 p) { return AddPacketReg(p, 0x42); }
// EE: 0x002136A8
u32 AddPacketTex1_2(u32 p) { return AddPacketReg(p, 0x14); }
// EE: 0x00213740
u32 AddPacketBitBltBuf(u32 p) { return AddPacketReg(p, 0x50); }
// EE: 0x002137D8
u32 AddPacketTrxPos(u32 p) { return AddPacketReg(p, 0x51); }
// EE: 0x00213870
u32 AddPacketTrxReg(u32 p) { return AddPacketReg(p, 0x52); }
// EE: 0x00213908
u32 AddPacketTrxDir(u32 p) { return AddPacketReg(p, 0x53); }
// EE: 0x002139A0
u32 AddPacketTexFlush(u32 p) { return AddPacketReg(p, 0x3F); }
// EE: 0x00213A38
u32 AddPacketTex0_1(u32 p) { return AddPacketReg(p, 6); }
// EE: 0x00213AD0
u32 AddPacketPrim(u32 p) { return AddPacketReg(p, 0); }

// EE: 0x00213B68
// PRIM = `prim` | 0x116 (sprite, textured, UV), then a REGLIST tag for `count` sprites (RGBAQ UV
// XYZ2 UV XYZ2, NLOOP `count`, EOP); returns the first sprite's data.
u32 AddPacketSprites(u32 p, u64 prim, u64 count) {
    const u32 e = AddPacketPrim(p);
    GRef<u64>(e) = (prim & 0xFFFF) | 0x116;
    const u32 open = GRef<u32>(p + 0x10);
    if (open != 0) GRef<u64>(open) = GRef<u64>(p + 0x18) | static_cast<u64>(static_cast<s64>(GRef<s32>(p + 0x20)));
    const u32 n = ((static_cast<u32>(count) * 0x28 + 0xF) >> 4) + 1;
    const u32 idx = GRef<u32>(p + 8);
    const u32 tag = GRef<u32>(p) + idx * 16;
    GRef<u64>(p + 0x18) = count | 0x5400000000008000ull;
    GRef<u32>(p + 0x20) = 0;
    GRef<u32>(p + 0x10) = tag;
    GRef<u32>(p + 4) += n;
    GRef<u32>(p + 8) = idx + n;
    GRef<u64>(tag + 8) = 0x53531;
    return tag + 0x10;
}

// EE: 0x00213C28
void SendMoviePacket(u32 p, u32 chan) { sceDmaSend(chan, GRef<u32>(p) & 0xDFFFFFFFu); }

// EE: 0x00213048
// While the reader streams (states 3-4): decodes the next picture into the image buffer and sends
// one DMA chain to the GIF: per 16-pixel column an IMAGE transfer of the column's macroblocks (16 x
// height, CT32, to VRAM block `dbp`; REFS tags of at most 0x7FFF qwords) and a 16 x height sprite
// textured from it, centred in 640 x 448 plus the request's offset. The sprite alpha is 0 once a
// stop fade has ended. Counts the picture in +0x94.
s32 DrawMoviePicture(u32 r, u64 dbp) {
    const u32 chan = sceDmaGetChan(2);
    if (static_cast<u32>(GRef<s32>(r + 0x90) - 3) >= 2) return 0;
    sceMpegGetPicture(r + 0x10, GRef<u32>(r + 0x5C), 0x460);
    const s32 x0 = (0x280 - GRef<s32>(r + 0x10)) / 2 + GRef<s32>(r + 0x98);
    const s32 y0 = (0x1C0 - GRef<s32>(r + 0x14)) / 2 + GRef<s32>(r + 0x9C);
    u8 alpha = 0x80;
    if (GRef<u32>(r + 0xA0) != 0 && GRef<f32>(r + 0xA8) <= 0.0f) alpha = 0;
    guest::GuestFrame frame(0x24);
    const u32 p = frame.Address();
    const u32 buf = GRef<u32>(r + 0x60);
    GRef<u32>(p + 8) = 1;
    GRef<u32>(p + 0xC) = buf;
    GRef<u32>(p) = buf;
    GRef<u32>(p + 4) = 0;
    GRef<u32>(p + 0x10) = 0;
    GRef<u32>(p + 0x20) = 0;
    GRef<u64>(AddPacketTest2(p)) = 0xD;
    const u32 alpha_reg = AddPacketAlpha2(p);
    GRef<u32>(alpha_reg + 4) = 0x80;  // FIX
    // A = Cs, B = Cd, C = As, D = Cd; bits 8-31 of the word keep what was in the buffer.
    GRef<u32>(alpha_reg) = (GRef<u32>(alpha_reg) & 0xFFFFFF00u) | 0x44;
    GRef<u64>(AddPacketTex1_2(p)) = 0;
    s32 col = 0;
    while (col < GRef<s32>(r + 0x10) / 16) {
        const u32 bitblt = AddPacketBitBltBuf(p);
        GRef<u8>(bitblt + 6) = 1;  // DBW 1
        GRef<u8>(bitblt + 7) = 0;  // DPSM CT32
        GRef<u16>(bitblt) = 0;
        GRef<u8>(bitblt + 2) = 1;
        GRef<u8>(bitblt + 3) = 0;
        GRef<u16>(bitblt + 4) = static_cast<u16>(dbp);
        const u32 trxpos = AddPacketTrxPos(p);
        GRef<u16>(trxpos + 6) = 0;
        GRef<u16>(trxpos) = 0;
        GRef<u16>(trxpos + 2) = 0;
        GRef<u16>(trxpos + 4) = 0;
        const u32 trxreg = AddPacketTrxReg(p);
        GRef<u32>(trxreg) = 0x10;
        GRef<u32>(trxreg + 4) = GRef<u32>(r + 0x14);
        const u32 trxdir = AddPacketTrxDir(p);
        const s32 h = GRef<s32>(r + 0x14);
        u32 qwc = static_cast<u32>(h) << 2;
        GRef<u64>(trxdir) = 0;
        u32 src = GRef<u32>(r + 0x5C) + static_cast<u32>(col * ((h / 16) << 10));
        const u32 open = GRef<u32>(p + 0x10);
        if (open != 0) GRef<u64>(open) = GRef<u64>(p + 0x18) | static_cast<u64>(static_cast<s64>(GRef<s32>(p + 0x20)));
        GRef<u32>(p + 0x20) = 0;
        const u32 base = GRef<u32>(p);
        while (qwc > 0x7FFF) {
            const u32 idx = GRef<u32>(p + 8);
            GRef<u64>(base + idx * 16) = 0x0800000000007FFFull;  // IMAGE, NLOOP 0x7FFF
            GRef<u64>(GRef<u32>(p + 0xC)) = static_cast<u64>((GRef<u32>(p + 4) + 1) | 0x10000000u);  // cnt
            const u32 refs = base + (idx + 1) * 16;
            GRef<u64>(refs) = static_cast<u64>(src) << 32 | 0x40007FFFull;  // refs
            GRef<u32>(p + 0xC) = refs + 0x10;
            src += 0x7FFF0;
            GRef<u32>(p + 8) = idx + 3;
            qwc -= 0x7FFF;
            GRef<u32>(p + 4) = 0;
        }
        const u32 idx = GRef<u32>(p + 8);
        GRef<u64>(p + 0x18) = static_cast<u64>(qwc) | 0x0800000000000000ull;  // IMAGE, closed later
        GRef<u64>(GRef<u32>(p + 0xC)) = static_cast<u64>((GRef<u32>(p + 4) + 1) | 0x10000000u);
        const u32 refs = base + (idx + 1) * 16;
        GRef<u64>(refs) = static_cast<u64>(qwc | 0x40000000u) | static_cast<u64>(src) << 32;
        GRef<u32>(p + 0x10) = base + idx * 16;
        GRef<u32>(p + 0xC) = refs + 0x10;
        GRef<u32>(p + 8) = idx + 3;
        GRef<u32>(p + 4) = 0;
        AddPacketTexFlush(p);  // data qword left as it is
        GRef<u64>(AddPacketTex0_1(p)) = dbp | 0x20000006A8004000ull;  // TBW 1, CT32, 1024 x 1024, TCC, CLD 1
        const u32 s = AddPacketSprites(p, 0x40, 1);
        const s32 x = col * 16;
        GRef<u8>(s + 3) = alpha;
        GRef<u8>(s + 2) = 0x80;
        GRef<u8>(s) = 0x80;
        GRef<u16>(s + 0x10) = static_cast<u16>((x0 + x + 0x6C0) << 4);
        GRef<u8>(s + 1) = 0x80;
        GRef<u16>(s + 0x12) = static_cast<u16>((y0 + 0x720) << 4);
        GRef<u32>(s + 0x14) = 0;
        GRef<u32>(s + 4) = 0;
        ++col;
        GRef<u16>(s + 0x20) = static_cast<u16>((x0 + col * 16 + 0x6C0) << 4);
        GRef<u32>(s + 0x24) = 0;
        GRef<u16>(s + 0x22) = static_cast<u16>((y0 + GRef<u16>(r + 0x14) + 0x720) << 4);
        GRef<u16>(s + 8) = 0;
        GRef<u16>(s + 0xA) = 0;
        GRef<u16>(s + 0x18) = 0x100;
        GRef<u16>(s + 0x1A) = static_cast<u16>(GRef<u16>(r + 0x14) << 4);
    }
    const u32 open = GRef<u32>(p + 0x10);
    if (open != 0) {
        GRef<u32>(p + 0x10) = 0;
        GRef<u64>(open) = GRef<u64>(p + 0x18) | static_cast<u64>(static_cast<s64>(GRef<s32>(p + 0x20) | 0x8000));
    }
    GRef<u64>(GRef<u32>(p + 0xC)) = static_cast<u64>(GRef<u32>(p + 4) | 0x70000000u);  // end
    FlushCache(0);
    SendMoviePacket(p, chan);
    if (sceGsSyncPath(0, 0) == -1) sceGsResetPath();
    GRef<u32>(r + 0x94) += 1;
    return 0;
}

// ---- Audio feeder ----------------------------------------------------------------------------

// EE: 0x00213CA0
// Stops a running PCM transfer; 0, or -1 if stopping failed.
s32 StopMovieAudioIfRunning(u32 a) {
    if (GRef<u32>(a + 4) == 0) return 0;
    return StopMovieAudio(a) == 0 ? 0 : -1;
}

// EE: 0x00213CD8
s32 IsMovieAudioIdle(u32 a) { return GRef<u32>(a + 4) == 0 ? 1 : 0; }

// EE: 0x00213CE8
// Half size `size` and the IOP ring (VAG slot 0's first ring segment, *0x00353BF8); 0 releases it.
s32 SetMovieAudioBuffer(u32 a, s64 size) {
    if (size == 0) {
        GRef<u32>(a + 0xC) = 0;
    } else {
        GRef<s32>(a + 0xC) = static_cast<s32>(size);
        GRef<u32>(a + 8) = GRef<u32>(0x00353BF8);
    }
    return 0;
}

// EE: 0x00213D10
// SIF DMA of one half-size block from `src` into ring half `half`.
s32 SendMovieAudioBlock(u32 a, u32 src, s64 half) {
    u32 dst = GRef<u32>(a + 8);
    if (dst == 0) return -1;
    const s32 size = GRef<s32>(a + 0xC);
    if (half != 0) dst += static_cast<u32>(size);
    guest::GuestFrame frame(0x10);
    const u32 d = frame.Address();
    GRef<u32>(d) = src;
    GRef<u32>(d + 4) = dst;
    GRef<s32>(d + 8) = size;
    GRef<u32>(d + 0xC) = 0;
    GRef<u32>(a + 0x10) = sceSifSetDma(d, 1);
    return 0;
}

// EE: 0x00213D80
// 1 while the last block's SIF DMA is still running.
s32 IsMovieAudioDmaBusy(u32 a) {
    if (GRef<u32>(a + 0x10) == 0) return 0;
    if (sceSifDmaStat(GRef<u32>(a + 0x10)) < 0) {
        GRef<u32>(a + 0x10) = 0;
        return 0;
    }
    return 1;
}

// EE: 0x00213DC8
// Starts the PCM transfer of the whole ring (SOUND command 0xA, destination 0 = core-0 input).
s32 StartMovieAudio(u32 a) {
    if (GRef<u32>(a + 8) == 0 || GRef<u32>(a + 0xC) == 0) return -1;
    if (GRef<u32>(a + 4) == 1) return -1;
    GRef<s32>(a + 0x24) = func_00217680(GRef<u32>(a + 8), 0, GRef<u32>(a + 0xC) << 1);
    GRef<u32>(a + 4) = 1;
    return 0;
}

// EE: 0x00213E30
// Stops the PCM transfer (SOUND command 0xA {0, 0, 0}).
s32 StopMovieAudio(u32 a) {
    if (GRef<u32>(a + 4) != 0) {
        GRef<s32>(a + 0x24) = func_00217680(0, 0, 0);
        GRef<u32>(a + 4) = 0;
    }
    return 0;
}

// EE: 0x00213E78
// The ring half the SPU2 is playing (block-transfer status of the last SOUND reply); -1 when no
// transfer runs. Confirmed by the S3 PCSX2 batch (port/validation.md S3-3, S3-5).
s32 GetMovieAudioHalf(u32 a) {
    if (GRef<u32>(a + 4) == 0) return -1;
    return static_cast<s32>(GetSoundTransferStatus());
}

// EE: 0x00213EA0
// Input volume `level` (0-1): at once when `time` <= 0, otherwise a fade over `time` (stepped by
// StepMovieAudioVolume). A fade in progress is completed first. -1 for a level outside 0-1.
s32 SetMovieAudioVolume(f32 level, f32 time, u32 a) {
    if (level < 0.0f || 1.0f < level) return -1;
    if (GRef<u32>(a + 0x14) != 0) {
        GRef<u32>(a + 0x14) = 0;
        GRef<u32>(a) = GRef<u32>(a + 0x18);
    }
    const s32 target = fpu::FloatToInt(level * K(0x0040FF9C));
    GRef<s32>(a + 0x18) = target;
    if (time <= 0.0f) {
        GRef<s32>(a) = target;
        const s16 v = static_cast<s16>(fpu::FloatToInt(level * 255.0f) & 0xFF);
        SetSoundDataInputVolume(0, v, v);
        return 0;
    }
    GRef<f32>(a + 0x1C) = 0.0f;
    GRef<u32>(a + 0x14) = 1;
    GRef<f32>(a + 0x20) = time;
    return 0;
}

// EE: 0x00213F60
// One step of the volume fade: linear from the start to the target volume (0-0x5FFF) over the
// fade length, sent as 0-255.
s32 StepMovieAudioVolume(u32 a) {
    if (GRef<u32>(a + 0x14) == 0) return 0;
    const f32 t = GRef<f32>(a + 0x1C);
    const f32 len = GRef<f32>(a + 0x20);
    if (len < t) {
        GRef<u32>(a + 0x14) = 0;
        GRef<u32>(a) = GRef<u32>(a + 0x18);
        return 0;
    }
    const f32 from = static_cast<f32>(GRef<s32>(a));
    const f32 to = static_cast<f32>(GRef<s32>(a + 0x18));
    const f32 rest = fpu::Sub(len, t);
    const f32 v = fpu::Add(from * rest, to * t) / len;
    const s32 iv = fpu::FloatToInt(v);
    const s16 vol = static_cast<s16>((iv * 255) / 0x5FFF);
    SetSoundDataInputVolume(0, vol, vol);
    GRef<f32>(a + 0x1C) = fpu::Add(GRef<f32>(a + 0x1C), K(0x0040FFA0));
    return 0;
}

// ---- Arena -----------------------------------------------------------------------------------
// Arena (0x10 bytes): +0 base, +4 size, +8 used, +0xC alignment.

// EE: 0x00214058
u32 AllocMovieBlock(u32 size, u32 arena) { return AllocFromMovieArena(arena, size); }

// EE: 0x00214080
u32 AllocMovieWork(u32 size, u32 arena) { return AllocFromMovieArena(arena, size); }

// EE: 0x002140C0
void InitMovieArena(u32 arena, u32 base, u32 size) {
    GRef<u32>(arena) = base;
    GRef<u32>(arena + 4) = size;
    GRef<u32>(arena + 8) = 0;
}

// EE: 0x002140D0
// Allocates `size` bytes at the current offset; the new offset is the end rounded up to the
// alignment (so a block is aligned only if the previous end was). 0 when it does not fit.
u32 AllocFromMovieArena(u32 arena, u32 size) {
    const s32 used = GRef<s32>(arena + 8);
    const s32 align = GRef<s32>(arena + 0xC);
    const u32 end = static_cast<u32>(((used + static_cast<s32>(size) + align - 1) / align) * align);
    if (end > GRef<u32>(arena + 4)) return 0;
    GRef<u32>(arena + 8) = end;
    return GRef<u32>(arena) + static_cast<u32>(used);
}

// EE: 0x00214130
// Creates the reader in the `size`-byte block at `buf` for the file at `lsn`, pictures uploaded to
// VRAM block `dbp`. 0, or -1 when it does not fit.
s32 CreateSmvReader(u32 buf, u32 size, u32 lsn, u32 dbp, u32 desc) {
    guest::GuestFrame frame(0x10);
    const u32 arena = frame.Address();
    func_002140A8(arena, 0x40);
    InitMovieArena(arena, buf, size);
    const u32 r = AllocMovieBlock(0x40C0, arena);
    g_SmvReader() = r;
    if (r == 0) return -1;
    if (InitSmvReader(r, arena, lsn, desc) != 0) {
        g_SmvReader() = 0;
        return -1;
    }
    g_MovieImageDbp() = dbp;
    return 0;
}

// EE: 0x002141E8
// Reader step and picture upload; the reader is dropped at the end. 0 playing, 1 ended, -1
// stopped or no reader.
s32 UpdateSmvReader(s64 stop) {
    if (g_SmvReader() == 0) return -1;
    const s32 r = StepSmvReader(g_SmvReader(), stop);
    DrawMoviePicture(g_SmvReader(), g_MovieImageDbp());
    if (r < 0) {
        g_SmvReader() = 0;
        return -1;
    }
    if (r < 1) return 0;
    g_SmvReader() = 0;
    return 1;
}

}  // namespace ac04::game
