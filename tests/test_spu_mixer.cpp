// SPU2 subset mixer (src/platform/audio/spu_mixer.cpp). Synthetic ADPCM data only.
#include <doctest/doctest.h>

#include <vector>

#include "platform/audio/spu_mixer.h"

using ac04::platform::audio::SpuMixer;

TEST_CASE("mixer: 800.8 samples per game step") {
    SpuMixer m;
    m.SetCapture(true);
    for (int i = 0; i < 5; ++i) m.AdvanceStep();
    CHECK(m.Capture().size() == 4004u * 2);
    for (int i = 0; i < 5 * 59; ++i) m.AdvanceStep();
    CHECK(m.Capture().size() == 4004u * 2 * 60);  // 60 * 5 steps = 48048 samples
}

TEST_CASE("mixer: ADPCM block, ADSR attack and the end flag") {
    SpuMixer m;
    // One block: shift 0, filter 0, end flag without repeat; nibbles 7 (= 0x7000 >> 0).
    std::vector<std::uint8_t> block(16, 0x77);
    block[0] = 0x00;
    block[1] = 0x01;
    m.WriteRam(0x1000, block.data(), 16);
    SpuMixer::KeyOnParams p;
    p.start = 0x1000;
    p.pitch = 0x1000;
    p.adsr1 = 0x000F;  // fastest linear attack, sustain level max
    p.adsr2 = 0x0000;
    p.vol_left = p.vol_right = 0x3FFF;
    m.KeyOn(0, p);
    CHECK(m.Status(0) == 1);
    std::vector<std::int16_t> out(10 * 2);
    m.Mix(out.data(), 10);
    CHECK(out[2 * 9] > 0);  // the envelope has risen and the samples are positive
    // After the 28 samples of the block the voice stops (end flag, no repeat).
    std::vector<std::int16_t> rest(40 * 2);
    m.Mix(rest.data(), 40);
    CHECK(m.Status(0) == 0);
    CHECK(rest[2 * 39] == 0);
}

TEST_CASE("mixer: key off reports released while the envelope sounds") {
    SpuMixer m;
    std::vector<std::uint8_t> block(16, 0x11);
    block[0] = 0x00;
    block[1] = 0x06;  // loop start + repeat: loops forever
    block[1] |= 0x01;
    m.WriteRam(0x2000, block.data(), 16);
    SpuMixer::KeyOnParams p;
    p.start = 0x2000;
    p.adsr1 = 0x000F;
    p.adsr2 = 0x001F;  // slowest linear release
    p.vol_left = p.vol_right = 0x3FFF;
    m.KeyOn(5, p);
    std::vector<std::int16_t> out(200 * 2);
    m.Mix(out.data(), 200);
    CHECK(m.Status(5) == 1);
    m.KeyOff(5);
    m.Mix(out.data(), 10);
    CHECK(m.Status(5) == 4);
}

TEST_CASE("mixer: note to pitch") {
    CHECK(SpuMixer::NoteToPitch(60, 0, 60, 0, 48000) == 0x1000);
    CHECK(SpuMixer::NoteToPitch(60, 0, 72, 0, 48000) == 0x2000);
    CHECK(SpuMixer::NoteToPitch(60, 0, 60, 0, 24000) == 0x800);
}

namespace {

// A LIBSD.IRX stand-in: the reverb preset table at its offset (0x4224, 0x44 bytes per mode), with a
// synthetic mode 1 (only its first two registers match the real room preset, as the loader checks).
std::vector<std::uint8_t> SyntheticLibsd() {
    std::vector<std::uint8_t> irx(0x4224 + 10 * 0x44, 0);
    auto reg = [&irx](int mode, int i, std::uint16_t v) {
        const std::size_t o = 0x4224 + static_cast<std::size_t>(mode) * 0x44 + static_cast<std::size_t>(i) * 2;
        irx[o] = static_cast<std::uint8_t>(v);
        irx[o + 1] = static_cast<std::uint8_t>(v >> 8);
    };
    reg(1, 0, 0x7D);    // dAPF1
    reg(1, 1, 0x5B);    // dAPF2
    reg(1, 2, 0x6000);  // vIIR
    reg(1, 3, 0x6000);  // vCOMB1
    reg(1, 10, 0x10);
    reg(1, 11, 0x10);  // mL/RSAME: written 0x40 halfwords ahead of the position
    reg(1, 12, 0x08);
    reg(1, 13, 0x08);  // mL/RCOMB1: read 0x20 ahead, so 0x20 steps after the write
    reg(1, 26, 0x100);
    reg(1, 27, 0x100);  // mL/RAPF1
    reg(1, 28, 0x200);
    reg(1, 29, 0x200);  // mL/RAPF2 (vAPF1/2 0: each stage is a plain delay of dAPF1 / dAPF2)
    reg(1, 16, 0x08);
    reg(1, 17, 0x08);   // dL/RSAME
    reg(1, 30, 0x7FFF);
    reg(1, 31, 0x7FFF);  // vLIN, vRIN
    return irx;
}

// Plays a short looping tone on voice 0 (with reverb when `wet`), releases it, and returns the
// output from the release on.
std::vector<std::int16_t> ToneTail(bool load, bool wet, bool depth) {
    SpuMixer m;
    if (load) CHECK(m.LoadReverbPresets(SyntheticLibsd()));
    m.SetReverbMode(0, 1, 0x1DB7C0, 0x1DFFFF);
    if (depth) m.SetReverbDepth(0, 0x7FFF, 0x7FFF);
    m.SetVoiceReverb(0, wet);
    std::vector<std::uint8_t> block(16, 0x33);
    block[0] = 0x00;
    block[1] = 0x07;  // loop start, end, repeat
    m.WriteRam(0x3000, block.data(), 16);
    SpuMixer::KeyOnParams p;
    p.start = 0x3000;
    p.adsr1 = 0x000F;
    p.adsr2 = 0x0000;  // fastest release
    p.vol_left = p.vol_right = 0x3FFF;
    m.KeyOn(0, p);
    std::vector<std::int16_t> out(4000 * 2);
    m.Mix(out.data(), 4000);
    m.KeyOff(0);
    std::vector<std::int16_t> tail(8000 * 2);
    m.Mix(tail.data(), 8000);
    return tail;
}

}  // namespace

TEST_CASE("mixer: reverb only for voices sent to an enabled effect unit") {
    const std::vector<std::int16_t> dry = ToneTail(true, false, true);  // voice not sent to the reverb
    CHECK(ToneTail(true, true, false) == dry);                         // effect unit not enabled (depth)
    CHECK(ToneTail(false, true, true) == dry);                         // no preset table
    const std::vector<std::int16_t> wet = ToneTail(true, true, true);
    CHECK(wet != dry);
    CHECK(ToneTail(true, true, true) == wet);  // deterministic
}

TEST_CASE("mixer: reverb presets are found only at the libsd table") {
    SpuMixer m;
    CHECK_FALSE(m.LoadReverbPresets(std::vector<std::uint8_t>(0x100, 0)));
    std::vector<std::uint8_t> irx = SyntheticLibsd();
    irx[0x4224 + 0x44] = 0x7E;  // not the room preset's first register
    CHECK_FALSE(m.LoadReverbPresets(irx));
}
