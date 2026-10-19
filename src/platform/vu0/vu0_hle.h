// VU0 micro mode for the sequence character engine (docs/subsystems/vu-programs.md "Skinning
// program S", "VIF0 path"): the VU0 register file and data memory, the VIF0 DMA chains that fill
// them, and a C++ rewrite of the nine entries of program S (.vutext 0x0029C200). VU0 macro-mode
// code elsewhere is translated to plain arithmetic (common/vu0_math.h) and does not use this state.
#pragma once

#include "common/types.h"

namespace ac04::vu0hle {

// lqc2 vfN, (addr): loads the 128-bit guest quadword into VU0 register vfN (vf0 is constant).
void LoadVf(int n, u32 addr);
// sqc2 vfN, (addr).
void StoreVf(int n, u32 addr);
// ctc2 value, viN: the low 16 bits (vi0 is constant).
void SetVi(int n, u32 value);
// cfc2 viN: the 16-bit integer register (vi0 reads 0).
u32 GetVi(int n);
// vcallms pair: runs program S from micro pair `pair` to its E bit (entries 0x00, 0x02, 0x06,
// 0x08, 0x0A, 0x0E, 0x10, 0x12, 0x16). Without the program uploaded, or for another pair, logs
// once and does nothing.
void CallMs(u32 pair);
// Runs the VIF0 DMA source chain starting at tag address `tadr` (bit 31: scratchpad) to its end
// tag: VIF codes NOP, MPG (program S) and UNPACK V3-16 / V4-8 into VU0 data memory. Returns the
// address after the end tag (the channel's final TADR).
u32 RunVif0Chain(u32 tadr);

}  // namespace ac04::vu0hle
