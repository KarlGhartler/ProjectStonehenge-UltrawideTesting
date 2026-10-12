// USB joystick client (USBJOY.IRX, RPC 0x80000210; docs/game/iop-interface.md). Every frame the
// pad task calls RPC fno 2 synchronously and converts the reply (two devices) into two 0x14-byte
// pad-like records at 0x004121A8 (+1 and +0x15), which UpdatePads merges into the port records.
#include "game/game.h"

#include "sce/sce.h"

namespace ac04::game {

using guest::GRef;

constexpr u32 kUsbClient = 0x00420400;  // sceSifClientData
constexpr u32 kUsbSend = 0x00420440;
constexpr u32 kUsbReply = 0x004204C0;   // 0x80 bytes, one value per word

namespace {

// Byte n of the reply (each value sits in the low byte of a word).
u8 Reply(u32 offset) { return GRef<u8>(kUsbReply + offset); }

// Shared shape of the eight scaling helpers: below `lo` -> 0, above `hi` -> 0xFF, else
// ((v - lo) * mul) >> 12.
u32 ScaleByte(u32 v, u32 lo, u32 hi, u32 mul) {
    v &= 0xFF;
    if (v < lo) return 0;
    if (hi < v) return 0xFF;
    return static_cast<u32>((static_cast<u64>(static_cast<s64>(static_cast<s32>(v - lo)) * mul) & 0xFFFFFFFFull) >> 12) & 0xFF;
}

}  // namespace

// EE: 0x001096D8
s32 BindUsbJoyRpc() {
    for (;;) {
        if (sceSifBindRpc(kUsbClient, 0x80000210u, 0) < 0) {
            ee::printf(0x00403C40 /* "bind errr\n" */, nullptr, 0);
            for (;;) {
                // The original spins forever on a bind error.
                sceGsSyncV(0);
            }
        }
        if (GRef<u32>(kUsbClient + 0x24) != 0) break;  // cd->serve
        // Delay loop of 0x10001 iterations (no observable effect).
    }
    return 0;
}

// EE: 0x00109CA0
u32 func_00109CA0(u32 v) { return ScaleByte(v, 0x20, 0xDF, 0x1571); }
// EE: 0x00109CF8
u32 func_00109CF8(u32 v) { return ScaleByte(v, 0x20, 0xDF, 0x1571); }
// EE: 0x00109D50
u32 func_00109D50(u32 v) { return ScaleByte(v, 0x10, 0xEF, 0x125E); }
// EE: 0x00109DA8
u32 func_00109DA8(u32 v) { return ScaleByte(v, 0x10, 0xEF, 0x125E); }
// EE: 0x00109E00
u32 func_00109E00(u32 v) { return ScaleByte(v, 0x3F, 0x8F, 0x3333); }
// EE: 0x00109E58
u32 func_00109E58(u32 v) { return ScaleByte(v, 0x3F, 0x8F, 0x3333); }
// EE: 0x00109EB0
u32 func_00109EB0(u32 v) { return ScaleByte(v, 0x10, 0xEF, 0x125E); }
// EE: 0x00109F08
u32 func_00109F08(u32 v) { return ScaleByte(v, 0x10, 0xEF, 0x125E); }

namespace {

// One device: reply words starting at `base` -> 0x14-byte record at `out`.
// Layout of the reply per device (offsets from base): +0x08 left x, +0x0C left y, +0x10 right y,
// +0x14 right x, +0x18 left trigger, +0x1C right trigger... (as read by the original: see below).
void ConvertDevice(u32 out, u8 a_lx, u8 a_ly, u8 a_rx, u8 a_ry, u8 trig_l, u8 trig_r, u8 hat_x, u8 hat_y,
                   u8 buttons1, u8 buttons2_in, u32 first) {
    u8 buttons2 = buttons2_in;
    GRef<u8>(out + first + 4) = static_cast<u8>(func_00109CA0(a_lx));
    GRef<u8>(out + first + 5) = static_cast<u8>(func_00109CF8(a_ly));
    GRef<u8>(out + first + 6) = static_cast<u8>(func_00109D50(a_rx));
    GRef<u8>(out + first + 7) = static_cast<u8>(func_00109DA8(a_ry));
    GRef<u8>(out + first + 8) = (buttons1 | 0xDF) == 0xFF ? 0 : 0xFF;
    GRef<u8>(out + first + 9) = (buttons1 | 0x7F) == 0xFF ? 0 : 0xFF;
    GRef<u8>(out + first + 10) = (buttons1 | 0xEF) == 0xFF ? 0 : 0xFF;
    GRef<u8>(out + first + 11) = (buttons1 | 0xBF) == 0xFF ? 0 : 0xFF;
    GRef<u8>(out + first + 13) = (buttons2 | 0xDF) == 0xFF ? 0 : 0xFF;
    GRef<u8>(out + first + 14) = (buttons2 | 0xBF) == 0xFF ? 0 : 0xFF;
    u8 t = static_cast<u8>(func_00109E00(trig_l));
    GRef<u8>(out + first + 12) = t;
    if (t != 0) buttons2 &= 0xEF;
    t = static_cast<u8>(func_00109E58(trig_r));
    GRef<u8>(out + first + 15) = t;
    if (t != 0) buttons2 &= 0x7F;
    // Hat X -> two half axes (+0x10 negative, +0x11 positive).
    u32 h = func_00109EB0(hat_x);
    GRef<u8>(out + first + 17) = (h & 0x80) == 0 ? 0 : static_cast<u8>(h << 1);
    if (h > 0x80) buttons2 &= 0xF7;
    if (h == 0xFF) GRef<u8>(out + first + 17) = 0xFF;
    GRef<u8>(out + first + 16) = (h & 0x80) == 0 ? static_cast<u8>(~h << 1) : 0;
    if (h < 0x7F) buttons2 &= 0xFB;
    if (h == 0) GRef<u8>(out + first + 16) = 0xFF;
    // Hat Y.
    h = func_00109F08(hat_y);
    GRef<u8>(out + first + 19) = (h & 0x80) == 0 ? 0 : static_cast<u8>(h << 1);
    if (h > 0x80) buttons2 &= 0xFD;
    if (h == 0xFF) GRef<u8>(out + first + 19) = 0xFF;
    GRef<u8>(out + first + 18) = (h & 0x80) == 0 ? static_cast<u8>(~h << 1) : 0;
    if (h < 0x7F) buttons2 &= 0xFE;
    if (h == 0) GRef<u8>(out + first + 18) = 0xFF;
    GRef<u8>(out + first + 2) = buttons1;
    GRef<u8>(out + first + 3) = buttons2;
}

}  // namespace

// EE: 0x00109780
// The reply holds one value per word: +0x00 device-present bits, +0x04 copied to out[0],
// device 0 at +0x08..+0x2C, device 1 at +0x30..+0x54.
s32 ReadUsbJoy(u32 out) {
    sceSifCallRpc(kUsbClient, 2, 1, kUsbSend, 0x80, kUsbReply, 0x80, 0, 0);
    while (sceSifCheckStatRpc(kUsbClient) != 0) {
    }
    const u32 present = Reply(0x00);
    GRef<u8>(out) = Reply(0x04);
    // Device 0 -> out[1..0x14]
    GRef<u8>(out + 1) = static_cast<u8>((present & 1) - 1);  // 0 = present (status byte), 0xFF = none
    GRef<u8>(out + 2) = 0xFF;
    ConvertDevice(out, Reply(0x18), Reply(0x1C), Reply(0x08), Reply(0x0C), Reply(0x20), Reply(0x24), Reply(0x14),
                  Reply(0x10), Reply(0x28), Reply(0x2C), 1);
    // Device 1 -> out[0x15..0x28]
    GRef<u8>(out + 0x15) = static_cast<u8>(((present & 2) >> 1) - 1);
    GRef<u8>(out + 0x16) = 0xFF;
    ConvertDevice(out, Reply(0x40), Reply(0x44), Reply(0x30), Reply(0x34), Reply(0x48), Reply(0x4C), Reply(0x3C),
                  Reply(0x38), Reply(0x50), Reply(0x54), 0x15);
    return 0;
}

}  // namespace ac04::game
