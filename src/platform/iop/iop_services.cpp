#include "platform/iop/iop_services.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "platform/log.h"

namespace ac04::platform::iop {
namespace {

void PutWord(std::uint8_t* reply, std::uint32_t reply_size, std::uint32_t offset, std::uint32_t value) {
    if (offset + 4 <= reply_size) std::memcpy(reply + offset, &value, 4);
}

void LogUnhandled(const char* service, std::uint32_t fno) {
    const std::string key = std::string("iop:") + service + ":" + std::to_string(fno);
    log::WriteOnce(key.c_str(), log::Level::Warning, "iop", "%s: fno %u not implemented (accepted)", service, fno);
}

// SOUND.IRX fno 1 (docs/subsystems/iop-sound.md "Command records" / "Reply"; sifrpc_server
// 0x0B90 in SOUND.IRX): the batch is {count, records...} with a fixed size per id; the reply is
// {count = records + 2, one 12-byte record per command with ids 1-4/6-9, the transfer record
// {10, last finished ticket, status}, the voice-status record {0xF, output mode, 48 bytes}}.
// - Every reply is built in one IOP area (0x3B40, 0x600 bytes) that is never cleared, and the
//   whole area is copied back: bytes after the last record (and the unwritten words of id 6-9
//   records) are leftovers of earlier replies (PCSX2 run 06, s1_S_sub7.p2s 0x00429E00).
// - Transfer record (trans_bd): the ticket is the one handler_DmaInt stored when the DMA ended;
//   the reply of the batch that starts a transfer is built before main_TransBd runs, so it still
//   carries the previous ticket (HYPOTHESIS: the DMA ends before the next batch). A block-transfer
//   stop ({0, 0, 0}) starts no DMA and its ticket is never reported (main_TransBd 0x135C,
//   handler_DmaInt 0x1300). The status word
//   is sceSdBlockTransStatus >> 24: 0 in every measured reply (no movie); with the movie PCM
//   transfer running it is the half of the ring being played (confirmed, S3 PCSX2 batch: SMV reader
//   +0x70 equal mid-movie, the movie sound matches the capture).
// - Voice status follows get_ch_stat (see the reply code): the VAG.IRX stream voices 42-47 report 4
//   (measured in runs 05/06) because VAG.IRX, not SOUND, keyed them on and they never stop.
// - Id 6 reply word (sifrpc_server 0xEB8-0xF14): after set_reverb_mode (sceSdSetEffectAttr,
//   which writes the core's ESA register synchronously: LIBSD 0x0900, setter 0x1BA8), the lower of
//   the two cores' effect start addresses read back with sceSdGetAddr (register halves, bit 0
//   dropped). ESA = EEA - (size_table[mode] * 8 - 2) with libsd's work-area table (LIBSD.IRX
//   0x4148, SDK constants) and the end addresses 0x001DFFFF (core 0) and 0x001FFFFF (core 1).
//   Measured in the PCSX2 SPU2 state: core 1 in mode 2 = 0x001FE0C0 and core 0 in mode 0 =
//   0x001DFFF0 in runs 06/09/10 (reply 0x001DFFF0); core 0 in mode 3 = 0x001DB7C0 in
//   scenestep_first_2.p2s (reply 0x001DB7C0). validation.md S4-7.
// Stand-in limits (no mixer yet): keyed-on voices stay "playing" until keyed off.
constexpr std::uint32_t kEffectEnd[2] = {0x001DFFFFu, 0x001FFFFFu};
// libsd effect work-area size per reverb mode, in 8-byte units (LIBSD.IRX 0x4148).
constexpr std::uint32_t kEffectAreaUnits[10] = {0x2, 0x4D8, 0x3E8, 0x908, 0xDFC, 0x15BC, 0x1ED8, 0x3008, 0x3008, 0x780};

std::uint32_t EffectStart(std::uint32_t core, std::uint32_t mode) {
    if (mode >= 10) {
        log::WriteOnce("iop:sound:reverb-mode", log::Level::Warning, "iop",
                       "SOUND: reverb mode %u out of range (sceSdSetEffectAttr rejects it)", mode);
        mode = 0;
    }
    const std::uint32_t esa = kEffectEnd[core & 1] - (kEffectAreaUnits[mode] * 8 - 2);
    return esa & ~1u;  // sceSdGetAddr: (hi << 17) | (lo << 1)
}

class SoundService final : public RpcService {
public:
    std::uint32_t RpcNumber() const override { return 0x81000101u; }
    const char* Name() const override { return "SOUND"; }
    void Call(std::uint32_t fno, const std::uint8_t* send, std::uint32_t send_size, std::uint8_t* reply,
              std::uint32_t reply_size) override {
        std::uint8_t* const area = area_.data();
        const std::uint32_t area_size = static_cast<std::uint32_t>(area_.size());
        if (fno == 0) {
            // iop_sound_init; the reply records go to IOP address 0 (s5 = 0), only the count 2
            // lands in the reply area.
            PutWord(area, area_size, 0, 2);
            voice_state_.fill(0);
            effect_start_[0] = EffectStart(0, 0);
            effect_start_[1] = EffectStart(1, 0);
            std::memcpy(reply, area, std::min(reply_size, area_size));
            return;
        }
        if (fno != 1) {
            LogUnhandled(Name(), fno);
            return;
        }
        if (transfer_running_) {
            // handler_DmaInt of the previous batch's transfer has run (see above).
            finished_ticket_ = running_ticket_;
            transfer_running_ = false;
        }
        const std::uint32_t count = Word(send, send_size, 0);
        std::uint32_t in = 4;
        std::uint32_t out = 4;
        std::uint32_t records = 0;
        for (std::uint32_t i = 0; i < count && in + 4 <= send_size; ++i) {
            const std::uint32_t id = Word(send, send_size, in);
            std::uint32_t size;
            switch (id) {
                case 1: case 2: case 3: case 4: size = 0x48; break;
                case 5: case 0xB: case 0xC: case 0xD: size = 12; break;
                case 6: case 7: case 8: case 9: size = 16; break;
                case 0xA: size = 20; break;
                case 0xE: size = 8; break;
                default:
                    LogUnhandled("SOUND command", id);
                    size = 0;
                    break;
            }
            if (size == 0 || in + size > send_size) break;
            const std::uint8_t* rec = send + in;
            // sifrpc_server reads the voice as a signed byte at +0x0E (also echoed as the reply tag).
            const std::uint8_t voice = rec[0x0E];
            switch (id) {
                case 1:
                case 3:
                    if (voice < 48) {
                        voice_state_[voice] = 1;
                        if (mixer_) mixer_->KeyOn(voice, VoiceParams(rec));
                    }
                    break;
                case 2:
                    if (voice < 48 && mixer_) {
                        // set_tone_attr: flags word +0x10: bit 0 volume, bit 1 raw pitch, bit 2 note pitch.
                        const std::uint32_t flags = Word(rec, size, 0x10);
                        const audio::SpuMixer::KeyOnParams p = VoiceParams(rec);
                        if (flags & 1) mixer_->SetVolume(voice, p.vol_left, p.vol_right);
                        if (flags & 2) mixer_->SetPitch(voice, static_cast<std::uint16_t>(rec[0x38] | rec[0x39] << 8));
                        if (flags & 4) mixer_->SetPitch(voice, p.pitch);
                    }
                    break;
                case 4:
                    if (voice < 48) {
                        voice_state_[voice] = 0;
                        if (mixer_) mixer_->KeyOff(voice);
                    }
                    break;
                case 5: {
                    const std::uint32_t m0 = Word(rec, size, 4), m1 = Word(rec, size, 8);
                    for (std::uint32_t v = 0; v < 24; ++v) {
                        if (m0 & (1u << v)) {
                            voice_state_[v] = 0;
                            if (mixer_) mixer_->KeyOff(static_cast<int>(v));
                        }
                        if (m1 & (1u << v)) {
                            voice_state_[24 + v] = 0;
                            if (mixer_) mixer_->KeyOff(static_cast<int>(24 + v));
                        }
                    }
                    break;
                }
                case 0xA: {
                    running_ticket_ = Word(rec, size, 0x10);
                    const std::uint32_t src = Word(rec, size, 4), dst = Word(rec, size, 8), n = Word(rec, size, 0xC);
                    // main_TransBd (SOUND.IRX 0x135C): dst = 0 and src = 0 is sceSdBlockTrans in
                    // mode 2 (stop). No DMA runs, so the transfer interrupt (handler_DmaInt 0x1300,
                    // the only writer of the finished ticket) does not fire and the stop's ticket is
                    // never reported finished: PCSX2 shows the ticket one behind after every movie
                    // (S4 title card, campaign menu and hangar checkpoints, validation.md S4-7).
                    transfer_running_ = !(dst == 0 && src == 0);
                    if (dst == 0) {
                        // sceSdBlockTrans on core 0 (movie PCM into the sound-data input): a source
                        // starts the loop-mode transfer of n bytes, {0, 0, 0} stops it.
                        if (mixer_ && iop_read_ && src != 0 && n != 0) {
                            mixer_->StartBlockInput(iop_read_, src, n);
                        } else if (mixer_) {
                            mixer_->StopBlockInput();
                        }
                    } else if (mixer_ && iop_read_) {
                        const std::uint8_t* data = iop_read_(src, n);
                        if (data) mixer_->WriteRam(dst, data, n);
                    }
                    break;
                }
                case 0xB:
                    if (mixer_) {
                        std::int16_t l, r;
                        std::memcpy(&l, rec + 6, 2);
                        std::memcpy(&r, rec + 8, 2);
                        mixer_->SetMasterVolume(rec[4], l, r);
                    }
                    break;
                case 6: case 7: case 8: case 9:
                    log::WriteOnce("iop:sound:reverb", log::Level::Info, "iop", "SOUND: reverb is not mixed yet (slice S2a)");
                    break;
                case 0xD:
                    // set_datainput_volume: core 0 sound-data input (movie PCM).
                    if (mixer_ && rec[4] == 0) {
                        std::int16_t l, r;
                        std::memcpy(&l, rec + 6, 2);
                        std::memcpy(&r, rec + 8, 2);
                        mixer_->SetInputVolume(l, r);
                    }
                    break;
                case 0xE:
                    output_mode_ = Word(rec, size, 4);
                    break;
                default:
                    break;
            }
            if (id >= 1 && id <= 4 && out + 12 <= area_size) {
                // {id, request id, u8 tag, u8 status 1 (0xFF on failure)}
                PutWord(area, area_size, out, id);
                PutWord(area, area_size, out + 4, Word(rec, size, 4));
                area[out + 8] = rec[0x0E];
                area[out + 9] = 1;
                out += 12;
                ++records;
            } else if (id >= 6 && id <= 9 && out + 12 <= area_size) {
                // {id, (id 6: effect start address; else not written), u8 status 1}; bytes 9-11
                // are not written either.
                PutWord(area, area_size, out, id);
                if (id == 6) {
                    // set_reverb_mode returns -1 for core >= 2 (the registers are left alone).
                    const std::uint32_t core = rec[4];
                    const std::uint32_t mode = Word(rec, size, 8);
                    if (core < 2 && mode < 10) effect_start_[core] = EffectStart(core, mode);
                    PutWord(area, area_size, out + 4, std::min(effect_start_[0], effect_start_[1]));
                }
                area[out + 8] = 1;
                out += 12;
                ++records;
            }
            in += size;
        }
        // trans_bd: {10, last finished ticket, status (BlockTransStatus >> 24)}
        PutWord(area, area_size, out, 10);
        PutWord(area, area_size, out + 4, finished_ticket_);
        // Status: sceSdBlockTransStatus(core 0) >> 24 = the half of the PCM ring being played
        // (the movie feeder refills the other half when it changes).
        PutWord(area, area_size, out + 8, mixer_ ? mixer_->BlockInputHalf() : 0);
        out += 12;
        PutWord(area, area_size, out, 0xF);
        PutWord(area, area_size, out + 4, output_mode_);
        // get_ch_stat: a voice SOUND keyed on (state 1) reports 1 until its ENDX bit is set; any
        // other voice reports 4 while its envelope (ENVX) is non-zero, else 0. The VAG.IRX stream
        // voices 42-47 (keyed by VAG.IRX, looping forever) therefore report 4.
        std::array<std::uint8_t, 48> status = voice_state_;
        if (mixer_) {
            for (int v = 0; v < 48; ++v) {
                std::uint8_t s = voice_state_[static_cast<std::size_t>(v)];
                if (s == 1 && mixer_->Endx(v)) s = 0;
                if (s == 0 || s == 4) s = mixer_->Sounding(v) ? 4 : 0;
                status[static_cast<std::size_t>(v)] = s;
            }
        }
        if (out + 8 + 48 <= area_size) std::memcpy(area + out + 8, status.data(), 48);
        PutWord(area, area_size, 0, records + 2);
        std::memcpy(reply, area, std::min(reply_size, area_size));
    }

