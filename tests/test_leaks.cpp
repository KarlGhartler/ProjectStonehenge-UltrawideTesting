// Heap leak checks (MSVC Debug builds: the CRT debug heap counts every allocation). Each case runs
// its work once to fill function-local statics and caches, then again between two checkpoints:
// what the second run allocates must all be freed when its objects are destroyed.
#include <doctest/doctest.h>

#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>

#include <cstdint>
#include <string>
#include <vector>

#include "platform/render/gs/gs_state.h"
#include "platform/settings.h"

namespace {

template <typename Fn>
bool LeaksMemory(Fn&& work) {
    work();  // warm-up: statics, one-time logs
    _CrtMemState before, after, diff;
    _CrtMemCheckpoint(&before);
    work();
    _CrtMemCheckpoint(&after);
    return _CrtMemDifference(&diff, &before, &after) != 0 && diff.lCounts[_NORMAL_BLOCK] != 0;
}

}  // namespace

TEST_CASE("no heap leaks: settings store load, save text and change notification") {
    CHECK_FALSE(LeaksMemory([] {
        ac04::platform::SettingsStore store;
        std::vector<std::string> warnings;
        store.LoadFromText(R"({"presentation":{"internal_scale":4},"input":{"modern_controls":[false,true]}})", warnings);
        store.LoadFromText("{ broken", warnings);
        const int id = store.Subscribe(ac04::platform::SettingsSection::Presentation, [](const auto&) {});
        store.Update(ac04::platform::SettingsSection::Presentation, [](auto& s) { s.presentation.vsync = false; });
        store.Unsubscribe(id);
        (void)store.ToText();
    }));
}

TEST_CASE("no heap leaks: GS state with uploads, draws and texture decodes") {
    CHECK_FALSE(LeaksMemory([] {
        using namespace ac04::render::gs;
        GsState gs;
        std::vector<std::uint64_t> w;
        auto tag = [&](std::uint64_t nloop, std::uint64_t flg, std::uint64_t nreg, std::uint64_t regs, bool eop = false) {
            w.push_back(nloop | static_cast<std::uint64_t>(eop) << 15 | flg << 58 | nreg << 60);
            w.push_back(regs);
        };
        auto ad = [&](std::uint64_t reg, std::uint64_t v) {
            w.push_back(v);
            w.push_back(reg);
        };
        // 16 x 16 CT32 upload at block 0x400, then a textured sprite over it.
        tag(4, 0, 1, 0xE);
        ad(0x50, 0x400ull << 32 | 1ull << 48);
        ad(0x51, 0);
        ad(0x52, 16 | 16ull << 32);
        ad(0x53, 0);
        tag(64, 2, 0, 0);
        for (int i = 0; i < 128; ++i) w.push_back(0x80402010ull * static_cast<std::uint64_t>(i + 1));
        tag(2, 0, 1, 0xE);
        ad(0x06, 0x400 | 1ull << 14 | 4ull << 26 | 4ull << 30 | 1ull << 34);
        ad(0x4C, 1ull << 16);
        tag(1, 1, 6, 0x535310, true);
        w.push_back(0x116);
        w.push_back(0x80808080ull);
        w.push_back(0);
        w.push_back(0);
        w.push_back(16 * 16 | 16ull * 16 << 16);
        w.push_back(16 * 16 | 16ull * 16 << 16);
        for (int frame = 0; frame < 3; ++frame) {
            gs.ProcessGif(reinterpret_cast<const std::uint8_t*>(w.data()), w.size() * 8);
            (void)gs.TakeDraws();
        }
    }));
}

#endif
