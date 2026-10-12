// ps2ptr<T>: a 32-bit guest address with pointer syntax (ADR-001).
// - Stored in guest structs exactly like the PS2 stores a pointer (4 bytes).
// - Dereferencing translates through Mem(); arithmetic is in units of T, like a C pointer.
// - Never store a host pointer in guest memory; store a ps2ptr.
#pragma once

#include <cstddef>

#include "common/guest_memory.h"

namespace ac04::guest {

namespace detail {
template <typename T>
struct Stride {
    static constexpr u32 value = sizeof(T);
};
template <>
struct Stride<void> {
    static constexpr u32 value = 1;
};
template <>
struct Stride<const void> {
    static constexpr u32 value = 1;
};
}  // namespace detail

template <typename T>
struct ps2ptr {
    u32 addr = 0;

    constexpr ps2ptr() = default;
    constexpr explicit ps2ptr(u32 a) : addr(a) {}

    // Evaluated lazily (in member bodies), so a struct may hold a ps2ptr to its own type.
    static constexpr u32 Stride() { return detail::Stride<T>::value; }

    // Host pointer to the pointee. An address that is not RAM/scratchpad is logged and yields a
    // zeroed dummy object (GuestMemory::TranslateOrDummy), so bad pointers fail safely.
    T* get() const { return reinterpret_cast<T*>(Mem().TranslateOrDummy(addr, Stride())); }

    template <typename U = T, typename = std::enable_if_t<!std::is_void<U>::value>>
    U& operator*() const { return *get(); }
    T* operator->() const { return get(); }
    template <typename U = T, typename = std::enable_if_t<!std::is_void<U>::value>>
    U& operator[](s32 index) const {
        return *reinterpret_cast<U*>(Mem().TranslateOrDummy(addr + static_cast<u32>(index) * Stride(), Stride()));
    }

    ps2ptr operator+(s32 n) const { return ps2ptr(addr + static_cast<u32>(n) * Stride()); }
    ps2ptr operator-(s32 n) const { return ps2ptr(addr - static_cast<u32>(n) * Stride()); }
    ps2ptr& operator+=(s32 n) { addr += static_cast<u32>(n) * Stride(); return *this; }
    ps2ptr& operator-=(s32 n) { addr -= static_cast<u32>(n) * Stride(); return *this; }
    ps2ptr& operator++() { addr += Stride(); return *this; }
    ps2ptr& operator--() { addr -= Stride(); return *this; }
    s32 operator-(const ps2ptr& other) const { return static_cast<s32>(addr - other.addr) / static_cast<s32>(Stride()); }

    explicit operator bool() const { return addr != 0; }
    bool operator==(const ps2ptr& o) const { return addr == o.addr; }
    bool operator!=(const ps2ptr& o) const { return addr != o.addr; }

    template <typename U>
    ps2ptr<U> cast() const { return ps2ptr<U>(addr); }
};

static_assert(sizeof(ps2ptr<int>) == 4, "ps2ptr must be 4 bytes like a PS2 pointer");
static_assert(sizeof(ps2ptr<void>) == 4, "ps2ptr must be 4 bytes like a PS2 pointer");
static_assert(std::is_trivially_copyable<ps2ptr<int>>::value, "ps2ptr is stored in guest memory");

}  // namespace ac04::guest