    SoundService(audio::SpuMixer* mixer, IopRead iop_read) : mixer_(mixer), iop_read_(std::move(iop_read)) {}

private:
    static std::uint32_t Word(const std::uint8_t* p, std::uint32_t size, std::uint32_t offset) {
        std::uint32_t v = 0;
        if (offset + 4 <= size) std::memcpy(&v, p + offset, 4);
        return v;
    }
    // Voice request fields (docs/subsystems/iop-sound.md "The 72-byte voice request").
    audio::SpuMixer::KeyOnParams VoiceParams(const std::uint8_t* rec) const {
        audio::SpuMixer::KeyOnParams p;
        std::memcpy(&p.start, rec + 0x40, 4);
        std::memcpy(&p.adsr1, rec + 0x24, 2);
        std::memcpy(&p.adsr2, rec + 0x26, 2);
        std::int16_t l, r;
        std::memcpy(&l, rec + 0x2C, 2);
        std::memcpy(&r, rec + 0x2E, 2);
        l = static_cast<std::int16_t>(std::clamp<int>(l, -0x3FFF, 0x3FFF));
        r = static_cast<std::int16_t>(std::clamp<int>(r, -0x3FFF, 0x3FFF));
        if (output_mode_ == 2) l = r = static_cast<std::int16_t>((l + r) / 2);  // mono
        p.vol_left = l;
        p.vol_right = r;
        const int rate = rec[0x3A] | rec[0x3B] << 8;
        p.pitch = audio::SpuMixer::NoteToPitch(rec[0x30], rec[0x31], rec[0x32], static_cast<std::int8_t>(rec[0x33]), rate);
        return p;
    }
    audio::SpuMixer* mixer_ = nullptr;
    IopRead iop_read_;
    std::array<std::uint8_t, 48> voice_state_{};
    std::array<std::uint8_t, 0x600> area_{};  // SOUND.IRX reply area (IOP 0x3B40), never cleared
    std::uint32_t finished_ticket_ = 0;
    std::uint32_t running_ticket_ = 0;
    bool transfer_running_ = false;
    std::uint32_t output_mode_ = 0;
    // Effect start addresses after iop_sound_init (both cores in mode 0).
    std::uint32_t effect_start_[2] = {EffectStart(0, 0), EffectStart(1, 0)};
};

// VAG.IRX (docs/subsystems/iop-sound.md "VAG.IRX"; functions named by the module's own symbol
// table): 4 stream channels (0-1 stereo at pitch 0xEB4, 2-3 mono at 0x75A) on core-1 voices that
// loop forever over two half-buffers in SPU RAM. The SPU2 IRQ on channel 1's right voice (one
// half-buffer period, 448 samples at 44.1 kHz) runs VagMain, which copies the next half of each
// channel from its IOP ring segment (silence while no data is ready) with MarkTop/MarkBottom loop
// flags, advances the position and hands finished ring segments back to the EE (reply ring index).
// The EE fills the ring segments with sceCdReadIOPm (IOP RAM, src/sce/libcdvd.cpp).
class VagService final : public RpcService {
public:
    VagService(audio::SpuMixer* mixer, IopRead iop_read) : mixer_(mixer), iop_read_(std::move(iop_read)) {
        PutWord(rbuf_.data(), kRbufSize, 0x50, 0x200);  // measured (run 06); not written by the code read so far
        PutWord(rbuf_.data(), kRbufSize, 0x60, 0x200);
        if (mixer_) mixer_->SetIrqHandler([this](int core) {
            if (core == 1 && initialised_) VagMain();
        });
    }
    std::uint32_t RpcNumber() const override { return 0x81000102u; }
    const char* Name() const override { return "VAG"; }
    void Call(std::uint32_t fno, const std::uint8_t* send, std::uint32_t send_size, std::uint8_t* reply,
              std::uint32_t reply_size) override {
        if (fno == 0) {
            VagAllInit(send, send_size);
        } else if (fno == 1) {
            VagCtrl(send, send_size);
        } else {
            LogUnhandled(Name(), fno);
            return;
        }
        std::memcpy(reply, rbuf_.data(), std::min<std::uint32_t>(reply_size, kRbufSize));
    }

private:
    static constexpr std::uint32_t kRbufSize = 0x100;
    static constexpr std::uint32_t kStreamBase = 0x0008DA40u;  // IOP vag_buf, 64-byte aligned (measured)
    struct Channel {
        std::uint32_t read_ring = 0, ring = 1;  // vag_param +0 (ring being read), +4 (ring the EE fills)
        std::uint32_t half = 0, next_half = 1;  // +8 / +0xC
        std::uint32_t spu[2][2] = {};           // +0x10: [voice][half] SPU addresses
        std::uint32_t rings[3] = {};            // +0x20 IOP ring segments
        std::uint32_t silence = 0;              // +0x2C
        std::int32_t pos = 0, next_pos = 0;     // +0x30 / +0x34
        std::int32_t half_size = 0;             // +0x38
        std::int32_t under = 1, prev_under = 1; // +0x3C / +0x40
        std::uint32_t voice[2] = {};            // +0x44
        std::int32_t channels = 1;              // +0x4C
        std::uint32_t freq = 0;                 // +0x54
        std::uint32_t flags = 1;                // +0x58: 1 idle, 4/8 start, 0x20 playing, 0x40 ending, 0x80 chained
        std::uint32_t chained = 0;              // +0x5C
        std::int32_t total = 0, played = 0;     // +0x60 / +0x64
        std::uint32_t ready = 0;                // +0x68
    };
    static std::uint32_t ChannelOf(std::uint32_t slot) { return slot == 4 ? 2 : slot == 5 ? 3 : slot; }

