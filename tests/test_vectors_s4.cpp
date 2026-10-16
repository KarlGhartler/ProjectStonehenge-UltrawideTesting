// Slice S4 function test vectors from PCSX2 save states (validation-strategy.md §4). The states stay
// outside the repo; set AC04_TEST_STATES to the folder holding them, otherwise the cases are skipped.
#include <doctest/doctest.h>

#include <cstdio>
#include <cstring>
#include <vector>

#include "common/guest_memory.h"
#include "game/game.h"
#include "p2s_state.h"

using ac04::guest::Mem;
using ac04::test::P2sState;

namespace {

constexpr u32 kRamSize = 32u * 1024 * 1024;

}  // namespace

// S4 run B: the hangar commit after buying the F-5E (161,000 -> 109,000 credits) and leaving with
// Triangle. Entry at 0x00233F10 (ra 0x00235C00, the cancel path of AircraftSelectRun), exit at
// 0x00235C00. The only guest writes besides the stack frame: game[0] (0x0029F350), the purchase
// state back in save order (h+0x40 and its copy at 0x002A0320) and the credits (0x002A0D30).
TEST_CASE("CommitHangarSelection: PCSX2 run B purchase commit (s4_commit_before/after)") {
    P2sState before, after;
    if (!before.Open("s4_commit_before.p2s") || !after.Open("s4_commit_after.p2s")) {
        MESSAGE("skipped: AC04_TEST_STATES not set or the S4 run B states are missing");
        return;
    }
    const std::vector<uint8_t> ram_before = before.Entry("eeMemory.bin");
    const std::vector<uint8_t> ram_after = after.Entry("eeMemory.bin");
    const std::vector<uint8_t> regs = before.Entry("PCSX2 Internal Structures.dat");
    REQUIRE(ram_before.size() == kRamSize);
    REQUIRE(ram_after.size() == kRamSize);
    REQUIRE(regs.size() > 0x162 + 32 * 16);

    const u32 a0 = static_cast<u32>(P2sState::Gpr(regs, 4));
    const u32 sp = static_cast<u32>(P2sState::Gpr(regs, 29));
    CHECK(P2sState::Gpr(regs, 31) == 0x00235C00u);

    Mem().Reset();
    std::memcpy(Mem().Ram(), ram_before.data(), kRamSize);
    ac04::game::CommitHangarSelection(a0);

    // The original's stack frame below sp holds saved registers; the port keeps them on the host.
    const u8* port = Mem().Ram();
    u32 differences = 0, first = 0;
    for (u32 i = 0; i < kRamSize; ++i) {
        if (i >= sp - 0x40 && i < sp) continue;
        if (port[i] != ram_after[i]) {
            if (differences++ == 0) first = i;
        }
    }
    char where[32];
    std::snprintf(where, sizeof where, "0x%08X", first);
    INFO("first difference at " << where);
    CHECK(differences == 0);
    CHECK(Mem().Read<u32>(0x002A0D30) == 109000u);
}
