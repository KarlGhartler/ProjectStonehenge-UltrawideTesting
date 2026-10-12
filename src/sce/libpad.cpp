// libpad over the input backend: each port behaves like a DualShock 2. It starts in digital mode
// (id 4), as PCSX2's pads do, switches to analog (id 7) when the game calls scePadSetMainMode, and
// reports pressure data (type 0x79) after scePadEnterPressMode. Requests complete at once.
#include <array>

#include "common/guest_memory.h"
#include "common/guest_ref.h"
#include "platform/log.h"
#include "sce/sce.h"
#include "sce/sce_host.h"

using ac04::guest::GRef;

namespace {

struct Port {
    bool open = false;
    bool analog = false;
    bool pressure = false;
};
std::array<Port, 2> g_ports;

bool Valid(s32 port, s32 slot) { return port >= 0 && port < 2 && slot == 0; }

ac04::platform::PadState Sample(s32 port) {
    const auto& read = ac04::sce::Host().read_pad;
    return read ? read(port) : ac04::platform::PadState{};
}

}  // namespace

// PS2: initialises libpad; returns 1.
s32 scePadInit(s32) {
    g_ports = {};
    return 1;
}

// PS2: closes libpad; returns 1.
s32 scePadEnd() {
    g_ports = {};
    return 1;
}

// PS2: opens port/slot with a 256-byte DMA buffer `addr` the pad driver writes every frame;
// returns 1. Port: the buffer is not written (expected RAM difference); data comes from scePadRead.
s32 scePadPortOpen(s32 port, s32 slot, u32) {
    if (!Valid(port, slot)) return 0;
    g_ports[static_cast<size_t>(port)] = Port{true, false, false};
    return 1;
}

// PS2: copies the last pad data (32 bytes) into `rdata`: [0] 0 = ok, [1] type (0x41 digital,
// 0x73 analog, 0x79 pressure), [2..3] buttons (active low), [4..7] right x/y, left x/y,
// [8..19] pressures. Returns the byte count, 0 when there is no data.
s32 scePadRead(s32 port, s32 slot, u32 rdata) {
    if (!Valid(port, slot) || !g_ports[static_cast<size_t>(port)].open) return 0;
    const auto st = Sample(port);
    ac04::guest::Mem().Fill(rdata, 0, 32);
    if (!st.connected) {
        GRef<u8>(rdata) = 0xFF;
        return 0;
    }
    const Port& p = g_ports[static_cast<size_t>(port)];
    GRef<u8>(rdata + 0) = 0;
    GRef<u8>(rdata + 1) = !p.analog ? 0x41 : (p.pressure ? 0x79 : 0x73);
    GRef<u8>(rdata + 2) = static_cast<u8>(st.buttons & 0xFF);
    GRef<u8>(rdata + 3) = static_cast<u8>(st.buttons >> 8);
    if (!p.analog) return 4;
    GRef<u8>(rdata + 4) = st.rx;
    GRef<u8>(rdata + 5) = st.ry;
    GRef<u8>(rdata + 6) = st.lx;
    GRef<u8>(rdata + 7) = st.ly;
    if (!p.pressure) return 8;
    for (u32 i = 0; i < 12; ++i) GRef<u8>(rdata + 8 + i) = st.pressure[i];
    return 20;
}

// PS2: 0 disconnected, 6 stable (2 = stable in "find controller" state). Port: 6 or 0.
s32 scePadGetState(s32 port, s32 slot) {
    if (!Valid(port, slot) || !g_ports[static_cast<size_t>(port)].open) return 0;
    return Sample(port).connected ? 6 : 0;
}

// PS2: 0 = last request complete, 1 = failed, 2 = busy. Port: always complete.
s32 scePadGetReqState(s32, s32) { return 0; }

// PS2: actuator info. actno -1 = number of actuators (2 on a DualShock 2).
s32 scePadInfoAct(s32 port, s32 slot, s32 actno, s32 term) {
    if (!Valid(port, slot)) return 0;
    if (actno == -1) return 2;
    (void)term;
    return actno < 2 ? 1 : 0;
}

// PS2: term 1 = current id (4 digital, 7 analog), term 2 = current extended id (offs -1: number
// of modes, 2), term 3 = current mode index, term 4 = mode table entry.
s32 scePadInfoMode(s32 port, s32 slot, s32 term, s32 offs) {
    if (!Valid(port, slot) || !Sample(port).connected) return 0;
    const Port& p = g_ports[static_cast<size_t>(port)];
    switch (term) {
        case 1: return p.analog ? 7 : 4;
        case 2: return offs == -1 ? 2 : (p.analog ? 7 : 0);
        case 3: return p.analog ? 1 : 0;
        case 4: return offs == 0 ? 4 : (offs == 1 ? 7 : 0);
        default: return 0;
    }
}

// PS2: sets digital (0) / analog (1) mode, `lock` 3 = locked; returns 1 if the request was sent.
s32 scePadSetMainMode(s32 port, s32 slot, s32 offs, s32) {
    if (!Valid(port, slot)) return 0;
    g_ports[static_cast<size_t>(port)].analog = offs == 1;
    return 1;
}

// PS2: sets the vibration motor values (6 bytes at `data`); returns 1. Port: vibration output is
// not implemented yet (input backend), the call succeeds so the game's state is unchanged.
s32 scePadSetActDirect(s32 port, s32 slot, u32) {
    ac04::log::WriteOnce("pad:vibration", ac04::log::Level::Info, "pad", "vibration output not implemented yet");
    return Valid(port, slot) ? 1 : 0;
}

// PS2: maps the actuators (6 bytes at `data`); returns 1.
s32 scePadSetActAlign(s32 port, s32 slot, u32) { return Valid(port, slot) ? 1 : 0; }

// PS2: 1 if the pad supports pressure mode (DualShock 2).
s32 scePadInfoPressMode(s32 port, s32 slot) { return Valid(port, slot) ? 1 : 0; }

// PS2: enters pressure mode; returns 1 if the request was sent.
s32 scePadEnterPressMode(s32 port, s32 slot) {
    if (!Valid(port, slot)) return 0;
    g_ports[static_cast<size_t>(port)].pressure = true;
    return 1;
}