    std::uint32_t Read32(const std::uint8_t* p, std::uint32_t size, std::uint32_t off) const {
        std::uint32_t v = 0;
        if (off + 4 <= size) std::memcpy(&v, p + off, 4);
        return v;
    }
    void SetFreq(std::uint32_t slot, std::uint32_t freq) {
        if (slot >= 4) return;  // the IRX writes past channel 3 (into next_vag_param); no effect here
        ch_[slot].freq = freq;
        ch_[slot].silence = freq != 0xEB4 ? kStreamBase + 0x100 : kStreamBase;
    }
    void ChgNextIopBuf(Channel& c) {
        const std::uint32_t r = c.ring;
        c.ring = r + 1;
        c.read_ring = r;
        if (c.ring > 2) c.ring = 0;
        c.pos = 0;
        c.next_pos = c.half_size;
    }
    // sceSdVoiceTrans of `size` bytes from IOP `src` to SPU `dst` with MarkTop (half 0) or
    // MarkBottom (half 1) applied to the copy: block 0 flags 6 / 2, block 1 flags 2, last block
    // flags 2 / 3 (loop start at the top, loop end at the bottom).
    void Transfer(std::uint32_t src, std::uint32_t dst, std::int32_t size, std::uint32_t half) {
        if (!mixer_ || size <= 0) return;
        std::vector<std::uint8_t> data(static_cast<std::size_t>(size), 0);
        if (const std::uint8_t* p = iop_read_ ? iop_read_(src, static_cast<std::uint32_t>(size)) : nullptr) {
            std::memcpy(data.data(), p, data.size());
        }
        data[1] = half == 0 ? 6 : 2;
        if (size > 0x11) data[0x11] = 2;
        data[static_cast<std::size_t>(size) - 0xF] = half == 0 ? 2 : 3;
        mixer_->WriteRam(dst, data.data(), static_cast<std::uint32_t>(size));
    }

