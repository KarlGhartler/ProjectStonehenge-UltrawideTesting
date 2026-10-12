#include "platform/sha1.h"

#include <cstring>

namespace ac04::platform {
namespace {

inline std::uint32_t Rol(std::uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }

}  // namespace

void Sha1::Reset() {
    h_[0] = 0x67452301u;
    h_[1] = 0xEFCDAB89u;
    h_[2] = 0x98BADCFEu;
    h_[3] = 0x10325476u;
    h_[4] = 0xC3D2E1F0u;
    length_ = 0;
    used_ = 0;
}

void Sha1::Block(const std::uint8_t* p) {
    std::uint32_t w[80];
    for (int i = 0; i < 16; ++i)
        w[i] = (std::uint32_t(p[i * 4]) << 24) | (std::uint32_t(p[i * 4 + 1]) << 16) |
               (std::uint32_t(p[i * 4 + 2]) << 8) | std::uint32_t(p[i * 4 + 3]);
    for (int i = 16; i < 80; ++i) w[i] = Rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    std::uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3], e = h_[4];
    for (int i = 0; i < 80; ++i) {
        std::uint32_t f, k;
        if (i < 20) {
            f = (b & c) | (~b & d);
            k = 0x5A827999u;
        } else if (i < 40) {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1u;
        } else if (i < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDCu;
        } else {
            f = b ^ c ^ d;
            k = 0xCA62C1D6u;
        }
        const std::uint32_t t = Rol(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = Rol(b, 30);
        b = a;
        a = t;
    }
    h_[0] += a;
    h_[1] += b;
    h_[2] += c;
    h_[3] += d;
    h_[4] += e;
}

void Sha1::Update(const void* data, size_t bytes) {
    const auto* p = static_cast<const std::uint8_t*>(data);
    length_ += bytes;
    if (used_ > 0) {
        const size_t take = bytes < 64 - used_ ? bytes : 64 - used_;
        std::memcpy(buffer_ + used_, p, take);
        used_ += take;
        p += take;
        bytes -= take;
        if (used_ == 64) {
            Block(buffer_);
            used_ = 0;
        }
    }
    while (bytes >= 64) {
        Block(p);
        p += 64;
        bytes -= 64;
    }
    if (bytes > 0) {
        std::memcpy(buffer_, p, bytes);
        used_ = bytes;
    }
}

std::string Sha1::FinalHex() {
    const std::uint64_t bits = length_ * 8;
    const std::uint8_t pad = 0x80;
    Update(&pad, 1);
    const std::uint8_t zero = 0;
    while (used_ != 56) Update(&zero, 1);
    std::uint8_t len[8];
    for (int i = 0; i < 8; ++i) len[i] = static_cast<std::uint8_t>(bits >> (56 - 8 * i));
    Update(len, 8);
    static const char* hex = "0123456789abcdef";
    std::string out;
    out.reserve(40);
    for (std::uint32_t v : h_)
        for (int s = 28; s >= 0; s -= 4) out.push_back(hex[(v >> s) & 0xF]);
    return out;
}

std::string Sha1Hex(const void* data, size_t bytes) {
    Sha1 s;
    s.Update(data, bytes);
    return s.FinalHex();
}

}  // namespace ac04::platform
