// SPU2 subset mixer (ADR-004): 2 MiB sound RAM, 48 voices (24 per core) playing SPU-ADPCM with
// loop flags, ADSR envelopes, pitch and signed (phase-inverting) volumes, mixed to 48 kHz stereo,
// and the effect unit (reverb) of each core. Noise and FM are not modelled.
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

    // Reverb (SPU2 effect unit, one per core), as SOUND.IRX drives it through libsd:
    // - presets: the 10 libsd reverb modes in the PS1 register layout (32 halfwords each: dAPF1,
    //   dAPF2, vIIR, vCOMB1-4, vWALL, vAPF1-2, mLSAME, mRSAME, mLCOMB1, mRCOMB1, mLCOMB2, mRCOMB2,
    //   dLSAME, dRSAME, mLDIFF, mRDIFF, mLCOMB3, mRCOMB3, mLCOMB4, mRCOMB4, dLDIFF, dRDIFF, mLAPF1,
    //   mRAPF1, mLAPF2, mRAPF2, vLIN, vRIN; addresses in 8-byte units), read from the disc's
    //   LIBSD.IRX at startup (LoadReverbPresets);
    // - set_reverb_mode (sceSdSetEffectAttr, mode | clear work area, depth 0): SetReverbMode with the
    //   work area [esa, eea] (bytes) the IOP service computes;
    // - set_reverb_depth (EFFECT_ENABLE on, EVOL L/R): SetReverbDepth;
    // - set_reverb_on / set_reverb_off (the voice's VMIXEL / VMIXER bits): SetVoiceReverb.
    // False when the table is not where the supported disc has it (reverb stays off).
    bool LoadReverbPresets(const std::vector<std::uint8_t>& libsd_irx);
    void SetReverbMode(int core, int mode, std::uint32_t esa, std::uint32_t eea);
    void SetReverbDepth(int core, std::int16_t left, std::int16_t right);
    void SetVoiceReverb(int voice, bool on);

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

    // One core's effect unit: the reverb runs at 24 kHz, left and right on alternate 48 kHz
    // samples, behind a 39-tap half-band down- and upsampling filter (as the SPU2; the formula is
    // the PS1 one, `Reverb` in spu_mixer.cpp).
    struct Reverb {
        bool presets = false;  // a preset table was loaded
        bool enabled = false;  // EFFECT_ENABLE (set_reverb_depth)
        int mode = 0;
        std::array<std::int16_t, 32> regs{};
        std::int16_t evol[2] = {0, 0};
        std::uint32_t start = 0, size = 0;  // work area in halfwords
        std::uint32_t pos = 0;              // current position in the work area (halfwords)
        bool right = false;                 // this sample computes the right channel
        std::uint32_t buf_pos = 0;
        std::array<std::int32_t, 128> down[2]{};  // input history (each sample twice: pos and pos | 64)
        std::array<std::int32_t, 128> up[2]{};    // output history, zero on the other channel's samples
    };
    // Adds one 48 kHz input sample pair to core `core`'s reverb and returns its output pair.
    void ReverbStep(Reverb& r, std::int32_t in_left, std::int32_t in_right, std::int32_t out[2]);
    // Halfword of the work area at `offset` halfwords from the current position (wrapping).
    std::int32_t RevGet(const Reverb& r, std::int64_t offset) const;
    void RevSet(const Reverb& r, std::int64_t offset, std::int16_t v);
    std::size_t RevIndex(const Reverb& r, std::int64_t offset) const;

    std::vector<std::uint8_t> ram_;
    std::array<Voice, kVoices> voices_{};
    std::array<bool, kVoices> voice_reverb_{};  // VMIXEL / VMIXER (kept across key on, as the switch)
    std::array<std::array<std::int16_t, 32>, 10> reverb_presets_{};
    bool reverb_presets_loaded_ = false;
    Reverb reverb_[2];
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
