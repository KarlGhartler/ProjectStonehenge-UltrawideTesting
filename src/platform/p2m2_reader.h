// PCSX2 input recording reader (docs/formats/p2m2.md, file version 1; slices.md §B P2). Each frame
// record is the 18 DualShock 2 data bytes (scePadRead bytes 2-19) of ports 0 and 1. Recording frame
// f is fed to the port's game tick `f - offset` (the offset covers PCSX2's power-on frames before
// the executable starts and the pad latency; calibrated once, docs/port/validation-strategy.md §3.2).
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "platform/input.h"

namespace ac04::platform {

class P2m2Reader {
public:
    // False (and `error` set) for a missing file, another file version or a truncated header.
    bool Open(const std::filesystem::path& path, std::string& error);
    std::uint32_t FrameCount() const { return static_cast<std::uint32_t>(frames_.size() / 36); }
    bool FromSavestate() const { return from_savestate_; }
    const std::string& GameName() const { return game_name_; }
    // The two pads of recording frame `frame`; false past the end.
    bool Frame(std::uint32_t frame, PadState out[2]) const;

private:
    std::vector<std::uint8_t> frames_;
    bool from_savestate_ = false;
    std::string game_name_;
};

}  // namespace ac04::platform
