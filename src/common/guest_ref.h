// Access to guest globals and strings by address (ADR-001).
// - GRef<T>(address): a reference to a T stored in guest memory. Invalid addresses are logged and
//   give a dummy object (GuestMemory::TranslateOrDummy).
// - AC04_GUEST_VAR(type, name, address) declares `type& name()` for a named global, so translated
//   code reads like the original: `g_WorkHeapOffset() += size;`.
// - GStr(address): host view of a NUL-terminated guest string (read-only use).
#pragma once

#include <string>

#include "common/guest_memory.h"
#include "common/ps2ptr.h"

namespace ac04::guest {

template <typename T>
inline T& GRef(u32 address) {
    return *reinterpret_cast<T*>(Mem().TranslateOrDummy(address, sizeof(T)));
}

// Copies a guest C string (at most `max` bytes) into a host std::string.
inline std::string GStr(u32 address, u32 max = 4096) {
    std::string s;
    for (u32 i = 0; i < max; ++i) {
        const u8* p = Mem().Translate(address + i, 1);
        if (!p || *p == 0) break;
        s.push_back(static_cast<char>(*p));
    }
    return s;
}

// Writes a host string (with the terminating NUL) to guest memory.
inline void GPutStr(u32 address, const std::string& s) {
    Mem().WriteBytes(address, s.c_str(), static_cast<u32>(s.size() + 1));
}

}  // namespace ac04::guest

#define AC04_GUEST_VAR(type, name, address)                                  \
    inline type& name() { return ::ac04::guest::GRef<type>(address); }
