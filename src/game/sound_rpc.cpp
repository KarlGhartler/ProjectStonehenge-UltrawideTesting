// Sound RPC client for SOUND.IRX (RPC 0x81000101; docs/game/iop-interface.md,
// docs/subsystems/iop-sound.md, docs/subsystems/sound.md): command batches double-buffered and sent
// once per frame, the 48-voice table, voice command builders, sound bank table, bank loading and
// category volumes. The IOP side is the port's SOUND service (ADR-004).
//
// Client struct (*0x004117FC -> 0x0033E558), u32 words: [0] commands in batch, [1]/[2] send
// buffers, [3]/[4] receive buffers, [5] write offset, [6]/[9] init parameters, [7] current buffer,
// [8] previous buffer, [10]/[11] tickets, [12] done ticket, +0x34 sceSifClientData.
// Voice table: 48 records of 0x48 bytes at 0x00424250; voice status bytes at 0x00424FD0.
#include "game/game.h"

#include "common/ee_math.h"
#include "common/guest_stack.h"
#include "sce/sce.h"

namespace ac04::game {

using guest::GRef;

namespace {

constexpr u32 kVoices = 0x00424250;      // 48 x 0x48
constexpr u32 kVoiceStatus = 0x00424FD0; // 48 bytes
constexpr u32 kBankCount = 0x0033E190;
constexpr u32 kBankHeader = 0x0033E194;  // u32[32]
constexpr u32 kBankSpu = 0x0033E214;     // u32[32]
constexpr u32 kBankId = 0x0033E298;      // u16[32]

u32 Client() { return GRef<u32>(0x004117FC); }
u32& Cw(u32 word) { return GRef<u32>(Client() + 4 * word); }
u32 SendBuffer() { return Cw(Cw(7) + 1); }
u32 Voice(s32 v) { return kVoices + static_cast<u32>(v) * 0x48; }

void Copy(u32 dst, u32 src, u32 size) {
    for (u32 i = 0; i < size; i += 4) GRef<u32>(dst + i) = GRef<u32>(src + i);
}

}  // namespace

// EE: 0x002172C8
// Binds the SOUND server (first call) and sends fno 0; later calls report completion.
s32 BindSoundRpc(u32 client, u32 send, u32 receive, u32 param, u32 param2) {
    u32& state = GRef<u32>(0x00410CF8);
    if (state == 0) {
        GRef<u32>(client + 0x24) = param2;
        GRef<u32>(client + 0x18) = param;
        GRef<u32>(0x004117FC) = client;
        for (;;) {
            sceSifBindRpc(client + 0x34, 0x81000101u, 0);
            if (GRef<u32>(client + 0x58) != 0) break;  // cd->serve
        }
        Cw(5) = 4;
        Cw(7) = 0;
        Cw(8) = 1;
        Cw(0) = 0;
        Cw(10) = 1;
        Cw(11) = 1;
        Cw(12) = 0;
        GRef<u32>(send) = 0;
        Cw(2) = send + 0x2400;
        Cw(3) = receive;
        Cw(4) = receive + 0x600;
        Cw(1) = send;
        GRef<u32>(send + 4) = Cw(9);
        sceSifCallRpc(Client() + 0x34, 0, 1, Cw(Cw(7) + 1), 0x2400, Cw(Cw(7) + 3), 0x600, 0, 0);
        state = 1;
        return 0;
    }
    if (state == 1) return IsSoundRpcIdle(Client()) == 1 ? 1 : 0;
    return 0;
}

// EE: 0x00217428
// Returns the sceSifCallRpc result (v0 is not overwritten before the return).
s32 SendSoundCommands() {
    const u32 buf = Cw(Cw(7) + 1);
    GRef<u32>(buf) = Cw(0);
    const s32 r = sceSifCallRpc(Client() + 0x34, 1, 1, buf, 0x2400, Cw(Cw(7) + 3), 0x600, 0, 0);
    const u32 cur = Cw(7);
    Cw(5) = 4;
    Cw(8) = cur;
    Cw(7) = 1 - cur;
    Cw(0) = 0;
    return r;
}

// EE: 0x002174B0
s32 IsSoundRpcIdle(u32 client) {
    return sceSifCheckStatRpc(client + 0x34) == 0 ? 1 : 0;
}

// EE: 0x002174D0
// Sets the request type (+0) of a voice record and runs its builder: 1 key on (new), 2 set
// attributes, 3 key on (reuse), 4 key off.
s32 func_002174D0(s32 type, u32 rec) {
    GRef<s32>(rec) = type;
    s32 r;
    switch (type) {
        case 1: r = func_002181E8(rec); break;
        case 2: r = func_002183E8(rec); break;
        case 3: r = func_002187B0(rec); break;
        case 4: r = func_00218620(rec); break;
        default: return 1;
    }
    return ~r == 0 ? -1 : 1;  // builders return -1 on failure
}

// EE: 0x00217578
s32 QueueSoundVoiceCmd(u32 rec) {
    if (Cw(5) + 0x48 > 0x2400) return -1;
    Copy(SendBuffer() + Cw(5), rec, 0x48);
    Cw(0) += 1;
    Cw(5) += 0x48;
    return 1;
}

// EE: 0x00217680
// Command 0xA (IOP -> SPU transfer); returns its ticket.
s32 func_00217680(u32 src, u32 dst, u32 size) {
    if (Cw(5) + 0x48 > 0x2400) return -1;
    const u32 p = SendBuffer() + Cw(5);
    const s32 ticket = static_cast<s32>(func_00217828()) + 1;
    GRef<u32>(p) = 10;
    func_00217818(static_cast<u32>(ticket));
    GRef<u32>(p + 12) = size;
    GRef<u32>(p + 16) = static_cast<u32>(ticket);
    GRef<u32>(p + 4) = src;
    GRef<u32>(p + 8) = dst;
    Cw(0) += 1;
    Cw(5) += 0x14;
    return ticket;
}

// EE: 0x00217758
void func_00217758(u32 v) { GRef<u32>(Client() + 0x18) = v; }
// EE: 0x00217768
u32 func_00217768() { return GRef<u32>(Client() + 0x24); }
// EE: 0x00217778
void func_00217778(u32 v) { GRef<u32>(Client() + 0x24) = v; }
// EE: 0x002177C8
u32 func_002177C8() { return Client(); }
// EE: 0x002177D0
u32 func_002177D0() { return GRef<u32>(Client() + 0x20); }
// EE: 0x002177E0
u32 func_002177E0(s32 n) { return GRef<u32>(Client() + 4u * static_cast<u32>(n) + 0xC); }
// EE: 0x002177F8
void func_002177F8(u32 v) { GRef<u32>(Client() + 0x2C) = v; }
// EE: 0x00217808
u32 func_00217808() { return GRef<u32>(Client() + 0x2C); }
// EE: 0x00217818
void func_00217818(u32 v) { GRef<u32>(Client() + 0x28) = v; }
// EE: 0x00217828
u32 func_00217828() { return GRef<u32>(Client() + 0x28); }
// EE: 0x00217838
void func_00217838(u32 v) { GRef<u32>(Client() + 0x30) = v; }
// EE: 0x00217858
// Transfer `ticket` done (signed 64-bit compare of sign-extended words).
s32 func_00217858(s32 ticket) {
    return static_cast<s64>(ticket) <= static_cast<s64>(static_cast<s32>(func_00217808())) ? 1 : 0;
}

// EE: 0x00217888
s32 QueueSoundCmd8(u32 cmd) {
    if (Cw(5) + 8 > 0x2400) return -1;
    GRef<u64>(SendBuffer() + Cw(5)) = GRef<u64>(cmd);
    Cw(0) += 1;
    Cw(5) += 8;
    return 1;
}

// EE: 0x002178F0
s32 QueueSoundCmd12(u32 cmd) {
    if (Cw(5) + 0xC > 0x2400) return -1;
    const u32 p = SendBuffer() + Cw(5);
    GRef<u64>(p) = GRef<u64>(cmd);
    GRef<u32>(p + 8) = GRef<u32>(cmd + 8);
    Cw(0) += 1;
    Cw(5) += 0xC;
    return 1;
}

// EE: 0x00217960
s32 QueueSoundCmd16(u32 cmd) {
    if (Cw(5) + 0x10 > 0x2400) return -1;
    const u32 p = SendBuffer() + Cw(5);
    GRef<u64>(p) = GRef<u64>(cmd);
    GRef<u64>(p + 8) = GRef<u64>(cmd + 8);
    Cw(0) += 1;
    Cw(5) += 0x10;
    return 1;
}

// EE: 0x00217A48
void InitSoundVoiceTable() {
    for (u32 v = 0; v < 0x30; ++v) {
        GRef<u8>(kVoices + 0xE + v * 0x48) = static_cast<u8>(v);
        GRef<u8>(kVoiceStatus + v) = 0;
    }
}

// EE: 0x00217A88
// Reply type 0xF: copies the 48 voice states (except "3 = reserved, IOP says 0") and marks
// looping voices (+0x1C bit 0) that the IOP reports idle as busy.
void UpdateSoundVoiceStatus(u32 reply) {
    for (u32 v = 0; v < 0x30; ++v) {
        u8& st = GRef<u8>(kVoiceStatus + v);
        const u8 iop = GRef<u8>(reply + 8 + v);
        if (st != 3 || iop != 0) st = iop;
        if ((GRef<u8>(Voice(static_cast<s32>(v)) + 0x1C) & 1) != 0) {
            if (st == 0 || st == 4) st = 1;
        }
    }
    GRef<u32>(0x00410CFC) = GRef<u32>(reply + 4);
}

// EE: 0x00217BF8
// Picks a voice for a sound: group range and maximum from table 0x0033DF10 (key byte 2), priority
// in key byte 3. A free voice in the range is taken at once; otherwise the lowest-priority busy
// voice, ties broken by age. Quirks kept from the original: the tie-break loop tests the loop flag
// of voice `i` instead of candidate `i`, and with no busy candidate the first candidate is an
// uninitialised stack byte (zero in the port, whose guest frames are cleared).
s32 AllocSoundVoice(u32 key_ptr) {
    const u64 key = GRef<u64>(key_ptr);
    const u8 group = static_cast<u8>(key >> 16);
    const u8 priority = static_cast<u8>(key >> 24);
    const s32 gi = static_cast<s32>(static_cast<s8>(group)) * 4;
    const u8 last = GRef<u8>(0x0033DF11 + static_cast<u32>(gi));
    u32 v = GRef<u8>(0x0033DF10 + static_cast<u32>(gi));
    u8 cand[48] = {};
    u8 tie[48] = {};
    u32 count = 0;
    if (v <= last) {
        do {
            const u8 st = GRef<u8>(kVoiceStatus + v);
            if (st == 1 || st == 3 || st == 4) {
                const u8 max = GRef<u8>(0x0033DF12 + static_cast<u32>(gi));
                const u32 next = (count + 1) & 0xFF;
                cand[count] = static_cast<u8>(v);
                count = next;
                if (max <= next) break;
            } else if (st == 0) {
                GRef<u8>(kVoiceStatus + static_cast<u32>(static_cast<s8>(v))) = 3;
                return static_cast<s8>(v);
            }
            ++v;
        } while (static_cast<s32>(v) <= static_cast<s32>(last));
    }
    u32 ties = 1;
    tie[0] = cand[0];
    u8 min_prio = GRef<u8>(Voice(cand[0]) + 0x1B);
    for (u32 i = 1; i < count; ++i) {
        const u8 p = GRef<u8>(Voice(cand[i]) + 0x1B);
        if (p < min_prio) {
            ties = 1;
            tie[0] = cand[i];
            min_prio = p;
        } else if (p == min_prio) {
            tie[ties] = cand[i];
            ties = (ties + 1) & 0xFF;
        }
    }
    if (min_prio > priority) return -1;
    if (ties == 1) {
        GRef<u8>(kVoiceStatus + static_cast<u32>(static_cast<s8>(tie[0]))) = 3;
        return static_cast<s8>(tie[0]);
    }
    s64 best = -1;
    u32 best_age = 0;
    for (u32 i = 0; i < ties; ++i) {
        if ((GRef<u8>(Voice(static_cast<s32>(i)) + 0x1C) & 1) == 0) {  // voice i (sic)
            const u32 age = GRef<u32>(Voice(tie[i]) + 4);
            if (best == -1 || age < best_age) {
                best = static_cast<s8>(tie[i]);
                best_age = age;
            }
        }
    }
    if (best == -1) {
        best = static_cast<s8>(tie[0]);
        u32 a0 = GRef<u32>(Voice(tie[0]) + 4);
        for (u32 i = 1; i < ties; ++i) {
            const u32 a = GRef<u32>(Voice(tie[i]) + 4);
            if (a < a0) {
                best = static_cast<s8>(tie[i]);
                a0 = a;
            }
        }
    }
    GRef<u8>(kVoiceStatus + static_cast<u32>(best)) = 3;
    return static_cast<s32>(best);
}

// EE: 0x00217F28
// Renumbers the voice request ids (+4) in age order after the counter 0x00410D00 wrapped.
s32 func_00217F28() {
    u32& counter = GRef<u32>(0x00410D00);
    counter = 0;
    u32 prev = 0;
    s32 renumbered = 0;
    for (;;) {
        const u32 before = counter;
        counter = before + 1;
        s32 pick = -1;
        u32 pick_age = prev;
        for (s32 v = 0; v < 0x30; ++v) {
            if (GRef<u8>(kVoiceStatus + static_cast<u32>(v)) == 0) continue;
            const u32 age = GRef<u32>(Voice(v) + 4);
            if (pick == -1 && pick_age == 0) {
                // first candidate when nothing was picked in earlier rounds
            } else if (age <= prev || (pick_age <= age && pick_age != prev)) {
                continue;
            }
            pick = static_cast<s8>(v);
            pick_age = age;
        }
        if (pick == -1) return static_cast<s32>(counter);
        ++renumbered;
        GRef<u32>(Voice(pick) + 4) = counter;
        prev = pick_age;
        if (renumbered > 0x2F) {
            counter = before + 2;
            return static_cast<s32>(counter);
        }
    }
}

// EE: 0x00218008
// Copies the voice record whose request id is `id` into `out` (0) or returns -1.
s32 func_00218008(u32 id, u32 out) {
    if (GRef<u32>(out + 4) == 0) return -1;
    for (s32 v = 0; v < 0x30; ++v) {
        if (GRef<u32>(Voice(v) + 4) == id) {
            Copy(out, Voice(v), 0x48);
            return 0;
        }
    }
    return -1;
}

// EE: 0x002181E8
// Key on (type 1): sample lookup, new request id, voice allocation when not fixed.
s32 func_002181E8(u32 rec) {
    s8 note = -1;
    if (GRef<s8>(rec + 0x1A) >= 0) note = GRef<s8>(rec + 0x1A);
    if (func_00218868(GRef<s16>(rec + 0xC), GRef<s16>(rec + 0x18), rec + 0x18) == -1) return -1;
    if (note != -1) GRef<s8>(rec + 0x1A) = note;
    u32& counter = GRef<u32>(0x00410D00);
    counter += 1;
    GRef<u32>(rec + 4) = counter;
    if (counter == 0) GRef<u32>(rec + 4) = static_cast<u32>(func_00217F28());
    if (GRef<u8>(rec + 0xE) < 0x30) return 0;
    guest::GuestFrame key(0x30);
    Copy(key.Address(), rec + 0x18, 0x30);
    const s32 v = AllocSoundVoice(key.Address());
    if (v == -1 || v >= 0x30) return -1;
    GRef<s8>(rec + 0xE) = static_cast<s8>(v);
    return 0;
}

// EE: 0x00218318
void func_00218318(u32 rec) {
    Copy(Voice(GRef<s8>(rec + 0xE)), rec, 0x48);
}

// EE: 0x002183E8
// Set attributes (type 2) on a playing voice: flags +0x10 bit 0 pitch pair, bit 1 volume, bit 2
// pan bytes; then copies the voice back into the request.
s32 func_002183E8(u32 rec) {
    const s8 v = GRef<s8>(rec + 0xE);
    if (v < 0) return -1;
    const u32 voice = Voice(v);
    const u32 flags = GRef<u32>(rec + 0x10);
    if ((flags & 0x4000) == 0 && (GRef<u32>(rec + 4) == 0 || GRef<u32>(rec + 4) != GRef<u32>(voice + 4))) return -1;
    GRef<u32>(voice) = GRef<u32>(rec);
    GRef<u32>(voice + 0x10) = flags;
    if ((flags & 1) != 0) {
        GRef<u16>(voice + 0x2C) = GRef<u16>(rec + 0x2C);
        GRef<u16>(voice + 0x2E) = GRef<u16>(rec + 0x2E);
    }
    if ((flags & 2) != 0) GRef<u16>(voice + 0x38) = GRef<u16>(rec + 0x38);
    if ((flags & 4) != 0) {
        GRef<u8>(voice + 0x32) = GRef<u8>(rec + 0x32);
        GRef<u8>(voice + 0x33) = GRef<u8>(rec + 0x33);
    }
    Copy(rec, voice, 0x48);
    return 0;
}

// EE: 0x00218620
// Key off (type 4).
s32 func_00218620(u32 rec) {
    if (GRef<u32>(rec + 4) == 0) return -1;
    s8 v = GRef<s8>(rec + 0xE);
    if (GRef<u32>(rec + 4) != GRef<u32>(Voice(v) + 4) && (GRef<u32>(rec + 0x10) & 0x4000) == 0) return -1;
    if (GRef<u8>(kVoiceStatus + static_cast<u32>(v)) == 3) {
        GRef<u8>(kVoiceStatus + static_cast<u32>(v)) = 0;
        v = GRef<s8>(rec + 0xE);
    }
    GRef<u8>(Voice(v) + 0x1C) = 0;
    return 0;
}

// EE: 0x002187B0
// Key on with a fresh voice (type 3).
s32 func_002187B0(u32 rec) {
    guest::GuestFrame key(0x30);
    Copy(key.Address(), rec + 0x18, 0x30);
    const s32 v = AllocSoundVoice(key.Address());
    if (v == -1 || v >= 0x30) return -1;
    GRef<s8>(rec + 0xE) = static_cast<s8>(v);
    return 0;
}

// EE: 0x00218860
u32 func_00218860() { return GRef<u32>(0x00410CFC); }

// EE: 0x00218868
// Finds bank `bank_id`, copies sound `sound`'s 0x30-byte sample record into `out`, applies the
// note offset and relocates its SPU address.
s32 func_00218868(s16 bank_id, s16 sound, u32 out) {
    s32 bank = -1;
    const s32 count = GRef<s32>(kBankCount);
    for (s32 i = 0; i < count; ++i) {
        if (static_cast<s64>(bank_id) == static_cast<s64>(GRef<s16>(kBankId + 2u * static_cast<u32>(i)))) {
            bank = i;
            break;
        }
    }
    if (bank == -1) return -1;
    const u32 header = GRef<u32>(kBankHeader + 4u * static_cast<u32>(bank));
    if (static_cast<s32>(sound) >= static_cast<s32>(GRef<u16>(header + 8))) return -1;
    const u32 src = static_cast<u32>(sound) * 0x30 + header + 0x10;
    Copy(out, src, 0x30);
    const u16 r = func_0021CD70(
        ((static_cast<u32>(GRef<u8>(src + 0x18)) + static_cast<u32>(static_cast<s32>(GRef<s8>(src + 0x1C)))) * 0x100) & 0xFF00,
        static_cast<s16>(static_cast<s32>((static_cast<u32>(GRef<u8>(src + 0x19)) +
                                           static_cast<u32>(static_cast<s32>(GRef<s8>(src + 0x1D)))) * 0x10000) >> 16));
    GRef<u8>(out + 0x1B) = static_cast<u8>(r & 0x7F);
    GRef<u8>(out + 0x1A) = static_cast<u8>((r >> 8) & 0x7F);
    GRef<u32>(out + 0x28) = GRef<u32>(out + 0x2C) + GRef<u32>(kBankSpu + 4u * static_cast<u32>(bank));
    return 0;
}

// EE: 0x00218A00
// Registers bank n: header pointer, SPU address (explicit, or after bank n-1, or 0x5A10 for
// bank 0), id; checks it fits below the SPU limit.
s32 func_00218A00(s32 n, u32 header, s64 spu) {
    GRef<u32>(kBankHeader + 4u * static_cast<u32>(n)) = header;
    if (spu == 0) {
        GRef<u32>(kBankSpu + 4u * static_cast<u32>(n)) = n == 0 ? 0x5A10u : func_00218CB0(n - 1);
    } else {
        GRef<u32>(kBankSpu + 4u * static_cast<u32>(n)) = static_cast<u32>(spu);
    }
    GRef<u16>(kBankId + 2u * static_cast<u32>(n)) = GRef<u16>(GRef<u32>(kBankHeader + 4u * static_cast<u32>(n)) + 4);
    const s64 end = static_cast<s32>(func_00218CB0(n));
    if (static_cast<s32>(func_00218EF0()) <= end) {
        if (static_cast<s32>(func_00218ED8()) < end) return -1;
        func_00218EF8(static_cast<u32>(end));
    }
    if (GRef<s32>(kBankCount) <= n) GRef<s32>(kBankCount) = n + 1;
    return 0;
}

// EE: 0x00218B00
void func_00218B00(s32 n, u32 header, s64 spu) {
    if (n < 0) n = GRef<s32>(kBankCount);
    func_00218A00(n, header, spu);
}

// EE: 0x00218C20
void func_00218C20() {
    func_00218EF8(GRef<u32>(0x00410D04));
    GRef<u32>(kBankCount) = 0;
    for (u32 i = 0; i < 32; ++i) {
        GRef<u16>(kBankId + 2 * i) = 0xFFFF;
        GRef<u32>(kBankSpu + 4 * i) = 0;
    }
}

// EE: 0x00218C80
// Dead: only callers are unreachable.
u32 func_00218C80(s32 n) { return GRef<u32>(kBankHeader + 4u * static_cast<u32>(n)); }

// EE: 0x00218CB0
u32 func_00218CB0(s32 n) {
    return GRef<u32>(kBankSpu + 4u * static_cast<u32>(n)) + GRef<u32>(GRef<u32>(kBankHeader + 4u * static_cast<u32>(n)) + 0xC);
}

// EE: 0x00218CD8
s32 func_00218CD8(s16 bank_id) {
    const s32 count = GRef<s32>(kBankCount);
    for (s32 i = 0; i < count; ++i) {
        if (static_cast<s64>(bank_id) == static_cast<s64>(GRef<s16>(kBankId + 2u * static_cast<u32>(i)))) {
            return GRef<s32>(kBankSpu + 4u * static_cast<u32>(i));
        }
    }
    return -1;
}

// EE: 0x00218D58
s32 func_00218D58() { return GRef<s32>(kBankCount); }
// EE: 0x00218ED8
u32 func_00218ED8() { return GRef<u32>(0x00410D08); }
// EE: 0x00218EE8
void func_00218EE8(u32 v) { GRef<u32>(0x00410D08) = v; }
// EE: 0x00218EF0
u32 func_00218EF0() { return GRef<u32>(0x00411800); }
// EE: 0x00218EF8
void func_00218EF8(u32 v) { GRef<u32>(0x00411800) = v; }

// EE: 0x00218F18
// Per frame: when the previous batch completed, process its reply list, then send the next batch.
s32 UpdateSoundRpc() {
    if (IsSoundRpcIdle(func_002177C8()) != 1) return -1;
    u32& pending = GRef<u32>(0x00410D0C);
    if (pending != 0) {
        u32 p = func_002177E0(static_cast<s32>(func_002177D0()));
        const s32 count = GRef<s32>(p);
        p += 4;
        for (s32 i = 0; i < count; ++i) {
            switch (GRef<u32>(p)) {
                case 1: case 2: case 3: case 4: case 7: case 8: case 9:
                    p += 0xC;
                    break;
                case 6:
                    func_00218EE8(GRef<u32>(p + 4));
                    p += 0xC;
                    break;
                case 10:
                    func_002177F8(GRef<u32>(p + 4));
                    func_00217838(GRef<u32>(p + 8));
                    p += 0xC;
                    break;
                case 0xF:
                    UpdateSoundVoiceStatus(p);  // the original does not advance here
                    break;
                default:
                    break;  // unknown types: no advance (as the original)
            }
        }
        pending = 0;
    }
    if (SendSoundCommands() >= 0) pending = 1;
    return 1;
}

// EE: 0x00219030
s32 InitSoundRpc() {
    u32& state = GRef<u32>(0x00410D10);
    if (state == 0) {
        sceSifInitIopHeap();
        InitSoundVoiceTable();
        for (s32 i = 0; i < 0x10; ++i) func_0021CCF0(1.0f, i);
        func_00218C20();
        state = 1;
    } else if (state != 1) {
        return 0;
    }
    return BindSoundRpc(0x0033E558, 0x00425000, 0x00429800, 0x0033E5B8, 0);
}

// EE: 0x00219110
// Positional SE defaults (ranges 100/1000/3000, volume 0x50).
void func_00219110() {
    GRef<u32>(0x0034EB9C) = 0xF;
    GRef<u32>(0x0034EFC0) = 0x50;
    GRef<u32>(0x0034E780) = 0x50;
    GRef<u32>(0x0034EBA0) = 0;
    GRef<u32>(0x0034EFB4) = 100;
    GRef<u32>(0x0034EFB8) = 1000;
    GRef<u32>(0x0034EFBC) = 3000;
    GRef<u32>(0x0034E774) = 100;
    GRef<u32>(0x0034E778) = 1000;
    GRef<u32>(0x0034E77C) = 3000;
    GRef<u32>(0x0034EB94) = 0xF;
    GRef<u32>(0x0034EB98) = 0xF;
    func_00219260();
}

// EE: 0x00219260
// Stops every event SE voice of the three lists (pointer 0x0034E630[i], count 0x0034E620[i]).
void func_00219260() {
    for (u32 i = 0; i < 3; ++i) {
        const u32 list = GRef<u32>(0x0034E630 + 4 * i);
        const s32 count = GRef<s32>(0x0034E620 + 4 * i);
        GRef<u32>(list) = 0;
        for (s32 j = 0; j < count; ++j) {
            const u32 rec = list + static_cast<u32>(j) * 0x80 + 0x20;
            GRef<u32>(rec + 0x68) = 1;
            StopSound(rec);
        }
    }
}

// EE: 0x00219810
void func_00219810(s32 n, u32 header) { func_00218B00(n, header, 0); }
// EE: 0x00219830
u32 func_00219830() { return 0x0034F600; }
// EE: 0x00219840
u32 func_00219840() { return 0x00350E00; }

// EE: 0x00219850
// Direction of `to` seen from `from` in the frame of `angles`: atan2 of the rotated offset.
f32 func_00219850(u32 to, u32 from, u32 angles) {
    guest::GuestFrame frame(0xD0);
    const u32 m_unit = frame.Address(0x00);
    const u32 m_rot = frame.Address(0x40);
    const u32 delta = frame.Address(0x80);
    const u32 out = frame.Address(0x90);
    GRef<f32>(delta + 12) = fpu::Sub(GRef<f32>(to + 12), GRef<f32>(from + 12));
    GRef<f32>(delta) = fpu::Sub(GRef<f32>(to), GRef<f32>(from));
    GRef<f32>(delta + 4) = fpu::Sub(GRef<f32>(to + 4), GRef<f32>(from + 4));
    GRef<f32>(delta + 8) = fpu::Sub(GRef<f32>(to + 8), GRef<f32>(from + 8));
    sceVu0UnitMatrix(m_unit);
    func_00102D28(m_rot, m_unit, angles);
    sceVu0InversMatrix(m_unit, m_rot);
    sceVu0ApplyMatrix(out, m_unit, delta);
    return ee::atan2f(GRef<f32>(out), GRef<f32>(out + 8));
}

// EE: 0x00219900
// Left/right gains for an angle: sin/cos of the half angle, sign by side.
void func_00219900(f32 angle, u32 left, u32 right) {
    const f32 c = GRef<f32>(0x0040FFB8);
    const f32 half = fpu::Mul(fpu::Sub(angle, c), 0.5f);
    f32 s = ee::sinf(half);
    if (GRef<f32>(0x0040FFBC) < angle) s = -s;
    GRef<f32>(left) = s;
    f32 co = ee::cosf(half);
    if (c < angle) co = -co;
    GRef<f32>(right) = co;
}

// EE: 0x00219B48
// Resets the bank loader state 0x00351600 (two IOP buffers: base and base + 0x30000).
void func_00219B48() {
    GRef<u32>(0x00351600) = 0;
    GRef<u32>(0x00351604) = 0;
    GRef<u32>(0x00351608) = func_00217768();
    const u32 base = func_00217768();
    GRef<u32>(0x00351618) = 0;
    GRef<u32>(0x0035160C) = base + 0x30000;
}

// EE: 0x00219B90
// HYPOTHESIS: LoadSoundBankData. Loads a DATA entry holding sound bank headers and registers each.
s32 LoadSoundBankData(s32 entry, u32 dest) {
    u32& state = GRef<u32>(0x00351600);
    if (state != 1) {
        if (state > 1) {
            if (state != 2) return 0;
            if (IsDataLoadDone() == 0) return 0;
            const s32 count = GRef<s32>(dest);
            func_00217758(dest + GRef<u32>(dest + 4));
            u32 p = dest;
            for (s32 i = count; i > 0; --i) {
                p += 4;
                func_00219810(func_00218D58(), dest + GRef<u32>(p));
            }
            state = 0;
            return 1;
        }
        if (state != 0) return 0;
        state = 1;
    }
    LoadDataEntryAsync(entry, dest);
    state = 2;
    return 0;
}

// EE: 0x00219C80
// HYPOTHESIS: StreamBdEntryToIop. Streams BD.CDP entry `entry` into the IOP in chunks of up to
// 0x30000 bytes (two buffers) and transfers each into SPU memory at the bank's address
// (SOUND command 0xA).
s32 StreamBdEntryToIop(s32 bank_id, s32 entry) {
    u32& state = GRef<u32>(0x00351600);
    auto W = [](u32 a) -> u32& { return GRef<u32>(a); };
    s32 result = 0;
    switch (state) {
        case 0:
            W(0x00351624) = 0;
            state = 3;
            W(0x00351618) = 0;
            [[fallthrough]];
        case 3: {
            W(0x00351610) = W(0x00351608 + 4 * W(0x00351618));
            W(0x00351628) = GetTblEntrySector(1, entry);
            const s32 size = static_cast<s32>(GetTblEntrySize(1, entry));
            W(0x0035162C) = static_cast<u32>(size);
            const s32 rounded = size < 0 ? size + 0x7FF : size;
            if ((rounded >> 11) < 0x60) {
                W(0x00351620) = static_cast<u32>((rounded >> 11) + 1);
                W(0x0035161C) = W(0x0035162C);
            } else {
                W(0x0035161C) = 0x30000;
                W(0x00351620) = 0x60;
            }
            W(0x00351604) = QueueCdRead(1, GetCdFileLsn(1), 1, W(0x00351610), W(0x00351628),
                                        static_cast<s32>(W(0x00351620)), -1);
            W(0x00351614) = static_cast<u32>(func_00218CD8(static_cast<s16>(bank_id)));
            state = 4;
            break;
        }
        case 4: {
            if (GetCdDoneTicket(1) < W(0x00351604)) return 0;
            const s32 ticket = func_00217680(W(0x00351610), W(0x00351614), W(0x0035161C));
            W(0x00351630) = static_cast<u32>(ticket);
            if (ticket != -1) state = 5;
            break;
        }
        case 5:
            if (func_00217858(static_cast<s32>(W(0x00351630))) != 0) {
                W(0x00351618) = 1 - W(0x00351618);
                W(0x00351628) += W(0x00351620);
                W(0x00351610) = W(0x00351608 + 4 * W(0x00351618));
                W(0x00351624) += W(0x0035161C);
                if (static_cast<s32>(W(0x00351624)) < static_cast<s32>(W(0x0035162C))) {
                    W(0x00351614) += W(0x0035161C);
                    W(0x0035161C) = W(0x0035162C) - W(0x00351624);
                    if (static_cast<s32>(W(0x0035162C) - W(0x00351624)) > 0x2FFFF) W(0x0035161C) = 0x30000;
                    const s32 chunk = static_cast<s32>(W(0x0035161C));
                    const s32 rounded = chunk < 0 ? chunk + 0x7FF : chunk;
                    W(0x00351620) = 0x60;
                    if ((rounded >> 11) < 0x60) W(0x00351620) = static_cast<u32>((rounded >> 11) + 1);
                    W(0x00351604) = QueueCdRead(1, GetCdFileLsn(1), 1, W(0x00351610), W(0x00351628),
                                                static_cast<s32>(W(0x00351620)), -1);
                    state = 4;
                } else {
                    state = 6;
                }
            }
            break;
        case 6:
            state = 0;
            result = 1;
            break;
        default:
            break;
    }
    return result;
}

// EE: 0x0021A1C0
// Applies the 16 category volumes 0x0034F540 (sound commands) and the BGM/voice volumes to the 6
// stream slots, resets the four option volumes, then applies the sound options.
void func_0021A1C0() {
    for (s32 i = 0; i < 0x10; ++i) {
        if (i == 3) {
            func_0021CCF0(0.0f, 3);
        } else {
            func_0021CCF0(fpu::Div(GRef<f32>(0x0034F540 + 4u * static_cast<u32>(i)), 255.0f), i);
        }
    }
    guest::GuestFrame lr(0x10);
    for (s32 slot = 0; slot < 6; ++slot) {
        const u32 idx = slot >= 2 ? 2 : 3;  // slots 0-1 use volume 3, slots 2-5 volume 2
        const s32 v = fpu::FloatToInt(GRef<f32>(0x0034F540 + 4 * idx));
        func_0021F138(slot, v, v);
        func_0021F208(slot, lr.Address(0), lr.Address(4));
        func_0021F190(0.0f, slot, GRef<s32>(lr.Address(0)), GRef<s32>(lr.Address(4)), 1);
    }
    for (s32 i = 0; i < 4; ++i) {
        GRef<f32>(0x0034F5C0 + 4u * static_cast<u32>(i)) = 1.0f;
        func_0021A328(1.0f, i);
    }
    ApplySoundOptions(0x002A15C4);
}

// EE: 0x0021A328
// Sets option volume n (0 BGM? 1 SE? ...): scales the categories mapped to it (table built on the
// stack) and pushes them to the sound categories or the stream slots (2: slots 2-5, 3: slots 0-1).
void func_0021A328(f32 volume, s32 n) {
    GRef<f32>(0x0034F5C0 + 4u * static_cast<u32>(n)) = volume;
    static const s8 kMap[4][16] = {
        {3, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
        {0, 1, 4, 5, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
        {2, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
        {6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1}};
    guest::GuestFrame lr(0x10);
    for (s32 i = 0; i < 16; ++i) {
        const s8 cat = kMap[n][i];
        if (cat == -1) continue;
        const f32 base = GRef<f32>(0x0034F540 + 4u * static_cast<u32>(cat));
        f32 v = fpu::Mul(base, volume);
        if (255.0f < fpu::Mul(base, volume)) v = 255.0f;
        GRef<f32>(0x0034F580 + 4u * static_cast<u32>(cat)) = v;
        if (cat == 2) {
            for (s32 slot = 2; slot < 6; ++slot) {
                func_0021F138(slot, fpu::FloatToInt(v), fpu::FloatToInt(v));
                func_0021F208(slot, lr.Address(0), lr.Address(4));
                func_0021F190(0.0f, slot, GRef<s32>(lr.Address(0)), GRef<s32>(lr.Address(4)), 1);
            }
            func_0021CCF0(fpu::Div(v, 255.0f), 2);
        } else if (cat == 3) {
            for (s32 slot = 0; slot < 2; ++slot) {
                func_0021F138(slot, fpu::FloatToInt(v), fpu::FloatToInt(v));
                func_0021F208(slot, lr.Address(0), lr.Address(4));
                func_0021F190(0.0f, slot, GRef<s32>(lr.Address(0)), GRef<s32>(lr.Address(4)), 1);
            }
        } else {
            func_0021CCF0(fpu::Div(v, 255.0f), cat);
        }
    }
}

// EE: 0x0021A680
// SOUND commands 6/7 for channel 1: reverb mode 2, depth 10000/10000.
void func_0021A680() {
    func_0021CF70(1, 2);
    func_0021CFA0(1, 10000, 10000);
}

// EE: 0x0021A750
// Copies the 0x200-byte sound settings (pan law table) to 0x0042A4E8.
void func_0021A750(u32 src) {
    Copy(0x0042A4E8, src, 0x200);
}

// EE: 0x0021A800
// Volume x pan: linear (vol * pan / 255) or the pan law table 0x0042A4E8 for signed pan.
s16 func_0021A800(s64 law, s16 volume, s16 pan) {
    const s32 v = volume;
    s32 p = pan;
    if (law == 0) return static_cast<s16>((v * p) / 0xFF);
    if (p < 0) {
        if (p < -0xFF) p = -0xFF;
        return static_cast<s16>((-v * static_cast<s32>(GRef<s16>(0x0042A4E8 + static_cast<u32>(p + 0xFF) * 2))) / 0x7FFF);
    }
    if (p > 0xFF) p = 0xFF;
    return static_cast<s16>((v * static_cast<s32>(GRef<s16>(0x0042A4E8 + static_cast<u32>(0xFF - p) * 2))) / 0x7FFF);
}

// EE: 0x0021AD48
// Pan mode from the signs of the left/right volumes.
s32 func_0021AD48(s32 left, s16 right) {
    if ((left << 16) < 0) return right >= 0 ? 1 : 3;
    return right >= 0 ? 0 : 2;
}

// EE: 0x0021ADF8
void func_0021ADF8() {
    GRef<u32>(0x00351F60) = 0;
    GRef<u32>(0x00351F60 - 0x110) = 0;
}

// EE: 0x0021CCF0
void func_0021CCF0(f32 volume, s32 n) {
    GRef<f32>(0x0042CA50 + 4u * static_cast<u32>(n)) = volume;
}

// EE: 0x0021CD70
// Adds a fine offset to a packed note/fine pair and clamps to 0x7F00.
u16 func_0021CD70(u32 pair, s16 offset) {
    u32 v = static_cast<u32>(static_cast<s32>((((pair & 0xFF00) >> 1 | (pair & 0x7F)) + static_cast<u32>(static_cast<s32>(offset))) * 0x10000) >> 16);
    if (static_cast<s32>(v) < 0) v = 0;
    u16 r = static_cast<u16>(((v & 0x7F80) << 1) | (v & 0x7F));
    if (static_cast<s16>(r) > 0x7F00) r = 0x7F00;
    return r;
}

namespace {

// Builds a small command on the guest stack and queues it.
template <typename Fill>
void QueueSmallCommand(u32 size, Fill fill, s32 (*queue)(u32)) {
    guest::GuestFrame cmd(0x10);
    fill(cmd.Address());
    (void)size;
    queue(cmd.Address());
}

}  // namespace

// EE: 0x0021CE08
void SetSoundOutputMode(u32 mode) {
    QueueSmallCommand(8, [mode](u32 c) {
        GRef<u32>(c) = 0xE;
        GRef<u32>(c + 4) = mode;
    }, QueueSoundCmd8);
}

// EE: 0x0021CE38
// Command 0xB: master volume pair scaled to 0x3FFF.
void func_0021CE38(u8 core, s16 left, s16 right) {
    QueueSmallCommand(12, [core, left, right](u32 c) {
        GRef<u32>(c) = 0xB;
        GRef<u8>(c + 4) = core;
        GRef<s16>(c + 6) = func_0021A800(1, 0x3FFF, left);
        GRef<s16>(c + 8) = func_0021A800(1, 0x3FFF, right);
    }, QueueSoundCmd12);
}

// EE: 0x0021CF08
// Command 0xD: sound-data input (movie PCM) volume, scaled to 0x7FFF.
void SetSoundDataInputVolume(u8 core, s16 left, s16 right) {
    QueueSmallCommand(12, [core, left, right](u32 c) {
        GRef<u32>(c) = 0xD;
        GRef<u8>(c + 4) = core;
        GRef<s16>(c + 6) = func_0021A800(1, 0x7FFF, left);
        GRef<s16>(c + 8) = func_0021A800(1, 0x7FFF, right);
    }, QueueSoundCmd12);
}

// EE: 0x0021CF70
// Command 6: reverb mode for core n.
void func_0021CF70(u8 core, u32 mode) {
    QueueSmallCommand(16, [core, mode](u32 c) {
        GRef<u32>(c) = 6;
        GRef<u8>(c + 4) = core;
        GRef<u32>(c + 8) = mode;
    }, QueueSoundCmd16);
}

// EE: 0x0021CFA0
// Command 7: reverb depth left/right for core n.
void func_0021CFA0(u8 core, s16 left, s16 right) {
    QueueSmallCommand(16, [core, left, right](u32 c) {
        GRef<u32>(c) = 7;
        GRef<u8>(c + 4) = core;
        GRef<s16>(c + 0xC) = left;
        GRef<s16>(c + 0xE) = right;
    }, QueueSoundCmd16);
}

// EE: 0x0021D138
s32 func_0021D138(u32 rec) {
    if (func_002174D0(3, rec) == -1) return -1;
    func_00218318(rec);
    QueueSoundVoiceCmd(rec);
    return 0;
}

// EE: 0x0021D680
// Sets the raw volume pair of a playing voice; when the pan mode changes on a looping voice it is
// restarted (key on a copy, key off the old one). Follows the chained voice (+8) recursively.
s32 func_0021D680(s16 left, s16 right, u32 rec) {
    GRef<s16>(rec + 0x2C) = left;
    GRef<s16>(rec + 0x2E) = right;
    const s32 mode = func_0021AD48(static_cast<u16>(left), right);
    guest::GuestFrame frame(0xA0);
    const u32 chained = frame.Address(0x00);  // 0x50 bytes
    const u32 old = frame.Address(0x50);      // 0x48 bytes
    u32 next_id;
    if (GRef<s32>(rec + 0x14) == mode || (GRef<u8>(rec + 0x1C) & 1) == 0) {
        GRef<s32>(rec + 0x14) = mode;
        GRef<u32>(rec + 0x10) = 1;
        if (func_002174D0(2, rec) == -1) return -1;
        QueueSoundVoiceCmd(rec);
        next_id = GRef<u32>(rec + 8);
    } else {
        GRef<s32>(rec + 0x14) = mode;
        Copy(old, rec, 0x48);
        func_0021D138(rec);
        StopSound(old);
        next_id = GRef<u32>(rec + 8);
    }
    if (next_id != 0 && func_00218008(next_id, chained) != -1) func_0021D680(left, right, chained);
    return 0;
}

// EE: 0x0021E058
// StopSound: key off request (type 4).
s32 StopSound(u32 rec) {
    GRef<u32>(rec + 0x10) = 0;
    if (func_002174D0(4, rec) == -1) return -1;
    QueueSoundVoiceCmd(rec);
    return 0;
}

// EE: 0x0021E240
// Sets the volume of voice `voice` directly (attribute request with flag 0x4001).
s32 func_0021E240(u8 voice, s16 volume, u8 left_pan, u8 right_pan) {
    guest::GuestFrame frame(0x50);
    const u32 rec = frame.Address();
    GRef<u8>(rec + 0xE) = voice;
    const s16 l = func_0021A800(1, volume, left_pan);
    GRef<s16>(rec + 0x2E) = func_0021A800(1, volume, right_pan);
    GRef<u32>(rec + 0x10) = 0x4001;
    GRef<s16>(rec + 0x2C) = l;
    if (func_002174D0(2, rec) == -1) return -1;
    QueueSoundVoiceCmd(rec);
    return 0;
}

}  // namespace ac04::game
