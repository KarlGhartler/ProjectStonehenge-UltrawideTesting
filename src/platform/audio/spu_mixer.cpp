#include "platform/audio/spu_mixer.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace ac04::platform::audio {
namespace {

constexpr int kFilter[5][2] = {{0, 0}, {60, 0}, {115, -52}, {98, -55}, {122, -60}};
constexpr std::size_t kRingFrames = 48000 / 4;  // 250 ms

std::int16_t Clamp16(std::int32_t v) { return static_cast<std::int16_t>(std::clamp(v, -32768, 32767)); }

// libsd's reverb preset table in LIBSD.IRX of the supported disc (verified by its SHA-1 at startup):
// modes 0-9 (off, room, studio A-C, hall, space, echo, delay, pipe), 0x44 bytes each: the 32
// registers, then 4 more bytes.
constexpr std::size_t kPresetTable = 0x4224;
constexpr std::size_t kPresetStride = 0x44;

// Half-band FIR of the SPU2 reverb's 48 -> 24 kHz input and 24 -> 48 kHz output (39 taps, sum
// 32766: unity at >> 15).
constexpr std::int32_t kReverbFir[39] = {-1,    0, 2,     0, -10,  0, 35,    0, -103,  0, 266,  0, -616,
                                         0,     1332, 0,  -2960, 0, 10246, 16384, 10246, 0, -2960, 0,
                                         1332,  0, -616,  0, 266,  0, -103,  0, 35,   0, -10,  0, 2,
                                         0,     -1};

std::int32_t Mul15(std::int32_t a, std::int32_t b) { return a * b >> 15; }

}  // namespace

SpuMixer::SpuMixer() : ram_(kRamSize, 0), ring_(kRingFrames * 2, 0) {}

void SpuMixer::WriteRam(std::uint32_t dst, const std::uint8_t* data, std::uint32_t size) {
    for (std::uint32_t i = 0; i < size; ++i) ram_[(dst + i) & (kRamSize - 1)] = data[i];
}

std::uint16_t SpuMixer::NoteToPitch(int centre, int centre_fine, int note, int fine, int rate) {
    const double semis = (note - centre) + (fine - centre_fine) / 128.0;
    const double p = 4096.0 * std::pow(2.0, semis / 12.0) * rate / 48000.0;
    return static_cast<std::uint16_t>(std::clamp(p, 0.0, 16383.0));
}

void SpuMixer::KeyOn(int voice, const KeyOnParams& p) {
    if (voice < 0 || voice >= kVoices) return;
    Voice& v = voices_[voice];
    v = Voice{};
    v.phase = Phase::kAttack;
    v.addr = p.start & (kRamSize - 1) & ~15u;
    v.loop_addr = v.addr;
    v.pitch = p.pitch;
    v.adsr1 = p.adsr1;
    v.adsr2 = p.adsr2;
    v.vol_left = p.vol_left;
    v.vol_right = p.vol_right;
    v.block_pos = 29;  // decode the first block without moving on
}

void SpuMixer::KeyOff(int voice) {
    if (voice < 0 || voice >= kVoices) return;
    Voice& v = voices_[voice];
    if (v.phase == Phase::kOff) return;
    v.phase = Phase::kRelease;
    v.released = true;
    v.env_wait = 0;
}

void SpuMixer::SetVolume(int voice, std::int16_t left, std::int16_t right) {
    if (voice < 0 || voice >= kVoices) return;
    voices_[voice].vol_left = left;
    voices_[voice].vol_right = right;
}

void SpuMixer::SetPitch(int voice, std::uint16_t pitch) {
    if (voice >= 0 && voice < kVoices) voices_[voice].pitch = pitch;
}

void SpuMixer::SetMasterVolume(int core, std::int16_t left, std::int16_t right) {
    master_[core & 1][0] = left;
    master_[core & 1][1] = right;
}

