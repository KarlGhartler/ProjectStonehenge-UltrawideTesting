#include "platform/audio/spu_mixer.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace ac04::platform::audio {
namespace {

constexpr int kFilter[5][2] = {{0, 0}, {60, 0}, {115, -52}, {98, -55}, {122, -60}};
constexpr std::size_t kRingFrames = 48000 / 4;  // 250 ms

std::int16_t Clamp16(std::int32_t v) { return static_cast<std::int16_t>(std::clamp(v, -32768, 32767)); }

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
            acc[core][0] += e * v.vol_left >> 14;
            acc[core][1] += e * v.vol_right >> 14;
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
