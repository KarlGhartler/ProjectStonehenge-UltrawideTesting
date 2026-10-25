// ISO 9660 reader for the user's disc image (2048-byte sectors, docs/platform/executable.md).
// File names are matched exactly as stored on the disc (ADR-005: no case folding, e.g. "DATA.CDP");
// the ";1" version suffix is optional in lookups.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ac04::platform {

constexpr std::uint32_t kSectorSize = 2048;

// Where sectors come from: the image file, or memory in tests.
class SectorSource {
public:
    virtual ~SectorSource() = default;
    virtual std::uint64_t SizeBytes() const = 0;
    virtual bool ReadSectors(std::uint32_t lsn, std::uint32_t count, void* dst) const = 0;
};

struct IsoEntry {
    std::string name;          // as stored, without the ";1" version suffix
    std::uint32_t lsn = 0;     // first sector
    std::uint32_t size = 0;    // bytes
    bool is_directory = false;
};

struct PrimaryVolume {
    std::string system_id;     // trailing spaces removed
    std::string volume_id;
    std::uint32_t volume_space_blocks = 0;
    IsoEntry root;
};

class Iso9660 {
public:
    // Reads and checks the primary volume descriptor (sector 16).
    bool Open(std::shared_ptr<const SectorSource> source, std::string& error);

    const PrimaryVolume& Volume() const { return volume_; }

    // Looks up "BIN/DATA.CDP", "\\SLUS_201.52;1", "SLUS_201.52" (case-sensitive). Separators '/'
    // or '\\'. Returns false if any component is missing.
    bool Find(const std::string& path, IsoEntry& out) const;
    std::vector<IsoEntry> List(const IsoEntry& directory) const;

    // Reads a whole file (small files only: the executable, tables).
    bool ReadFile(const IsoEntry& entry, std::vector<std::uint8_t>& out) const;
    bool ReadSectors(std::uint32_t lsn, std::uint32_t count, void* dst) const;

    const SectorSource* Source() const { return source_.get(); }

private:
    std::shared_ptr<const SectorSource> source_;
    PrimaryVolume volume_;
};

// Parses one directory record at `p` (at most `avail` bytes). Returns its length, or 0 at the end
// of the records in this sector.
std::uint32_t ParseDirectoryRecord(const std::uint8_t* p, std::uint32_t avail, IsoEntry& out);

}  // namespace ac04::platform
