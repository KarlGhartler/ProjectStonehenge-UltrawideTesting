// newlib float maths (src/sce/libm.cpp): sanity check against the host's correctly rounded results.
// The routines read their tables from the executable's .data in guest RAM, so the test loads a RAM
// image dumped by the port (--dump-ram-at) from the file named by AC04_TEST_RAM; skipped without it.
#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <random>

#include "common/guest_memory.h"
#include "sce/sce.h"

namespace {

bool LoadRam() {
    const char* path = std::getenv("AC04_TEST_RAM");
    if (path == nullptr) return false;
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    f.read(reinterpret_cast<char*>(ac04::guest::Mem().Ram()), ac04::guest::kRamSize);
    return static_cast<bool>(f);
}

int64_t UlpDistance(float a, float b) {
    int32_t ia, ib;
    std::memcpy(&ia, &a, 4);
    std::memcpy(&ib, &b, 4);
    if (ia < 0) ia = static_cast<int32_t>(0x80000000u - static_cast<uint32_t>(ia));
    if (ib < 0) ib = static_cast<int32_t>(0x80000000u - static_cast<uint32_t>(ib));
    return std::llabs(static_cast<int64_t>(ia) - ib);
}

}  // namespace

TEST_CASE("newlib sinf/cosf/atan2f: within 2 ULP of the correctly rounded value") {
    if (!LoadRam()) {
        MESSAGE("AC04_TEST_RAM not set: skipped");
        return;
    }
    std::mt19937 rng(11);
    std::uniform_real_distribution<float> small(-10.0f, 10.0f), large(-30000.0f, 30000.0f);
    int64_t worst_sin = 0, worst_cos = 0, worst_atan = 0;
    for (int i = 0; i < 200000; ++i) {
        const float x = (i & 1) ? small(rng) : large(rng);
        const float s = ee::sinf(x), c = ee::cosf(x);
        const float hs = static_cast<float>(std::sin(static_cast<double>(x)));
        const float hc = static_cast<float>(std::cos(static_cast<double>(x)));
        if (std::fabs(hs) > 1e-3f) worst_sin = std::max(worst_sin, UlpDistance(s, hs));
        if (std::fabs(hc) > 1e-3f) worst_cos = std::max(worst_cos, UlpDistance(c, hc));
        const float y = small(rng);
        const float a = ee::atan2f(y, x);
        const float ha = static_cast<float>(std::atan2(static_cast<double>(y), static_cast<double>(x)));
        if (std::fabs(ha) > 1e-3f) worst_atan = std::max(worst_atan, UlpDistance(a, ha));
    }
    MESSAGE("worst ULP: sin " << worst_sin << " cos " << worst_cos << " atan2 " << worst_atan);
    CHECK(worst_sin <= 2);
    CHECK(worst_cos <= 2);
    CHECK(worst_atan <= 2);
    CHECK(ee::atan2f(0.0f, 0.0f) == 0.0f);
}