    void VagAllInit(const std::uint8_t* send, std::uint32_t send_size) {
        if (initialised_) return;
        initialised_ = true;
        std::uint32_t spu = 0x5010;
        for (std::uint32_t i = 0; i < 4; ++i) {
            Channel& c = ch_[i];
            c = Channel{};
            c.half_size = i < 2 ? 0x100 : 0x80;
            c.next_pos = c.half_size;
            c.channels = i < 2 ? 2 : 1;
            SetFreq(i, i < 2 ? 0xEB4 : 0x75A);
            for (std::int32_t v = 0; v < c.channels; ++v) {
                c.voice[v] = Read32(send, send_size, 4 * i) + static_cast<std::uint32_t>(v);
                for (std::uint32_t h = 0; h < 2; ++h) {
                    c.spu[v][h] = spu;
                    spu += static_cast<std::uint32_t>(c.half_size);
                }
            }
        }
        // Ring segments: channels 0-1 three 0x20000 segments each, 2-3 three 0x10000 segments in the
        // area of the stereo channels (begin_PlayVag_main recomputes them per EE slot).
        for (std::uint32_t i = 0; i < 2; ++i)
            for (std::uint32_t r = 0; r < 3; ++r) ch_[i].rings[r] = kStreamBase + 0x180 + (i * 3 + r) * 0x20000;
        for (std::uint32_t i = 0; i < 4; ++i) slot_rings_[i] = ch_[0].rings[0] + i * 0x30000;
        for (std::uint32_t i = 2; i < 4; ++i)
            for (std::uint32_t r = 0; r < 3; ++r) ch_[i].rings[r] = slot_rings_[i - 2] + r * 0x10000;
        for (std::uint32_t i = 0; i < 4; ++i) {
            Channel& c = ch_[i];
            for (std::int32_t v = 0; v < c.channels; ++v) {
                for (std::uint32_t h = 0; h < 2; ++h) Transfer(c.silence, c.spu[v][h], c.half_size, h);
            }
            PutWord(rbuf_.data(), kRbufSize, 0x24 + 4 * i, c.ring);
            ChgNextIopBuf(c);
        }
        PutWord(rbuf_.data(), kRbufSize, 0, kStreamBase);
        if (!mixer_) return;
        mixer_->SetIrqAddress(1, ch_[1].spu[1][1]);
        mixer_->SetIrqEnable(1, true);
        for (std::uint32_t i = 0; i < 4; ++i) {
            Channel& c = ch_[i];
            for (std::int32_t v = 0; v < c.channels; ++v) {
                audio::SpuMixer::KeyOnParams p;
                p.start = c.spu[v][0];
                p.pitch = static_cast<std::uint16_t>(i < 2 ? 0xEB4 : 0x75A);
                p.adsr1 = 0xFF;
                p.adsr2 = 0x1FC0;
                mixer_->KeyOn(24 + static_cast<int>(c.voice[v] & 0x1F), p);  // volume 0 until the EE sets it
            }
        }
    }

