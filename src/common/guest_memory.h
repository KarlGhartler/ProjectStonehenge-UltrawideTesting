// Guest memory (ADR-001 option B): the PS2 EE address space as seen by translated game code.
// - 32 MiB main RAM and the 16 KiB scratchpad, at their original 32-bit addresses.
// - Address decode: 0x0xxxxxxx (cached), 0x2xxxxxxx (uncached), 0x3xxxxxxx (uncached accelerated)
//   and kseg0/kseg1 (0x8xxxxxxx / 0xAxxxxxxx) map onto RAM; 0x70000000-0x70003FFF is the
//   scratchpad; the hardware ranges (0x1000xxxx registers, 0x1100xxxx VU memory, 0x1200xxxx GS)
//   go to registered handlers. Anything else is an invalid access: it is logged once per 64 KiB
//   page, reads return 0 and writes are dropped.
// - PS2 and x86 are both little-endian, so values are stored as-is.
#pragma once

#include <cstring>
#include <memory>
#include <type_traits>
#include <vector>

#include "common/types.h"

namespace ac04::guest {

constexpr u32 kRamSize = 32u * 1024u * 1024u;
constexpr u32 kScratchpadBase = 0x70000000u;
constexpr u32 kScratchpadSize = 16u * 1024u;

// Hardware range handler (registers, VU memory, GS). Sizes are 1, 2, 4, 8 or 16 bytes.
class HardwareHandler {
public:
    virtual ~HardwareHandler() = default;
    virtual void Read(u32 address, void* dst, u32 size) = 0;
    virtual void Write(u32 address, const void* src, u32 size) = 0;
};

class GuestMemory {
public:
    GuestMemory();

    // Clears RAM and scratchpad to zero.
    void Reset();

    // Host pointer for [address, address+size) if the whole range is RAM or scratchpad;
    // nullptr otherwise (hardware, unmapped, or crossing the end of a region).
    u8* Translate(u32 address, u32 size);
    const u8* Translate(u32 address, u32 size) const;

    // Like Translate, but never null: an invalid range is reported (logged once per page) and a
    // zeroed scratch buffer is returned instead, whose contents are discarded. Used by ps2ptr<T>
    // dereferences so a bad guest pointer fails safely instead of crashing the host.
    u8* TranslateOrDummy(u32 address, u32 size);

    template <typename T>
    T Read(u32 address) {
        static_assert(std::is_trivially_copyable<T>::value, "guest values must be trivially copyable");
        T value{};
        ReadBytes(address, &value, sizeof(T));
        return value;
    }
    template <typename T>
    void Write(u32 address, const T& value) {
        static_assert(std::is_trivially_copyable<T>::value, "guest values must be trivially copyable");
        WriteBytes(address, &value, sizeof(T));
    }

    void ReadBytes(u32 address, void* dst, u32 size);
    void WriteBytes(u32 address, const void* src, u32 size);
    void Fill(u32 address, u8 value, u32 size);

    // Registers a handler for [base, base+size) of the physical hardware space (0x10000000+).
    void MapHardware(u32 base, u32 size, HardwareHandler* handler);

    u8* Ram() { return ram_.get(); }
    u8* Scratchpad() { return spr_.get(); }

    // Number of invalid accesses seen (for tests and the overlay).
    u64 InvalidAccessCount() const { return invalid_count_; }

    // Physical RAM offset for a RAM address, or -1 if `address` is not RAM.
    static s64 RamOffset(u32 address);

private:
    struct HwRange {
        u32 base, size;
        HardwareHandler* handler;
    };
    HardwareHandler* FindHardware(u32 physical, u32 size);
    void ReportInvalid(u32 address, u32 size, bool write);

    std::unique_ptr<u8[]> ram_;
    std::unique_ptr<u8[]> spr_;
    std::vector<HwRange> hardware_;
    u64 invalid_count_ = 0;
};

// The one guest address space of the process. Translated code uses it through Mem().
GuestMemory& Mem();

// Fast path for the common case (inline): RAM through the cached segment.
inline u8* HostPtr(u32 address, u32 size) {
    return Mem().Translate(address, size);
}

}  // namespace ac04::guest
