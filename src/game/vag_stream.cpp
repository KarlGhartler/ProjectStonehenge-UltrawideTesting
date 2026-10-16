// VAG streaming client for VAG.IRX (RPC 0x81000102; docs/subsystems/iop-sound.md): 6 stream slots
// fed from the VAG/*.VPK packs through the CD queue 0, the 32-entry play/stop request queue and the
// per-slot volume fades. The IOP side is the port's VAG service (ADR-004).
//
// Slot s = 0x00353BF8 + s * 0x94 (u32 fields unless noted):
//   +0x00/+0x04/+0x08 IOP ring buffers   +0x0C ring index (IOP)   +0x10 previous ring index
//   +0x14 SPU voice   +0x18 block size   +0x1C next LSN (0 = end)   +0x20 entry offset
//   +0x24 sectors read   +0x28 block count   +0x2C IOP position   +0x30 chunk bytes
//   +0x34 state (1 idle, 2/0x80 starting, 0x20 playing, 0x40 ending, 8 last chunk)
//   +0x38 flags (bit 0 stop)   +0x3C entry param   +0x40 CD ticket   +0x44 entry   +0x48 pack
//   +0x4C next pack (-1 none)   +0x50 start pending   +0x54 skip next read   +0x58 flag
//   +0x5C/+0x60 entry volumes   +0x64 u16 fade mode   +0x68 f32 fade time   +0x6C/+0x70 fade target
//   +0x7C/+0x80 current volume   +0x84/+0x88 base volume   +0x8C/+0x90 output volume
// Client data: 0x00353F94 (sceSifClientData); batch count 0x00353F70, write offset 0x00353F74,
// send buffers 0x00353F78/7C, receive buffers 0x00353F80/84, current 0x00353F88, previous
// 0x00353F8C, ready mask 0x00353F90; channel busy flags 0x00353BE8 + ch * 4.
#include "game/game.h"

#include "common/ee_math.h"
#include "common/guest_stack.h"
#include "sce/sce.h"