    void VagCtrl(const std::uint8_t* send, std::uint32_t send_size) {
        const std::uint32_t count = Read32(send, send_size, 0);
        const std::uint32_t ready = Read32(send, send_size, 4);
        for (std::uint32_t slot = 0; slot < 6; ++slot) {
            Channel& c = ch_[ChannelOf(slot)];
            const std::uint32_t bit = (ready >> slot) & 1;
            if (c.flags == 1) {
                c.ready = 0;
            } else if (c.ready == 1) {
                if (bit == 0) {
                    c.ready = 2;
                    next_[ChannelOf(slot)].ready = 0;
                } else {
                    next_[ChannelOf(slot)].ready = 1;
                }
            } else if (bit != 0) {
                c.ready = 1;  // from 0 or 2
            }
        }
        for (std::uint32_t i = 0; i < count; ++i) {
            const std::uint32_t off = 8 + 20 * i;
            const std::uint32_t id = Read32(send, send_size, off);
            const std::uint32_t slot = Read32(send, send_size, off + 4);
            if (slot >= 6) continue;
            switch (id) {
                case 1: BeginPlayVag(send, send_size, off, 4); break;
                case 2: BeginPlayVag(send, send_size, off, 8); break;
                case 3: {
                    Channel& c = ch_[ChannelOf(slot)];
                    c.played = 0;
                    c.ready = 0;
                    c.flags = 1;
                    break;
                }
                case 4: ch_[ChannelOf(slot)].flags = 0x40; break;
                default:
                    LogUnhandled("VAG command", id);
                    break;
            }
        }
    }

