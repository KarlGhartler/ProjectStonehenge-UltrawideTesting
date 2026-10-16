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
