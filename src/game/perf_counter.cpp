// C_PCCON (class name from its own error strings 0x00403B20..0x00403C18): dead debug profiler over
// the EE performance counters (libpc). No game code creates or calls one; docs/port/dead-code.md.
//
// Object (0x108 bytes): +0 flags (bit 0 stopped, bit 1 running), +4 running flow (-1 none),
// +0x08[16] PCR0 value per flow, +0x48[16] PCR1 value, +0x88[16] PCCR control word, +0xC8[16]
// measurement count.
#include "game/game.h"

#include "common/ee_math.h"
#include "sce/sce.h"

namespace ac04::game {

using guest::GRef;

namespace {

u32 Slot(u32 pc, u32 flow) { return pc + flow * 4; }

void PrintFlow(u32 fmt, u32 n) {
    const u64 args[1] = {n};
    ee::printf(fmt, args, 1);
}

}  // namespace

// EE: 0x00108400
// Constructor: stopped, no flow, all tables cleared.
void InitPcCon(u32 pc) {
    GRef<u32>(pc) = 1;
    GRef<s32>(pc + 4) = -1;
    for (u32 i = 0; i < 16; ++i) {
        GRef<u32>(pc + 8 + i * 4) = 0;
        GRef<u32>(pc + 0x48 + i * 4) = 0;
        GRef<u32>(pc + 0x88 + i * 4) = 0;
        GRef<u32>(pc + 0xC8 + i * 4) = 0;
    }
}

// EE: 0x00108458
// start: starts the counters with flow `flow`'s control word and initial values.
void StartPcCon(u32 pc, u32 flow) {
    if (!(flow < 0x10)) {
        PrintFlow(0x00403B20, flow);  // "C_PCCON::start : error : no flow! : %d\n"
        return;
    }
    if ((GRef<u32>(pc) & 2) != 0) {
        ee::printf(0x00403B48, nullptr, 0);  // "C_PCCON::start : error : Now running!\n"
        return;
    }
    GRef<u32>(pc + 4) = flow;
    const u32 s = Slot(pc, flow);
    scePcStart(GRef<s32>(s + 0x88), GRef<s32>(s + 8), GRef<s32>(s + 0x48));
    GRef<u32>(pc) = (GRef<u32>(pc) & 0xFFFFFFFEu) | 2;
}

// EE: 0x001084F8
// stop: stops the counters and stores PCR0/PCR1 for the running flow; counts the measurement.
void StopPcCon(u32 pc) {
    if ((GRef<u32>(pc) & 1) != 0) {
        ee::printf(0x00403B70, nullptr, 0);  // "C_PCCON::stop : error : Now Stopping!\n"
        return;
    }
    scePcStop();
    GRef<s32>(Slot(pc, GRef<u32>(pc + 4)) + 8) = scePcGetCounter0();
    GRef<s32>(Slot(pc, GRef<u32>(pc + 4)) + 0x48) = scePcGetCounter1();
    const u32 count = Slot(pc, GRef<u32>(pc + 4)) + 0xC8;
    GRef<u32>(count) += 1;
    GRef<s32>(pc + 4) = -1;
    GRef<u32>(pc) = (GRef<u32>(pc) & 0xFFFFFFFDu) | 1;
}

// EE: 0x001085A8
// get: counter `which` (0 PCR0, else PCR1) of flow `flow` / 4800000 (the k at 0x0040DA80); -1.0
// for a bad flow or while running. A bad counter number is reported but read as PCR1.
f32 GetPcConTime(u32 pc, u32 flow, u32 which) {
    if (!(flow < 0x10)) {
        PrintFlow(0x00403B98, flow);  // "C_PCCON::get : error : no flow! : %d\n"
        return -1.0f;
    }
    if ((GRef<u32>(pc) & 2) != 0) {
        ee::printf(0x00403BC0, nullptr, 0);  // "C_PCCON::get : error : running!\n"
        return -1.0f;
    }
    if (!(which < 2)) PrintFlow(0x00403BE8, which);  // "C_PCCON::get : error : counter no flow! : %d\n"
    const s32 v = which == 0 ? GRef<s32>(Slot(pc, flow) + 8) : GRef<s32>(Slot(pc, flow) + 0x48);
    return fpu::Div(static_cast<f32>(v), GRef<f32>(0x0040DA80));
}

// EE: 0x00108678
// Measurement count of flow `flow` (-1 for a bad flow or while running).
s32 GetPcConCount(u32 pc, u32 flow) {
    if (!(flow < 0x10)) {
        PrintFlow(0x00403B98, flow);  // "C_PCCON::get : error : no flow! : %d\n"
        return -1;
    }
    if ((GRef<u32>(pc) & 2) != 0) {
        ee::printf(0x00403BC0, nullptr, 0);  // "C_PCCON::get : error : running!\n"
        return -1;
    }
    return GRef<s32>(Slot(pc, flow) + 0xC8);
}

// EE: 0x001086D8
// set: control word and initial counter values of flow `flow`; its count restarts at 0.
void SetPcCon(u32 pc, u32 flow, s32 control, s32 counter0, s32 counter1) {
    if (!(flow < 0x10)) {
        PrintFlow(0x00403C18, flow);  // "C_PCCON::set : error : no flow! : %d\n"
        return;
    }
    const u32 s = Slot(pc, flow);
    GRef<s32>(s + 8) = counter0;
    GRef<s32>(s + 0x48) = counter1;
    GRef<s32>(s + 0x88) = control;
    GRef<u32>(s + 0xC8) = 0;
}

}  // namespace ac04::game