    // begin_PlayVag_main: an idle channel starts at once (no stream chained: next.flags = 1),
    // a busy one gets the request chained in next_vag_param.
    void BeginPlayVag(const std::uint8_t* send, std::uint32_t send_size, std::uint32_t off, std::uint32_t mode) {
        const std::uint32_t slot = Read32(send, send_size, off + 4);
        const std::uint32_t n = ChannelOf(slot);
        Channel* p;
        if (ch_[n].flags == 1) {
            p = &ch_[n];
            next_[n].flags = 1;
        } else {
            p = &next_[n];
        }
        if (slot < 2) {
            for (std::uint32_t r = 0; r < 3; ++r) p->rings[r] = ch_[0].rings[0] + slot * 0x60000 + r * 0x20000;
        } else {
            for (std::uint32_t r = 0; r < 3; ++r) p->rings[r] = slot_rings_[slot - 2] + r * 0x10000;
        }
        p->total = static_cast<std::int32_t>(Read32(send, send_size, off + 8));
        p->channels = static_cast<std::int32_t>(Read32(send, send_size, off + 0xC));
        SetFreq(slot, Read32(send, send_size, off + 0x10));
        p->flags = mode;
    }

    void VagMain() {
        if (mixer_) mixer_->SetIrqEnable(1, false);
        for (std::uint32_t i = 0; i < 4; ++i) {
            Channel& c = ch_[i];
            std::int32_t played_now = 0;
            std::uint32_t flags = c.flags;
            const std::uint32_t next_half = c.next_half;
            const std::uint32_t half = c.half;
            const std::int32_t size = c.half_size;
            if ((flags & 0xC) != 0) {
                if (c.chained == 0) ChgNextIopBuf(c);
                c.chained = 0;
                c.played = 0;
                std::uint32_t f = 0x20;
                if ((flags & 8) != 0) f = (next_[i].flags & 1) == 0 ? 0x80 : 0x40;
                c.flags = f;
                flags = f;
            }
            int kind = 0;  // 0 ring data, 1 silence, 2 no transfer
            std::uint32_t src = 0;
            if (c.ready == 0 || (flags & 0x60) == 0) {
                ++c.under;
                if (c.prev_under < 3) {
                    src = c.silence;
                    kind = 1;
                } else {
                    kind = 2;
                }
            } else {
                c.under = 0;
                c.prev_under = 0;
            }
            auto ring_src = [&](std::int32_t extra) {
                const std::int32_t blocks = (c.pos < 0 ? c.pos + 0x7FF : c.pos) >> 11;
                return c.rings[c.read_ring % 3] + static_cast<std::uint32_t>(blocks * c.channels * 0x800 + c.pos - blocks * 0x800 + extra);
            };
            if (kind == 0) src = ring_src(0);
            if (kind != 2) Transfer(src, c.spu[0][half & 1], size, half & 1);
            if (c.channels > 1) {
                if (kind == 0) src = ring_src(0x800);
                if (kind != 2) Transfer(src, c.spu[1][half & 1], size, half & 1);
            }
            if ((flags & 0xF0) != 0 && c.under == 0) {
                played_now = c.played + size;
                c.pos = c.next_pos;
                c.next_pos += size;
                c.played = played_now;
                bool change = true;
                if (played_now < c.total) {
                    if (c.pos < 0x10000) {
                        change = false;
                    } else if (c.ready == 2) {
                        c.ready = 0;
                    }
                } else if (next_[i].flags == 1) {
                    c.played = 0;
                    c.flags = 1;
                } else {
                    c.chained = 1;
                    c.total = next_[i].total;
                    c.channels = next_[i].channels;
                    c.ready = next_[i].ready;
                    next_[i].flags = 1;
                    if (c.channels < 2) {
                        for (std::uint32_t r = 0; r < 3; ++r) c.rings[r] = next_[i].rings[r];
                    }
                    c.flags = 4;
                }
                if (change) ChgNextIopBuf(c);
            }
            PutWord(rbuf_.data(), kRbufSize, 4 + 4 * i, static_cast<std::uint32_t>(played_now));
            PutWord(rbuf_.data(), kRbufSize, 0x14 + 4 * i, flags);
            PutWord(rbuf_.data(), kRbufSize, 0x24 + 4 * i, c.ring);
            c.half = next_half;
            c.next_half = next_half + 1 > 1 ? 0 : next_half + 1;
            c.prev_under = c.under;
        }
        if (mixer_) {
            mixer_->SetIrqAddress(1, ch_[1].spu[1][ch_[1].next_half & 1]);
            mixer_->SetIrqEnable(1, true);
        }
    }

