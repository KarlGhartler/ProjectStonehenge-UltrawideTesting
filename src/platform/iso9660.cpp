#include "platform/iso9660.h"

#include <cstring>

namespace ac04::platform {
namespace {

std::uint32_t Le32(const std::uint8_t* p) {
    return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16) |
           (std::uint32_t(p[3]) << 24);
}

std::string Trim(const std::uint8_t* p, size_t n) {
    std::string s(reinterpret_cast<const char*>(p), n);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\0')) s.pop_back();
    return s;
}

std::vector<std::string> SplitPath(const std::string& path) {
    std::vector<std::string> parts;
    std::string cur;
    for (char c : path) {
        if (c == '/' || c == '\\') {
            if (!cur.empty()) parts.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) parts.push_back(cur);
    return parts;
}

std::string StripVersion(const std::string& name) {
    const auto semi = name.find(';');
    return semi == std::string::npos ? name : name.substr(0, semi);
}

}  // namespace

std::uint32_t ParseDirectoryRecord(const std::uint8_t* p, std::uint32_t avail, IsoEntry& out) {
    if (avail < 1) return 0;
    const std::uint32_t len = p[0];
    if (len == 0 || len < 34 || len > avail) return 0;
    out.lsn = Le32(p + 2);
    out.size = Le32(p + 10);
    out.is_directory = (p[25] & 0x02) != 0;
    const std::uint8_t name_len = p[32];
    if (33u + name_len > len) return 0;
    if (name_len == 1 && (p[33] == 0 || p[33] == 1)) {
        out.name = p[33] == 0 ? "." : "..";
    } else {
        out.name = StripVersion(std::string(reinterpret_cast<const char*>(p + 33), name_len));
    }
    return len;
}

bool Iso9660::Open(std::shared_ptr<const SectorSource> source, std::string& error) {
    source_ = std::move(source);
    std::uint8_t pvd[kSectorSize];
    if (!source_ || source_->SizeBytes() < 17ull * kSectorSize || !source_->ReadSectors(16, 1, pvd)) {
        error = "the file is too small to be a disc image";
        return false;
    }
    if (pvd[0] != 1 || std::memcmp(pvd + 1, "CD001", 5) != 0) {
        error = "the file is not an ISO 9660 disc image (no primary volume descriptor at sector 16)";
        return false;
    }
    const std::uint32_t block = pvd[128] | (pvd[129] << 8);
    if (block != kSectorSize) {
        error = "unsupported logical block size " + std::to_string(block);
        return false;
    }
    volume_.system_id = Trim(pvd + 8, 32);
    volume_.volume_id = Trim(pvd + 40, 32);
    volume_.volume_space_blocks = Le32(pvd + 80);
    if (ParseDirectoryRecord(pvd + 156, 34, volume_.root) == 0 || !volume_.root.is_directory) {
        error = "invalid root directory record";
        return false;
    }
    volume_.root.name = "/";
    return true;
}

std::vector<IsoEntry> Iso9660::List(const IsoEntry& directory) const {
    std::vector<IsoEntry> entries;
    if (!directory.is_directory) return entries;
    const std::uint32_t sectors = (directory.size + kSectorSize - 1) / kSectorSize;
    std::vector<std::uint8_t> buf(static_cast<size_t>(sectors) * kSectorSize);
    if (sectors == 0 || !ReadSectors(directory.lsn, sectors, buf.data())) return entries;
    for (std::uint32_t s = 0; s < sectors; ++s) {
        // Records never cross a sector boundary; a zero length byte ends the sector.
        std::uint32_t off = 0;
        const std::uint8_t* sector = buf.data() + static_cast<size_t>(s) * kSectorSize;
        while (off < kSectorSize) {
            IsoEntry e;
            const std::uint32_t len = ParseDirectoryRecord(sector + off, kSectorSize - off, e);
            if (len == 0) break;
            if (e.name != "." && e.name != "..") entries.push_back(e);
            off += len;
        }
    }
    return entries;
}

bool Iso9660::Find(const std::string& path, IsoEntry& out) const {
    IsoEntry cur = volume_.root;
    for (const std::string& part : SplitPath(path)) {
        const std::string want = StripVersion(part);
        bool found = false;
        for (const IsoEntry& e : List(cur)) {
            if (e.name == want) {  // exact, case-sensitive
                cur = e;
                found = true;
                break;
            }
        }
        if (!found) return false;
    }
    out = cur;
    return true;
}

bool Iso9660::ReadFile(const IsoEntry& entry, std::vector<std::uint8_t>& out) const {
    const std::uint32_t sectors = (entry.size + kSectorSize - 1) / kSectorSize;
    out.resize(static_cast<size_t>(sectors) * kSectorSize);
    if (sectors > 0 && !ReadSectors(entry.lsn, sectors, out.data())) return false;
    out.resize(entry.size);
    return true;
}

bool Iso9660::ReadSectors(std::uint32_t lsn, std::uint32_t count, void* dst) const {
    return source_ && source_->ReadSectors(lsn, count, dst);
}

}  // namespace ac04::platform
