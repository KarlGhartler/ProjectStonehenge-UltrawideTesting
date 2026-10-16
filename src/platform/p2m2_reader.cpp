#include "platform/p2m2_reader.h"

#include <cstring>
#include <fstream>

namespace ac04::platform {

namespace {

constexpr std::size_t kHeader = 0x23A;
constexpr std::size_t kFrame = 36;

PadState Decode(const std::uint8_t* b) {
    PadState s;
    s.connected = true;
    s.buttons = static_cast<std::uint16_t>(b[0] | b[1] << 8);
    s.rx = b[2];
    s.ry = b[3];
    s.lx = b[4];
    s.ly = b[5];
    // Bytes 6-17 are the pressures in scePadRead order, the same order as PadState::pressure.
    std::memcpy(s.pressure.data(), b + 6, 12);
    return s;
}

}  // namespace

bool P2m2Reader::Open(const std::filesystem::path& path, std::string& error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "cannot open the file";
        return false;
    }
    std::vector<std::uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (data.size() < kHeader) {
        error = "file shorter than the header";
        return false;
    }
    if (data[0] != 1) {
        error = "file version " + std::to_string(data[0]) + " (only version 1 is supported)";
        return false;
    }
    const char* name = reinterpret_cast<const char*>(&data[0x132]);
    game_name_.assign(name, strnlen(name, 255));
    std::uint32_t total = 0;
    std::memcpy(&total, &data[0x231], 4);
    from_savestate_ = data[0x239] != 0;
    const std::size_t available = (data.size() - kHeader) / kFrame;
    const std::size_t frames = total < available ? total : available;  // total_frames may lag the data
    frames_.assign(data.begin() + kHeader, data.begin() + static_cast<std::ptrdiff_t>(kHeader + frames * kFrame));
    return true;
}

bool P2m2Reader::Frame(std::uint32_t frame, PadState out[2]) const {
    if (frame >= FrameCount()) return false;
    const std::uint8_t* rec = frames_.data() + static_cast<std::size_t>(frame) * kFrame;
    out[0] = Decode(rec);
    out[1] = Decode(rec + 18);
    // HYPOTHESIS: the recordings are made with pad 2 unplugged (PCSX2 still stores 18 bytes for it,
    // the file has no connection flag); to be checked with the first real recording.
    out[1].connected = false;
    return true;
}

}  // namespace ac04::platform
