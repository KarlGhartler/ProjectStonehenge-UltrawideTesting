// A PCSX2 Folder Memory Card (docs/formats/folder-memcard.md) as libmc sees it through PCSX2: card
// presence and format, free space, directory entries with the sizes, modes and times PCSX2
// presents, and (slice S2b) file reads, writes, mkdir, delete and format with the `_pcsx2_index`
// bookkeeping PCSX2 does, so a save folder can be copied between the port and PCSX2.
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace ac04::platform::memcard {

// PS2 memory card time: {u8 unused, sec, min, hour, day, month, u16 year} (PCSX2 converts host
// times as UTC).
struct McTime {
    std::uint8_t sec = 0, min = 0, hour = 0, day = 0, month = 0;
    std::uint16_t year = 0;
};
McTime McTimeFromUnix(std::int64_t seconds);

struct McEntry {
    std::string name;
    bool is_dir = false;
    std::uint32_t size = 0;      // bytes (file) or number of entries including "." and ".." (directory)
    std::uint16_t mode = 0;      // 0x8497 file, 0x8427 directory (PCSX2 defaults)
    McTime created, modified;
    std::int64_t order = 0;      // PCSX2 index order (files); directories sort first
};

// Minimal parser for PCSX2's `_pcsx2_index` (YAML flow style, one line):
// {$ROOT: {timeCreated: T,timeModified: T},name: {order: N,timeCreated: T,timeModified: T},...}
struct IndexEntry {
    std::string name;
    std::optional<std::int64_t> order, time_created, time_modified;
};
std::vector<IndexEntry> ParsePcsx2Index(const std::string& text);

class FolderCard {
public:
    // `root` is the card folder. `assume_formatted`: a folder without `_pcsx2_superblock` counts as
    // a formatted empty card (the port's own saves folder before the first save) instead of no card.
    FolderCard(std::filesystem::path root, bool assume_formatted);

    bool Present() const { return present_; }
    bool Formatted() const { return formatted_; }
    // Free clusters (1 KiB) as reported by sceMcGetInfo: 8001 minus the clusters used by files
    // (ceil(size / 1024)) and by directory entries (two 512-byte entries per cluster, root included).
    std::int32_t FreeClusters() const;

    // Entries matching `path` ("/DIR", "/DIR/FILE", "/DIR/*", "/*"), in card order, at most `max`.
    std::vector<McEntry> GetDir(const std::string& path, std::size_t max) const;

    // Results below are libmc results: 0 done, -2 unformatted, -3 card full, -4 no such entry,
    // -6 directory not empty. `now` is the host time in Unix seconds (stored as PCSX2 does, UTC).
    // Contents of file "/DIR/FILE", or nothing when it does not exist.
    std::optional<std::vector<std::uint8_t>> ReadFile(const std::string& path) const;
    bool FileExists(const std::string& path) const;
    bool DirExists(const std::string& path) const;
    // Creates directory "/DIR" (its `_pcsx2_index` holds the $ROOT times).
    int MakeDir(const std::string& path, std::int64_t now);
    // Creates or replaces file "/DIR/FILE". A new file gets the next `order`; a replaced file keeps
    // its order and gets new created/modified times (seen on PCSX2 folder cards, §9); $ROOT's
    // modified time becomes `now`.
    int WriteFile(const std::string& path, const std::vector<std::uint8_t>& data, std::int64_t now);
    // Deletes file "/DIR/FILE" or the empty directory "/DIR".
    int Delete(const std::string& path);
    // Formats the card: writes `_pcsx2_superblock` (0x2000 bytes, §9 field values).
    int Format();

private:
    struct Dir {
        McEntry self;
        std::vector<McEntry> files;
    };
    void Load();

    std::filesystem::path root_;
    bool assume_formatted_ = false;
    bool present_ = false;
    bool formatted_ = false;
    std::vector<Dir> dirs_;
};

}  // namespace ac04::platform::memcard
