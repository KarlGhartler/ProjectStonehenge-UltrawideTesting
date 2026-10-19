// PCSX2 GS dump reader (development builds, --play-gsdump): the GS memory image, the privileged
// registers and the GIF transfers of a single-frame dump, so the renderer can draw exactly what
// PCSX2 drew and the result can be compared with the screenshot stored in the dump.
//
// Format (PCSX2 2.x GSDumpFile, uncompressed; tools/research/gsdump.py): u32 0xFFFFFFFF,
// u32 header size, header {state_version, state_size, serial_offset, serial_size, crc, shot_w,
// shot_h, shot_offset, shot_size} + serial + screenshot (RGBA, shot_w x shot_h), the GS state
// (state_size bytes; the 4 MiB memory image at offset 0x1A9 for state version 9, found by
// tools/research/gsvram.py), the privileged registers (8192 bytes), then packets: u8 0 transfer
// {u8 path, u32 size, data}, u8 1 vsync {u8 field}, u8 2 read FIFO {u32 size}, u8 3 registers
// {8192 bytes}.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace ac04::render {

struct GsDump {
    std::vector<std::uint8_t> memory;               // 4 MiB GS memory image
    std::vector<std::uint8_t> privileged;           // 8192 bytes; the last register packet wins
    std::vector<std::vector<std::uint8_t>> frames;  // GIF data per frame (split at vsync packets)
    int shot_width = 0, shot_height = 0;
    std::vector<std::uint8_t> shot_rgba;            // PCSX2's screenshot
};

bool LoadGsDump(const std::filesystem::path& path, GsDump& dump, std::string& error);

}  // namespace ac04::render
