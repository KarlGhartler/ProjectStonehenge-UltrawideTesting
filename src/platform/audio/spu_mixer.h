// SPU2 subset mixer (ADR-004): 2 MiB sound RAM, 48 voices (24 per core) playing SPU-ADPCM with
// loop flags, ADSR envelopes, pitch and signed (phase-inverting) volumes, mixed to 48 kHz stereo.
// Reverb, noise and FM are not modelled (logged by the SOUND service when requested).
//
// Determinism (docs/port/validation-strategy.md §3.5): the mixer runs on the game thread, advanced
// by exactly 48000 / 59.94 samples per game step (fractional accumulator), so the voice status the
// game reads back depends only on the step count, never on the host audio clock. The mixed samples
// go to a ring that the audio device thread drains.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <mutex>
#include <vector>

namespace ac04::platform::audio {

class SpuMixer {
public:
    static constexpr std::uint32_t kRamSize = 2u * 1024u * 1024u;
    static constexpr int kVoices = 48;

    SpuMixer();

    void WriteRam(std::uint32_t dst, const std::uint8_t* data, std::uint32_t size);
    const std::vector<std::uint8_t>& Ram() const { return ram_; }

    struct KeyOnParams {
        std::uint32_t start = 0;       // SPU start address (bytes)
        std::uint16_t pitch = 0x1000;  // 4.12, 0x1000 = 48 kHz
        std::uint16_t adsr1 = 0, adsr2 = 0;
        std::int16_t vol_left = 0, vol_right = 0;
    };
    void KeyOn(int voice, const KeyOnParams& p);
    void KeyOff(int voice);
    void SetVolume(int voice, std::int16_t left, std::int16_t right);
    void SetPitch(int voice, std::uint16_t pitch);
    void SetMasterVolume(int core, std::int16_t left, std::int16_t right);

    // SPU2 IRQ (IRQA, core attribute IRQ enable): when an enabled core's voice reads the 16-byte
    // block at the IRQ address, irq runs after the current output sample (VAG.IRX's VagMain
    // is driven this way).
    void SetIrqAddress(int core, std::uint32_t addr) { irq_addr_[core & 1] = addr & (kRamSize - 1) & ~15u; }
    void SetIrqEnable(int core, bool on) { irq_on_[core & 1] = on; }
    void SetIrqHandler(std::function<void(int core)> irq) { irq_ = std::move(irq); }

    // Core-0 sound-data input (sceSdBlockTrans on core 0, loop mode): 48 kHz 16-bit stereo PCM read
    // from a `size`-byte ring at `src` in IOP RAM, left and right in alternating 0x200-byte blocks
    // (HYPOTHESIS until checked against a PCSX2 capture). Used by the movie audio (SMV).
    using InputRead = std::function<const std::uint8_t*(std::uint32_t addr, std::uint32_t size)>;
    void StartBlockInput(InputRead read, std::uint32_t src, std::uint32_t size);
    void StopBlockInput() { input_on_ = false; }
    void SetInputVolume(std::int16_t left, std::int16_t right) { input_vol_[0] = left; input_vol_[1] = right; }
    // Half of the ring being read (sceSdBlockTransStatus >> 24): 0 or 1.
    std::uint32_t BlockInputHalf() const { return input_on_ && input_size_ != 0 && input_pos_ >= input_size_ / 2 ? 1u : 0u; }

    // 1 keyed on and sounding, 4 released but still sounding, 0 silent.
    std::uint8_t Status(int voice) const;
    // ENDX: the voice has played a block with the loop-end flag since its key on (SPU2 0x1700).
    bool Endx(int voice) const { return voices_[static_cast<std::size_t>(voice)].endx; }
    // ENVX != 0 (SPU2 voice register 0x500).
    bool Sounding(int voice) const { return voices_[static_cast<std::size_t>(voice)].phase != Phase::kOff && voices_[static_cast<std::size_t>(voice)].env != 0; }

    // Advances one game step (800 or 801 samples) and appends the output to the ring.
    void AdvanceStep();
    // Mixes `frames` stereo frames into `out` (s16 interleaved).
    void Mix(std::int16_t* out, std::uint32_t frames);

    // Audio thread: takes up to `frames` frames from the ring (silence when it runs empty).
    void Drain(std::int16_t* out, std::uint32_t frames);
    // Empties the ring (after --fast-until, so the output does not keep its 250 ms backlog).
    void DropQueued();
    std::uint64_t Underruns() const { return underruns_; }
    std::uint64_t Overruns() const { return overruns_; }

    // Development: keep a copy of everything mixed (--dump-audio).
    void SetCapture(bool on) { capture_on_ = on; }
    const std::vector<std::int16_t>& Capture() const { return capture_; }

    // Pitch of `note`/`fine` relative to the centre note (sceSdNote2Pitch), scaled by the sample
    // rate: 0x1000 * 2^((note - centre + (fine - centre_fine) / 128) / 12) * rate / 48000.
    static std::uint16_t NoteToPitch(int centre, int centre_fine, int note, int fine, int rate);

private:
    enum class Phase : std::uint8_t { kOff, kAttack, kDecay, kSustain, kRelease };
    struct Voice {
        Phase phase = Phase::kOff;
        bool released = false;
        bool endx = false;
        std::uint32_t addr = 0;       // current block
        std::uint32_t loop_addr = 0;
        std::uint32_t counter = 0;    // 4.12 position inside the decoded block
        std::uint16_t pitch = 0x1000;
        std::uint16_t adsr1 = 0, adsr2 = 0;
        std::int32_t env = 0;         // 0..0x7FFF
        std::uint32_t env_wait = 0;
        std::int16_t vol_left = 0, vol_right = 0;
        std::int32_t hist1 = 0, hist2 = 0;
        std::array<std::int16_t, 28> block{};
        int block_pos = 28;           // next sample of `block`; 28 = block done, 29 = start at addr
        std::int16_t prev = 0, cur = 0;
    };
    void DecodeBlock(Voice& v);
    void StepEnvelope(Voice& v);

    std::vector<std::uint8_t> ram_;
    std::array<Voice, kVoices> voices_{};
    std::int16_t master_[2][2] = {{0x3FFF, 0x3FFF}, {0x3FFF, 0x3FFF}};
    std::uint32_t step_fraction_ = 0;
    std::uint32_t irq_addr_[2] = {0, 0};
    bool irq_on_[2] = {false, false};
    std::function<void(int core)> irq_;
    InputRead input_read_;
    bool input_on_ = false;
    std::uint32_t input_src_ = 0, input_size_ = 0, input_pos_ = 0;  // input_pos_: byte offset of the frame (L/R pair) in the ring layout
    std::int16_t input_vol_[2] = {0, 0};

    std::mutex ring_mutex_;
    std::vector<std::int16_t> ring_;
    std::size_t ring_read_ = 0, ring_count_ = 0;
    std::uint64_t underruns_ = 0, overruns_ = 0;
    bool capture_on_ = false;
    std::vector<std::int16_t> capture_;
};

}  // namespace ac04::platform::audio
