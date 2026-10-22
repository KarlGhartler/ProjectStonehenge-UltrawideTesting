// C runtime used by the game (newlib, libm, libgcc): guest-memory versions.
// - malloc/free use newlib's chunk sizes on the EE (4-byte size header, 16-byte alignment: a request
//   of n bytes takes (n + 4 + 15) & ~15, at least 16) in the region the PS2 heap gives the game:
//   the first chunk is at 0x004CC758 (user block 0x004CC760) and the arena holds 0x01A318B0 bytes,
//   so ProbeLargestMallocSize finds the measured 0x01A318A4 (docs/pcsx2/sessions.md run 03,
//   docs/port/validation.md E-1). The size word (size | 1) is written before each block as newlib
//   does; the free lists and the top-chunk slack (0xFF8 bytes on the PS2) stay on the host.
// - rand/srand are newlib's LCG on the original state word (*_impure_ptr + 0x58).
// - printf/sprintf format guest strings with EE varargs (one 64-bit slot per argument; floats are
//   passed as doubles). printf output goes to the session log once per format string (debug text
//   of the game, never per frame).
// - sinf/cosf/atan2f/atanf: bit-exact newlib translations in libm.cpp (S4). sqrtf still uses the host
//   (sqrt is correctly rounded on both; the EE flush/clamp rules are applied).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>

#include "common/guest_call.h"
#include "common/guest_ref.h"
#include "platform/log.h"
#include "sce/sce.h"
#include "sce/sce_host.h"

