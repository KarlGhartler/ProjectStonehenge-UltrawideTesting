#include <doctest/doctest.h>

#include "platform/iso9660.h"
#include "synthetic_iso.h"

using namespace ac04::platform;

TEST_CASE("iso9660: volume descriptor") {
    Iso9660 iso;
    std::string error;
    REQUIRE(iso.Open(test::MakeTestIso(), error));
    CHECK(iso.Volume().system_id == "PLAYSTATION");
    CHECK(iso.Volume().volume_id == "TESTVOLUME");
    CHECK(iso.Volume().volume_space_blocks == 23);
}

TEST_CASE("iso9660: exact-case lookup, optional version suffix, both separators") {
    Iso9660 iso;
    std::string error;
    REQUIRE(iso.Open(test::MakeTestIso(), error));
    IsoEntry e;
    REQUIRE(iso.Find("SLUS_TEST.01", e));
    CHECK(e.lsn == 20);
    CHECK(e.size == 3000);
    CHECK_FALSE(e.is_directory);
    CHECK(iso.Find("\\SLUS_TEST.01;1", e));
    REQUIRE(iso.Find("BIN/DATA.CDP", e));
    CHECK(e.lsn == 22);
    CHECK(iso.Find("\\BIN\\DATA.CDP;1", e));
    // No case folding (ADR-005).
    CHECK_FALSE(iso.Find("bin/data.cdp", e));
    CHECK_FALSE(iso.Find("slus_test.01", e));
    CHECK_FALSE(iso.Find("BIN/MISSING.TBL", e));
}

TEST_CASE("iso9660: directory listing skips . and ..") {
    Iso9660 iso;
    std::string error;
    REQUIRE(iso.Open(test::MakeTestIso(), error));
    auto root = iso.List(iso.Volume().root);
    REQUIRE(root.size() == 2);
    CHECK(root[0].name == "BIN");
    CHECK(root[0].is_directory);
    CHECK(root[1].name == "SLUS_TEST.01");
}

TEST_CASE("iso9660: file contents across sectors") {
    Iso9660 iso;
    std::string error;
    REQUIRE(iso.Open(test::MakeTestIso(), error));
    IsoEntry e;
    REQUIRE(iso.Find("SLUS_TEST.01", e));
    std::vector<std::uint8_t> data;
    REQUIRE(iso.ReadFile(e, data));
    REQUIRE(data.size() == 3000);
    CHECK(data[0] == 0);
    CHECK(data[2047] == static_cast<std::uint8_t>((2047 * 7) & 0xFF));
    CHECK(data[2999] == static_cast<std::uint8_t>((2999 * 7) & 0xFF));
}

TEST_CASE("iso9660: rejects non-ISO data") {
    Iso9660 iso;
    std::string error;
    auto junk = std::make_shared<test::MemorySectorSource>(std::vector<std::uint8_t>(20 * kSectorSize, 0xAA));
    CHECK_FALSE(iso.Open(junk, error));
    CHECK_FALSE(error.empty());
    auto tiny = std::make_shared<test::MemorySectorSource>(std::vector<std::uint8_t>(100, 0));
    CHECK_FALSE(iso.Open(tiny, error));
}
