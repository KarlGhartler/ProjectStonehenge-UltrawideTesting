// Explicit EE hardware register access for translated game code (.claude/rules/game-code.md:
// game code never touches 0x1000xxxx addresses itself). A few game functions program EE timer 0
// and the DMAC directly instead of through an SDK call; they use these functions, named after the
// register, and the port implements them on the register model in hw_registers.cpp.
#pragma once

#include "common/types.h"

namespace ac04::sce {

enum class EeReg {
    kT0Count,   // RCNT0_COUNT 0x10000000
    kT0Mode,    // RCNT0_MODE  0x10000010
    kD0Chcr,    // VIF0 channel CHCR 0x10008000 (writing STR runs the chain, see EeDmaWriteChcr)
    kD0Qwc,     // VIF0 QWC 0x10008020
    kD0Tadr,    // VIF0 TADR 0x10008030
    kD2Chcr,    // GIF channel CHCR 0x1000A000
    kD2Tadr,    // GIF channel TADR 0x1000A030
    kD8Chcr,    // fromSPR channel CHCR 0x1000D000
    kD8Madr,    // fromSPR MADR 0x1000D010
    kD8Qwc,     // fromSPR QWC 0x1000D020
    kD8Sadr,    // fromSPR SADR 0x1000D080
    kD9Chcr,    // toSPR channel CHCR 0x1000D400
    kD9Madr,    // toSPR MADR 0x1000D410
    kD9Qwc,     // toSPR QWC 0x1000D420
    kD9Sadr,    // toSPR SADR 0x1000D480
    kDmacCtrl,  // D_CTRL 0x1000E000
    kDmacStat,  // D_STAT 0x1000E010
    kDmacRbsr,  // D_RBSR 0x1000E040
    kDmacRbor,  // D_RBOR 0x1000E050
};

u32 EeHwRead(EeReg reg);
void EeHwWrite(EeReg reg, u32 value);

// Called from a game busy-wait loop on timer 0 that has not reached its count: one H-blank passes,
// or the vblank at the end of the field (the loop then reads the advanced count).
void EeHwWaitTimer();

// CHCR of a DMA channel identified by the handle sceDmaGetChan returned (its register block).
// Setting STR on the VIF0 channel (0x10008000) runs its chain at once (vu0hle::RunVif0Chain) and
// leaves the channel idle: STR cleared, QWC 0, TADR past the end tag.
u32 EeDmaReadChcr(u32 chan);
void EeDmaWriteChcr(u32 chan, u32 value);
// TADR / QWC of a channel identified by its register block (as above).
void EeDmaWriteTadr(u32 chan, u32 value);
void EeDmaWriteQwc(u32 chan, u32 value);

}  // namespace ac04::sce
