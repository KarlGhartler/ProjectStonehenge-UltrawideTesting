#include "sce/hw_registers.h"

#include <cstring>

namespace ac04::sce {
namespace {

constexpr u32 kT0Count = 0x10000000u;

}  // namespace

EeRegisters& EeRegisters::Get() {
    static EeRegisters regs;
    return regs;
}

void EeRegisters::Install(guest::GuestMemory& memory) {
    std::memset(regs_, 0, sizeof(regs_));
    t0_count_ = 0;
    memory.MapHardware(kBase, kSize, this);
}

void EeRegisters::OnVBlank(u64 vblank_index) {
    // NTSC interlaced: 525 lines per frame, alternating 262 / 263 per field.
    t0_count_ = (t0_count_ + ((vblank_index & 1) ? 263u : 262u)) & 0xFFFFu;
}

void EeRegisters::Read(u32 address, void* dst, u32 size) {
    const u32 off = address - kBase;
    if (address == kT0Count && size == 4) {
        t0_count_ = (t0_count_ + 1) & 0xFFFFu;
        std::memcpy(dst, &t0_count_, 4);
        return;
    }
    std::memcpy(dst, regs_ + off, size);
}

void EeRegisters::Write(u32 address, const void* src, u32 size) {
    const u32 off = address - kBase;
    if (address == kT0Count && size >= 4) {
        std::memcpy(&t0_count_, src, 4);
        t0_count_ &= 0xFFFFu;
        return;
    }
    std::memcpy(regs_ + off, src, size);
}

u32 EeRegisters::Word(u32 address) const {
    u32 v = 0;
    std::memcpy(&v, regs_ + (address - kBase), 4);
    return v;
}

void EeRegisters::SetWord(u32 address, u32 value) {
    std::memcpy(regs_ + (address - kBase), &value, 4);
}

}  // namespace ac04::sce

// ---- Explicit register access for game code (ee_hw.h) ---------------------------------------
#include "sce/ee_hw.h"

namespace ac04::sce {
namespace {

u32 Address(EeReg reg) {
    switch (reg) {
        case EeReg::kT0Count: return 0x10000000u;
        case EeReg::kT0Mode: return 0x10000010u;
        case EeReg::kD2Tadr: return 0x1000A030u;
        case EeReg::kD8Chcr: return 0x1000D000u;
        case EeReg::kD8Madr: return 0x1000D010u;
        case EeReg::kD8Qwc: return 0x1000D020u;
        case EeReg::kD8Sadr: return 0x1000D080u;
        case EeReg::kDmacCtrl: return 0x1000E000u;
        case EeReg::kDmacRbsr: return 0x1000E040u;
        case EeReg::kDmacRbor: return 0x1000E050u;
    }
    return 0x1000F000u;
}

bool IsChannelBlock(u32 chan) { return chan >= 0x10008000u && chan < 0x1000E000u && (chan & 0x3FFu) == 0; }

}  // namespace

u32 EeHwRead(EeReg reg) {
    u32 v = 0;
    EeRegisters::Get().Read(Address(reg), &v, 4);
    return v;
}

void EeHwWrite(EeReg reg, u32 value) { EeRegisters::Get().Write(Address(reg), &value, 4); }

u32 EeDmaReadChcr(u32 chan) {
    if (!IsChannelBlock(chan)) return 0;
    u32 v = 0;
    EeRegisters::Get().Read(chan, &v, 4);
    return v;
}

void EeDmaWriteChcr(u32 chan, u32 value) {
    if (IsChannelBlock(chan)) EeRegisters::Get().Write(chan, &value, 4);
}

}  // namespace ac04::sce