bool SpuMixer::LoadReverbPresets(const std::vector<std::uint8_t>& irx) {
    if (irx.size() < kPresetTable + 10 * kPresetStride) return false;
    for (std::size_t m = 0; m < 10; ++m) {
        for (std::size_t i = 0; i < 32; ++i) {
            const std::size_t o = kPresetTable + m * kPresetStride + i * 2;
            reverb_presets_[m][i] = static_cast<std::int16_t>(irx[o] | irx[o + 1] << 8);
        }
    }
    // Mode 1 (room) starts with dAPF1 0x7D, dAPF2 0x5B: the table is where it is expected.
    reverb_presets_loaded_ = reverb_presets_[1][0] == 0x7D && reverb_presets_[1][1] == 0x5B;
    return reverb_presets_loaded_;
}

void SpuMixer::SetReverbMode(int core, int mode, std::uint32_t esa, std::uint32_t eea) {
    if (mode < 0 || mode >= 10) return;
    Reverb& r = reverb_[core & 1];
    r.presets = reverb_presets_loaded_;
    r.mode = mode;
    r.regs = reverb_presets_[static_cast<std::size_t>(mode)];
    r.evol[0] = r.evol[1] = 0;  // sceSdSetEffectAttr with depth 0
    esa &= kRamSize - 1;
    eea &= kRamSize - 1;
    r.start = esa / 2;
    r.size = eea >= esa ? (eea - esa + 1) / 2 : 0;
    r.pos = 0;
    // The mode carries the "clear work area" flag (SOUND set_reverb_mode: mode | 0x100).
    if (r.size != 0) std::fill(ram_.begin() + esa, ram_.begin() + esa + r.size * 2, std::uint8_t{0});
    for (auto& b : r.down) b.fill(0);
    for (auto& b : r.up) b.fill(0);
}

void SpuMixer::SetReverbDepth(int core, std::int16_t left, std::int16_t right) {
    Reverb& r = reverb_[core & 1];
    r.enabled = true;
    r.evol[0] = left;
    r.evol[1] = right;
}

void SpuMixer::SetVoiceReverb(int voice, bool on) {
    if (voice < 0 || voice >= kVoices) return;
    voice_reverb_[static_cast<std::size_t>(voice)] = on;
}

std::size_t SpuMixer::RevIndex(const Reverb& r, std::int64_t offset) const {
    std::int64_t i = (static_cast<std::int64_t>(r.pos) + offset) % static_cast<std::int64_t>(r.size);
    if (i < 0) i += r.size;
    return (static_cast<std::size_t>(r.start + static_cast<std::uint32_t>(i)) * 2) & (kRamSize - 1);
}

std::int32_t SpuMixer::RevGet(const Reverb& r, std::int64_t offset) const {
    std::int16_t v;
    std::memcpy(&v, &ram_[RevIndex(r, offset)], 2);
    return v;
}

void SpuMixer::RevSet(const Reverb& r, std::int64_t offset, std::int16_t v) { std::memcpy(&ram_[RevIndex(r, offset)], &v, 2); }

