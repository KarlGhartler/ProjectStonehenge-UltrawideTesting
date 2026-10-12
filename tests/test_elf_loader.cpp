#include <doctest/doctest.h>

#include <cstring>
#include <vector>

#include "common/guest_memory.h"
#include "platform/elf_loader.h"

using namespace ac04;

namespace {

template <typename T>
void Put(std::vector<u8>& v, size_t off, T x) {
    std::memcpy(v.data() + off, &x, sizeof(T));
}

// A minimal made-up MIPS ELF built in memory (never written to disk): one PT_LOAD segment with
// 16 bytes of file data and 0x40 bytes in memory, a .bss (NOBITS) section and a .reginfo section.
std::vector<u8> MakeElf() {
    std::vector<u8> e(0x400, 0);
    std::memcpy(e.data(), "\x7F" "ELF", 4);
    e[4] = 1;  // 32-bit
    e[5] = 1;  // little-endian
    e[6] = 1;
    Put<u16>(e, 16, 2);       // ET_EXEC
    Put<u16>(e, 18, 8);       // EM_MIPS
    Put<u32>(e, 24, 0x00100008);
    Put<u32>(e, 28, 0x40);    // phoff
    Put<u32>(e, 32, 0x100);   // shoff
    Put<u16>(e, 42, 32);      // phentsize
    Put<u16>(e, 44, 1);
    Put<u16>(e, 46, 40);      // shentsize
    Put<u16>(e, 48, 3);
    // Program header: PT_LOAD, file offset 0x300, vaddr 0x00100000, filesz 16, memsz 0x40.
    Put<u32>(e, 0x40, 1);
    Put<u32>(e, 0x44, 0x300);
    Put<u32>(e, 0x48, 0x00100000);
    Put<u32>(e, 0x50, 16);
    Put<u32>(e, 0x54, 0x40);
    for (int i = 0; i < 16; ++i) e[0x300 + i] = u8(0xA0 + i);
    // Section 1: .bss NOBITS, alloc, 0x00100100 size 0x20.
    Put<u32>(e, 0x100 + 40 + 4, 8);
    Put<u32>(e, 0x100 + 40 + 8, 3);
    Put<u32>(e, 0x100 + 40 + 12, 0x00100100);
    Put<u32>(e, 0x100 + 40 + 20, 0x20);
    // Section 2: .reginfo, data at 0x380, gp at +0x14.
    Put<u32>(e, 0x100 + 80 + 4, 0x70000006);
    Put<u32>(e, 0x100 + 80 + 16, 0x380);
    Put<u32>(e, 0x100 + 80 + 20, 0x18);
    Put<u32>(e, 0x380 + 0x14, 0x004159F0);
    return e;
}

}  // namespace

TEST_CASE("elf loader: segments, NOBITS zeroing and gp") {
    guest::GuestMemory m;
    m.Fill(0x00100000, 0xCC, 0x200);  // stale contents that the loader must overwrite
    platform::LoadedExecutable exe;
    std::string error;
    REQUIRE(platform::LoadElfImage(MakeElf(), m, exe, error));
    CHECK(exe.entry == 0x00100008);
    CHECK(exe.gp == 0x004159F0);
    REQUIRE(exe.segments.size() == 1);
    CHECK(m.Read<u8>(0x00100000) == 0xA0);
    CHECK(m.Read<u8>(0x0010000F) == 0xAF);
    CHECK(m.Read<u8>(0x00100010) == 0);   // memsz beyond filesz is zeroed
    CHECK(m.Read<u8>(0x0010003F) == 0);
    CHECK(m.Read<u8>(0x00100040) == 0xCC);
    CHECK(m.Read<u32>(0x00100100) == 0);  // .bss zeroed
    CHECK(m.Read<u8>(0x00100120) == 0xCC);
}

TEST_CASE("elf loader: rejects other files") {
    guest::GuestMemory m;
    platform::LoadedExecutable exe;
    std::string error;
    CHECK_FALSE(platform::LoadElfImage(std::vector<u8>(64, 0), m, exe, error));
    auto e = MakeElf();
    Put<u16>(e, 18, 3);  // x86
    CHECK_FALSE(platform::LoadElfImage(e, m, exe, error));
}
