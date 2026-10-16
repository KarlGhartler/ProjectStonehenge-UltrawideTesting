// CPU reimplementations of the VU1 microprograms (docs/subsystems/vu-programs.md; ADR-003).
#pragma once

#include <cstdint>
#include <vector>

#include "platform/render/vu1/vif1.h"

namespace ac04::render::vu1 {

// Guest address of the first microcode pair of each .vutext block (the MPG data after the block's
// DMA tag and FLUSH/MPG codes).
constexpr std::uint32_t kProgramA = 0x00297C18;
constexpr std::uint32_t kProgramB = 0x00298AD8;
constexpr std::uint32_t kProgramC = 0x00299838;
constexpr std::uint32_t kProgramD = 0x0029A948;
constexpr std::uint32_t kProgramE = 0x0029B108;
// Terrain program T: .vudata 0x00402200 (FLUSH, then MPG of 256 pairs at 0 from 0x00402208).
constexpr std::uint32_t kProgramT = 0x00402208;

// Runs the program whose pair 0 was uploaded from `source`, starting at micro address `pc`, as the
// VU would until its next E bit; GIF packets kicked with XGKICK are appended to `gif`. `next_pc`
// receives where an MSCNT continues. Returns false for a program that is not reimplemented.
bool RunVu1Program(Vu1State& vu, std::uint32_t source, std::uint32_t pc, std::vector<std::uint8_t>& gif,
                   std::uint32_t& next_pc);

// XGKICK: appends the GIF packet chain at VU address `addr` (tags until EOP) to `gif`.
void Xgkick(const Vu1State& vu, std::uint32_t addr, std::vector<std::uint8_t>& gif);

}  // namespace ac04::render::vu1
