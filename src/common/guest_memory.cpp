#include "common/guest_memory.h"

#include <cstdio>

#include "platform/crash.h"
#include "platform/log.h"

namespace ac04::guest {
namespace {

// Physical address for the KSEG-style segments the EE uses; returns false for segments that do
// not map onto physical memory (e.g. the scratchpad window, which is handled separately).
bool ToPhysical(u32 address, u32& physical) {
    switch (address >> 28) {
        case 0x0:
        case 0x1:
            physical = address;
            return true;
        case 0x2:  // uncached RAM alias
        case 0x3:  // uncached accelerated RAM alias
            physical = address & 0x0FFFFFFFu;
            return physical < kRamSize;
        case 0x8:
        case 0x9:  // kseg0
        case 0xA:
        case 0xB:  // kseg1
            physical = address & 0x1FFFFFFFu;
            return true;
        default:
            return false;
    }
}

}  // namespace

GuestMemory::GuestMemory()
    : ram_(new u8[kRamSize]()), spr_(new u8[kScratchpadSize]()) {}

void GuestMemory::Reset() {
    std::memset(ram_.get(), 0, kRamSize);
    std::memset(spr_.get(), 0, kScratchpadSize);
    invalid_count_ = 0;
    reported_.fill(0);
}

s64 GuestMemory::RamOffset(u32 address) {
    u32 phys = 0;
    if ((address & 0xFFFFC000u) == kScratchpadBase) return -1;
    if (!ToPhysical(address, phys) || phys >= kRamSize) return -1;
    return phys;
}

u8* GuestMemory::TranslateSegment(u32 address, u32 size) {
    u32 phys = 0;
    if (ToPhysical(address, phys) && phys < kRamSize && size <= kRamSize - phys) return ram_.get() + phys;
    return nullptr;
}

u8* GuestMemory::Dummy(u32 address, u32 size) {
    ReportInvalid(address, size, Access::kReference);
    // Large enough for any single guest object dereferenced through ps2ptr (structs up to 64 KiB).
    static thread_local std::unique_ptr<u8[]> dummy(new u8[0x10000]());
    if (size > 0x10000) size = 0x10000;
    std::memset(dummy.get(), 0, size);
    return dummy.get();
}

HardwareHandler* GuestMemory::FindHardware(u32 physical, u32 size) {
    for (const HwRange& r : hardware_)
        if (physical >= r.base && physical - r.base + size <= r.size) return r.handler;
    return nullptr;
}

void GuestMemory::ReportInvalid(u32 address, u32 size, Access access) {
    ++invalid_count_;
    // Once per 64 KiB page, with the host code that made the access (its return addresses,
    // symbolised with the PDB): a guest pointer that went wrong is found from the log alone.
    const u32 page = address >> 16;
    if ((reported_[page >> 6] >> (page & 63) & 1) != 0) return;
    reported_[page >> 6] |= 1ull << (page & 63);
    static const char* const kKinds[] = {"read", "write", "access (read or write)"};
    char callers[256];
    platform::DescribeCallers(callers, sizeof(callers), 1, 6);
    log::Write(log::Level::Error, "guest",
               "invalid %s of %u bytes at guest address 0x%08X (no RAM/scratchpad/hardware there; "
               "further accesses in this 64 KiB page are not logged) from %s",
               kKinds[static_cast<int>(access)], size, address, callers);
}

void GuestMemory::ReadBytes(u32 address, void* dst, u32 size) {
    if (const u8* p = Translate(address, size)) {
        std::memcpy(dst, p, size);
        return;
    }
    u32 phys = 0;
    if (ToPhysical(address, phys)) {
        if (HardwareHandler* h = FindHardware(phys, size)) {
            h->Read(phys, dst, size);
            return;
        }
    }
    ReportInvalid(address, size, Access::kRead);
    std::memset(dst, 0, size);
}

void GuestMemory::WriteBytes(u32 address, const void* src, u32 size) {
    if (u8* p = Translate(address, size)) {
        std::memcpy(p, src, size);
        return;
    }
    u32 phys = 0;
    if (ToPhysical(address, phys)) {
        if (HardwareHandler* h = FindHardware(phys, size)) {
            h->Write(phys, src, size);
            return;
        }
    }
    ReportInvalid(address, size, Access::kWrite);
}

void GuestMemory::Fill(u32 address, u8 value, u32 size) {
    if (u8* p = Translate(address, size)) {
        std::memset(p, value, size);
        return;
    }
    for (u32 i = 0; i < size; ++i) WriteBytes(address + i, &value, 1);
}

void GuestMemory::MapHardware(u32 base, u32 size, HardwareHandler* handler) {
    hardware_.push_back({base, size, handler});
}

namespace detail {
GuestMemory g_memory;
}

}  // namespace ac04::guest
