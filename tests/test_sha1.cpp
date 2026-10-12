#include <doctest/doctest.h>

#include <string>

#include "platform/sha1.h"

using ac04::platform::Sha1;
using ac04::platform::Sha1Hex;

// Known-answer vectors from FIPS 180 / RFC 3174.
TEST_CASE("sha1: standard vectors") {
    CHECK(Sha1Hex("", 0) == "da39a3ee5e6b4b0d3255bfef95601890afd80709");
    CHECK(Sha1Hex("abc", 3) == "a9993e364706816aba3e25717850c26c9cd0d89d");
    const std::string two_blocks = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    CHECK(Sha1Hex(two_blocks.data(), two_blocks.size()) == "84983e441c3bd26ebaae4aa1f95129e5e54670f1");
}

TEST_CASE("sha1: one million 'a' fed in uneven chunks") {
    Sha1 s;
    const std::string chunk(997, 'a');
    size_t left = 1000000;
    while (left > 0) {
        const size_t n = left < chunk.size() ? left : chunk.size();
        s.Update(chunk.data(), n);
        left -= n;
    }
    CHECK(s.FinalHex() == "34aa973cd4c4daa4f61eeb2bdbad27316534016f");
}