namespace ee {

using ac04::guest::GRef;
using ac04::guest::GStr;

namespace {

constexpr u32 kChunkBase = 0x004CC758u;  // first newlib chunk; user block at +8
constexpr u32 kArenaSize = 0x01A318B0u;  // largest chunk ProbeLargestMallocSize can get on the PS2
constexpr u32 kImpurePtr = 0x00401D74u;  // _impure_ptr (newlib reent), rand state at +0x58

struct Heap {
    std::map<u32, u32> used;                            // chunk address -> chunk size
    std::map<u32, u32> free{{kChunkBase, kArenaSize}};  // chunk address -> chunk size
};

Heap& TheHeap() {
    static Heap heap;
    return heap;
}

u32 RandState() { return GRef<u32>(kImpurePtr) + 0x58; }

// ---- printf formatting ----------------------------------------------------------------------

std::string Format(u32 fmt, const u64* args, s32 count) {
    const std::string f = GStr(fmt);
    std::string out;
    s32 next = 0;
    auto take = [&]() -> u64 { return next < count && args ? args[next++] : (++next, 0ull); };
    for (size_t i = 0; i < f.size(); ++i) {
        if (f[i] != '%') {
            out.push_back(f[i]);
            continue;
        }
        std::string spec = "%";
        ++i;
        while (i < f.size() && std::strchr("-+ #0", f[i])) spec.push_back(f[i++]);
        if (i < f.size() && f[i] == '*') {
            spec += std::to_string(static_cast<s32>(take()));
            ++i;
        }
        while (i < f.size() && f[i] >= '0' && f[i] <= '9') spec.push_back(f[i++]);
        if (i < f.size() && f[i] == '.') {
            spec.push_back(f[i++]);
            if (i < f.size() && f[i] == '*') {
                spec += std::to_string(static_cast<s32>(take()));
                ++i;
            }
            while (i < f.size() && f[i] >= '0' && f[i] <= '9') spec.push_back(f[i++]);
        }
        int longs = 0;
        while (i < f.size() && std::strchr("hlLqjzt", f[i])) {
            if (f[i] == 'l' || f[i] == 'q' || f[i] == 'L') ++longs;
            ++i;
        }
        if (i >= f.size()) break;
        const char conv = f[i];
        char buf[512];
        buf[0] = 0;
        switch (conv) {
            case '%':
                out.push_back('%');
                continue;
            case 'd':
            case 'i': {
                const u64 v = take();
                const long long x = longs >= 2 ? static_cast<long long>(v) : static_cast<long long>(static_cast<s32>(v));
                std::snprintf(buf, sizeof(buf), (spec + "lld").c_str(), x);
                break;
            }
            case 'u':
            case 'x':
            case 'X':
            case 'o': {
                const u64 v = take();
                const unsigned long long x = longs >= 2 ? v : static_cast<u32>(v);
                std::snprintf(buf, sizeof(buf), (spec + "ll" + conv).c_str(), x);
                break;
            }
            case 'p':
                std::snprintf(buf, sizeof(buf), "0x%08x", static_cast<u32>(take()));
                break;
            case 'c':
                std::snprintf(buf, sizeof(buf), (spec + "c").c_str(), static_cast<int>(static_cast<u8>(take())));
                break;
            case 's': {
                const std::string s = GStr(static_cast<u32>(take()));
                std::snprintf(buf, sizeof(buf), (spec + "s").c_str(), s.c_str());
                break;
            }
            case 'f':
            case 'F':
            case 'e':
            case 'E':
            case 'g':
            case 'G': {
                const u64 bits = take();
                double d;
                std::memcpy(&d, &bits, sizeof(d));
                std::snprintf(buf, sizeof(buf), (spec + conv).c_str(), d);
                break;
            }
            default:
                out.push_back('%');
                out.push_back(conv);
                continue;
        }
        out += buf;
    }
    return out;
}

// C++ runtime constructors in the .ctors list (libstdc++ init, SDK code): nothing the port needs.
void CxxRuntimeCtor() {}
AC04_GUEST_FUNCTION(0x0027CF88, CxxRuntimeCtor);
void CxxRuntimeCtor2() {}
AC04_GUEST_FUNCTION(0x0027C680, CxxRuntimeCtor2);

}  // namespace

u32 malloc(u32 size) {
    Heap& h = TheHeap();
    if (size > 0x7FFFFFF0u) return 0;
    const u32 need = std::max(16u, (size + 4u + 15u) & ~15u);
    for (auto it = h.free.begin(); it != h.free.end(); ++it) {
        if (it->second >= need) {
            const u32 chunk = it->first;
            const u32 rest = it->second - need;
            h.free.erase(it);
            if (rest != 0) h.free.emplace(chunk + need, rest);
            h.used.emplace(chunk, need);
            GRef<u32>(chunk + 4) = need | 1u;  // newlib size word, PREV_INUSE
            return chunk + 8;
        }
    }
    return 0;
}

void free(u32 ptr) {
    if (ptr == 0) return;
    Heap& h = TheHeap();
    const auto it = h.used.find(ptr - 8);
    if (it == h.used.end()) {
        AC04_LOG_WARN("libc", "free(0x%08X): not a malloc block", ptr);
        return;
    }
    u32 addr = it->first;
    u32 size = it->second;
    h.used.erase(it);
    auto next = h.free.lower_bound(addr);
    if (next != h.free.end() && next->first == addr + size) {
        size += next->second;
        next = h.free.erase(next);
    }
    if (next != h.free.begin()) {
        auto prev = std::prev(next);
        if (prev->first + prev->second == addr) {
            addr = prev->first;
            size += prev->second;
            h.free.erase(prev);
        }
    }
    h.free.emplace(addr, size);
}

u32 memset(u32 dst, s32 value, u32 size) {
    ac04::guest::Mem().Fill(dst, static_cast<u8>(value), size);
    return dst;
}

s32 printf(u32 fmt, const u64* args, s32 count) {
    const std::string text = Format(fmt, args, count);
    char key[32];
    std::snprintf(key, sizeof(key), "printf:%08X", fmt);
    std::string line = text;
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
    ac04::log::WriteOnce(key, ac04::log::Level::Info, "game", "printf: %s", line.c_str());
    return static_cast<s32>(text.size());
}

s32 sprintf(u32 dst, u32 fmt, const u64* args, s32 count) {
    const std::string text = Format(fmt, args, count);
    ac04::guest::GPutStr(dst, text);
    return static_cast<s32>(text.size());
}

void srand(u32 seed) { GRef<u32>(RandState()) = seed; }

s32 rand() {
    u32& x = GRef<u32>(RandState());
    x = x * 0x41C64E6Du + 0x3039u;
    return static_cast<s32>(x & 0x7FFFFFFFu);
}

u32 strcat(u32 dst, u32 src) {
    strcpy(dst + strlen(dst), src);
    return dst;
}

// newlib strncmp (0x00292AB0): compares at most n characters as unsigned bytes.
s32 strncmp(u32 a, u32 b, u32 n) {
    for (u32 i = 0; i < n; ++i) {
        const u8 ca = GRef<u8>(a + i);
        const u8 cb = GRef<u8>(b + i);
        if (ca != cb) return static_cast<s32>(ca) - static_cast<s32>(cb);
        if (ca == 0) return 0;
    }
    return 0;
}

s32 strcmp(u32 a, u32 b) {
    for (u32 i = 0;; ++i) {
        const u8 ca = GRef<u8>(a + i);
        const u8 cb = GRef<u8>(b + i);
        if (ca != cb) return static_cast<s32>(ca) - static_cast<s32>(cb);
        if (ca == 0) return 0;
    }
}

u32 strcpy(u32 dst, u32 src) {
    for (u32 i = 0;; ++i) {
        const u8 c = GRef<u8>(src + i);
        GRef<u8>(dst + i) = c;
        if (c == 0) break;
    }
    return dst;
}

u32 strlen(u32 s) {
    u32 n = 0;
    while (GRef<u8>(s + n) != 0) ++n;
    return n;
}

// newlib strchr (0x0029258C): first `c` (as a char) in `s`, the terminator included; 0 if none.
u32 strchr(u32 s, s32 c) {
    const u8 ch = static_cast<u8>(c);
    for (u32 i = 0;; ++i) {
        const u8 v = GRef<u8>(s + i);
        if (v == ch) return s + i;
        if (v == 0) return 0;
    }
}

// sinf, cosf, atan2f, atanf: src/sce/libm.cpp (bit-exact newlib translations).
f32 sqrtf(f32 x) { return x <= 0.0f ? 0.0f : std::sqrt(x); }
// newlib floorf (0x00284748): exact on any IEEE host (no rounding involved).
f32 floorf(f32 x) { return std::floor(x); }
// newlib fmodf (0x00284DE0, wrapper of __ieee754_fmodf): the remainder is exact on any IEEE host.
// HYPOTHESIS: the wrapper's error path (y == 0 or a NaN operand with _LIB_VERSION != _IEEE_) is not
// reached by the game; the hangar camera (0x002318F8) divides by the constant 12000.
f32 fmodf(f32 x, f32 y) { return std::fmod(x, y); }

// libgcc __main 0x00288950 / __do_global_ctors 0x002888A0: runs once (flag 0x004CC56C), then the
// .ctors list at 0x0029F308 from the last entry to the first. A count of -1 means "count until the
// null entry".
void __main() {
    u32& done = GRef<u32>(0x004CC56C);
    if (done != 0) return;
    done = 1;
    constexpr u32 kCtors = 0x0029F308;
    s32 count = GRef<s32>(kCtors);
    if (count == -1) {
        count = 0;
        while (GRef<u32>(kCtors + 4u * static_cast<u32>(count + 1)) != 0) ++count;
    }
    for (s32 i = count; i >= 1; --i) ac04::guest::CallGuest(GRef<u32>(kCtors + 4u * static_cast<u32>(i)));
}

// Exception frame registration: the game never throws; nothing to register on the host.
void __register_frame_info(u32 /*begin*/, u32 /*object*/) {}
void __deregister_frame_info(u32 /*begin*/) {}

}  // namespace ee