// The PS1 / SPU2 reverb (no$psx "SPU Reverb Formula"; the SPU2 runs it for left and right on
// alternate samples): input through the 39-tap filter, same-side and cross-side reflections (IIR,
// WALL), four comb taps, two all-pass filters, output through the filter again. Addresses are in
// 8-byte units relative to the current position in the work area (one halfword per 24 kHz step).
void SpuMixer::ReverbStep(Reverb& r, std::int32_t in_left, std::int32_t in_right, std::int32_t out[2]) {
    const std::uint32_t p = r.buf_pos;
    r.down[0][p] = r.down[0][p | 64] = Clamp16(in_left);
    r.down[1][p] = r.down[1][p | 64] = Clamp16(in_right);
    const bool R = r.right;
    const auto& g = r.regs;
    auto a = [&](int reg) { return static_cast<std::int64_t>(static_cast<std::uint16_t>(g[static_cast<std::size_t>(reg)])) * 4; };
    // Downsampled input of this channel: the 39 samples ending at the newest.
    std::int32_t acc = 0;
    const std::uint32_t first = (p + 64 - 38) & 63;
    for (int i = 0; i < 39; ++i) acc += r.down[R ? 1 : 0][first + static_cast<std::uint32_t>(i)] * kReverbFir[i];
    const std::int32_t in = Mul15(R ? g[31] : g[30], Clamp16(acc >> 15));
    const std::int32_t viir = g[2], vwall = g[7], vapf1 = g[8], vapf2 = g[9];
    const std::int64_t same_dst = a(R ? 11 : 10), same_src = a(R ? 17 : 16);
    const std::int64_t diff_dst = a(R ? 19 : 18), diff_src = a(R ? 24 : 25);
    const std::int32_t same_prv = RevGet(r, same_dst - 1), diff_prv = RevGet(r, diff_dst - 1);
    const std::int32_t same = Mul15(viir, in + Mul15(vwall, RevGet(r, same_src)) - same_prv) + same_prv;
    const std::int32_t diff = Mul15(viir, in + Mul15(vwall, RevGet(r, diff_src)) - diff_prv) + diff_prv;
    std::int32_t o = Mul15(g[3], RevGet(r, a(R ? 13 : 12))) + Mul15(g[4], RevGet(r, a(R ? 15 : 14))) +
                     Mul15(g[5], RevGet(r, a(R ? 21 : 20))) + Mul15(g[6], RevGet(r, a(R ? 23 : 22)));
    const std::int64_t apf1_dst = a(R ? 27 : 26), apf1_src = apf1_dst - a(0);
    const std::int64_t apf2_dst = a(R ? 29 : 28), apf2_src = apf2_dst - a(1);
    const std::int32_t apf1 = o - Mul15(vapf1, RevGet(r, apf1_src));
    o = RevGet(r, apf1_src) + Mul15(vapf1, apf1);
    const std::int32_t apf2 = o - Mul15(vapf2, RevGet(r, apf2_src));
    o = RevGet(r, apf2_src) + Mul15(vapf2, apf2);
    RevSet(r, same_dst, Clamp16(same));
    RevSet(r, diff_dst, Clamp16(diff));
    RevSet(r, apf1_dst, Clamp16(apf1));
    RevSet(r, apf2_dst, Clamp16(apf2));
    r.up[R ? 1 : 0][p] = r.up[R ? 1 : 0][p | 64] = Clamp16(o);
    r.up[R ? 0 : 1][p] = r.up[R ? 0 : 1][p | 64] = 0;
    // Upsampled output: half the history is zero, so the sum is doubled (>> 14).
    for (int c = 0; c < 2; ++c) {
        std::int32_t s = 0;
        for (int i = 0; i < 39; ++i) s += r.up[c][first + static_cast<std::uint32_t>(i)] * kReverbFir[i];
        out[c] = Clamp16(s >> 14);
    }
    r.buf_pos = (p + 1) & 63;
    if (R) r.pos = (r.pos + 1) % r.size;
    r.right = !R;
}

std::uint8_t SpuMixer::Status(int voice) const {
    const Voice& v = voices_[voice];
    if (v.phase == Phase::kOff) return 0;
    return v.released ? 4 : 1;
}

void SpuMixer::DecodeBlock(Voice& v) {
    const std::uint8_t* b = &ram_[v.addr & (kRamSize - 1)];
    const int shift = b[0] & 0xF;
    const int filter = std::min((b[0] >> 4) & 7, 4);
    if ((b[1] & 4) != 0) v.loop_addr = v.addr;
    for (int i = 0; i < 28; ++i) {
        const int nibble = (b[2 + i / 2] >> ((i & 1) * 4)) & 0xF;
        std::int32_t s = static_cast<std::int16_t>(static_cast<std::uint16_t>(nibble << 12)) >> std::min(shift, 12);
        s += (v.hist1 * kFilter[filter][0] + v.hist2 * kFilter[filter][1] + 32) / 64;
        s = Clamp16(s);
        v.hist2 = v.hist1;
        v.hist1 = s;
        v.block[static_cast<std::size_t>(i)] = static_cast<std::int16_t>(s);
    }
    v.block_pos = 0;
}

