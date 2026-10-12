// Indirect calls (ADR-001): the game calls functions through addresses stored in guest memory
// (scene table 0x0029FDA0, dispatch tables, callbacks). Every translated function that can be a
// target is registered here with its original EE address; CallGuest(address, args...) looks it up.
//
// Arguments follow the EE EABI that the original code used: integer/pointer arguments in
// a0-a3, t0-t3 (8 registers), float arguments in f12-f19 (counted separately). The result is v0
// (integers) or f0 (float).
//
// An unregistered target (not translated yet, or a bad pointer) is logged once per address and
// returns 0: the call fails safely instead of crashing.
#pragma once

#include <array>
#include <tuple>
#include <type_traits>
#include <utility>

#include "common/ps2ptr.h"
#include "common/types.h"

namespace ac04::guest {

struct GuestArgs {
    u64 gpr[8] = {};
    f32 fpr[8] = {};
};

struct GuestResult {
    u64 v0 = 0;
    f32 f0 = 0.0f;
};

using GuestThunk = GuestResult (*)(const GuestArgs&);

void RegisterGuestFunction(u32 address, GuestThunk thunk, const char* name);
bool IsGuestFunctionRegistered(u32 address);
const char* GuestFunctionName(u32 address);  // nullptr if unknown
GuestResult CallGuestRaw(u32 address, const GuestArgs& args);
size_t RegisteredGuestFunctionCount();

namespace detail {

template <typename T>
struct IsPs2Ptr : std::false_type {};
template <typename T>
struct IsPs2Ptr<ps2ptr<T>> : std::true_type {};

template <typename T>
constexpr bool kIsFloat = std::is_same<std::decay_t<T>, f32>::value;

template <typename T>
u64 ToGpr(T v) {
    if constexpr (IsPs2Ptr<T>::value) {
        return v.addr;
    } else if constexpr (std::is_signed<T>::value) {
        return static_cast<u64>(static_cast<s64>(v));  // EE sign-extends 32-bit values
    } else {
        return static_cast<u64>(v);
    }
}

template <typename T>
T FromGpr(u64 v) {
    if constexpr (IsPs2Ptr<T>::value) {
        return T(static_cast<u32>(v));
    } else {
        return static_cast<T>(v);
    }
}

// Register index of each argument (separate counters for integer and float registers).
template <typename... Args>
struct ArgSlots {
    static constexpr int kCount = sizeof...(Args);
    static constexpr std::array<int, kCount == 0 ? 1 : kCount> Compute() {
        std::array<int, kCount == 0 ? 1 : kCount> slots{};
        int g = 0, f = 0, i = 0;
        ((slots[i++] = kIsFloat<Args> ? f++ : g++), ...);
        return slots;
    }
    static constexpr auto value = Compute();
};

template <typename R, typename... Args, size_t... I>
GuestResult Invoke(R (*fn)(Args...), const GuestArgs& a, std::index_sequence<I...>) {
    constexpr auto slots = ArgSlots<Args...>::value;
    auto arg = [&](auto tag, int slot) {
        using T = typename decltype(tag)::type;
        if constexpr (kIsFloat<T>) return a.fpr[slot];
        else return FromGpr<T>(a.gpr[slot]);
    };
    GuestResult r;
    if constexpr (std::is_void<R>::value) {
        fn(arg(std::common_type<Args>{}, slots[I])...);
    } else if constexpr (kIsFloat<R>) {
        r.f0 = fn(arg(std::common_type<Args>{}, slots[I])...);
    } else {
        r.v0 = ToGpr<R>(fn(arg(std::common_type<Args>{}, slots[I])...));
    }
    return r;
}

template <auto Fn>
struct ThunkFor;
template <typename R, typename... Args, R (*Fn)(Args...)>
struct ThunkFor<Fn> {
    static GuestResult Call(const GuestArgs& a) { return Invoke(Fn, a, std::index_sequence_for<Args...>{}); }
};

}  // namespace detail

// Calls the translated function registered at `address` with EE-style argument passing.
template <typename R = void, typename... Args>
R CallGuest(u32 address, Args... args) {
    GuestArgs a;
    int g = 0, f = 0;
    auto put = [&](auto v) {
        if constexpr (detail::kIsFloat<decltype(v)>) a.fpr[f++] = v;
        else a.gpr[g++] = detail::ToGpr(v);
    };
    (put(args), ...);
    const GuestResult r = CallGuestRaw(address, a);
    if constexpr (std::is_void<R>::value) {
        return;
    } else if constexpr (detail::kIsFloat<R>) {
        return r.f0;
    } else {
        return detail::FromGpr<R>(r.v0);
    }
}

// Static registration: AC04_GUEST_FUNCTION(0x00100B18, SceneStep);
struct GuestFunctionRegistrar {
    GuestFunctionRegistrar(u32 address, GuestThunk thunk, const char* name) {
        RegisterGuestFunction(address, thunk, name);
    }
};

}  // namespace ac04::guest

#define AC04_GUEST_CONCAT2(a, b) a##b
#define AC04_GUEST_CONCAT(a, b) AC04_GUEST_CONCAT2(a, b)
#define AC04_GUEST_FUNCTION(address, fn)                                                          \
    static ::ac04::guest::GuestFunctionRegistrar AC04_GUEST_CONCAT(g_guest_fn_, __LINE__)(        \
        address, &::ac04::guest::detail::ThunkFor<&fn>::Call, #fn)
