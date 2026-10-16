#include "platform/memcard/folder_card.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <ctime>
#include <fstream>
#include <sstream>

namespace ac04::platform::memcard {

namespace fs = std::filesystem;

namespace {

constexpr std::uint16_t kModeFile = 0x8497;
constexpr std::uint16_t kModeDir = 0x8427;

// Host file time to Unix seconds. MSVC's file_time_type counts 100 ns ticks since 1601-01-01.
std::int64_t UnixSeconds(fs::file_time_type t) {
#ifdef _WIN32
    const std::int64_t ticks = t.time_since_epoch().count();
    return ticks / 10000000 - 11644473600LL;
#else
    return std::chrono::duration_cast<std::chrono::seconds>(t.time_since_epoch()).count();
#endif
}

bool Glob(const char* p, const char* s) {
    if (*p == 0) return *s == 0;
    if (*p == '*') return Glob(p + 1, s) || (*s != 0 && Glob(p, s + 1));
    if (*s == 0) return false;
    return (*p == '?' || *p == *s) && Glob(p + 1, s + 1);
}

bool IsMeta(const std::string& name) { return name.rfind("_pcsx2_", 0) == 0; }

// PCSX2 "filter by game" (on by default): only top-level directories whose name contains the
// game serial or the system directories are loaded (docs/formats/folder-memcard.md §7).
bool PassesFilter(const std::string& name) {
    return name.find("SLUS-20152") != std::string::npos || name.find("DATA-SYSTEM") != std::string::npos ||
           name.find("BWNETCNF") != std::string::npos;
}

std::string ReadAll(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// "/DIR/FILE" -> {"DIR", "FILE"} (empty parts dropped).
std::vector<std::string> SplitPath(const std::string& path) {
    std::vector<std::string> parts;
    std::string cur;
    for (char c : path) {
        if (c == '/') {
            if (!cur.empty()) parts.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) parts.push_back(cur);
    return parts;
}

// `_pcsx2_index` text as PCSX2 writes it: YAML flow style on one line, no trailing newline,
// $ROOT first, then the files in `order`.
std::string FormatIndex(const std::vector<IndexEntry>& entries) {
    std::string out = "{";
    bool first = true;
    for (const IndexEntry& e : entries) {
        if (!first) out += ",";
        first = false;
        out += e.name + ": {";
        bool inner = false;
        if (e.order) {
            out += "order: " + std::to_string(*e.order);
            inner = true;
        }
        if (e.time_created) {
            out += std::string(inner ? "," : "") + "timeCreated: " + std::to_string(*e.time_created);
            inner = true;
        }
        if (e.time_modified) out += std::string(inner ? "," : "") + "timeModified: " + std::to_string(*e.time_modified);
        out += "}";
    }
    return out + "}";
}

bool WriteAll(const fs::path& p, const void* data, std::size_t size) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
    return static_cast<bool>(out);
}

std::vector<IndexEntry> LoadIndex(const fs::path& dir) {
    std::error_code ec;
    const fs::path p = dir / "_pcsx2_index";
    if (!fs::is_regular_file(p, ec)) return {};
    std::vector<IndexEntry> index = ParsePcsx2Index(ReadAll(p));
    for (IndexEntry& e : index) {
        if (e.name == "%ROOT") e.name = "$ROOT";  // legacy key, renamed as PCSX2 does
    }
    return index;
}

void SaveIndex(const fs::path& dir, std::vector<IndexEntry> index) {
    std::stable_sort(index.begin(), index.end(), [](const IndexEntry& a, const IndexEntry& b) {
        const bool ra = a.name == "$ROOT";
        const bool rb = b.name == "$ROOT";
        if (ra != rb) return ra;
        return a.order.value_or(0) < b.order.value_or(0);
    });
    const std::string text = FormatIndex(index);
    WriteAll(dir / "_pcsx2_index", text.data(), text.size());
}

}  // namespace

McTime McTimeFromUnix(std::int64_t seconds) {
    const std::time_t t = static_cast<std::time_t>(seconds);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    McTime m;
    m.sec = static_cast<std::uint8_t>(tm.tm_sec);
    m.min = static_cast<std::uint8_t>(tm.tm_min);
    m.hour = static_cast<std::uint8_t>(tm.tm_hour);
    m.day = static_cast<std::uint8_t>(tm.tm_mday);
    m.month = static_cast<std::uint8_t>(tm.tm_mon + 1);
    m.year = static_cast<std::uint16_t>(tm.tm_year + 1900);
    return m;
}

std::vector<IndexEntry> ParsePcsx2Index(const std::string& text) {
    // Tokens: '{', '}', ',', ':', and bare words. Keys may contain '.', '$', '%', '-'.
    std::vector<IndexEntry> out;
    size_t i = 0;
    auto skip = [&]() {
        while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
    };
    auto word = [&]() {
        skip();
        const size_t start = i;
        while (i < text.size() && text[i] != ':' && text[i] != ',' && text[i] != '{' && text[i] != '}') ++i;
        std::string w = text.substr(start, i - start);
        while (!w.empty() && std::isspace(static_cast<unsigned char>(w.back()))) w.pop_back();
        return w;
    };
    skip();
    if (i >= text.size() || text[i] != '{') return out;
    ++i;
    for (;;) {
        skip();
        if (i >= text.size() || text[i] == '}') break;
        IndexEntry e;
        e.name = word();
        skip();
        if (i >= text.size() || text[i] != ':') break;
        ++i;
        skip();
        if (i < text.size() && text[i] == '{') {
            ++i;
            for (;;) {
                skip();
                if (i >= text.size()) break;
                if (text[i] == '}') {
                    ++i;
                    break;
                }
                const std::string key = word();
                skip();
                if (i >= text.size() || text[i] != ':') break;
                ++i;
                const std::string value = word();
                std::int64_t v = 0;
                try {
                    v = std::stoll(value);
                } catch (...) {
                    v = 0;
                }
                if (key == "order") e.order = v;
                else if (key == "timeCreated") e.time_created = v;
                else if (key == "timeModified") e.time_modified = v;
                skip();
                if (i < text.size() && text[i] == ',') ++i;
            }
        } else {
            word();
        }
        out.push_back(e);
        skip();
        if (i < text.size() && text[i] == ',') ++i;
    }
    return out;
}

FolderCard::FolderCard(fs::path root, bool assume_formatted) : root_(std::move(root)), assume_formatted_(assume_formatted) {
    Load();
}

void FolderCard::Load() {
    std::error_code ec;
    present_ = false;
    formatted_ = false;
    dirs_.clear();
    if (!fs::is_directory(root_, ec)) return;
    const fs::path superblock = root_ / "_pcsx2_superblock";
    if (fs::is_regular_file(superblock, ec)) {
        present_ = true;
        // Formatted when the superblock holds the format magic (byte 0x16 = 'o' of "Sony PS2 Memory
        // Card Format "). An empty superblock file is an unformatted card (PCSX2 run 06).
        const std::string sb = ReadAll(superblock);
        formatted_ = sb.size() > 0x16 && sb.compare(0, 28, "Sony PS2 Memory Card Format ") == 0;
    } else if (assume_formatted_) {
        present_ = true;
        formatted_ = true;
    } else {
        return;
    }
    if (!formatted_) return;
    std::vector<fs::path> dir_paths;
    for (const auto& de : fs::directory_iterator(root_, ec)) {
        const std::string name = de.path().filename().string();
        if (IsMeta(name) || !de.is_directory(ec) || !PassesFilter(name)) continue;
        dir_paths.push_back(de.path());
    }
    std::sort(dir_paths.begin(), dir_paths.end());
    for (const fs::path& dp : dir_paths) {
        Dir d;
        d.self.name = dp.filename().string();
        d.self.is_dir = true;
        d.self.mode = kModeDir;
        std::vector<IndexEntry> index;
        const fs::path index_path = dp / "_pcsx2_index";
        if (fs::is_regular_file(index_path, ec)) index = ParsePcsx2Index(ReadAll(index_path));
        auto find = [&](const std::string& n) -> const IndexEntry* {
            for (const IndexEntry& e : index) {
                if (e.name == n) return &e;
            }
            return nullptr;
        };
        const std::int64_t dir_time = UnixSeconds(fs::last_write_time(dp, ec));
        const IndexEntry* root_entry = find("$ROOT");
        if (root_entry == nullptr) root_entry = find("%ROOT");
        d.self.created = McTimeFromUnix(root_entry && root_entry->time_created ? *root_entry->time_created : dir_time);
        d.self.modified = McTimeFromUnix(root_entry && root_entry->time_modified ? *root_entry->time_modified : dir_time);
        std::int64_t legacy = -1000000;
        for (const auto& fe : fs::directory_iterator(dp, ec)) {
            const std::string name = fe.path().filename().string();
            if (IsMeta(name) || !fe.is_regular_file(ec)) continue;
            McEntry f;
            f.name = name;
            f.mode = kModeFile;
            f.size = static_cast<std::uint32_t>(fs::file_size(fe.path(), ec));
            const std::int64_t host = UnixSeconds(fs::last_write_time(fe.path(), ec));
            const IndexEntry* ie = find(name);
            f.created = McTimeFromUnix(ie && ie->time_created ? *ie->time_created : host);
            f.modified = McTimeFromUnix(ie && ie->time_modified ? *ie->time_modified : host);
            f.order = ie && ie->order ? *ie->order : legacy++;
            d.files.push_back(f);
        }
        std::stable_sort(d.files.begin(), d.files.end(), [](const McEntry& a, const McEntry& b) { return a.order < b.order; });
        d.self.size = static_cast<std::uint32_t>(d.files.size() + 2);
        dirs_.push_back(std::move(d));
    }
}

std::int32_t FolderCard::FreeClusters() const {
    if (!present_ || !formatted_) return 0;
    std::int64_t used = (2 + static_cast<std::int64_t>(dirs_.size()) + 1) / 2;  // root directory entries
    for (const Dir& d : dirs_) {
        used += (static_cast<std::int64_t>(d.files.size()) + 2 + 1) / 2;
        for (const McEntry& f : d.files) used += (static_cast<std::int64_t>(f.size) + 1023) / 1024;
    }
    return static_cast<std::int32_t>(8001 - used);
}

std::optional<std::vector<std::uint8_t>> FolderCard::ReadFile(const std::string& path) const {
    if (!present_ || !formatted_ || !FileExists(path)) return std::nullopt;
    const std::vector<std::string> parts = SplitPath(path);
    const std::string data = ReadAll(root_ / parts[0] / parts[1]);
    return std::vector<std::uint8_t>(data.begin(), data.end());
}

bool FolderCard::FileExists(const std::string& path) const {
    const std::vector<std::string> parts = SplitPath(path);
    if (parts.size() != 2) return false;
    for (const Dir& d : dirs_) {
        if (d.self.name != parts[0]) continue;
        for (const McEntry& f : d.files) {
            if (f.name == parts[1]) return true;
        }
    }
    return false;
}

bool FolderCard::DirExists(const std::string& path) const {
    const std::vector<std::string> parts = SplitPath(path);
    if (parts.size() != 1) return false;
    for (const Dir& d : dirs_) {
        if (d.self.name == parts[0]) return true;
    }
    return false;
}

int FolderCard::MakeDir(const std::string& path, std::int64_t now) {
    if (!present_) return -4;
    if (!formatted_) return -2;
    const std::vector<std::string> parts = SplitPath(path);
    if (parts.size() != 1) return -4;  // HYPOTHESIS: libmc result for a missing parent
    if (DirExists(path)) return -4;    // HYPOTHESIS: libmc result for an existing directory
    if (FreeClusters() < 1) return -3;
    std::error_code ec;
    // The port's own saves folder becomes a real PCSX2 folder card with its first save (§8).
    if (assume_formatted_ && !fs::is_regular_file(root_ / "_pcsx2_superblock", ec) && Format() != 0) return -4;
    const fs::path dir = root_ / parts[0];
    fs::create_directories(dir, ec);
    if (ec) return -4;
    IndexEntry root;
    root.name = "$ROOT";
    root.time_created = now;
    root.time_modified = now;
    SaveIndex(dir, {root});
    Load();
    return 0;
}

int FolderCard::WriteFile(const std::string& path, const std::vector<std::uint8_t>& data, std::int64_t now) {
    if (!present_) return -4;
    if (!formatted_) return -2;
    const std::vector<std::string> parts = SplitPath(path);
    if (parts.size() != 2 || !DirExists("/" + parts[0])) return -4;
    // Space: the clusters of the new contents against the free clusters plus those of the old file.
    std::int64_t old_clusters = 0;
    bool exists = false;
    for (const Dir& d : dirs_) {
        if (d.self.name != parts[0]) continue;
        for (const McEntry& f : d.files) {
            if (f.name == parts[1]) {
                exists = true;
                old_clusters = (static_cast<std::int64_t>(f.size) + 1023) / 1024;
            }
        }
    }
    const std::int64_t new_clusters = (static_cast<std::int64_t>(data.size()) + 1023) / 1024;
    const std::int64_t entry_clusters = exists ? 0 : 1;  // HYPOTHESIS: room for one more directory entry
    if (FreeClusters() + old_clusters < new_clusters + entry_clusters) return -3;
    const fs::path dir = root_ / parts[0];
    if (!WriteAll(dir / parts[1], data.data(), data.size())) return -4;
    std::vector<IndexEntry> index = LoadIndex(dir);
    std::int64_t max_order = 0;
    bool found = false;
    for (const IndexEntry& e : index) {
        if (e.order) max_order = std::max(max_order, *e.order);
        if (e.name == parts[1]) found = true;
    }
    if (!found) {
        IndexEntry e;
        e.name = parts[1];
        e.order = max_order + 1;
        index.push_back(e);
    }
    bool has_root = false;
    for (IndexEntry& e : index) {
        if (e.name == parts[1]) {
            e.time_created = now;
            e.time_modified = now;
        } else if (e.name == "$ROOT") {
            has_root = true;
            if (!e.time_created) e.time_created = now;
            e.time_modified = now;
        }
    }
    if (!has_root) {
        IndexEntry r;
        r.name = "$ROOT";
        r.time_created = now;
        r.time_modified = now;
        index.push_back(r);
    }
    SaveIndex(dir, index);
    Load();
    return 0;
}

int FolderCard::Delete(const std::string& path) {
    if (!present_) return -4;
    if (!formatted_) return -2;
    const std::vector<std::string> parts = SplitPath(path);
    std::error_code ec;
    if (parts.size() == 1) {
        for (const Dir& d : dirs_) {
            if (d.self.name != parts[0]) continue;
            if (!d.files.empty()) return -6;
            fs::remove_all(root_ / parts[0], ec);
            Load();
            return ec ? -4 : 0;
        }
        return -4;
    }
    if (parts.size() != 2 || !FileExists(path)) return -4;
    const fs::path dir = root_ / parts[0];
    fs::remove(dir / parts[1], ec);
    if (ec) return -4;
    std::vector<IndexEntry> index = LoadIndex(dir);
    index.erase(std::remove_if(index.begin(), index.end(), [&](const IndexEntry& e) { return e.name == parts[1]; }),
                index.end());
    SaveIndex(dir, index);
    Load();
    return 0;
}

int FolderCard::Format() {
    std::error_code ec;
    fs::create_directories(root_, ec);
    std::vector<std::uint8_t> sb(0x2000, 0);
    auto put = [&](std::size_t off, const void* p, std::size_t n) { std::memcpy(sb.data() + off, p, n); };
    auto u16 = [&](std::size_t off, std::uint16_t v) { put(off, &v, 2); };
    auto u32 = [&](std::size_t off, std::uint32_t v) { put(off, &v, 4); };
    put(0, "Sony PS2 Memory Card Format ", 28);
    put(0x1C, "1.2.0.0", 7);
    u16(0x28, 0x200);
    u16(0x2A, 2);
    u16(0x2C, 16);
    u16(0x2E, 0xFF00);
    u32(0x30, 0x2000);
    u32(0x34, 0x29);
    u32(0x38, 0x1FC7);
    u32(0x3C, 0);
    u32(0x40, 0x3FF);
    u32(0x44, 0x3FE);
    u32(0x50, 8);
    for (std::size_t i = 0; i < 32; ++i) u32(0xD0 + 4 * i, 0xFFFFFFFFu);
    sb[0x150] = 2;
    sb[0x151] = 0x2B;
    if (!WriteAll(root_ / "_pcsx2_superblock", sb.data(), sb.size())) return -4;
    Load();
    return 0;
}

std::vector<McEntry> FolderCard::GetDir(const std::string& path, std::size_t max) const {
    std::vector<McEntry> out;
    if (!present_ || !formatted_) return out;
    std::vector<std::string> parts;
    std::string cur;
    for (char c : path) {
        if (c == '/') {
            if (!cur.empty()) parts.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) parts.push_back(cur);
    auto add = [&](const McEntry& e) {
        if (out.size() < max) out.push_back(e);
    };
    if (parts.size() == 1) {
        for (const Dir& d : dirs_) {
            if (Glob(parts[0].c_str(), d.self.name.c_str())) add(d.self);
        }
    } else if (parts.size() == 2) {
        for (const Dir& d : dirs_) {
            if (d.self.name != parts[0]) continue;
            const bool wildcard = parts[1].find_first_of("*?") != std::string::npos;
            if (wildcard) {
                McEntry dot = d.self;
                dot.name = ".";
                add(dot);
                McEntry dotdot = d.self;
                dotdot.name = "..";
                add(dotdot);
            }
            for (const McEntry& f : d.files) {
                if (Glob(parts[1].c_str(), f.name.c_str())) add(f);
            }
        }
    }
    return out;
}

}  // namespace ac04::platform::memcard
