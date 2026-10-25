// PCSX2 folder memory card view and PNG writer. Synthetic folders in the temp directory only.
#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "platform/memcard/folder_card.h"
#include "platform/png_writer.h"

using namespace ac04::platform;
namespace fs = std::filesystem;

namespace {

fs::path TempDir(const char* name) {
    const fs::path p = fs::temp_directory_path() / ("ac04_test_" + std::string(name) + "_" +
                                                    std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(p);
    return p;
}

void WriteFile(const fs::path& p, const std::string& data) {
    std::ofstream f(p, std::ios::binary);
    f << data;
}

}  // namespace

TEST_CASE("folder card: presence, format and free space") {
    const fs::path root = TempDir("card");
    {
        memcard::FolderCard none(root / "missing", false);
        CHECK_FALSE(none.Present());
    }
    WriteFile(root / "_pcsx2_superblock", "");
    {
        memcard::FolderCard unformatted(root, false);
        CHECK(unformatted.Present());
        CHECK_FALSE(unformatted.Formatted());
        CHECK(unformatted.FreeClusters() == 0);
    }
    std::string sb = "Sony PS2 Memory Card Format 1.2.0.0";
    sb.resize(0x2000, '\0');
    WriteFile(root / "_pcsx2_superblock", sb);
    {
        memcard::FolderCard empty(root, false);
        CHECK(empty.Formatted());
        CHECK(empty.FreeClusters() == 8000);  // an empty formatted 8 MB card
    }
    // The AC04 save of PCSX2 run 06: three files of 0x3C4, 0xCB98 and 0x4B10 bytes -> 7925 KB free.
    const fs::path dir = root / "BASLUS-20152AC04";
    fs::create_directories(dir);
    WriteFile(dir / "icon.sys", std::string(0x3C4, 'a'));
    WriteFile(dir / "icon1.ico", std::string(0xCB98, 'b'));
    WriteFile(dir / "BASLUS-20152AC04", std::string(0x4B10, 'c'));
    WriteFile(dir / "_pcsx2_index",
              "{$ROOT: {timeCreated: 1791770213,timeModified: 1791961353},icon.sys: {order: 1,timeCreated: 1791770213,"
              "timeModified: 1791770213},icon1.ico: {order: 2,timeCreated: 1791770213,timeModified: 1791770213},"
              "BASLUS-20152AC04: {order: 3,timeCreated: 1791961353,timeModified: 1791961353}}");
    memcard::FolderCard card(root, false);
    CHECK(card.FreeClusters() == 7925);
    auto d = card.GetDir("/BASLUS-20152AC04", 18);
    REQUIRE(d.size() == 1);
    CHECK(d[0].mode == 0x8427);
    auto f = card.GetDir("/BASLUS-20152AC04/BASLUS-20152AC04", 18);
    REQUIRE(f.size() == 1);
    CHECK(f[0].size == 0x4B10);
    CHECK(f[0].mode == 0x8497);
    // 1791961353 = 2026-10-14 07:02:33 UTC (PCSX2 shows UTC, run 06).
    CHECK(f[0].modified.year == 2026);
    CHECK(f[0].modified.month == 10);
    CHECK(f[0].modified.day == 14);
    CHECK(f[0].modified.hour == 7);
    CHECK(f[0].modified.sec == 33);
    auto all = card.GetDir("/BASLUS-20152AC04/*", 18);
    REQUIRE(all.size() == 5);  // ".", "..", then the files in index order
    CHECK(all[2].name == "icon.sys");
    CHECK(all[4].name == "BASLUS-20152AC04");
    fs::remove_all(root);
}

TEST_CASE("folder card: writing a save as CreateMcSave does (S2b)") {
    const fs::path root = TempDir("write");
    memcard::FolderCard card(root, true);  // the port's own saves folder: no superblock yet
    REQUIRE(card.Formatted());
    CHECK(card.MakeDir("/BASLUS-20152AC04", 1000) == 0);
    CHECK(fs::file_size(root / "_pcsx2_superblock") == 0x2000);  // a real folder card now
    CHECK(card.MakeDir("/BASLUS-20152AC04", 1001) == -4);
    CHECK(card.WriteFile("/BASLUS-20152AC04/icon.sys", std::vector<std::uint8_t>(0x3C4, 1), 1000) == 0);
    CHECK(card.WriteFile("/BASLUS-20152AC04/icon1.ico", std::vector<std::uint8_t>(0xCB98, 2), 1000) == 0);
    CHECK(card.WriteFile("/BASLUS-20152AC04/BASLUS-20152AC04", std::vector<std::uint8_t>(0x4B10, 3), 1000) == 0);
    CHECK(card.FreeClusters() == 7925);  // as PCSX2 reports for the same save
    CHECK(card.WriteFile("/NODIR/x", std::vector<std::uint8_t>(4, 0), 1000) == -4);
    // Overwrite: order kept, both times renewed, $ROOT modified (seen on a PCSX2 folder card).
    CHECK(card.WriteFile("/BASLUS-20152AC04/BASLUS-20152AC04", std::vector<std::uint8_t>(0x4B10, 4), 2000) == 0);
    std::string index;
    {
        std::ifstream in(root / "BASLUS-20152AC04" / "_pcsx2_index", std::ios::binary);
        index.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    CHECK(index ==
          "{$ROOT: {timeCreated: 1000,timeModified: 2000},icon.sys: {order: 1,timeCreated: 1000,timeModified: 1000},"
          "icon1.ico: {order: 2,timeCreated: 1000,timeModified: 1000},BASLUS-20152AC04: {order: 3,timeCreated: 2000,"
          "timeModified: 2000}}");
    auto data = card.ReadFile("/BASLUS-20152AC04/BASLUS-20152AC04");
    REQUIRE(data.has_value());
    CHECK(data->size() == 0x4B10);
    CHECK((*data)[0] == 4);
    CHECK(card.Delete("/BASLUS-20152AC04") == -6);  // not empty
    CHECK(card.Delete("/BASLUS-20152AC04/icon.sys") == 0);
    CHECK(card.Delete("/BASLUS-20152AC04/icon.sys") == -4);
    CHECK(card.Delete("/BASLUS-20152AC04/icon1.ico") == 0);
    CHECK(card.Delete("/BASLUS-20152AC04/BASLUS-20152AC04") == 0);
    CHECK(card.Delete("/BASLUS-20152AC04") == 0);
    CHECK(card.FreeClusters() == 8000);
    fs::remove_all(root);
}

TEST_CASE("PNG writer: valid stream") {
    std::vector<std::uint8_t> rgba(3 * 2 * 4);
    for (std::size_t i = 0; i < rgba.size(); ++i) rgba[i] = static_cast<std::uint8_t>(i * 11);
    const auto png = EncodePng(rgba.data(), 3, 2);
    REQUIRE(png.size() > 33);
    CHECK(png[1] == 'P');
    // IDAT starts after the signature (8) and IHDR (25 bytes); inflate it and check the pixels.
    std::size_t len = static_cast<std::size_t>(png[33]) << 24 | png[34] << 16 | png[35] << 8 | png[36];
    CHECK(std::string(png.begin() + 37, png.begin() + 41) == "IDAT");
    // zlib header (2 bytes), one final stored block {1, LEN, ~LEN}, the raw scanlines, Adler-32.
    const std::uint8_t* z = png.data() + 41;
    CHECK(z[2] == 1);
    const std::size_t n = static_cast<std::size_t>(z[3] | z[4] << 8);
    REQUIRE(n == 2 * (1 + 3 * 4));
    CHECK(len == 2 + 5 + n + 4);
    const std::vector<std::uint8_t> raw(z + 7, z + 7 + n);
    CHECK(raw[0] == 0);
    CHECK(raw[1 + 5] == rgba[5]);
    CHECK(raw[13 + 1 + 3] == rgba[12 + 3]);
}

TEST_CASE("folder card: a new card made from scratch has PCSX2's format fields") {
    // At first start the port formats its empty saves folder (main.cpp). The superblock's defined
    // fields (0x00-0x153) must be those of a card PCSX2 formatted (owner's cards, docs/formats/
    // folder-memcard.md §9); PCSX2 reads only those (FolderMemoryCard: IsFormatted is byte 0x16).
    const fs::path root = TempDir("fresh");
    memcard::FolderCard card(root / "saves", true);
    REQUIRE(card.Format() == 0);
    std::string sb;
    {
        std::ifstream in(root / "saves" / "_pcsx2_superblock", std::ios::binary);
        sb.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    REQUIRE(sb.size() == 0x2000);
    CHECK(sb.compare(0, 28, "Sony PS2 Memory Card Format ") == 0);
    CHECK(sb[0x16] == 0x6F);  // PCSX2's IsFormatted test
    CHECK(sb.compare(0x1C, 7, "1.2.0.0") == 0);
    auto u16 = [&](std::size_t o) { return static_cast<unsigned>(static_cast<unsigned char>(sb[o]) | static_cast<unsigned char>(sb[o + 1]) << 8); };
    auto u32 = [&](std::size_t o) { return u16(o) | u16(o + 2) << 16; };
    CHECK(u16(0x28) == 0x200);    // page length
    CHECK(u16(0x2A) == 2);        // pages per cluster
    CHECK(u16(0x2C) == 16);       // pages per block
    CHECK(u32(0x30) == 0x2000);   // clusters per card (8 MB)
    CHECK(u32(0x34) == 0x29);     // alloc offset
    CHECK(u32(0x38) == 0x1FC7);   // alloc end
    CHECK(u32(0x3C) == 0);        // root directory cluster
    CHECK(u32(0x40) == 0x3FF);    // backup block 1
    CHECK(u32(0x44) == 0x3FE);    // backup block 2
    CHECK(u32(0x50) == 8);        // indirect FAT cluster list
    CHECK(u32(0xD0) == 0xFFFFFFFFu);  // no bad blocks
    CHECK(static_cast<unsigned char>(sb[0x150]) == 2);
    CHECK(static_cast<unsigned char>(sb[0x151]) == 0x2B);
    memcard::FolderCard reopened(root / "saves", false);  // as PCSX2 would see it: no assumption
    CHECK(reopened.Present());
    CHECK(reopened.Formatted());
    CHECK(reopened.FreeClusters() == 8000);  // only the root directory's entries
    fs::remove_all(root);
}