    audio::SpuMixer* mixer_ = nullptr;
    IopRead iop_read_;
    std::array<std::uint8_t, kRbufSize> rbuf_{};
    std::array<Channel, 4> ch_{};
    std::array<Channel, 4> next_{};
    std::uint32_t slot_rings_[4] = {};
    bool initialised_ = false;
};

class UsbJoyService final : public RpcService {
public:
    std::uint32_t RpcNumber() const override { return 0x80000210u; }
    const char* Name() const override { return "USBJOY"; }
    void Call(std::uint32_t fno, const std::uint8_t*, std::uint32_t, std::uint8_t* reply, std::uint32_t reply_size) override {
        if (fno != 2) {
            LogUnhandled(Name(), fno);
            return;
        }
        std::memset(reply, 0, reply_size);
        for (std::uint32_t dev = 0; dev < 2; ++dev) {
            const std::uint32_t base = 0x08 + dev * 0x28;
            for (std::uint32_t i = 0; i < 6; ++i) PutWord(reply, reply_size, base + 4 * i, 0x80);
            PutWord(reply, reply_size, base + 0x20, 0xFF);
            PutWord(reply, reply_size, base + 0x24, 0xFF);
        }
    }
};

}  // namespace

std::unique_ptr<RpcService> MakeSoundService(audio::SpuMixer* mixer, IopRead iop_read) {
    return std::make_unique<SoundService>(mixer, std::move(iop_read));
}
std::unique_ptr<RpcService> MakeVagService(audio::SpuMixer* mixer, IopRead iop_read) {
    return std::make_unique<VagService>(mixer, std::move(iop_read));
}
std::unique_ptr<RpcService> MakeUsbJoyService() { return std::make_unique<UsbJoyService>(); }

}  // namespace ac04::platform::iop