namespace ac04::game {

using guest::GRef;

namespace {

constexpr u32 kSlots = 0x00353BF8;
constexpr u32 kVagClient = 0x00353F94;
constexpr u32 kChannelBusy = 0x00353BE8;
constexpr u32 kVpkTable = 0x00365B40;  // 21 x {lsn, header, entries}
constexpr u32 kPackTable = 0x003546DC; // n x 0x10

u32 Slot(s32 s) { return kSlots + static_cast<u32>(s) * 0x94; }
u32& Sw(s32 s, u32 off) { return GRef<u32>(Slot(s) + off); }
s32& Ss(s32 s, u32 off) { return GRef<s32>(Slot(s) + off); }

u32& BatchCount() { return GRef<u32>(0x00353F70); }
u32& WriteOffset() { return GRef<u32>(0x00353F74); }
u32& Current() { return GRef<u32>(0x00353F88); }
u32& Previous() { return GRef<u32>(0x00353F8C); }
u32& ReadyMask() { return GRef<u32>(0x00353F90); }
u32 SendBuffer(u32 i) { return GRef<u32>(0x00353F78 + 4 * i); }
u32 ReceiveBuffer(u32 i) { return GRef<u32>(0x00353F80 + 4 * i); }

// Signed division by 2048 as the compiler emits it (round toward zero).
s32 Sectors(s32 bytes) { return (bytes < 0 ? bytes + 0x7FF : bytes) >> 11; }

// Stream request queue (32 entries of 8 bytes: s8 slot/mask, u8 type, u16 a, u16 b).
u32& QueueState() { return GRef<u32>(0x0042CEB8); }
s32& QueueRead() { return GRef<s32>(0x0042CEAC); }
s32& QueueWrite() { return GRef<s32>(0x0042CEB0); }
s32& QueueCount() { return GRef<s32>(0x0042CEB4); }
u32 QueueEntry(s32 i) { return 0x0042CEBC + static_cast<u32>(i) * 8; }

}  // namespace

// EE: 0x0021E2D8
// Starts the VAG client: voices 0x12/0x14 (slots 0-1, stereo pairs) and 0x16/0x17 (slots 2-5),
// polling the CD queue until the server answered.
void func_0021E2D8() {
    guest::GuestFrame voices(0x18);
    GRef<u64>(voices.Address(0x00)) = 0x0000001400000012ull;
    GRef<u64>(voices.Address(0x08)) = 0x0000001700000016ull;
    GRef<u64>(voices.Address(0x10)) = 0x0000001700000016ull;
    while (InitVagStream(0x0042CA90, 0x0042CC90, voices.Address()) != 1) {
        UpdateCdQueue();
        sceGsSyncV(0);
    }
}

// EE: 0x0021E370
s32 InitVagStream(u32 send, u32 receive, u32 voices) {
    u32& state = GRef<u32>(0x00410D34);
    if (state == 0) {
        for (;;) {
            sceSifBindRpc(kVagClient, 0x81000102u, 0);
            if (GRef<u32>(0x00353FB8) != 0) break;  // cd->serve
            // Delay loop of 0x10001 iterations (no observable effect).
        }
        Previous() = 1;
        WriteOffset() = 8;
        ReadyMask() = 0;
        Current() = 0;
        BatchCount() = 0;
        GRef<u32>(0x00353F78) = send;
        GRef<u32>(0x00353F7C) = send + 0x100;
        GRef<u32>(0x00353F80) = receive;
        GRef<u32>(0x00353F84) = receive + 0x100;
        for (s32 s = 0; s < 6; ++s) {
            Sw(s, 0x0C) = 0;
            Sw(s, 0x10) = 2;
            Sw(s, 0x68) = 0;
            Sw(s, 0x30) = s < 2 ? 0x20000 : 0x10000;
            Sw(s, 0x14) = GRef<u32>(voices + 4u * static_cast<u32>(s));
            Sw(s, 0x1C) = 0;
            Sw(s, 0x24) = 0;
            Sw(s, 0x50) = 0;
            Sw(s, 0x4C) = 0xFFFFFFFFu;
            Sw(s, 0x54) = 0;
            Sw(s, 0x48) = 0xFFFFFFFFu;
            Sw(s, 0x44) = 0xFFFFFFFFu;
            Sw(s, 0x34) = 1;
            Sw(s, 0x8C) = 0;
            Sw(s, 0x90) = 0;
            Sw(s, 0x5C) = 0;
            Sw(s, 0x60) = 0;
            Sw(s, 0x6C) = 0;
            Sw(s, 0x70) = 0;
            Sw(s, 0x7C) = 0;
            Sw(s, 0x80) = 0;
            Sw(s, 0x84) = 0xFF;
            GRef<u16>(Slot(s) + 0x64) = 0;
            Sw(s, 0x88) = 0xFF;
        }
        for (s32 s = 0; s < 4; ++s) GRef<u32>(SendBuffer(Current()) + 4u * static_cast<u32>(s)) = Sw(s, 0x14);
        FlushCache(0);
        sceSifCallRpc(kVagClient, 0, 1, SendBuffer(Current()), 0x100, ReceiveBuffer(Current()), 0x100, 0, 0);
        state = 1;
        return 0;
    }
    if (state != 1 || IsVagRpcIdle() != 1) return 0;
    // Ring buffers in IOP memory after the reply's base: slots 0-1 0x20000 each, slots 2-5
    // 0x10000 each starting again from slot 0's first ring (they share the memory).
    u32 addr = GRef<u32>(ReceiveBuffer(Current())) + 0x180;
    for (s32 s = 0; s < 2; ++s) {
        for (u32 r = 0; r < 3; ++r) {
            Sw(s, 4 * r) = addr;
            addr += 0x20000;
        }
    }
    addr = Sw(0, 0);
    for (s32 s = 2; s < 6; ++s) {
        for (u32 r = 0; r < 3; ++r) {
            Sw(s, 4 * r) = addr;
            addr += 0x10000;
        }
    }
    Previous() = Current();
    Current() = 1 - Current();
    // Voice records: slots 0-1 as left/right pairs, then two centred records (slots 2-3 only; the
    // original loop bound stops there).
    u32 rec = 0x00353FC0;
    for (s32 s = 0; s < 2; ++s, rec += 0x90) {
        const u8 voice = GRef<u8>(Slot(s) + 0x14);
        GRef<u8>(rec + 0xE) = voice;
        GRef<u8>(rec + 0x56) = static_cast<u8>(voice + 1);
        func_0021D680(0x3FFF, 0, rec);
        func_0021D680(0, 0x3FFF, rec + 0x48);
    }
    rec = 0x003540E0;
    for (s32 s = 2; rec <= 0x003541FF; ++s, rec += 0x90) {
        GRef<u8>(rec + 0xE) = GRef<u8>(Slot(s) + 0x14);
        func_0021D680(0x3FFF, 0x3FFF, rec);
    }
    return 1;
}

// EE: 0x0021E770
s32 IsVagRpcIdle() { return sceSifCheckStatRpc(kVagClient) == 0 ? 1 : 0; }

// EE: 0x0021E850
// Slot command {type, slot, block count, block size, entry param}.
s32 QueueVagCmd(s32 slot, u32 type) {
    if (WriteOffset() + 0x14 > 0x100) return -1;
    const u32 p = SendBuffer(Current()) + WriteOffset();
    GRef<u32>(p) = type;
    GRef<u32>(p + 4) = static_cast<u32>(slot);
    GRef<u32>(p + 8) = Sw(slot, 0x28);
    GRef<u32>(p + 12) = Sw(slot, 0x18);
    GRef<u32>(p + 16) = Sw(slot, 0x3C);
    BatchCount() += 1;
    WriteOffset() += 0x14;
    return 1;
}

// EE: 0x0021E8F0
// Ready mask: bit s set when slot s's last CD read finished; cleared (with its paired channel
// bits) while a start is pending (+0x58). Returns the sceSifCallRpc result.
s32 SendVagCommands() {
    const u32 buf = SendBuffer(Current());
    for (u32 s = 0; s < 6; ++s) {
        const u32 bit = 1u << s;
        u32 mask = ReadyMask();
        if (GetCdDoneTicket(0) < Sw(static_cast<s32>(s), 0x40)) {
            mask &= bit ^ 0xFFFF;
        } else {
            mask |= bit;
        }
        if (Sw(static_cast<s32>(s), 0x58) != 0) {
            mask &= bit ^ 0xFFFF;
            if (s == 3) {
                mask &= 0xFFDF;
            } else if (s < 4) {
                mask &= 0xFFEF;
            } else {
                mask &= 0xFFFB;
                if (s != 4) mask &= 0xFFF7;
            }
        }
        ReadyMask() = mask;
    }
    GRef<u32>(buf) = BatchCount();
    GRef<u32>(buf + 4) = ReadyMask();
    FlushCache(0);
    const s32 r = sceSifCallRpc(kVagClient, 1, 1, buf, 0x100, ReceiveBuffer(Current()), 0x100, 0, 0);
    FlushCache(0);
    Previous() = Current();
    BatchCount() = 0;
    WriteOffset() = 8;
    Current() = 1 - Current();
    return r;
}

// EE: 0x0021EAB8
// Starts slot `slot` on VPK `pack` entry `entry`: entry fields, first CD read into the next ring,
// entry volumes and the fade towards them. Returns the CD ticket.
u32 func_0021EAB8(s32 slot, s32 pack, s32 entry) {
    s32 ring = Ss(slot, 0x0C);
    if (Sw(slot, 0x54) == 1) {
        ++ring;
        if (ring > 2) ring = 0;
    }
    const u32 dest = Sw(slot, 4u * static_cast<u32>(ring));
    Sw(slot, 0x1C) = func_0021F998(pack, entry);
    Sw(slot, 0x24) = 0;
    Sw(slot, 0x20) = func_0021F9D8(pack, entry);
    Sw(slot, 0x28) = func_0021FA08(pack, entry);
    Sw(slot, 0x2C) = 0;
    Sw(slot, 0x18) = func_0021FA38(pack, entry);
    Sw(slot, 0x3C) = func_0021FA68(pack, entry);
    Sw(slot, 0x38) = 0;
    Sw(slot, 0x34) = func_0021FB00(Ss(slot, 0x48)) == -1 ? 2u : 0x80u;
    const u32 ticket = QueueCdRead(0, Sw(slot, 0x1C), 1, dest, Sw(slot, 0x24), Sectors(Ss(slot, 0x30)), -1);
    const s32 total = static_cast<s32>(Sw(slot, 0x28) * Sw(slot, 0x18));
    Ss(slot, 0x24) += Sectors(Ss(slot, 0x30));
    if (Sectors(total) <= Ss(slot, 0x24)) {
        Sw(slot, 0x1C) = 0;
        Sw(slot, 0x34) = 8;
    }
    func_0021FA98(pack, entry, Slot(slot) + 0x5C, Slot(slot) + 0x60);
    s16 mode = GRef<s16>(Slot(slot) + 0x64);
    if (mode == 0) mode = 1;
    func_0021F190(GRef<f32>(Slot(slot) + 0x68), slot, Ss(slot, 0x6C), Ss(slot, 0x70), mode);
    return ticket;
}

// EE: 0x0021EC80
// Play command: 2 when the whole entry is already read (state 8), else 1.
void func_0021EC80(s32 slot) {
    if (Sw(slot, 0x34) == 8) {
        QueueVagCmd(slot, 2);
        Sw(slot, 0x34) = 0x40;
    } else {
        QueueVagCmd(slot, 1);
        Sw(slot, 0x34) = 0x20;
    }
}

// EE: 0x0021ECE8
// Stop command.
void func_0021ECE8(s32 slot) {
    QueueVagCmd(slot, 3);
    Sw(slot, 0x1C) = 0;
    Sw(slot, 0x24) = 0;
    Sw(slot, 0x20) = 0;
    Sw(slot, 0x8C) = 0;
    Sw(slot, 0x90) = 0;
    GRef<u16>(Slot(slot) + 0x64) = 0;
    Sw(slot, 0x38) |= 1;
}

// EE: 0x0021ED60
// Last chunk read: command 4.
void func_0021ED60(s32 slot) {
    QueueVagCmd(slot, 4);
    Sw(slot, 0x34) = 0x40;
    Sw(slot, 0x1C) = 0;
    Sw(slot, 0x24) = 0;
}

// EE: 0x0021EDB8
// Two-step start: first call issues the read, later calls return 1 once it completed and the
// play command was queued.
s32 func_0021EDB8(s32 slot, s32 pack, s32 entry, s32 param) {
    if (Sw(slot, 0x50) == 0) {
        Sw(slot, 0x40) = func_0021EAB8(slot, pack, entry);
        ReadyMask() &= (1u << slot) ^ 0xFFFF;
        Ss(slot, 0x44) = entry;
        Ss(slot, 0x48) = param;
        Sw(slot, 0x50) = 1;
        return 0;
    }
    if (Sw(slot, 0x40) > GetCdDoneTicket(0)) return 0;
    ReadyMask() |= 1u << slot;
    func_0021EC80(slot);
    Sw(slot, 0x50) = 0;
    Sw(slot, 0x4C) = 0xFFFFFFFFu;
    return 1;
}

// EE: 0x0021EEB0
s32 func_0021EEB0(s32 slot, s32 pack_entry) {
    return func_0021EDB8(slot, static_cast<s32>(func_0021FAD0(pack_entry)), static_cast<s32>(func_0021FAE8(pack_entry)),
                         pack_entry);
}

// EE: 0x0021EFD8
// Output volume of a slot: stereo slots (0-1) drive their voice pair hard left/right, the others
// one voice with a pan pair; mono output averages both sides.
s32 func_0021EFD8(s32 slot, s32 left, s32 right) {
    if (left >= 0x100) left = 0xFF;
    else if (left < 0) left = 0;
    if (right >= 0x100) right = 0xFF;
    else if (right < 0) right = 0;
    if (func_00218860() == 2) {
        right = (left + right) / 2;
        left = right;
    }
    u32 voice;
    u32 l;
    if (slot < 2) {
        const s32 v = Ss(slot, 0x14);
        func_0021E240(static_cast<u8>(v + 0x18), 0x3FFF, static_cast<u8>(left), 0);
        voice = static_cast<u32>(v + 0x19);
        l = 0;
    } else {
        l = static_cast<u32>(left) & 0xFF;
        voice = Sw(slot, 0x14) + 0x18;
    }
    const s32 r = func_0021E240(static_cast<u8>(voice), 0x3FFF, static_cast<u8>(l), static_cast<u8>(right));
    return r != 1 ? r : 1;
}

// EE: 0x0021F138
void func_0021F138(s32 slot, s32 left, s32 right) {
    Ss(slot, 0x84) = left;
    Ss(slot, 0x88) = right;
}

// EE: 0x0021F190
// Starts a volume fade over `time` seconds towards (left, right), clamped to 0..255.
s32 func_0021F190(f32 time, s32 slot, s32 left, s32 right, s16 mode) {
    if (time < 0.0f) time = 0.0f;
    if (left >= 0x100) left = 0xFF;
    else if (left < 0) left = 0;
    if (right >= 0x100) right = 0xFF;
    else if (right < 0) right = 0;
    GRef<s16>(Slot(slot) + 0x64) = mode;
    GRef<f32>(Slot(slot) + 0x68) = time;
    Ss(slot, 0x6C) = left;
    Ss(slot, 0x70) = right;
    return 1;
}

// EE: 0x0021F208
void func_0021F208(s32 slot, u32 left, u32 right) {
    GRef<u32>(left) = Sw(slot, 0x7C);
    GRef<u32>(right) = Sw(slot, 0x80);
}

// EE: 0x0021F298
// One fade step (per frame). At the end of the fade mode 2 stops the slot; the stored fade time is
// left unchanged on that path (the original's clear is overwritten by the final store).
void func_0021F298(s32 slot) {
    f32 time = GRef<f32>(Slot(slot) + 0x68);
    s32 left = Ss(slot, 0x6C);
    s32 right = Ss(slot, 0x70);
    const s32 base_l = Ss(slot, 0x84);
    const s32 base_r = Ss(slot, 0x88);
    if (time <= GRef<f32>(0x0040FFE8)) {
        GRef<f32>(Slot(slot) + 0x68) = 0.0f;
        if (GRef<s16>(Slot(slot) + 0x64) == 2) func_0021FDF8(static_cast<u8>(1u << slot));
        GRef<u16>(Slot(slot) + 0x64) = 0;
    } else {
        const f32 frames = fpu::Mul(time, 60.0f);
        const f32 dl = fpu::IntToFloat(left - Ss(slot, 0x7C));
        const f32 dr = fpu::IntToFloat(right - Ss(slot, 0x80));
        time = fpu::Sub(time, GRef<f32>(0x0040FFEC));
        left = Ss(slot, 0x7C) + fpu::FloatToInt(fpu::Div(dl, frames));
        right = Ss(slot, 0x80) + fpu::FloatToInt(fpu::Div(dr, frames));
    }
    s32 out_l = static_cast<s32>(static_cast<u32>(Ss(slot, 0x5C)) * static_cast<u32>(left) * static_cast<u32>(base_l)) / 0xFE01;
    if (out_l < 0) out_l = 0;
    else if (out_l > 0xFF) out_l = 0xFF;
    Ss(slot, 0x8C) = out_l;
    s32 out_r = static_cast<s32>(static_cast<u32>(Ss(slot, 0x60)) * static_cast<u32>(right) * static_cast<u32>(base_r)) / 0xFE01;
    if (out_r < 0) out_r = 0;
    else if (out_r > 0xFF) out_r = 0xFF;
    Ss(slot, 0x90) = out_r;
    func_0021EFD8(slot, out_l, out_r);
    Ss(slot, 0x80) = right;
    GRef<f32>(Slot(slot) + 0x68) = time;
    Ss(slot, 0x7C) = left;
}

// EE: 0x0021F448
// Per-slot service after each VAG reply: ring progress, next chunk reads, chained entries, stop
// acknowledgements and fades. Slots 4 and 5 use reply/channel entries 2 and 3.
void func_0021F448() {
    const u32 reply = ReceiveBuffer(Previous());
    for (s32 s = 0; s < 6; ++s) {
        const u32 ch = 4u * static_cast<u32>(s == 4 ? 2 : s == 5 ? 3 : s);
        const s32 ring = GRef<s32>(reply + 0x24 + ch);
        Ss(s, 0x0C) = ring;
        Sw(s, 0x2C) = GRef<u32>(reply + 4 + ch);
        u32& busy = GRef<u32>(kChannelBusy + ch);
        if (ring != Ss(s, 0x10)) {
            if (Sw(s, 0x34) == 0x20 && Sw(s, 0x1C) != 0) {
                if (Sw(s, 0x54) == 0) {
                    Sw(s, 0x40) = QueueCdRead(0, Sw(s, 0x1C), 1, Sw(s, 4u * static_cast<u32>(ring)), Sw(s, 0x24),
                                              Sectors(Ss(s, 0x30)), -1);
                    Ss(s, 0x24) += Sectors(Ss(s, 0x30));
                    const u32 total = Sw(s, 0x28) * Sw(s, 0x18);
                    s32 sectors = Sectors(static_cast<s32>(total));
                    if ((total & 0x7FF) != 0) ++sectors;
                    if (Ss(s, 0x24) >= sectors) func_0021ED60(s);
                } else {
                    Sw(s, 0x54) = 0;
                }
            }
            if ((Sw(s, 0x34) & 0xD8) != 0) {
                if ((Sw(s, 0x38) & 1) == 0 || busy != 0) {
                    Ss(s, 0x4C) = func_0021FB00(Ss(s, 0x48));
                } else {
                    Ss(s, 0x4C) = -1;
                }
                if (Ss(s, 0x4C) == -1) {
                    Sw(s, 0x38) |= 1;
                } else {
                    Sw(s, 0x34) = 0x80;
                    busy = 1;
                    Sw(s, 0x54) = 1;
                }
            }
        }
        if (Sw(s, 0x38) != 0 && (GRef<u32>(reply + 0x14 + ch) & 1) != 0) {
            Sw(s, 0x38) = 0;
            Sw(s, 0x34) = 1;
            Ss(s, 0x48) = -1;
            Ss(s, 0x4C) = -1;
            Ss(s, 0x44) = -1;
            Sw(s, 0x8C) = 0;
            Sw(s, 0x90) = 0;
            Sw(s, 0x54) = 0;
            Sw(s, 0x50) = 0;
            busy = 0;
        }
        Ss(s, 0x10) = ring;
        if (busy != 0 && Ss(s, 0x4C) != -1 && func_0021EEB0(s, Ss(s, 0x4C)) == 1) {
            busy = 0;
            if ((Sw(s, 0x38) & 1) != 0) Sw(s, 0x34) |= 0x40;
        }
        if (GRef<s16>(Slot(s) + 0x64) != 0) func_0021F298(s);
    }
}

// EE: 0x0021F8A8
u32 func_0021F8A8() { return 0x00354B40; }

// EE: 0x0021F8B8
// Builds the 21 VPK directory pointers from the offsets loaded at 0x00354B40.
void func_0021F8B8() {
    for (u32 n = 0; n < 21; ++n) {
        const u32 header = 0x00354B40 + GRef<u32>(0x00354B44 + 4 * n);
        GRef<u32>(kVpkTable + n * 12 + 4) = header;
        GRef<u32>(kVpkTable + n * 12 + 8) = header + 4;
    }
}

// EE: 0x0021F908
void func_0021F908(s32 n, u32 lsn) { GRef<u32>(kVpkTable + static_cast<u32>(n) * 12) = lsn; }

// EE: 0x0021F968
u32 func_0021F968(s32 n) { return GetCdFileLsn(GRef<s32>(0x00354680 + 4u * static_cast<u32>(n))); }

namespace {
u32 VpkEntry(s32 pack, s32 entry) {
    return static_cast<u32>(entry) * 0x18 + GRef<u32>(kVpkTable + static_cast<u32>(pack) * 12 + 8);
}
}  // namespace

// EE: 0x0021F998
u32 func_0021F998(s32 pack, s32 entry) {
    return GRef<u32>(kVpkTable + static_cast<u32>(pack) * 12) + func_0021F9D8(pack, entry);
}
// EE: 0x0021F9D8
u32 func_0021F9D8(s32 pack, s32 entry) { return GRef<u32>(VpkEntry(pack, entry)); }
// EE: 0x0021FA08
u32 func_0021FA08(s32 pack, s32 entry) { return GRef<u32>(VpkEntry(pack, entry) + 4); }
// EE: 0x0021FA38
u32 func_0021FA38(s32 pack, s32 entry) { return GRef<u32>(VpkEntry(pack, entry) + 8); }
// EE: 0x0021FA68
u32 func_0021FA68(s32 pack, s32 entry) { return GRef<u32>(VpkEntry(pack, entry) + 0xC); }
// EE: 0x0021FA98
void func_0021FA98(s32 pack, s32 entry, u32 left, u32 right) {
    const u32 e = VpkEntry(pack, entry);
    GRef<u32>(left) = GRef<u32>(e + 0x10);
    GRef<u32>(right) = GRef<u32>(e + 0x14);
}
// EE: 0x0021FAD0
u32 func_0021FAD0(s32 n) { return GRef<u32>(kPackTable + static_cast<u32>(n) * 0x10); }
// EE: 0x0021FAE8
u32 func_0021FAE8(s32 n) { return GRef<u32>(kPackTable + 4 + static_cast<u32>(n) * 0x10); }
// EE: 0x0021FB00
s32 func_0021FB00(s64 n) {
    if (n < 0) return -1;
    return GRef<s32>(kPackTable + 8 + static_cast<u32>(n) * 0x10);
}

// EE: 0x0021FB58
void func_0021FB58() {
    QueueWrite() = 0;
    QueueCount() = 0;
    QueueRead() = 0;
}

// EE: 0x0021FC20
void func_0021FC20() {
    QueueState() = 0;
    GRef<u32>(0x0042CE90) = 1;
    func_0021FB58();
    for (u32 i = 0; i < 6; ++i) GRef<u32>(0x0042CEA8 - 4 * i) = 1;
}

// EE: 0x0021FD40
// HYPOTHESIS: RequestStreamPlay(slot, id): stop the slot, then queue play {slot, 1, id}.
s32 func_0021FD40(u32 slot, u16 id) {
    if (func_0021FDF8(static_cast<u8>(1u << (slot & 0x1F))) == -1) return -1;
    if (QueueCount() + 1 >= 0x20) return -1;
    const u32 e = QueueEntry(QueueWrite());
    GRef<u8>(e) = static_cast<u8>(slot);
    GRef<u8>(e + 1) = 1;
    GRef<u16>(e + 2) = id;
    QueueCount() += 1;
    QueueWrite() += 1;
    if (QueueWrite() > 0x1F) QueueWrite() = 0;
    return 1;
}

// EE: 0x0021FC80
// Plays stream `id` with argument `arg` on `slot` (the MIS briefing movies' separate sound track,
// movie table +0xC/+0x10): stop the slot, then queue {slot, 0, id, arg}.
s32 RequestStreamPlayArg(u32 slot, u16 id, u16 arg) {
    if (func_0021FDF8(static_cast<u8>(1u << (slot & 0x1F))) == -1) return -1;
    if (QueueCount() + 1 >= 0x20) return -1;
    const u32 e = QueueEntry(QueueWrite());
    GRef<u8>(e) = static_cast<u8>(slot);
    GRef<u8>(e + 1) = 0;
    GRef<u16>(e + 4) = arg;
    GRef<u16>(e + 2) = id;
    QueueCount() += 1;
    QueueWrite() += 1;
    if (QueueWrite() > 0x1F) QueueWrite() = 0;
    return 1;
}

// EE: 0x0021FB70
// Fades all VAG slots out: stops the streams, clears the slot mask 0x3F and queues command 4
// (volume 0xFF, time in steps = `time` / the step length at 0x0040FFF0).
void func_0021FB70(f32 time) {
    func_0021FB58();
    func_0021FDF8(0x3F);
    if (QueueCount() + 1 >= 0x20) return;
    const f32 steps = time / GRef<f32>(0x0040FFF0);
    const u32 e = QueueEntry(QueueWrite());
    GRef<u8>(e + 1) = 4;
    GRef<u16>(e + 4) = 0xFF;
    QueueCount() += 1;
    QueueWrite() += 1;
    GRef<s16>(e + 2) = static_cast<s16>(fpu::FloatToInt(steps));
    if (QueueWrite() > 0x1F) QueueWrite() = 0;
}
// EE: 0x0021FDF8
// HYPOTHESIS: RequestStreamStop(mask): queue {mask, 3}.
s32 func_0021FDF8(u8 mask) {
    if (QueueCount() + 1 >= 0x20) return -1;
    const u32 e = QueueEntry(QueueWrite());
    GRef<u8>(e) = mask;
    GRef<u8>(e + 1) = 3;
    QueueWrite() += 1;
    QueueCount() += 1;
    if (QueueWrite() > 0x1F) QueueWrite() = 0;
    return 1;
}

// EE: 0x0021FE60
// Per frame: runs queued stream requests (play waits for its slot and the paired slots to be
// idle), then services the slots and sends the next VAG batch when the previous one completed.
void UpdateVagStream() {
    u32& state = QueueState();
    bool process = false;
    if (state == 1) {
        process = QueueCount() != 0;
    } else if (state == 0) {
        func_0021FC20();
        state = 1;
        process = QueueCount() != 0;
    } else if (state == 2) {
        process = true;
    }
    if (process) {
        if (state != 2) state = 2;
        u32& paired_start = GRef<u32>(0x00410D3C);
        s64 result = 0;  // kept across iterations as in the original
        if (QueueCount() > 0) {
            bool more = true;
            do {
                const u32 e = QueueEntry(QueueRead());
                const s8 type = GRef<s8>(e + 1);
                if (type == 1) {
                    const s32 slot = GRef<s8>(e);
                    static const s32 kPaired[12] = {2, 3, 4, 5, 0, 0, 0, 0, 1, 1, 1, 1};
                    if ((Sw(slot, 0x34) == 1 && Sw(kPaired[slot * 2], 0x34) == 1 &&
                         Sw(kPaired[slot * 2 + 1], 0x34) == 1) ||
                        paired_start != 0) {
                        Sw(slot, 0x54) = 0;
                        result = func_0021EEB0(slot, GRef<u16>(e + 2));
                        paired_start = 1;
                    }
                    if (result != 0) paired_start = 0;
                    more = false;
                } else if (type == 0) {
                    const s32 slot = GRef<s8>(e);
                    Sw(slot, 0x54) = 0;
                    result = func_0021EDB8(slot, GRef<u16>(e + 2), GRef<u16>(e + 4), -1);
                    more = false;
                } else if (type == 3) {
                    const s64 mask = GRef<s8>(e);
                    for (u32 s = 0; s < 6; ++s) {
                        if ((mask & (static_cast<s64>(1) << s)) != 0 && Sw(static_cast<s32>(s), 0x34) != 1) {
                            func_0021ECE8(static_cast<s32>(s));
                            more = false;
                        }
                    }
                    result = 1;
                } else if (type == 4) {
                    s32 busy = 0;
                    for (s32 s = 0; s < 6; ++s) {
                        if (Sw(s, 0x34) != 1) ++busy;
                    }
                    more = false;
                    if (busy == 0) {
                        const u16 volume = GRef<u16>(e + 4);
                        const f32 time = fpu::Mul(fpu::IntToFloat(GRef<u16>(e + 2)), GRef<f32>(0x0040FFF4));
                        for (s32 s = 0; s < 6; ++s) {
                            func_0021F190(time, s, volume, volume, 1);  // lhu: zero-extended
                            Sw(s, 0x58) = 0;
                        }
                        result = 1;
                    }
                }
                if (result == 1) {
                    QueueRead() += 1;
                    if (QueueRead() > 0x1F) QueueRead() = 0;
                    QueueCount() -= 1;
                    if (QueueCount() < 0) QueueCount() = 0;
                    if (QueueCount() == 0) {
                        more = false;
                        state = 1;
                    }
                }
            } while (more);
        }
    }
    if (IsVagRpcIdle() == 1) {
        func_0021F448();
        SendVagCommands();
    }
}

// EE: 0x0021E798
// Stream slot fade mode: 0 restores the saved volumes over 1.5 s, 1 saves the current (or target)
// volumes and fades to silence over 0.5 s; other modes are only stored.
void func_0021E798(s32 slot, s32 mode) {
    const u32 s = kSlots + static_cast<u32>(slot) * 0x94;
    GRef<s32>(s + 0x58) = mode;
    if (mode == 0) {
        func_0021F190(1.5f, slot, GRef<s32>(s + 0x74), GRef<s32>(s + 0x78), GRef<s16>(s + 0x66));
    } else if (mode == 1) {
        const s16 v = GRef<s16>(s + 0x64);
        u32 l, r;
        if (v == 0) {
            l = GRef<u32>(s + 0x7C);
            r = GRef<u32>(s + 0x80);
        } else {
            l = GRef<u32>(s + 0x6C);
            r = GRef<u32>(s + 0x70);
        }
        GRef<u32>(s + 0x74) = l;
        GRef<u32>(s + 0x78) = r;
        GRef<s16>(s + 0x66) = v;
        if (v == 0) GRef<s16>(s + 0x66) = 1;
        func_0021F190(0.5f, slot, 0, 0, 1);
    }
}

}  // namespace ac04::game