void SpuMixer::StepEnvelope(Voice& v) {
    bool dec = false, exp = false;
    int rate = 0;
    switch (v.phase) {
        case Phase::kAttack: rate = (v.adsr1 >> 8) & 0x7F; exp = (v.adsr1 & 0x8000) != 0; break;
        case Phase::kDecay: rate = ((v.adsr1 >> 4) & 0xF) * 4; dec = true; exp = true; break;
        case Phase::kSustain: rate = (v.adsr2 >> 6) & 0x7F; dec = (v.adsr2 & 0x4000) != 0; exp = (v.adsr2 & 0x8000) != 0; break;
        case Phase::kRelease: rate = (v.adsr2 & 0x1F) * 4; dec = true; exp = (v.adsr2 & 0x20) != 0; break;
        default: return;
    }
    if (v.env_wait > 0) {
        --v.env_wait;
        return;
    }
    const int shift = rate >> 2;
    std::int32_t step = dec ? -8 + (rate & 3) : 7 - (rate & 3);
    std::uint32_t cycles = 1u << std::max(0, shift - 11);
    step <<= std::max(0, 11 - shift);
    if (exp && !dec && v.env > 0x6000) cycles *= 4;
    if (exp && dec) step = step * v.env >> 15;
    v.env_wait = cycles - 1;
    v.env = std::clamp(v.env + step, 0, 0x7FFF);
    switch (v.phase) {
        case Phase::kAttack:
            if (v.env >= 0x7FFF) v.phase = Phase::kDecay;
            break;
        case Phase::kDecay:
            if (v.env <= ((v.adsr1 & 0xF) + 1) * 0x800) v.phase = Phase::kSustain;
            break;
        case Phase::kRelease:
            if (v.env <= 0) v.phase = Phase::kOff;
            break;
        default:
            break;
    }
}

void SpuMixer::Mix(std::int16_t* out, std::uint32_t frames) {
    for (std::uint32_t f = 0; f < frames; ++f) {
        std::int32_t acc[2][2] = {{0, 0}, {0, 0}};
        std::int32_t wet[2][2] = {{0, 0}, {0, 0}};  // voices sent to the core's reverb
        bool irq[2] = {false, false};
        for (int i = 0; i < kVoices; ++i) {
            Voice& v = voices_[static_cast<std::size_t>(i)];
            if (v.phase == Phase::kOff) continue;
            // Pitch counter (4.12): one source sample per 0x1000.
            v.counter += std::min<std::uint32_t>(v.pitch, 0x3FFF);
            while (v.counter >= 0x1000 && v.phase != Phase::kOff) {
                v.counter -= 0x1000;
                v.prev = v.cur;
                if (v.block_pos >= 28) {
                    if (v.block_pos == 28) {
                        // The block just played: an end flag loops or stops the voice.
                        const std::uint8_t flags = ram_[(v.addr + 1) & (kRamSize - 1)];
                        if ((flags & 1) != 0) {
                            v.endx = true;
                            if ((flags & 2) != 0) {
                                v.addr = v.loop_addr;
                            } else {
                                v.phase = Phase::kOff;
                                v.env = 0;
                                break;
                            }
                        } else {
                            v.addr = (v.addr + 16) & (kRamSize - 1);
                        }
                    }
                    DecodeBlock(v);
                    const int vc = i / 24;
                    if (irq_on_[vc] && v.addr == irq_addr_[vc]) irq[vc] = true;
                }
                v.cur = v.block[static_cast<std::size_t>(v.block_pos++)];
            }
            if (v.phase == Phase::kOff) continue;
            StepEnvelope(v);
            const std::int32_t frac = static_cast<std::int32_t>(v.counter & 0xFFF);
            const std::int32_t s = v.prev + ((v.cur - v.prev) * frac >> 12);
            const std::int32_t e = s * v.env >> 15;
            const int core = i / 24;
            const std::int32_t l = e * v.vol_left >> 14, r = e * v.vol_right >> 14;
            acc[core][0] += l;
            acc[core][1] += r;
            if (voice_reverb_[static_cast<std::size_t>(i)]) {
                wet[core][0] += l;
                wet[core][1] += r;
            }
        }
        for (int core = 0; core < 2; ++core) {
            Reverb& rv = reverb_[core];
            if (!rv.enabled || !rv.presets || rv.mode == 0 || rv.size == 0) continue;
            std::int32_t rev[2];
            ReverbStep(rv, wet[core][0], wet[core][1], rev);
            acc[core][0] += rev[0] * rv.evol[0] >> 15;
            acc[core][1] += rev[1] * rv.evol[1] >> 15;
        }
        if (input_on_ && input_read_) {
            // Sample i of the ring: left at (i / 256) * 0x400 + (i % 256) * 2, right 0x200 later.
            const std::uint32_t i = input_pos_ / 4;
            const std::uint32_t off = (i / 256) * 0x400 + (i % 256) * 2;
            const std::uint8_t* l = input_read_(input_src_ + off, 2);
            const std::uint8_t* r = input_read_(input_src_ + off + 0x200, 2);
            if (l != nullptr && r != nullptr) {
                const std::int32_t sl = static_cast<std::int16_t>(l[0] | l[1] << 8);
                const std::int32_t sr = static_cast<std::int16_t>(r[0] | r[1] << 8);
                acc[0][0] += sl * input_vol_[0] >> 15;
                acc[0][1] += sr * input_vol_[1] >> 15;
            }
            input_pos_ += 4;
            if (input_pos_ >= input_size_) input_pos_ = 0;
        }
        for (int c = 0; c < 2; ++c) {
            std::int32_t sum = 0;
            for (int core = 0; core < 2; ++core) sum += acc[core][c] * master_[core][c] >> 14;
            out[f * 2 + static_cast<std::uint32_t>(c)] = Clamp16(sum);
        }
        for (int core = 0; core < 2; ++core) {
            if (irq[core] && irq_on_[core] && irq_) irq_(core);
        }
    }
}

