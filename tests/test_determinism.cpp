// Determinism tools (docs/port/s5-plan.md P1): per-tick input log and RAM hash log.
#include <doctest/doctest.h>

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <filesystem>
#include <vector>

#include "platform/hash_log.h"
#include "platform/input_log.h"
#include "platform/p2m2_reader.h"

using namespace ac04::platform;

TEST_CASE("input log: ticks round-trip; the last state is held, before the first tick neutral") {
    const auto path = std::filesystem::temp_directory_path() / "ac04_test_input.ac4i";
    {
        InputLogWriter w;
        REQUIRE(w.Open(path));
        TickInput a;
        a.vblank = 10;
        a.pad[0].connected = true;
        a.pad[0].buttons = 0xFFF7;  // start
        a.pad[0].lx = 0x20;
        a.pad[0].pressure[6] = 0xFF;
        a.pad[1].connected = false;
        w.Write(a);
        TickInput b = a;
        b.vblank = 11;
        b.pad[0].buttons = 0xFFFF;
        w.Write(b);
    }
    InputLogReader r;
    REQUIRE(r.Open(path));
    CHECK(r.Size() == 2);
    PadState p[2];
    REQUIRE(r.Get(10, p));
    CHECK(p[0].connected);
    CHECK(p[0].buttons == 0xFFF7);
    CHECK(p[0].lx == 0x20);
    CHECK(p[0].pressure[6] == 0xFF);
    CHECK_FALSE(p[1].connected);
    REQUIRE(r.Get(500, p));
    CHECK(p[0].buttons == 0xFFFF);
    REQUIRE(r.Get(3, p));
    CHECK(p[0].connected);
    CHECK(p[0].lx == 0x7F);
    std::filesystem::remove(path);
}

TEST_CASE("RAM hash: equal data gives equal hashes, one changed byte changes it") {
    std::vector<std::uint8_t> a(1 << 16, 0x5A), b = a;
    CHECK(HashBytes(a.data(), a.size()) == HashBytes(b.data(), b.size()));
    b[12345] ^= 1;
    CHECK(HashBytes(a.data(), a.size()) != HashBytes(b.data(), b.size()));
}

TEST_CASE("p2m2 reader: header checks and the 18 pad bytes of both ports (docs/formats/p2m2.md)") {
    const auto path = std::filesystem::temp_directory_path() / "ac04_test.p2m2";
    {
        std::vector<std::uint8_t> f(0x23A + 2 * 36, 0);
        f[0] = 1;                                  // file version
        std::memcpy(&f[0x132], "ACE COMBAT 04", 13);
        const std::uint32_t total = 2;
        std::memcpy(&f[0x231], &total, 4);
        std::uint8_t* rec1 = &f[0x23A + 36];      // frame 1, port 0
        rec1[0] = 0xF7;                            // Start pressed
        rec1[1] = 0xBF;                            // Cross pressed
        rec1[4] = 0x10;                            // left stick X
        rec1[12] = 0xFF;                           // pressure Cross
        std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(f.data()), static_cast<std::streamsize>(f.size()));
    }
    P2m2Reader r;
    std::string err;
    REQUIRE(r.Open(path, err));
    CHECK(r.FrameCount() == 2);
    CHECK(r.GameName() == "ACE COMBAT 04");
    CHECK_FALSE(r.FromSavestate());
    PadState p[2];
    REQUIRE(r.Frame(1, p));
    CHECK(p[0].connected);
    CHECK(p[0].buttons == 0xBFF7);
    CHECK(p[0].lx == 0x10);
    CHECK(p[0].pressure[6] == 0xFF);  // Cross is the 7th pressure in scePadRead order
    CHECK_FALSE(p[1].connected);
    CHECK_FALSE(r.Frame(2, p));
    std::filesystem::remove(path);
}
