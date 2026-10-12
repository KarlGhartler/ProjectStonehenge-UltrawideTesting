#include "platform/elf_loader.h"

#include <cstdio>
#include <cstring>

#include "common/guest_memory.h"
#include "platform/iso9660.h"
#include "platform/iso_image.h"
#include "platform/log.h"
#include "platform/sha1.h"

namespace ac04::platform {
namespace {

// ELF32 little-endian structures (only the fields we use).
constexpr u32 PT_LOAD = 1;
constexpr u32 SHT_NOBITS = 8;
constexpr u32 SHT_MIPS_REGINFO = 0x70000006;
constexpr u16 EM_MIPS = 8;

template <typename T>
bool Get(const std::vector<u8>& img, size_t off, T& v) {
    if (off + sizeof(T) > img.size()) return false;
    std::memcpy(&v, img.data() + off, sizeof(T));
    return true;
}

std::string Hex(u32 v) {
    char b[16];
    std::snprintf(b, sizeof(b), "0x%08X", v);
    return b;
}

}  // namespace

bool LoadElfImage(const std::vector<u8>& img, guest::GuestMemory& memory, LoadedExecutable& out,
                  std::string& error) {
    if (img.size() < 52 || std::memcmp(img.data(), "\x7F" "ELF", 4) != 0 || img[4] != 1 || img[5] != 1) {
        error = "not a 32-bit little-endian ELF file";
        return false;
    }
    u16 machine = 0, phentsize = 0, phnum = 0, shentsize = 0, shnum = 0;
    u32 entry = 0, phoff = 0, shoff = 0;
    Get(img, 18, machine);
    Get(img, 24, entry);
    Get(img, 28, phoff);
    Get(img, 32, shoff);
    Get(img, 42, phentsize);
    Get(img, 44, phnum);
    Get(img, 46, shentsize);
    Get(img, 48, shnum);
    if (machine != EM_MIPS || phentsize < 32 || (shnum && shentsize < 40)) {
        error = "unexpected ELF header (machine " + std::to_string(machine) + ")";
        return false;
    }
    out = LoadedExecutable{};
    out.entry = entry;

    for (u16 i = 0; i < phnum; ++i) {
        const size_t ph = phoff + size_t(i) * phentsize;
        u32 type, offset, vaddr, filesz, memsz;
        if (!Get(img, ph, type) || !Get(img, ph + 4, offset) || !Get(img, ph + 8, vaddr) ||
            !Get(img, ph + 16, filesz) || !Get(img, ph + 20, memsz)) {
            error = "truncated program header table";
            return false;
        }
        if (type != PT_LOAD) continue;
        if (size_t(offset) + filesz > img.size() || filesz > memsz) {
            error = "segment " + std::to_string(i) + " lies outside the file";
            return false;
        }
        u8* dst = memory.Translate(vaddr, memsz);
        if (!dst) {
            error = "segment " + std::to_string(i) + " at " + Hex(vaddr) + " is not in guest RAM";
            return false;
        }
        std::memcpy(dst, img.data() + offset, filesz);
        std::memset(dst + filesz, 0, memsz - filesz);
        out.segments.push_back({vaddr, filesz, memsz});
    }

    for (u16 i = 0; i < shnum; ++i) {
        const size_t sh = shoff + size_t(i) * shentsize;
        u32 type, flags, addr, offset, size;
        if (!Get(img, sh + 4, type) || !Get(img, sh + 8, flags) || !Get(img, sh + 12, addr) ||
            !Get(img, sh + 16, offset) || !Get(img, sh + 20, size))
            break;
        constexpr u32 SHF_ALLOC = 2;
        if (type == SHT_NOBITS && (flags & SHF_ALLOC) && size > 0) {
            if (u8* p = memory.Translate(addr, size)) {
                std::memset(p, 0, size);
                out.zeroed.emplace_back(addr, size);
            }
        } else if (type == SHT_MIPS_REGINFO && size >= 0x18) {
            Get(img, size_t(offset) + 0x14, out.gp);  // ri_gp_value
        }
    }
    if (out.segments.empty()) {
        error = "no loadable segments";
        return false;
    }
    return true;
}

bool LoadGameExecutable(const Iso9660& iso, guest::GuestMemory& memory, bool allow_unverified,
                        LoadedExecutable& out, std::string& error) {
    IsoEntry entry;
    if (!iso.Find(SupportedImage::kExecutableName, entry) || entry.is_directory) {
        error = std::string(SupportedImage::kExecutableName) + " was not found on the disc image";
        return false;
    }
    std::vector<u8> image;
    if (!iso.ReadFile(entry, image)) {
        error = std::string("cannot read ") + SupportedImage::kExecutableName + " from the disc image";
        return false;
    }
    const std::string sha1 = Sha1Hex(image.data(), image.size());
    if (sha1 != SupportedImage::kExecutableSha1) {
        if (!allow_unverified) {
            error = std::string(SupportedImage::kExecutableName) + " has SHA-1 " + sha1 + " (expected " +
                    SupportedImage::kExecutableSha1 + ")";
            return false;
        }
        AC04_LOG_WARN("loader", "%s SHA-1 %s differs from the supported version (continuing: unverified image)",
                      SupportedImage::kExecutableName, sha1.c_str());
    }
    if (!LoadElfImage(image, memory, out, error)) return false;
    out.sha1 = sha1;
    if (out.entry != kExpectedEntry || out.gp != kExpectedGp) {
        const std::string msg = "entry " + Hex(out.entry) + " / gp " + Hex(out.gp) + " (expected " +
                                Hex(kExpectedEntry) + " / " + Hex(kExpectedGp) + ")";
        if (!allow_unverified) {
            error = "unexpected executable layout: " + msg;
            return false;
        }
        AC04_LOG_WARN("loader", "unexpected executable layout: %s", msg.c_str());
    }
    return true;
}

}  // namespace ac04::platform
