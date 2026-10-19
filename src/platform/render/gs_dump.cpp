#include "platform/render/gs_dump.h"

#include <cstring>
#include <fstream>
#include <iterator>

namespace ac04::render {
namespace {

constexpr std::uint32_t kStateVersion = 9;
constexpr std::size_t kMemoryOffset = 0x1A9;  // inside the GS state of version 9
constexpr std::size_t kMemorySize = 4u * 1024u * 1024u;
constexpr std::size_t kPrivilegedSize = 8192;

std::uint32_t U32(const std::vector<std::uint8_t>& d, std::size_t o) {
    std::uint32_t v = 0;
    std::memcpy(&v, d.data() + o, 4);
    return v;
}

}  // namespace

bool LoadGsDump(const std::filesystem::path& path, GsDump& dump, std::string& error) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        error = "cannot open the GS dump";
        return false;
    }
    const std::vector<std::uint8_t> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (d.size() < 8 + 36 || U32(d, 0) != 0xFFFFFFFFu) {
        error = "not an uncompressed PCSX2 GS dump (compressed .zst/.xz dumps must be unpacked first)";
        return false;
    }
    const std::size_t header_size = U32(d, 4);
    const std::uint32_t version = U32(d, 8), state_size = U32(d, 12);
    const std::uint32_t shot_w = U32(d, 8 + 20), shot_h = U32(d, 8 + 24);
    const std::uint32_t shot_off = U32(d, 8 + 28), shot_size = U32(d, 8 + 32);
    if (version != kStateVersion || state_size < kMemoryOffset + kMemorySize) {
        error = "unsupported GS state version " + std::to_string(version);
        return false;
    }
    std::size_t o = 8 + header_size;
    if (o + state_size + kPrivilegedSize > d.size()) {
        error = "truncated GS dump";
        return false;
    }
    // Serial and screenshot offsets count from the start of the header struct (byte 8).
    if (shot_size == shot_w * shot_h * 4 && 8 + shot_off + shot_size <= o) {
        dump.shot_width = static_cast<int>(shot_w);
        dump.shot_height = static_cast<int>(shot_h);
        dump.shot_rgba.assign(d.begin() + 8 + shot_off, d.begin() + 8 + shot_off + shot_size);
    }
    dump.memory.assign(d.begin() + o + kMemoryOffset, d.begin() + o + kMemoryOffset + kMemorySize);
    o += state_size;
    dump.privileged.assign(d.begin() + o, d.begin() + o + kPrivilegedSize);
    o += kPrivilegedSize;
    dump.frames.assign(1, {});
    while (o < d.size()) {
        const std::uint8_t type = d[o++];
        if (type == 0) {
            if (o + 5 > d.size()) break;
            const std::uint32_t size = U32(d, o + 1);
            o += 5;
            if (o + size > d.size()) break;
            dump.frames.back().insert(dump.frames.back().end(), d.begin() + o, d.begin() + o + size);
            o += size;
        } else if (type == 1) {
            o += 1;
            if (!dump.frames.back().empty()) dump.frames.emplace_back();
        } else if (type == 2) {
            o += 4;
        } else if (type == 3) {
            if (o + kPrivilegedSize > d.size()) break;
            dump.privileged.assign(d.begin() + o, d.begin() + o + kPrivilegedSize);
            o += kPrivilegedSize;
        } else {
            error = "unknown GS dump packet type " + std::to_string(type);
            return false;
        }
    }
    if (dump.frames.size() > 1 && dump.frames.back().empty()) dump.frames.pop_back();
    return true;
}

}  // namespace ac04::render
