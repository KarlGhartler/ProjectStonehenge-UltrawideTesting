// Loads SLUS_201.52 from the disc image into guest memory (ADR-001; CLAUDE.md "Game files": the
// executable's initialised data is read from the user's disc at run time, never stored in the repo).
// - Every PT_LOAD segment is copied to its original virtual address; the part beyond the file size
//   and every SHT_NOBITS section (.sbss, .bss) is zeroed.
// - The executable's SHA-1 is checked against docs/platform/executable.md.
// - gp is read from .reginfo and must be 0x004159F0.
#pragma once

#include <string>
#include <vector>

#include "common/types.h"

namespace ac04::guest {
class GuestMemory;
}

namespace ac04::platform {

class Iso9660;

struct ElfSegmentInfo {
    u32 vaddr, file_size, mem_size;
};

struct LoadedExecutable {
    u32 entry = 0;
    u32 gp = 0;
    std::string sha1;
    std::vector<ElfSegmentInfo> segments;
    std::vector<std::pair<u32, u32>> zeroed;  // NOBITS sections (address, size)
};

constexpr u32 kExpectedEntry = 0x00100008u;
constexpr u32 kExpectedGp = 0x004159F0u;

// Parses and loads an ELF image held in `image` (tests use synthetic images).
bool LoadElfImage(const std::vector<u8>& image, guest::GuestMemory& memory, LoadedExecutable& out,
                  std::string& error);

// Reads SLUS_201.52 from the disc, checks its SHA-1, entry point and gp, and loads it.
// `allow_unverified` (development builds with --allow-unverified-iso) turns a hash mismatch into
// a warning.
bool LoadGameExecutable(const Iso9660& iso, guest::GuestMemory& memory, bool allow_unverified,
                        LoadedExecutable& out, std::string& error);

}  // namespace ac04::platform