void SpuMixer::StartBlockInput(InputRead read, std::uint32_t src, std::uint32_t size) {
    input_read_ = std::move(read);
    input_src_ = src;
    input_size_ = size & ~0x3FFu;
    input_pos_ = 0;
    input_on_ = input_size_ != 0;
}

void SpuMixer::AdvanceStep() {
    // 48000 Hz / 59.94 Hz = 800.8 samples: 4004 samples every 5 steps.
    step_fraction_ += 4004;
    const std::uint32_t frames = step_fraction_ / 5;
    step_fraction_ %= 5;
    std::vector<std::int16_t> buf(frames * 2);
    Mix(buf.data(), frames);
    if (capture_on_) capture_.insert(capture_.end(), buf.begin(), buf.end());
    std::lock_guard<std::mutex> lock(ring_mutex_);
    const std::size_t cap = ring_.size() / 2;
    for (std::uint32_t f = 0; f < frames; ++f) {
        if (ring_count_ == cap) {
            ring_read_ = (ring_read_ + 1) % cap;
            --ring_count_;
            ++overruns_;
        }
        const std::size_t w = (ring_read_ + ring_count_) % cap;
        ring_[w * 2] = buf[f * 2];
        ring_[w * 2 + 1] = buf[f * 2 + 1];
        ++ring_count_;
    }
}

void SpuMixer::DropQueued() {
    std::lock_guard<std::mutex> lock(ring_mutex_);
    ring_read_ = 0;
    ring_count_ = 0;
}

void SpuMixer::Drain(std::int16_t* out, std::uint32_t frames) {
    std::lock_guard<std::mutex> lock(ring_mutex_);
    const std::size_t cap = ring_.size() / 2;
    for (std::uint32_t f = 0; f < frames; ++f) {
        if (ring_count_ == 0) {
            out[f * 2] = out[f * 2 + 1] = 0;
            ++underruns_;
            continue;
        }
        out[f * 2] = ring_[ring_read_ * 2];
        out[f * 2 + 1] = ring_[ring_read_ * 2 + 1];
        ring_read_ = (ring_read_ + 1) % cap;
        --ring_count_;
    }
}

}  // namespace ac04::platform::audio
