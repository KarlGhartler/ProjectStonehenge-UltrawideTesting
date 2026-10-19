// Radio chatter (slice S5e): request queues, matchers that turn requests into candidate messages,
// the scheduler and playback, subtitles. docs/subsystems/sound.md "Radio chatter and subtitles".
// Message database (mission blocks 34/35): header copy 0x004BA300 (word 0 = pointer to the
// bank header: u16 kind, u16 period, then the chatter probabilities), records of 8 bytes (+0 event
// kind, +1 side class, +2 flags) at header +4, count header +8; 0xA80 message pointers 0x004BA570.
// Every `rand` call stays in the original order (docs/game/random.md).
#include "game/game.h"

#include "common/ee_math.h"
#include "common/guest_stack.h"
#include "game/flight_common.h"
#include "sce/sce.h"

namespace ac04::game {

using guest::GRef;
using namespace fl;

namespace {

constexpr u32 kHeader = 0x004BA300;
constexpr u32 kFlags = 0x00411860;

u32 BankHeader() { return W(kHeader); }

// Speaker class of a chatter request (QueueRadioChatter / func_00252B88): 0xC0 | id for the
// player's wingmen and friends (< 0x22), 0x40 | id (< 0x28), 0x80 | id (< 0x2E), then fixed codes.
}  // namespace

// EE: 0x002507E0
u32 func_002507E0() { return kHeader; }

// EE: 0x002507F0
// Message record pointer of message `idx` (-1 when out of range).
u32 func_002507F0(u32 idx) {
    if (idx < 0xA80) return W(0x004BA570 + 4 * idx);
    return 0xFFFFFFFFu;
}

// EE: 0x00250820
// Dead (no reference): message record pointer of message `idx` (modulo 0xA80) = rec.
void func_00250820(u32 idx, u32 rec) { GRef<u32>(0x004BA570 + (idx % 0xA80u) * 4) = rec; }

// EE: 0x00250850
// Timer call-out record for message id (0xA5F-based, 9 bits).
u32 func_00250850(s32 id) { return W(0x004BCF70 + ((static_cast<u32>(id) - 0xA5Fu) & 0x1FFu) * 4); }

// EE: 0x002508A0
u32 func_002508A0() { return 0x004BD788; }

// EE: 0x002508B0
u32 func_002508B0() { return 0x004BA470; }

// EE: 0x002508C0
// Sets radio state flags.
void func_002508C0(u32 bits) { GRef<u32>(kFlags) |= bits; }

// EE: 0x002508D0
u32 func_002508D0() { return kFlags; }

// EE: 0x00250900
u32 func_00250900() { return 0x00411868; }

// EE: 0x00250910
u32 func_00250910() { return W(kHeader); }

// EE: 0x00250920
u32 func_00250920() { return 0x004BD770; }

// EE: 0x00251050
// Radio per frame: request matching (the matchers allowed by the bank kind and the radio state:
// mask bits 0x10 scripted, 0x08 target, 0x01 event, 0x02 timer, 0x04 situation, 0x80 extra),
// candidate sorting and scheduling, queue reset. Tutorial bank (kind 6): the reminder message 0x17
// at frame 540 of sub 0xC; in sub 0xC only scripted/event chatter (event chatter while a
// kind 6/7 request is queued or was seen).
void func_00251050() {
    u32 mask = 0;
    s32 n = 0;
    const u32 flags = func_002508D0();
    const u32 bank = W(kHeader);
    guest::GuestFrame fr(0x10);
    const u32 cntp = fr.Address();
    const u32 q = func_00253058(cntp);
    bool sub_c = false;
    if (GRef<u16>(bank) == 6) {
        if (Sub() == 0xC) {
            const s32 t = GRef<s32>(0x0041186C) + 1;
            GRef<s32>(0x0041186C) = t;
            if (t == 0x21C) QueueRadioMessage(0x17);
            sub_c = Sub() == 0xC;
        }
    } else {
        sub_c = Sub() == 0xC;
    }
    if (sub_c) {
        const u32 cnt = GRef<u8>(cntp);
        bool hit = false;
        for (u32 k = 0; k < cnt; ++k) {
            if (W(q + k * 0x18) - 6u < 2u) {
                hit = true;
                GRef<u8>(0x00411870) = 1;
            }
        }
        if (hit)
            mask = 0x1E;
        else
            mask = GRef<u8>(0x00411870) == 0 ? 0xFu : 0x9Fu;
    } else {
        const u32 f = W(flags);
        if ((f & 8u) != 0) {
            const u16 k = GRef<u16>(bank);
            if (k == 3 || k == 5 || k == 7 || k == 0x12) mask = 0xF;
            if ((f & 0x40000000u) != 0) {
                func_002559C8();
                mask |= 0x9F;
            }
        }
    }
    if ((mask & 0x10) == 0) n = func_00254B98(kHeader, 0x004BA330, 0);
    if ((mask & 8) == 0) n = func_00254930(kHeader, 0x004BA330, n);
    if ((mask & 1) == 0) n = func_00253158(kHeader, 0x004BA330, n);
    if ((mask & 2) == 0) n = func_00253F88(kHeader, 0x004BA330, n);
    if ((mask & 4) == 0) n = func_00254340(kHeader, 0x004BA330, n);
    if ((mask & 0x80) == 0) n = func_00254D40(kHeader, 0x004BA330, n);
    func_00255398(0x004BA470, n, 0x004BA330);
    func_002554B8(0x004BA470);
    func_00253038();
}

// EE: 0x002528C8
// Event chatter request: accepted when rand() < probability[kind] * 2^31 (table 0x00409DA8,
// copied to the stack by the original) into ring 0x004BD7C0 (4 × 0x18: kind, b, class code, a,
// d, e).
s32 QueueRadioChatter(s32 kind, s32 a, s32 b, s32 cls, s32 d, s32 e) {
    // The original copies the 29-entry table to the stack and indexes the copy; read in place.
    const f32 p = F(0x00409DA8 + 4u * static_cast<u32>(kind));
    const s32 lim = fpu::FloatToInt(p * 2147483648.0f);
    if (!(ee::rand() < lim)) return 0;
    const u8 c = static_cast<u8>(GRef<u8>(0x00411871) & 3);
    GRef<u8>(0x00411871) = c;
    const u32 e0 = 0x004BD7C0 + static_cast<u32>(c) * 0x18;
    GRef<s32>(e0) = kind;
    GRef<s32>(e0 + 0xC) = a;
    GRef<s32>(e0 + 4) = b;
    u32 code;
    if (cls < 0x22)
        code = (static_cast<u32>(cls) & 0x3F) | 0xC0;
    else if (cls < 0x28)
        code = (static_cast<u32>(cls) & 0x3F) | 0x40;
    else if (cls < 0x2E)
        code = (static_cast<u32>(cls) & 0x3F) | 0x80;
    else if (cls < 0x2F)
        code = 0x40;
    else if (cls < 0x30)
        code = 0x80;
    else if (cls < 0x31)
        code = 0xC0;
    else
        code = 0x31;
    GRef<u32>(e0 + 8) = code;
    GRef<s32>(e0 + 0x14) = e;
    GRef<s32>(e0 + 0x10) = d;
    GRef<u8>(0x00411871) = static_cast<u8>(GRef<u8>(0x00411871) + 1);
    return 0;
}

// EE: 0x00252AF0
// Waypoint distance call request (kind < 0x1D, clock 1-36, distance code 2/4/8/12/20/30) into
// ring 0x004BD820 (4 × 0xC).
s32 func_00252AF0(s32 kind, s32 clock, s32 code) {
    if ((code == 0x1E || code == 0x14 || code == 0xC || code == 8 || code == 4 || code == 2) && kind < 0x1D &&
        clock < 0x25) {
        const u8 c = static_cast<u8>(GRef<u8>(0x00411872) & 3);
        GRef<u8>(0x00411872) = static_cast<u8>(c + 1);
        const u32 e = 0x004BD820 + static_cast<u32>(c) * 0xC;
        GRef<s32>(e + 8) = code;
        GRef<s32>(e) = kind;
        GRef<s32>(e + 4) = clock;
    }
    return 0;
}

// EE: 0x00252B88
// Speaker request into ring 0x004BD850 (4 × 0x14: a, b, c, d, class code).
s32 func_00252B88(s32 a, s32 b, s32 c, s32 d, s32 cls) {
    const u8 n = static_cast<u8>(GRef<u8>(0x00411873) & 3);
    GRef<u8>(0x00411873) = n;
    const u32 e = 0x004BD850 + static_cast<u32>(n) * 0x14;
    GRef<s32>(e) = a;
    GRef<s32>(e + 4) = b;
    GRef<s32>(e + 8) = c;
    GRef<s32>(e + 0xC) = d;
    u32 code;
    if (cls < 0x22)
        code = (static_cast<u32>(cls) & 0x1F) | 0xC0;
    else if (cls < 0x28)
        code = (static_cast<u32>(cls) & 0x1F) | 0x40;
    else if (cls < 0x2E)
        code = (static_cast<u32>(cls) & 0x1F) | 0x80;
    else if (cls < 0x2F)
        code = 0x40;
    else if (cls < 0x31)
        code = 0xC0;
    else
        code = 0x80;
    GRef<u32>(e + 0x10) = code;
    GRef<u8>(0x00411873) = static_cast<u8>(GRef<u8>(0x00411873) + 1);
    return 0;
}

// EE: 0x00252C30
// Situation chatter request (kind < 0x1E: own side): when the ring 0x004BD8A0 is empty the battle
// state (counts a, b, c, d and flags e) is stored in the radio state 0x00411860; the bank kind
// adjusts the counts (2, 4, 0xB); mode 0-3 from the side and counts; every bank period
// (header +2, frames) or half period the call is forced/upgraded; the acceptance threshold for
// rand() comes from the bank header (+4..+0x2C by mode and count sums). Returns 1 when nothing
// applies (no rand call).
s32 func_00252C30(s32 kind, s32 a, s32 b, s32 c, s32 d, u32 e) {
    const u8 cnt = static_cast<u8>(GRef<u8>(0x00411874) & 3);
    GRef<u8>(0x00411874) = cnt;
    const u32 f = func_002508D0();
    s32 half = 0;
    const u32 h = func_00250910();
    const bool own = kind < 0x1E;
    if (GRef<u8>(0x00411874) == 0) {
        GRef<u32>(f) = 0;
        GRef<u8>(f + 4) = static_cast<u8>(a);
        GRef<u8>(f + 6) = static_cast<u8>(c);
        GRef<u8>(f + 5) = static_cast<u8>(b);
        GRef<u8>(f + 7) = static_cast<u8>(d);
        func_002508C0(e);
        if (own) func_002508C0(0x80000000u);
    }
    const u16 bk = GRef<u16>(h);
    if (bk == 2) {
        if (own && !(1 < c)) c = 2;
    } else if (bk == 4) {
        if (!(3 < c)) c = 4;
        if (!(3 < a)) a = 4;
    } else if (bk == 0xB) {
        a = 3;
        c = 3;
    }
    s32 mode;
    if (own)
        mode = (c == 0 && d == 0) ? 3 : 0;
    else
        mode = c != 0 ? 1 : 2;
    const u16 period = GRef<u16>(h + 2);
    const u32 rem = W(CtxAddr() + 0x2C) % period;  // divu
    bool tick = false;
    s32 res = 2;
    if (rem == 0)
        tick = true;
    else
        half = rem == static_cast<u32>(period >> 1) ? 1 : 0;
    if (!(a < 2) && !(c < 2) && tick) {
        res = 0;
    } else if (mode == 2) {
        const s32 v = tick ? 0 : 2;
        if (!(a < 2)) res = v;
    }
    const s32 bd = b + d;
    if (!(bd < 4) && half != 0) res = 1;
    if (res == 2) return 1;
    s32 lim = 0;
    if (res == 0) {
        const u32 ac = static_cast<u32>(a + c);
        if (mode == 1) {
            if (!(ac < 10))
                lim = GRef<s32>(h + 0x14);
            else if (!(ac < 7))
                lim = GRef<s32>(h + 0x18);
            else if (!(ac < 4))
                lim = GRef<s32>(h + 0x1C);
            else
                lim = GRef<s32>(h + 0x20);
        } else if (mode < 2) {
            if (mode == 0) {
                if (!(ac < 10))
                    lim = GRef<s32>(h + 4);
                else if (!(ac < 7))
                    lim = GRef<s32>(h + 8);
                else if (!(ac < 4))
                    lim = GRef<s32>(h + 0xC);
                else
                    lim = GRef<s32>(h + 0x10);
            }
        } else if (mode == 2) {
            if (!(ac < 5))
                lim = GRef<s32>(h + 0x24);
            else if (ac < 2)
                lim = GRef<s32>(h + 0x2C);
            else
                lim = GRef<s32>(h + 0x28);
        }
    } else if (mode == 0) {
        const u32 s = static_cast<u32>(bd);
        if (!(s < 9))
            lim = GRef<s32>(h + 4);
        else if (!(s < 5))
            lim = GRef<s32>(h + 8);
        else if (s < 2)
            lim = GRef<s32>(h + 0x10);
        else
            lim = GRef<s32>(h + 0xC);
    }
    if (ee::rand() < lim) {
        const u32 q = 0x004BD8A0 + static_cast<u32>(GRef<u8>(0x00411874)) * 0x10;
        GRef<s32>(q) = kind;
        GRef<u32>(q + 0xC) = e;
        GRef<s32>(q + 4) = mode;
        GRef<s32>(q + 8) = res;
        GRef<u8>(0x00411874) = static_cast<u8>(GRef<u8>(0x00411874) + 1);
    }
    return 0;
}

// EE: 0x00252F80
// Scripted message request into ring 0x004BD8E0 (4 ids).
s32 QueueRadioMessage(s32 id) {
    const u8 c = static_cast<u8>(GRef<u8>(0x00411875) & 3);
    GRef<u8>(0x00411875) = static_cast<u8>(c + 1);
    GRef<s32>(0x004BD8E0 + 4u * c) = id;
    return 0;
}

// EE: 0x00252FB8
// Scripted message id + rand() % n (the third argument passed by callers is unused).
void func_00252FB8(s32 id, s32 n, s32 /*unused*/) {
    const s32 r = ee::rand();
    QueueRadioMessage(id + r % n);
}

// EE: 0x00253000
// Extra message request into ring 0x004BD8F0 (4 ids).
s32 func_00253000(s32 id) {
    const u8 c = static_cast<u8>(GRef<u8>(0x00411876) & 3);
    GRef<u8>(0x00411876) = static_cast<u8>(c + 1);
    GRef<s32>(0x004BD8F0 + 4u * c) = id;
    return 0;
}

// EE: 0x00253058
u32 func_00253058(u32 count) {
    GRef<u8>(count) = GRef<u8>(0x00411871);
    return 0x004BD7C0;
}

// EE: 0x00253070
u32 func_00253070(u32 count) {
    GRef<u8>(count) = GRef<u8>(0x00411872);
    return 0x004BD820;
}

// EE: 0x00253088
u32 func_00253088(u32 count) {
    GRef<u8>(count) = GRef<u8>(0x00411873);
    return 0x004BD850;
}

// EE: 0x002530A0
u32 func_002530A0(u32 count) {
    GRef<u8>(count) = GRef<u8>(0x00411874);
    return 0x004BD8A0;
}

// EE: 0x002530B8
u32 func_002530B8(u32 count) {
    GRef<u8>(count) = GRef<u8>(0x00411875);
    return 0x004BD8E0;
}

// EE: 0x002530D0
u32 func_002530D0(u32 count) {
    GRef<u8>(count) = GRef<u8>(0x00411876);
    return 0x004BD8F0;
}

// EE: 0x002530E8
// Side match of a request (a) and a record (b): -1 both neutral (0x1E), 1 both own side (< 0x1E).
s32 func_002530E8(s32 a, s32 b) {
    if (a == 0x1E && b == 0x1E) return -1;
    if (a < 0x1E) return b < 0x1E ? 1 : 0;
    return 0;
}

// EE: 0x00253110
// Speaker match: -1 both neutral, own side: record 0x1D matches any, else equal ids.
s32 func_00253110(s32 a, s32 b) {
    if (a == 0x1E && b == 0x1E) return -1;
    if (a < 0x1E) {
        if (b != 0x1D) return a == b ? 1 : 0;
        return 1;
    }
    return 0;
}

// EE: 0x00253158
// Event chatter matcher: for each queued event request (kinds 0x1B/0x1C skipped) the records of
// the same event kind give candidate message indices (48-entry ring) by kind-specific rules: side
// and flag tests, ranges by target class from the byte tables 0x00411588-0x004115E0 (kinds 2/4/6/8),
// speaker-class offsets (0x11), fixed ids 0xBF-0xC8 (0x12), kill-count groups (0x18); records of
// kind 0x1C are skipped. One rand() % count picks the message; the candidate goes to ring `out`
// (20 × 0x10: index, 0, kind, record). Translated from the disassembly (the decompiler output
// drops 26 blocks).
s32 func_00253158(u32 hdr, u32 out, s32 n) {
    const u32 flags = func_002508D0();
    guest::GuestFrame fr(0x10);
    const u32 cntp = fr.Address();
    u32 q = func_00253058(cntp);
    s16 cand[0x30];
    for (s32 k = 0; k < GRef<u8>(cntp); ++k, q += 0x18) {
        if (W(q) - 0x1Bu < 2u) continue;
        s32 nc = 0;
        auto add = [&](s32 idx) {
            cand[nc % 0x30] = static_cast<s16>(idx);
            ++nc;
        };
        u32 rec = W(hdr + 4);
        for (s32 j = 0; j < static_cast<s32>(GRef<u16>(hdr + 8)); ++j, rec += 8) {
            if (W(q) != GRef<u8>(rec)) continue;
            const s32 side = func_002530E8(GRef<s32>(q + 4), GRef<u8>(rec + 1));
            const u32 kind = W(q);
            auto skip0x1c = [&](s32 off) { return GRef<u8>(rec + static_cast<u32>(off) * 8) == 0x1C; };
            const bool own = GRef<s32>(q + 4) < 0x1E;
            switch (kind < 0x1B ? kind : 0xFFu) {
                case 0:
                    if (own) break;
                    if ((W(q + 0x14) & 1u) != 0) {
                        if ((GRef<u16>(rec + 2) & 1) != 0) add(j);
                    } else if ((GRef<u16>(rec + 2) & 1) == 0) {
                        const s32 off = GRef<s32>(q + 0xC);
                        if (!skip0x1c(off)) add(j + off);
                    }
                    break;
                case 1:
                    if (own) break;
                    if ((W(q + 0x14) & GRef<u16>(rec + 2)) != 0) add(j);
                    break;
                case 2:
                case 4:
                case 6:
                case 8: {
                    const u32 t = kind == 2 ? 0 : kind == 4 ? 1 : kind == 6 ? 2 : 3;
                    auto tb = [&](u32 a) { return static_cast<s32>(GRef<u8>(a + t)); };
                    const u32 c = GRef<u8>(q + 8) & 0x3F;
                    s32 start2, count2;
                    if (c == 0x10) {
                        s32 start1, count1;
                        if (own) {
                            count1 = tb(0x004115E0);
                            start2 = tb(0x004115C0);
                            count2 = tb(0x004115C8);
                            start1 = tb(0x004115D8);
                        } else {
                            start2 = 0;
                            count2 = tb(0x004115B8);
                            start1 = tb(0x00411588);
                            count1 = tb(0x004115D0);
                        }
                        for (s32 i = 0; i < count1; ++i)
                            if (!skip0x1c(start1 + i)) add(j + start1 + i);
                    } else if (c < 0x10 || !(c < 0x22) || c < 0x15) {
                        if (own) {
                            count2 = tb(0x00411598);
                            start2 = tb(0x00411590);
                        } else {
                            start2 = 0;
                            count2 = tb(0x00411588);
                        }
                    } else if (own) {
                        count2 = tb(0x004115B0);
                        start2 = tb(0x004115A8);
                    } else {
                        start2 = 0;
                        count2 = tb(0x004115A0);
                    }
                    for (s32 i = 0; i < count2; ++i)
                        if (!skip0x1c(start2 + i)) add(j + start2 + i);
                    break;
                }
                case 7:
                case 0x13:
                case 0x14:
                case 0x17: add(j); break;
                case 0x11: {
                    const u32 c = GRef<u8>(q + 8) & 0x3F;
                    u32 hi = GRef<u8>(q + 8) & 0xC0;
                    s32 off;
                    switch (c) {
                        case 4: off = 0; break;
                        case 0x15: off = 4; break;
                        case 0x16: off = 6; break;
                        case 0x17: off = 8; break;
                        case 0x1C: off = 0xA; break;
                        case 0x22: off = 0xC; break;
                        case 0x23: off = 0xE; break;
                        case 0x24: off = 0x10; break;
                        case 0x25: off = 0x12; break;
                        case 0x26: off = 0x14; break;
                        case 0x27: off = 0x16; break;
                        case 0x28: off = 0x18; break;
                        case 0x29: off = 0x19; break;
                        case 0x2A: off = 0x1A; break;
                        case 0x2B: off = 0x1B; break;
                        case 0x2C: off = 0x1C; break;
                        case 0x2D: off = 0x1D; break;
                        default: off = 0xFF; break;
                    }
                    bool plain = false;  // the 0x40 / 0x80 single entries
                    if (off < 0xFF) {
                        if (own) {
                            const s32 v = hi == 0x80 ? j + off : j + off + 1;
                            for (s32 r = 0; r < 4; ++r) add(v);
                        } else if (hi != 0x80) {
                            for (s32 r = 0; r < 4; ++r) add(j + off);
                        }
                        // After the four entries the original clears 0x40/0x80 (no single entry).
                        if (hi == 0x40 || hi == 0x80) hi = 0;
                    } else {
                        plain = true;
                    }
                    if (plain && hi == 0x40) {
                        add(own ? j + 0x1F : j + 0x1E);
                    } else if (plain && hi == 0x80) {
                        if (own) add(j + 0x20);
                    } else if (hi == 0xC0) {
                        const u32 base = rec + 0x108;  // records from j + 0x21
                        const s32 b = j + 0x21;
                        s32 s2, n2;
                        if (c == 0x10) {
                            s32 s1, n1;
                            if (own) {
                                s2 = 0x22;
                                n2 = 0xC;
                                s1 = 0x3D;
                                n1 = 0xA;
                            } else {
                                s2 = 0;
                                n2 = 6;
                                s1 = 0x16;
                                n1 = 0xC;
                            }
                            for (s32 i = 0; i < n1; ++i)
                                if (GRef<u8>(base + static_cast<u32>(s1 + i) * 8) != 0x1C) add(s1 + b + i);
                        } else if (c < 0x10 || !(c < 0x22) || c < 0x15) {
                            s2 = own ? 0x22 : 0;
                            n2 = own ? 0x1B : 0x16;
                        } else {
                            s2 = own ? 0x22 : 0;
                            n2 = own ? 0xC : 6;
                        }
                        for (s32 i = 0; i < n2; ++i)
                            if (GRef<u8>(base + static_cast<u32>(s2 + i) * 8) != 0x1C) add(s2 + b + i);
                    }
                    break;
                }
                case 0x12: {
                    if (nc == 0) {
                        const u32 c = GRef<u8>(q + 8) & 0x3F;
                        s32 id = -1;
                        if (c == 4)
                            id = own ? 0xC0 : 0xBF;
                        else if (c == 0x15)
                            id = own ? 0xC4 : 0xC3;
                        else if (c == 0x16)
                            id = own ? 0xC6 : 0xC5;
                        else if (c == 0x17)
                            id = own ? 0xC8 : 0xC7;
                        if (id >= 0) {
                            add(id);
                            add(id);
                        }
                    }
                    add(j);
                    break;
                }
                case 0x16:
                    if (!(GRef<s32>(flags) < 0)) add(j);
                    break;
                case 0x18: {
                    const u32 v = W(q + 0xC);
                    u32 g = 0;
                    if (v < 0x23) {
                        g = v / 3;
                        if (v % 3 == 2) g = v / 3 + 1;
                    }
                    const s32 a = j + static_cast<s32>(g) * 3;
                    if (own) {
                        if (!skip0x1c(a + 1 - j)) add(a + 1);
                        if (!skip0x1c(a + 2 - j)) add(a + 2);
                    } else if (!skip0x1c(a - j)) {
                        add(a);
                    }
                    break;
                }
                case 0x19:
                case 0x1A: break;
                default:  // 3, 5, 9-0x10, 0x15 and > 0x1C: side match
                    if (side != 0) add(j);
                    break;
            }
        }
        if (nc == 0) continue;
        const s32 r = ee::rand();
        const s16 idx = cand[r % nc];
        const u32 m = func_002507F0(static_cast<u16>(idx));
        if (m == 0xFFFFFFFFu) continue;
        const u32 o = out + static_cast<u32>(n) * 0x10;
        GRef<s16>(o) = idx;
        GRef<u8>(o + 2) = 0;
        GRef<u32>(o + 4) = m;
        GRef<u8>(o + 3) = GRef<u8>(q);
        n = (n + 1) % 0x14;
    }
    return n;
}

// EE: 0x00251308
// Whether the message being shown is a timer call-out (class 1 of 0x00258D08).
s32 func_00251308() {
    s32 r = 0;
    if (!(GRef<s32>(func_00250920()) < 0)) r = func_00258D08(GRef<s32>(func_00250920())) == 1 ? 1 : 0;
    return r;
}

// EE: 0x00253F88
// Waypoint distance call-outs: when nothing of kind 1 is scheduled, no voice is busy and no
// call-out is shown, each request of ring 0x004BD820 builds the subtitle text (kind, clock
// direction, distance) in 0x004BD900 from the call-out records (0x00250850) and schedules three
// parts (records *0x004BD9A8 / *0x004BD9A4 / *0x004BD9A0); the original also prints the ids
// (debug printf). No rand.
s32 func_00253F88(u32 /*hdr*/, u32 out, s32 n) {
    if (func_00255020() != 0) return n;
    if (func_00255A58() != 0) return n;
    if (func_00251308() != 0) return n;
    guest::GuestFrame fr(0x80);
    const u32 sp = fr.Address();
    GRef<u16>(sp + 0x00) = 0xFFFD;
    GRef<u16>(sp + 0x40) = 1;
    GRef<u16>(sp + 0x50) = 7;
    GRef<u16>(sp + 0x60) = 0xD;
    GRef<u16>(sp + 0x10) = 4;
    GRef<u16>(sp + 0x12) = 0xFFFF;
    GRef<u16>(sp + 0x02) = 0xFFFF;
    GRef<u16>(sp + 0x20) = 0;
    GRef<u16>(sp + 0x22) = 0xFFFF;
    GRef<u16>(sp + 0x30) = 1;
    GRef<u16>(sp + 0x32) = 0xFFFF;
    GRef<u16>(sp + 0x42) = 0xFFFF;
    GRef<u16>(sp + 0x52) = 0xFFFF;
    GRef<u16>(sp + 0x62) = 0xFFFF;
    guest::GuestFrame cf(0x10);
    const u32 cntp = cf.Address();
    u32 q = func_00253070(cntp);
    for (s32 k = 0; k < GRef<u8>(cntp); ++k, q += 0xC) {
        u32 dist = 0xAA0;
        u32 clock = 0xA7C;
        switch (W(q + 8)) {
            case 4: dist = 0xAA1; break;
            case 8: dist = 0xAA2; break;
            case 0xC: dist = 0xAA3; break;
            case 0x14: dist = 0xAA4; break;
            case 0x1E: dist = 0xAA5; break;
            default: break;
        }
        if (GRef<s32>(q + 4) < 0x24) clock = static_cast<u16>(GRef<u16>(q + 4) + 0xA7C);
        const u32 id = static_cast<u16>(GRef<u16>(q) + 0xA5F);
        u32 t = func_00251F90(0x004BD900, sp + 0x20);
        t = func_00251F90(t, func_00250850(static_cast<s32>(id)));
        t = func_00251F90(t, sp + 0x60);
        t = func_00251F90(t, func_00250850(static_cast<s32>(clock)));
        t = func_00251F90(t, sp);
        t = func_00251F90(t, func_00250850(static_cast<s32>(dist)));
        func_00251F48(t, sp + 0x40);
        u32 r = W(0x004BD9A0 + 8);
        GRef<u16>(r + 4) = 0xFFFF;
        GRef<u8>(r + 9) = 3;
        GRef<u32>(r + 0xC) = 0x004BD900;
        const u32 r1 = W(0x004BD9A0 + 4);
        const u64 args[3] = {GRef<u16>(0x004BD902), id, dist};
        ee::printf(0x00409F38, args, 3);  // " %d %d %d\n"
        GRef<u8>(r1 + 9) = 3;
        GRef<u16>(r1 + 4) = static_cast<u16>(dist);
        GRef<u32>(r1 + 0xC) = 0x004BD900;
        r = W(0x004BD9A0);
        GRef<u32>(r + 0xC) = 0x004BD900;
        GRef<u16>(r + 4) = static_cast<u16>(clock);
        GRef<u8>(r + 9) = 7;
        if (r == 0xFFFFFFFFu) continue;
        const u32 o = out + static_cast<u32>(n) * 0x10;
        GRef<u32>(o + 4) = r;
        GRef<u16>(o) = static_cast<u16>(id);
        GRef<u8>(o + 2) = 1;
        GRef<u8>(o + 3) = 0;
        n = (n + 1) % 0x14;
    }
    return n;
}

// EE: 0x00254288
// Whether situation message idx fits the battle counts (record +10 class 1: a > 1; 0xD/0x11:
// c != 0); always in bank kind 0x12.
s32 func_00254288(s32 idx, u8 a, u8 c) {
    s32 ok = 0;
    const u32 h = func_00250910();
    const u32 r = func_002507F0(static_cast<u32>(idx));
    if (r != 0xFFFFFFFFu) {
        const u8 cls = GRef<u8>(r + 10);
        if (cls == 1)
            ok = 1 < a ? 1 : 0;
        else if (cls == 0xD || cls == 0x11)
            ok = c != 0 ? 1 : 0;
        else
            ok = 1;
        if (GRef<u16>(h) == 0x12) ok = 1;
    }
    return ok;
}

// EE: 0x00254340
// Situation chatter matcher (requests of ring 0x004BD850, kinds 0xD/0xE skipped): 12-byte records
// at header +0xC (count +0x10, ids from header +8): kind 0 generic lines (only within 1800 frames
// of the last pick, not yet played: mask 0x004BDF00 for ids 0x185-0x194), kind 10 by speaker class
// (0x40: 4 own + 4 generic, 0xC0: none, else the 4 generic ids 0-3), other kinds by side/target
// tests and 0x00254288. Up to 80 candidates, one rand() % count; played ids are marked.
s32 func_00254340(u32 hdr, u32 out, s32 n) {
    guest::GuestFrame cf(0x10);
    const u32 cntp = cf.Address();
    u32 q = func_00253088(cntp);
    const u32 f = func_002508D0();
    const u32 pm = func_00255670();
    u16 cand[0x50];
    for (s32 k = 0; k < GRef<u8>(cntp); ++k, q += 0x14) {
        const u32 kind = W(q);
        if (kind - 0xDu < 2u) continue;
        s32 nc = 0;
        auto add = [&](u32 idx) {
            cand[nc % 0x50] = static_cast<u16>(idx);
            ++nc;
        };
        auto fits = [&](u32 idx) {
            return func_00254288(static_cast<s32>(idx), GRef<u8>(f + 4), GRef<u8>(f + 6)) != 0;
        };
        auto unplayed = [&](u32 idx) { return (((GRef<s32>(pm) >> ((idx - 0x185u) & 0x1F)) ^ 1) & 1) != 0; };
        u32 rec = W(hdr + 0xC);
        for (u32 j = 0; j < GRef<u16>(hdr + 0x10); ++j, rec += 0xC) {
            if (W(q) != W(rec)) continue;
            const u32 id = j + GRef<u16>(hdr + 8);
            if (kind == 0) {
                const u32 last = W(pm + 4);
                if (!(W(CtxAddr() + 0x2C) - last < 0x708u) && last != 0) continue;
                const u8 b4 = GRef<u8>(rec + 4);
                if (b4 == 0) {
                    if (unplayed(id)) add(id);
                } else if (b4 == 1 && GRef<s32>(q + 4) == 1 && W(q + 8) == GRef<u8>(rec + 5) && unplayed(id)) {
                    add(id);
                }
            } else if (kind == 10) {
                const u8 c = GRef<u8>(q + 0x10);
                const u32 hi = c & 0xE0u;
                if (hi == 0xC0) continue;
                if (hi == 0x40) {
                    const u32 v = static_cast<u8>((c & 0x1F) - 0x22) * 4u;
                    for (u32 i = 0; i < 4; ++i)
                        if (fits(id + v + i)) add(id + v + i);
                    for (u32 i = 0; i < 4; ++i)
                        if (fits(v + i)) add(v + i);
                } else {
                    for (u32 i = 0; i < 4; ++i)
                        if (fits(i)) add(i);
                }
            } else {
                u8 b4 = GRef<u8>(rec + 4);
                if (b4 == 0 || GRef<u8>(rec + 6) == 0) {
                    if (fits(id)) add(id);
                    b4 = GRef<u8>(rec + 4);
                }
                if (b4 == 1 && GRef<s32>(q + 4) == 1 && W(q + 8) == GRef<u8>(rec + 5)) {
                    if (fits(id)) add(id);
                }
                if (static_cast<u32>(GRef<s32>(q + 4)) == GRef<u8>(rec + 4) && GRef<s32>(q + 4) < 4) {
                    if (fits(id)) add(id);
                } else if (W(q + 0xC) == GRef<u8>(rec + 6) && GRef<s32>(q + 0xC) < 6) {
                    if (fits(id)) add(id);
                }
            }
        }
        if (nc == 0) continue;
        const s32 r = ee::rand();
        const u16 idx = cand[r % nc];
        const u32 m = func_002507F0(idx);
        if (m == 0xFFFFFFFFu) continue;
        const u32 o = out + static_cast<u32>(n) * 0x10;
        GRef<u16>(o) = idx;
        GRef<u8>(o + 2) = 2;
        GRef<u32>(o + 4) = m;
        GRef<u8>(o + 3) = GRef<u8>(q);
        n = (n + 1) % 0x14;
        if (func_00258CC8(idx) != 0) {
            GRef<u32>(pm) |= 1u << ((idx - 0x185u) & 0x1F);
            GRef<u32>(pm + 4) = W(CtxAddr() + 0x2C);
        }
    }
    return n;
}

// EE: 0x00254930
// Target chatter matcher (requests of ring 0x004BD8A0, modes 4/5 skipped): 8-byte records at
// header +0x14 (count +0x18, ids from header +8 + header +0x10) not yet used (0x004BDB00) with the
// same mode and result, speaker match 0x00253110 and flag masks (bits 0-1 and 2-3); ids above
// 0x452 count three times. Up to 320 candidates, one rand() % count.
s32 func_00254930(u32 hdr, u32 out, s32 n) {
    guest::GuestFrame cf(0x10);
    const u32 cntp = cf.Address();
    u32 q = func_002530A0(cntp);
    const u32 used = func_00255680();
    const u32 base = static_cast<u32>(GRef<u16>(hdr + 8)) + GRef<u16>(hdr + 0x10);
    u16 cand[0x140];
    for (s32 k = 0; k < GRef<u8>(cntp); ++k, q += 0x10) {
        if (W(q + 4) - 4u < 2u) continue;
        s32 nc = 0;
        u32 rec = W(hdr + 0x14);
        for (u32 j = 0; j < GRef<u16>(hdr + 0x18); ++j, rec += 8) {
            if (GRef<u8>(used + j) != 0) continue;
            if (W(q + 4) != GRef<u8>(rec)) continue;
            if (W(q + 8) != GRef<u8>(rec + 1)) continue;
            if (func_00253110(GRef<s32>(q), GRef<u8>(rec + 2)) == 0) continue;
            const u32 fl = GRef<u8>(rec + 3);
            const u32 e = W(q + 0xC);
            if ((e & (fl & 3u)) == 0 || (e & (fl & 0xCu)) == 0) continue;
            const u32 id = j + base;
            cand[nc % 0x140] = static_cast<u16>(id);
            ++nc;
            if (!(static_cast<s32>(id) < 0x453)) {
                cand[nc % 0x140] = static_cast<u16>(id);
                ++nc;
                cand[nc % 0x140] = static_cast<u16>(id);
                ++nc;
            }
        }
        if (nc == 0) continue;
        const s32 r = ee::rand();
        const u16 idx = cand[r % nc];
        const u32 m = func_002507F0(idx);
        if (m == 0xFFFFFFFFu) continue;
        const u32 o = out + static_cast<u32>(n) * 0x10;
        GRef<u16>(o) = idx;
        GRef<u32>(o + 4) = m;
        GRef<u8>(o + 2) = 3;
        GRef<u8>(o + 3) = 0;
        n = (n + 1) % 0x14;
    }
    return n;
}

// EE: 0x00254B98
// Scripted messages (ring 0x004BD8E0, id + header +0x28 + 0x738): play-once records (+6 bit 0)
// are checked against the 48-entry played list 0x004BDF10 (skipped when found) and entered in its
// first free slot (also when found).
s32 func_00254B98(u32 hdr, u32 out, s32 n) {
    guest::GuestFrame cf(0x10);
    const u32 cntp = cf.Address();
    u32 q = func_002530B8(cntp);
    for (s32 k = 0; k < GRef<u8>(cntp); ++k, q += 4) {
        const s32 id = GRef<s32>(q) + GRef<u16>(hdr + 0x28) + 0x738;
        if (id < 0) continue;
        u32 m = func_002507F0(static_cast<u32>(id));
        if ((GRef<u16>(m + 6) & 1) != 0) {
            u32 l = func_00255660();
            for (s32 i = 0; i < 0x30; ++i, l += 4) {
                if (GRef<s32>(l) == id) {
                    m = 0xFFFFFFFFu;
                    break;
                }
            }
            l = func_00255660();
            for (s32 i = 0; i < 0x30; ++i, l += 4) {
                if (GRef<s32>(l) == -1) {
                    GRef<s32>(l) = id;
                    break;
                }
            }
        }
        if (m == 0xFFFFFFFFu) continue;
        const u32 o = out + static_cast<u32>(n) * 0x10;
        GRef<u32>(o + 4) = m;
        GRef<s16>(o) = static_cast<s16>(id);
        GRef<u8>(o + 2) = 4;
        GRef<u8>(o + 3) = 0;
        n = (n + 1) % 0x14;
    }
    return n;
}

// EE: 0x00254D40
// Extra messages (ring 0x004BD8F0, id + header +0x2A).
s32 func_00254D40(u32 hdr, u32 out, s32 n) {
    guest::GuestFrame cf(0x10);
    const u32 cntp = cf.Address();
    u32 q = func_002530D0(cntp);
    for (s32 k = 0; k < GRef<u8>(cntp); ++k, q += 4) {
        const s32 id = GRef<s32>(q) + GRef<u16>(hdr + 0x2A);
        if (id < 0) continue;
        const u32 m = func_002507F0(static_cast<u32>(id));
        if (m == 0xFFFFFFFFu) continue;
        const u32 o = out + static_cast<u32>(n) * 0x10;
        GRef<u32>(o + 4) = m;
        GRef<s16>(o) = static_cast<s16>(id);
        GRef<u8>(o + 2) = 7;
        GRef<u8>(o + 3) = 0;
        n = (n + 1) % 0x14;
    }
    return n;
}

// EE: 0x00254E80
// Call-out part record i (mod 3) of 0x004BD9A0.
u32 func_00254E80(s32 i) { return W(0x004BD9A0 + 4u * static_cast<u32>(i % 3)); }

// EE: 0x00254EB0
// Highest priority (record +9, at least 15) among the scheduled entries.
u8 func_00254EB0(u32 e) {
    u8 m = 0xF;
    if (GRef<u16>(e) == 0xFFFF) return m;
    for (s32 i = 1;; ++i) {
        const u8 p = GRef<u8>(W(e + 4) + 9);
        if (m < p) m = p;
        if (!(i < 0x10)) break;
        e += 0x10;
        if (GRef<u16>(e) == 0xFFFF) break;
    }
    return m;
}

// EE: 0x00254F00
// Sorts n 16-byte candidates by priority (record +9), lowest first (bubble sort; a pass is
// skipped when its top entry is empty).
void func_00254F00(u32 list, s32 n) {
    for (s32 t = n - 1; t >= 0; --t) {
        if (GRef<u16>(list + static_cast<u32>(t) * 0x10) == 0xFFFF) continue;
        for (s32 i = 0; i < t; ++i) {
            const u32 a = list + static_cast<u32>(i) * 0x10, b = a + 0x10;
            if (GRef<u8>(W(b + 4) + 9) < GRef<u8>(W(a + 4) + 9)) {
                const u64 a0 = GRef<u64>(a), a1 = GRef<u64>(a + 8);
                GRef<u64>(a) = GRef<u64>(b);
                GRef<u64>(a + 8) = GRef<u64>(b + 8);
                GRef<u64>(b) = a0;
                GRef<u64>(b + 8) = a1;
            }
        }
    }
}

// EE: 0x00254FD8
// Clears the 16 scheduler entries.
void func_00254FD8() {
    u32 e = func_002508B0();
    for (s32 i = 0xF; i >= 0; --i, e += 0x10) func_00254FB0(e);
}

// EE: 0x00255020
// Whether a timer call-out (kind 1) is scheduled.
s32 func_00255020() {
    u32 e = func_002508B0();
    for (s32 i = 0; i < 0x10; ++i, e += 0x10)
        if (GRef<u8>(e + 2) == 1) return 1;
    return 0;
}

// EE: 0x00255070
// Blocked pair (tables 0x003F5D90 for kind 0, 0x004115F8 for kind 2; entries {kind, a, b, type},
// type 3 ends, 1 inactive): 1 when the scheduler holds the partner (b, or any with 0x1B).
s32 func_00255070(u8 kind, u8 sub) {
    u32 t;
    if (kind == 0)
        t = func_00257330();
    else if (kind == 2)
        t = func_00257340();
    else
        return 0;
    for (; GRef<u8>(t + 3) != 3; t += 4) {
        if (GRef<u8>(t + 3) == 1 || GRef<u8>(t) != sub) continue;
        u32 e = func_002508B0();
        for (s32 i = 0; i < 0x10; ++i, e += 0x10) {
            if (GRef<u8>(t + 1) != GRef<u8>(e + 2)) continue;
            if (GRef<u8>(t + 2) == GRef<u8>(e + 3)) return 1;
            if (GRef<u8>(t + 2) == 0x1B) return 1;
        }
    }
    return 0;
}

// EE: 0x00255170
// Blocked message pairs (table 0x00411600: {u16 id, u16 other, s32 type}, type 3 ends, 1 inactive):
// 1 when `other` is scheduled.
s32 func_00255170(u16 id) {
    u32 t = func_00257350();
    for (; GRef<s32>(t + 4) != 3; t += 8) {
        if (GRef<s32>(t + 4) == 1 || GRef<u16>(t) != id) continue;
        u32 e = func_002508B0();
        for (s32 i = 0; i < 0x10; ++i, e += 0x10)
            if (GRef<u16>(t + 2) == GRef<u16>(e)) return 1;
    }
    return 0;
}

// EE: 0x00255230
// Collects the scheduled entries that may start now (delay +0xC <= 0; the first always, then
// kinds 2, 0 with record +10, 3 with priority >= 12, 4 for ids 0x87C-0x880) into list (cap
// entries, ring); returns the count minus the null slots.
s32 func_00255230(u32 e, u32 list, s32 cap) {
    s32 n = 0;
    if (GRef<u16>(e) != 0xFFFF) {
        for (s32 i = 0;; ++i, e += 0x10) {
            if (!(0 < GRef<s32>(e + 0xC))) {
                bool take = false;
                if (i == 0) {
                    take = true;
                } else {
                    const u8 k = GRef<u8>(e + 2);
                    if (k == 2)
                        take = true;
                    else if (k == 0)
                        take = GRef<u8>(W(e + 4) + 10) != 0;
                    else if (k == 3)
                        take = !(GRef<u8>(W(e + 4) + 9) < 0xC);
                    else if (k == 4)
                        take = func_00258CF8(GRef<u16>(e)) != 0;
                }
                if (take) {
                    GRef<u32>(list + 4u * static_cast<u32>(n % cap)) = e;
                    ++n;
                }
            }
            if (!(n < cap) || !(i + 1 < 0x10) || GRef<u16>(e + 0x10) == 0xFFFF) break;
        }
    }
    s32 zeros = 0;
    for (s32 i = 0; i < n; ++i)
        if (GRef<u32>(list + 4u * static_cast<u32>(i)) == 0) ++zeros;
    return n - zeros;
}

// EE: 0x00255398
// Inserts the sorted candidates whose priority does not exceed the scheduler's highest into the
// scheduler from the last slot down, skipping recently played, blocked or already scheduled ones;
// the entry keeps the frame (+8) and its delay (+0xC = record +0).
void func_00255398(u32 sched, s32 n, u32 cand) {
    u32 dst = sched + 0xF0;
    func_00254F00(cand, n);
    for (s32 i = 0; i < n; ++i, cand += 0x10) {
        if (static_cast<s32>(func_00254EB0(sched)) < GRef<u8>(W(cand + 4) + 9)) return;
        if (func_00255B98(GRef<u16>(cand)) != 0) continue;
        if (func_00255BD0(GRef<u8>(cand + 2), GRef<u8>(cand + 3)) != 0) continue;
        if (func_00255CA8(GRef<u16>(cand)) != 0) continue;
        if (func_00255070(GRef<u8>(cand + 2), GRef<u8>(cand + 3)) != 0) continue;
        if (func_00255170(GRef<u16>(cand)) != 0) continue;
        GRef<u64>(dst) = GRef<u64>(cand);
        GRef<u64>(dst + 8) = GRef<u64>(cand + 8);
        GRef<u32>(dst + 8) = W(CtxAddr() + 0x2C);
        GRef<u32>(dst + 0xC) = GRef<u16>(W(dst + 4));
        dst -= 0x10;
        if (dst < sched) return;
    }
}

// EE: 0x002554B8
// Scheduler upkeep: entries of priority >= 6 are dropped when blocked or after their duration
// (record +2 frames); the list is compacted, delays (+0xC) count down, then it is sorted.
void func_002554B8(u32 sched) {
    u32 e = sched;
    for (s32 i = 0xF; i >= 0; --i, e += 0x10) {
        if (GRef<u16>(e) == 0xFFFF) continue;
        if (GRef<u8>(W(e + 4) + 9) < 6) continue;
        if (func_00255B98(GRef<u16>(e)) != 0 || func_00255BD0(GRef<u8>(e + 2), GRef<u8>(e + 3)) != 0 ||
            func_00255CA8(GRef<u16>(e)) != 0 || func_00255070(GRef<u8>(e + 2), GRef<u8>(e + 3)) != 0 ||
            func_00255170(GRef<u16>(e)) != 0) {
            func_00254FB0(e);
        } else if (!(static_cast<s32>(W(CtxAddr() + 0x2C) - W(e + 8)) < static_cast<s32>(GRef<u16>(W(e + 4) + 2)))) {
            func_00254FB0(e);
        }
    }
    e = sched;
    for (s32 i = 0; i < 0x10;) {
        const bool empty = GRef<u16>(e) == 0xFFFF;
        ++i;
        if (empty) {
            for (s32 j = i; j < 0x10; ++j) {
                const u32 s = sched + static_cast<u32>(j) * 0x10;
                if (GRef<u16>(s) == 0xFFFF) continue;
                GRef<u64>(e) = GRef<u64>(s);
                GRef<u64>(e + 8) = GRef<u64>(s + 8);
                func_00254FB0(s);
                break;
            }
        }
        e += 0x10;
    }
    e = sched;
    for (s32 i = 0xF; i >= 0; --i, e += 0x10) GRef<s32>(e + 0xC) -= 1;
    func_00254F00(sched, 0x10);
}

// EE: 0x00255660
u32 func_00255660() { return 0x004BDF10; }

// EE: 0x00255670
u32 func_00255670() { return 0x004BDF00; }

// EE: 0x00255680
u32 func_00255680() { return 0x004BDB00; }

// EE: 0x00255690
// Dead (no reference): byte 0x004BD9B0 (cleared by func_002556A0).
u8 func_00255690() { return GRef<u8>(0x004BD9B0); }

// EE: 0x002559C8
// Radio cut-off: voices stopped (0x00257C28) and state flag 0x2000.
void func_002559C8() {
    func_00257C28();
    GRef<u16>(0x004BD9B2) = static_cast<u16>(GRef<u16>(0x004BD9B2) | 0x2000);
}

// EE: 0x002559F8
void func_002559F8() { GRef<u16>(0x004BD9B2) = static_cast<u16>(GRef<u16>(0x004BD9B2) | 0x1000); }

// EE: 0x00255A10
// Whether the other voice channel (ch ^ 1) is idle (state bits 1-4 clear).
s32 func_00255A10(u32 ch) {
    const u32 v = W(0x004BD9B4 + 4 * (ch ^ 1));
    if ((v & 2) == 0 && (v & 4) == 0 && (v & 8) == 0) return (v & 0x10) == 0 ? 1 : 0;
    return 0;
}

// EE: 0x00255A58
// Whether a voice channel is playing (state 1).
s32 func_00255A58() { return (GRef<s32>(0x004BD9F0) == 1 || GRef<s32>(0x004BD9EC) == 1) ? 1 : 0; }

// EE: 0x00255A88
// Adds a played message to the history ring 0x004BDA00 (16, cursor 0x004115E8); target chatter
// played normally (kind 3, cut = 0) marks its record used.
void func_00255A88(u32 e, s32 cut) {
    const u32 h = func_002507E0();
    const u32 p = W(0x004115E8);
    const u32 base = static_cast<u32>(GRef<u16>(h + 8)) + GRef<u16>(h + 0x10);
    GRef<u64>(p) = GRef<u64>(e);
    GRef<u64>(p + 8) = GRef<u64>(e + 8);
    GRef<u32>(p + 8) = 0;
    if (GRef<u8>(p + 2) == 3 && cut == 0) GRef<u8>(0x004BDB00 + (GRef<u16>(p) - base)) = 1;
    const u32 np = W(0x004115E8) + 0x10;
    GRef<u32>(0x004115E8) = np;
    if (!(static_cast<u32>(static_cast<s32>(np - 0x004BDA00) >> 4) < 0x10)) GRef<u32>(0x004115E8) = 0x004BDA00;
}

// EE: 0x00255B38
// Ages the history (+8 frames); entries older than 600 frames are cleared.
void func_00255B38() {
    u32 e = 0x004BDA00;
    for (s32 i = 0xF; i >= 0; --i, e += 0x10) {
        const u32 t = W(e + 8) + 1;
        GRef<u32>(e + 8) = t;
        if (!(t < 0x259)) func_00254FB0(e);
    }
}

// EE: 0x00255B98
// Whether message id was played in the last 600 frames.
s32 func_00255B98(u16 id) {
    u32 e = 0x004BDA00;
    for (s32 i = 0; i < 0x10; ++i, e += 0x10)
        if (GRef<u16>(e) == id) return 1;
    return 0;
}

// EE: 0x00255BD0
// As func_00255070 against the history (table entries with type 0 inactive).
s32 func_00255BD0(u8 kind, u8 sub) {
    u32 t;
    if (kind == 0)
        t = func_00257330();
    else if (kind == 2)
        t = func_00257340();
    else
        return 0;
    for (; GRef<u8>(t + 3) != 3; t += 4) {
        if (GRef<u8>(t + 3) == 0 || GRef<u8>(t) != sub) continue;
        u32 e = 0x004BDA00;
        for (s32 i = 0; i < 0x10; ++i, e += 0x10) {
            if (GRef<u8>(t + 1) != GRef<u8>(e + 2)) continue;
            if (GRef<u8>(t + 2) == GRef<u8>(e + 3)) return 1;
            if (GRef<u8>(t + 2) == 0x1B) return 1;
        }
    }
    return 0;
}

// EE: 0x00255CA8
// Blocked message pairs (table 0x00411600, type > 0): 1 when `other` was played recently.
s32 func_00255CA8(u16 id) {
    u32 t = func_00257350();
    for (; GRef<s32>(t + 4) != 3; t += 8) {
        if (!(0 < GRef<s32>(t + 4)) || GRef<u16>(t) != id) continue;
        if (func_00255B98(GRef<u16>(t + 2)) != 0) return 1;
    }
    return 0;
}

// EE: 0x00257330
u32 func_00257330() { return 0x003F5D90; }

// EE: 0x00257340
u32 func_00257340() { return 0x004115F8; }

// EE: 0x00257350
u32 func_00257350() { return 0x00411600; }

// EE: 0x00258400
// Requests tutorial hint icons (mask).
void func_00258400(u32 mask) { GRef<u32>(0x004BE040) |= mask; }

// EE: 0x00258C58
// Portrait window of the shown message (speaker +0x10 < 0x17).
u32 func_00258C58(u32 pkt, u32 /*col*/) {
    const u32 s = func_00250920();
    if (GRef<s32>(s + 4) != -1 && GRef<s16>(s + 0x10) < 0x17)
        pkt = func_00257D08(GRef<s16>(s + 0x10), pkt);  // the original also passes s +0x12 and col; 0x00257D08 reads neither
    return pkt;
}

// EE: 0x00258CC8
// Whether message idx is play-once (0x301-0x304, 0x185-0x194).
s32 func_00258CC8(u32 idx) { return (idx - 0x301u < 4u || idx - 0x185u < 0x10u) ? 1 : 0; }

// EE: 0x00258CF8
s32 func_00258CF8(u32 idx) { return idx - 0x87Cu < 5u ? 1 : 0; }

// EE: 0x00258D08
// Class of message id: 8 invalid, 0 event (< header +8), 2 situation, 3 target, 4 other,
// 7 extra (header +0x2A .. +0x28), 1 after.
s32 func_00258D08(s32 id) {
    const u32 h = func_002507E0();
    const s16 e = GRef<s16>(h + 8);
    const s16 sit = static_cast<s16>(static_cast<u16>(e) + GRef<u16>(h + 0x10));
    const s16 tgt = static_cast<s16>(static_cast<u16>(sit) + GRef<u16>(h + 0x18));
    const s16 x = GRef<s16>(h + 0x2A);
    const s16 xe = static_cast<s16>(x + 0x28);
    if (id < 0) return 8;
    if (id < e) return 0;
    if (id < sit) return 2;
    if (id < tgt) return 3;
    if (id < x) return 4;
    return id < xe ? 7 : 1;
}

// EE: 0x00258DB8
// Event kind of message id (event records byte 0, situation records word 0), else 0.
u32 func_00258DB8(s32 id) {
    const u32 h = func_002507E0();
    const s32 c = func_00258D08(id);
    if (c == 0) return GRef<u8>(static_cast<u32>(id) * 8 + W(h + 4));
    if (c == 2) return W((static_cast<u32>(id) - GRef<u16>(h + 8)) * 0xC + W(h + 0xC));
    return 0;
}

// EE: 0x00251F48
// Copies a u16 string up to and including its 0xFFFE/0xFFFF terminator; returns the end.
u32 func_00251F48(u32 dst, u32 src) {
    u16 c = GRef<u16>(src);
    while (c < 0xFFFE) {
        GRef<u16>(dst) = c;
        src += 2;
        dst += 2;
        c = GRef<u16>(src);
    }
    GRef<u16>(dst) = c;
    return dst;
}

// EE: 0x00251F90
// Appends a u16 string without its terminator; returns the end.
u32 func_00251F90(u32 dst, u32 src) {
    u16 c = GRef<u16>(src);
    while (c < 0xFFFE) {
        GRef<u16>(dst) = c;
        src += 2;
        dst += 2;
        c = GRef<u16>(src);
    }
    return dst;
}

// ---- Voice playback. Channel state S = 0x004BD9B0: +0 free mask (0 both free, 1/2 one busy, 4 both
// busy), +1 multi-part bits, +2 flags (bits 0-1 started, 2-3/4-5/8-9 click requests, 6-7 stop,
// 0x1000 hold, 0x2000 cut, 0x3000 forced stop), per channel ch (+4ch): +4 stream state, +0xC
// priority, +0x14 speaker class, +0x1C next part, +0x24 frames since start, +0x2C gap, +0x34 message,
// +0x3C kind. Shown message (subtitle) record `show` = 0x004BD770.

// EE: 0x002557D0
// Dead (no reference): byte 0x004BD9B0, as func_00255690.
u8 func_002557D0() { return GRef<u8>(0x004BD9B0); }

// EE: 0x002557E0
// Stops channel ch (stream stop when not yet stopped) and clears it; when cut (flag 0x2000) or not
// on hold (0x1000) the subtitle is cleared and the scheduler is emptied (keeping entry 0 when its
// priority is >= 2). Returns 1 when the stream was stopped here.
s32 func_002557E0(s32 ch, s32 stopped, u32 st, u32 sched, u32 show) {
    s32 r = 0;
    if (stopped == 0) {
        func_0021FDF8(static_cast<u8>(1u << ((static_cast<u32>(ch) + 4) & 0x1F)));
        GRef<u16>(st + 2) = static_cast<u16>(GRef<u16>(st + 2) | (ch == 0 ? 0x40 : 0x80));
        r = 1;
    }
    const u32 c = st + 4u * static_cast<u32>(ch);
    const u16 fl = GRef<u16>(st + 2);
    GRef<u32>(c + 0x24) = 0;
    GRef<u32>(c + 0xC) = 0xF;
    GRef<u32>(c + 0x3C) = 8;
    GRef<u32>(c + 0x14) = 0x17;
    GRef<s32>(c + 0x34) = -1;
    s32 from;
    if ((fl & 0x2000) == 0 && (fl & 0x1000) != 0) {
        from = 0x10;
    } else {
        GRef<u32>(show + 0x14) = 3;
        GRef<s32>(show) = -1;
        GRef<s32>(show + 4) = -1;
        GRef<u16>(show + 0x10) = 0x17;
        GRef<u16>(show + 0x12) = 7;
        GRef<u32>(show + 8) = 0;
        GRef<u32>(show + 0xC) = 0;
        GRef<u8>(st + 1) = static_cast<u8>(GRef<u8>(st + 1) & (ch == 0 ? 0xFE : 0xFD));
        GRef<s32>(c + 0x1C) = -1;
        if (GRef<u16>(sched) == 0xFFFF)
            from = 0;
        else
            from = GRef<u8>(W(sched + 4) + 9) < 2 ? 1 : 0;
    }
    for (s32 i = from; i < 0x10; ++i) func_00254FB0(sched + static_cast<u32>(i) * 0x10);
    return r;
}

// EE: 0x00255D28
// BGM/SE ducking while voices play: per active channel the volumes of the speaker class from table
// 0x00409FC0 (channel 1 scaled by C 0x00410458 for classes < 0x15, silent for >= 0x16) on stream
// slots 4/5 (0x0021F190, mode 1); the free mask is remembered in 0x004115EC.
void func_00255D28(u32 st) {
    const u8 b = GRef<u8>(st);
    if (b == 0) return;
    s32 ch, n;
    if ((b & 1) != 0) {
        ch = 0;
        n = 1;
    } else if ((b & 2) != 0) {
        ch = 1;
        n = 1;
    } else {
        ch = 0;
        n = 2;
    }
    const f32 k = F(0x00410458);
    auto vol = [](s32 cls, u32 i) { return GRef<s32>(0x00409FC0 + static_cast<u32>(cls) * 8 + 4 * i); };
    for (; n != 0; --n, ++ch) {
        if (ch == 0) {
            const s32 cls = GRef<s32>(st + 0x14);
            func_0021F190(0.0f, 4, vol(cls, 0), vol(cls, 1), 1);
            continue;
        }
        const s32 cls = GRef<s32>(st + 0x14 + 4u * static_cast<u32>(ch));
        if (cls < 0xD || cls < 0x15) {
            const s32 l = fpu::FloatToInt(fpu::IntToFloat(vol(cls, 0)) * k);
            const s32 r = fpu::FloatToInt(fpu::IntToFloat(vol(cls, 1)) * k);
            func_0021F190(0.0f, ch + 4, l, r, 1);
        } else if (cls < 0x16) {
            func_0021F190(0.0f, ch + 4, vol(cls, 0), vol(cls, 1), 1);
        } else {
            func_0021F190(0.0f, ch + 4, 0, 0, 1);
        }
    }
    GRef<u32>(0x004115EC) = GRef<u8>(st);
}

// EE: 0x00255F30
// Radio click SE of channel ch at message start (cut = 0) or end (cut = 1): start requests
// (flag 0x10 << ch: SE 0xD/0xE by class; 0x100 << ch: SE 0xF for the call-out ids 0x88B-0x88D,
// 0x897) or end requests (4 << ch: a random variant, rand() % 6, from 0 or 6 by class) from the
// record classes (+0xA, +0xB) and the follow-up message (+4); played (channel 0 only) with the
// class volumes of table 0x00409FC0 and the SE ids of table 0x0040A080. Returns 1 when played.
s32 func_00255F30(u32 st, s32 ch, s32 cut) {
    const u32 c = st + 4u * static_cast<u32>(ch);
    const u32 sh = static_cast<u32>(ch) & 0x1F;
    u32 cls;
    auto set = [&](u32 bits) { GRef<u16>(st + 2) = static_cast<u16>(GRef<u16>(st + 2) | bits); };
    if (cut == 0) {
        const u32 m = func_002507F0(W(c + 0x34));
        if (m == 0xFFFFFFFFu) return 0;
        const u32 id = W(c + 0x34);
        if (id - 0x88Bu < 3u || id == 0x897) {
            set(0x100u << sh);
            cls = W(c + 0x14);
        } else {
            cls = W(c + 0x14);
            if (!(static_cast<s32>(cls) < 0xD)) return 0;
            if (GRef<u8>(m + 0xB) != 1) return 0;
            if (ch == 0) {
                if ((GRef<u8>(st + 1) & 1) != 0 && GRef<u16>(m) < 0x78) return 0;
                set(0x10);
            } else {
                if ((GRef<u8>(st + 1) & 2) != 0 && GRef<u16>(m) < 0x78) return 0;
                set(0x10u << sh);
            }
        }
    } else {
        const u32 m = func_002507F0(W(c + 0x34));
        if (m == 0xFFFFFFFFu) return 0;
        const s16 next = GRef<s16>(m + 4);
        if (next == -1) {
            cls = W(c + 0x14);
            if (!(static_cast<s32>(cls) < 0xD)) return 0;
            set(4u << sh);
        } else {
            const u32 m2 = func_002507F0(static_cast<u32>(static_cast<s32>(next)));
            if (m2 == 0xFFFFFFFFu) return 0;
            const u8 b1 = GRef<u8>(m + 0xB);
            if (!(b1 < 3) && GRef<u8>(m2 + 0xB) == 1 && GRef<u8>(m2 + 0xA) < 0xD) {
                set(0x10u << sh);
                cls = GRef<u8>(m2 + 0xA);
            } else {
                const u8 a = GRef<u8>(m + 0xA), a2 = GRef<u8>(m2 + 0xA);
                if (a != a2 && GRef<u8>(m2 + 0xB) == 1 && a2 < 0xD) {
                    set(4u << sh);
                    cls = a;
                } else {
                    if (b1 != 1) return 0;
                    if (GRef<u8>(m2 + 0xB) < 3) return 0;
                    if (!(a < 0xD)) return 0;
                    set(4u << sh);
                    cls = GRef<u8>(m + 0xA);
                }
            }
        }
    }
    const u16 fl = GRef<u16>(st + 2);
    s32 se;
    if ((fl & (0x10u << sh)) != 0) {
        GRef<u16>(st + 2) = static_cast<u16>(fl & ~(0x10u << sh));
        se = cls == 0 ? 0xD : 0xE;
    } else if ((fl & (4u << sh)) != 0) {
        s32 base = 6;
        if (cls < 0x15) {
            switch (cls) {
                case 1:
                case 2:
                case 0xC:
                case 0xD:
                case 0xE:
                case 0x11: base = 6; break;
                default: base = 0; break;
            }
        }
        const s32 r = ee::rand();
        GRef<u16>(st + 2) = static_cast<u16>(GRef<u16>(st + 2) & ~(4u << sh));
        se = base + r % 6;
    } else if ((fl & (0x100u << sh)) != 0) {
        se = 0xF;
        GRef<u16>(st + 2) = static_cast<u16>(fl & ~(0x100u << sh));
    } else {
        return 0;
    }
    if (ch != 0) return 0;
    func_002196E0(10, GRef<s16>(0x0040A080 + 4u * static_cast<u32>(se)), GRef<s16>(0x00409FC0 + cls * 8),
                  GRef<s16>(0x00409FC0 + cls * 8 + 4));
    return 1;
}

// EE: 0x00256428
// Repeating beep (SE 0x40 of table 0x0040A128) every 60 frames while message 0x893/0x894/0x89A plays
// on channel ch (played for channel 0 with the volumes of table 0x0040A148). The original indexes a
// 20-pair stack copy of that table.
s32 func_00256428(u32 st, s32 ch) {
    const u32 c = st + 4u * static_cast<u32>(ch);
    const u32 id = W(c + 0x34);
    bool tick = false;
    if (id - 0x893u < 2u || id == 0x89A) tick = W(CtxAddr() + 0x2C) % 0x3Cu == 0;  // divu
    if (!tick) return 0;
    const u32 cls = W(c + 0x14);
    if (ch != 0) return 0;
    func_002196E0(10, GRef<s16>(0x0040A138), GRef<s16>(0x0040A148 + cls * 8), GRef<s16>(0x0040A148 + cls * 8 + 4));
    return 1;
}

// EE: 0x002565F0
// Starts message e on channel ch: -1 other channel busy, -2 channel flag set, -3 paused; in banks
// < 0x13 the voice stream (slot ch + 4) starts, the channel takes the message (class, priority,
// next part, gap after the message: 4 for multi-part, else base + rand() % range from the bank
// header by battle intensity), the start click, the subtitle record (channel 0: duration
// max(120, voice length / 210 + 180)), history; banks >= 0x13 only play SE 0x3D and show the text
// for 0x438 frames (returns 0x10).
s32 func_002565F0(u32 e, s32 ch, u32 st, u32 show) {
    const u32 m = W(e + 4);
    const u32 f = func_002508D0();
    if (func_00255A10(static_cast<u32>(ch)) == 0) return -1;
    if (((GRef<u16>(st + 2) >> (ch & 0x1F)) & 1) != 0) return -2;
    if ((Ctx()->flags & 0x100) != 0) return -3;
    const u32 h = func_00250910();
    const u32 c = st + 4u * static_cast<u32>(ch);
    if (!(GRef<u16>(h) < 0x13)) {
        func_002196E0(10, 0x3D, 0xFF, 0xFF);
        GRef<u32>(c + 0xC) = GRef<u8>(m + 9);
        GRef<s32>(c + 0x1C) = GRef<s16>(m + 4);
        GRef<u32>(c + 0x24) = 0;
        if (ch == 0) {
            GRef<u32>(show) = GRef<u16>(e);
            GRef<u32>(show + 4) = W(m + 0xC);
            GRef<u32>(show + 8) = W(CtxAddr() + 0x2C);
            GRef<u32>(show + 0xC) = 0x438;
            GRef<u16>(show + 0x10) = GRef<u8>(m + 0xA);
            GRef<u16>(show + 0x12) = GRef<u8>(m + 8);
            GRef<u32>(show + 0x14) = GRef<u8>(m + 0xB);
        }
        func_00254FB0(e);
        return 0x10;
    }
    const s32 ok = RequestStreamPlayArg(static_cast<u32>(ch) + 4, 1, GRef<u16>(e));
    if (ok != 1) return 0;
    GRef<u32>(c + 0x34) = GRef<u16>(e);
    GRef<u32>(c + 0x14) = GRef<u8>(m + 0xA);
    func_00255F30(st, ch, 0);
    GRef<u16>(st + 2) = static_cast<u16>(GRef<u16>(st + 2) | (ok << (ch & 0x1F)));
    GRef<u32>(c + 0xC) = GRef<u8>(m + 9);
    GRef<s32>(c + 0x1C) = GRef<s16>(m + 4);
    GRef<u32>(c + 0x24) = 0;
    const u32 ac = static_cast<u32>(GRef<u8>(f + 4)) + GRef<u8>(f + 6);
    const u32 bd = static_cast<u32>(GRef<u8>(f + 5)) + GRef<u8>(f + 7);
    if (GRef<s32>(c + 0x1C) != -1) {
        GRef<u32>(c + 0x2C) = 4;
        GRef<u8>(st + 1) = static_cast<u8>(GRef<u8>(st + 1) | (ch == 0 ? 1 : 2));
    } else {
        s32 r;
        u32 base, range;
        if (static_cast<s32>(ac) < 10 && static_cast<s32>(bd) < 0xE) {
            r = ee::rand();
            range = GRef<u16>(h + 0x3A);
            base = GRef<u16>(h + 0x38);
        } else {
            r = ee::rand();
            range = GRef<u16>(h + 0x36);
            base = GRef<u16>(h + 0x34);
        }
        GRef<u32>(c + 0x2C) = base + static_cast<u32>(r % static_cast<s32>(range));
    }
    GRef<u32>(c + 0x3C) = GRef<u8>(e + 2);
    if (ch == 0) {
        GRef<u32>(show) = GRef<u16>(e);
        GRef<u32>(show + 4) = W(m + 0xC);
        GRef<u32>(show + 8) = W(CtxAddr() + 0x2C);
        const s32 len = static_cast<s32>(func_0021FA08(1, GRef<u16>(e)));
        const s32 d = len / 0xD2 + 0xB4;
        GRef<s32>(show + 0xC) = d < 0x78 ? 0x78 : d;
        GRef<u16>(show + 0x10) = GRef<u8>(m + 0xA);
        GRef<u16>(show + 0x12) = GRef<u8>(m + 8);
        GRef<u32>(show + 0x14) = GRef<u8>(m + 0xB);
    }
    func_00255A88(e, ch);
    func_00254FB0(e);
    return 1;
}

// EE: 0x00256910
// End of a voice on channel ch (stream slot state 0x00353BF8 + (ch + 4) * 0x94 + 0x34 going idle):
// end click, then either the channel is freed or the next part (record +4: a call-out part for
// class 1 ids, else the follow-up message) is put in scheduler entry 4 to play immediately.
s32 func_00256910(u32 st, s32 ch, u32 /*show*/) {
    const u32 c = st + 4u * static_cast<u32>(ch);
    const u32 cur = W(0x00353BF8 + (static_cast<u32>(ch) + 4) * 0x94 + 0x34);
    if ((W(c + 4) & 1) != 0 || (cur & 1) == 0) {
        GRef<u32>(c + 4) = cur;
        return 0;
    }
    const u32 b = 0x40u << (ch & 0x1F);
    if ((GRef<u16>(st + 2) & b) != 0) GRef<u16>(st + 2) = static_cast<u16>(GRef<u16>(st + 2) & ~b);
    func_00255F30(st, ch, 1);
    if (GRef<s32>(c + 0x1C) == -1) {
        GRef<u32>(c + 0x24) = 0;
        GRef<u8>(st + 1) = static_cast<u8>(GRef<u8>(st + 1) & (ch == 0 ? 0xFE : 0xFD));
        GRef<u32>(c + 4) = 1;
        GRef<s32>(c + 0x1C) = -1;
        GRef<u32>(c + 0xC) = 0xF;
        GRef<u32>(c + 0x14) = 0x17;
        GRef<s32>(c + 0x34) = -1;
        GRef<u32>(c + 0x3C) = 8;
        return 1;
    }
    u32 m = func_002507F0(W(c + 0x1C));
    if (func_00258D08(GRef<s32>(c + 0x1C)) == 1) {
        const s32 v = GRef<s32>(c + 0x1C);
        if (v < 0xAA0)
            m = func_00254E80(1);
        else if (v < 0xAAC)
            m = func_00254E80(2);
        else
            m = 0xFFFFFFFFu;
    }
    if (m == 0xFFFFFFFFu) {
        GRef<u8>(st + 1) = static_cast<u8>(GRef<u8>(st + 1) & (ch == 0 ? 0xFE : 0xFD));
        GRef<s32>(c + 0x1C) = -1;
    } else {
        const u32 s = func_002508B0() + 0x40;
        GRef<u16>(s) = GRef<u16>(c + 0x1C);
        GRef<u8>(s + 2) = static_cast<u8>(func_00258D08(GRef<u16>(s)));
        GRef<u8>(s + 3) = static_cast<u8>(func_00258DB8(GRef<u16>(s)));
        GRef<u32>(s + 4) = m;
        GRef<u32>(s + 0xC) = GRef<u16>(m);
        GRef<u32>(s + 8) = W(CtxAddr() + 0x2C);
    }
    GRef<u32>(c + 0x24) = 0;
    GRef<u32>(c + 4) = 1;
    GRef<u32>(c + 0xC) = 0xF;
    GRef<s32>(c + 0x34) = -1;
    GRef<u32>(c + 0x3C) = 8;
    return 1;
}

// EE: 0x00256B78
// Channel for a new message: both free -> 0 when gap0 < gap1, else 1; channel 1 busy -> 0;
// channel 0 busy with priority bit 0 -> 1; else -1 (none).
s32 func_00256B78(u32 st, s32 a, s32 b) {
    const u8 f = GRef<u8>(st);
    if (f == 0) return (a < b) ? 0 : 1;
    if ((f & 2) != 0) return 0;
    if ((f & 1) != 0 && (W(st + 0xC) & 1) != 0) return 1;
    return -1;
}

// EE: 0x00256BC8
// Radio scheduler per frame (from the radio draw, 0x002512B8): beeps, channel ends, free mask,
// forced stops, then up to two startable entries (0x00255230) get a channel by kind and priority
// (0x00256B78) and start when their channel's gap has passed; ducking last.
void func_00256BC8(u32 sched, u32 show) {
    const u32 S = 0x004BD9B0;
    const u32 f = func_002508D0();
    const u32 h = func_00250910();
    func_00256428(S, 0);
    func_00256428(S, 1);
    bool force = false;
    if ((W(h + 0x3C) & 1) != 0) {
        if (!(static_cast<s32>(GRef<u8>(f + 4) + GRef<u8>(f + 6)) < 10) ||
            !(static_cast<s32>(GRef<u8>(f + 5) + GRef<u8>(f + 7)) < 0xE))
            force = true;
    }
    GRef<s32>(S + 0x24) += 1;
    GRef<s32>(S + 0x28) += 1;
    func_00256910(S, 0, show);
    func_00256910(S, 1, show);
    const u32 s0 = W(S + 4), s1 = W(S + 8);
    if ((s0 & 1) != 0)
        GRef<u8>(S) = (s1 & 1) != 0 ? 0 : 2;
    else
        GRef<u8>(S) = (s1 & 1) != 0 ? 1 : 4;
    if ((s0 & 1) != 0)
        GRef<u32>(0x004115F0) = 0;
    else if ((GRef<u16>(S + 2) & 1) != 0)
        GRef<u16>(S + 2) = static_cast<u16>(GRef<u16>(S + 2) & 0xFFFE);
    if ((s1 & 1) != 0)
        GRef<u32>(0x004115F4) = 0;
    else if ((GRef<u16>(S + 2) & 2) != 0)
        GRef<u16>(S + 2) = static_cast<u16>(GRef<u16>(S + 2) & 0xFFFD);
    func_00255B38();
    auto stop_both = [&]() {
        GRef<s32>(0x004115F0) = func_002557E0(0, GRef<s32>(0x004115F0), S, sched, show);
        GRef<s32>(0x004115F4) = func_002557E0(1, GRef<s32>(0x004115F4), S, sched, show);
    };
    if ((W(S) & 0x30000000u) != 0) {
        stop_both();
        GRef<u16>(S + 2) = static_cast<u16>(GRef<u16>(S + 2) & 0xCFFF);
        if (GRef<u8>(S) != 0) return;
    }
    guest::GuestFrame fr(0x10);
    const u32 list = fr.Address();
    const s32 n = func_00255230(sched, list, 2);
    if (n == 0) {
        if (GRef<u8>(S) != 0) func_00255D28(S);
        return;
    }
    const u32 e0 = W(list);
    s32 mode[2] = {0, 3};
    const u8 k0 = GRef<u8>(e0 + 2);
    if (static_cast<u32>(k0 - 1) < 2u || (GRef<u8>(S + 1) & 1) != 0)
        mode[0] = 1;
    else if (k0 == 3)
        mode[0] = (GRef<s32>(f) < 0 || GRef<u8>(f + 6) != 0) ? 1 : 2;
    s32 gap[2];
    for (u32 i = 0; i < 2; ++i) {
        if (mode[i] == 0) {
            gap[i] = 0;
            continue;
        }
        const s32 g = GRef<s32>(S + 0x2C + 4 * i);
        gap[i] = g;
        if (mode[i] == 2) {
            gap[i] = (g * 3) >> 2;
            if (gap[i] < 0x96) gap[i] = 0x96;
        } else if (mode[i] == 3) {
            gap[i] = g >> 1;
            if (gap[i] < 0x5A) gap[i] = 0x5A;
        }
    }
    if (GRef<u8>(W(e0 + 4) + 9) < 2) {
        stop_both();
        if (GRef<u8>(S) != 0) return;
    }
    s32 cnt = 0;
    if (func_00258CC8(GRef<u16>(W(list))) != 0) force = true;
    if (func_00258CF8(GRef<u16>(W(list))) != 0) force = true;
    s32 sel[2] = {-1, -1};
    if (GRef<u16>(W(list)) == 0x7FD) force = true;
    auto free0 = [&]() { return GRef<u8>(S) == 0 || (GRef<u8>(S) & 2) != 0; };
    auto free1 = [&]() { return GRef<u8>(S) == 0 || (GRef<u8>(S) & 1) != 0; };
    if (!force) {
        if (free0()) {
            sel[0] = 0;
            cnt = 1;
        }
    } else {
        for (s32 i = 0; i < n; ++i) {
            const u32 e = W(list + 4u * static_cast<u32>(i));
            const u8 k = GRef<u8>(e + 2);
            bool pick = false;
            if (k == 8) continue;
            if ((k < 2 && GRef<u8>(W(e + 4) + 10) != 0) || k == 2 || (k == 3 && !(GRef<u8>(W(e + 4) + 9) < 0xC))) {
                pick = true;
            } else if (k == 4 && (func_00258CF8(GRef<u16>(e)) != 0 || GRef<u16>(e) == 0x7FD)) {
                if (free1()) {
                    sel[i] = 1;
                    ++cnt;
                }
                continue;
            }
            if (pick) {
                ++cnt;
                sel[i] = func_00256B78(S, gap[0], gap[1]);
            } else if (free0()) {
                ++cnt;
                sel[i] = 0;
            }
        }
    }
    if (sel[0] == sel[1] && !(sel[0] < 0)) {
        --cnt;
        sel[1] = -1;
    }
    if ((W(f) & 0x20000000u) != 0) {
        cnt = 1;
        sel[1] = -1;
        if (!(sel[0] < 0)) {
            if (GRef<u16>(W(list)) - 0x842u < 3u)
                sel[0] = free0() ? 0 : -1;
            else
                sel[0] = free1() ? 1 : -1;
        }
    }
    for (s32 i = 0; i < cnt; ++i) {
        const u32 e = W(list + 4u * static_cast<u32>(i));
        if (sel[i] == 0) {
            if ((GRef<u8>(S + 1) & 1) != 0 && W(S + 0x1C) != GRef<u16>(e)) break;
            if (GRef<s32>(S + 0x24) < gap[0]) {
                if (GRef<u8>(S) != 0) func_00255D28(S);
                return;
            }
            func_002565F0(e, 0, S, show);
            break;
        }
        if (sel[i] == 1) {
            if (GRef<s32>(S + 0x28) < gap[1]) {
                if (GRef<u8>(S) != 0) func_00255D28(S);
                return;
            }
            func_002565F0(e, 1, S, show);
            break;
        }
    }
    func_00255D28(S);
}

// EE: 0x002512B8
// Radio window (HUD bucket 7): runs the radio scheduler (0x004BA470 -> show 0x004BD770), then draws
// the subtitle. Returns the packet end.
u32 func_002512B8(u32 pkt) {
    func_00256BC8(0x004BA470, 0x004BD770);
    return func_00251C40(0x004BD770, pkt);
}

// EE: 0x00251C40
// Subtitle of the shown message: portrait and text (hidden by the CAPTIONS option 0x002A15CC bit 1
// for priority >= 2), cleared when its duration has passed.
u32 func_00251C40(u32 show, u32 pkt) {
    guest::GuestFrame fr(0x10);
    const u32 col = fr.Address();
    ZeroQ(col);
    if ((GRef<u8>(0x002A15CC) & 2) != 0 && !(GRef<s32>(show + 0x14) < 2)) return pkt;
    pkt = func_00258C58(pkt, col);
    if (GRef<s32>(show + 4) == -1) return pkt;
    if (GRef<s32>(show + 0x14) < 3) pkt = func_002515B8(W(show + 4), pkt, GRef<s16>(show + 0x12));
    if (!(static_cast<s32>(W(CtxAddr() + 0x2C) - W(show + 8)) < GRef<s32>(show + 0xC))) {
        GRef<u32>(show + 0x14) = 3;
        GRef<s32>(show) = -1;
        GRef<s32>(show + 4) = -1;
        GRef<u16>(show + 0x10) = 0x17;
        GRef<u16>(show + 0x12) = 7;
        GRef<u32>(show + 8) = 0;
        GRef<u32>(show + 0xC) = 0;
    }
    return pkt;
}

// EE: 0x002593A0
// In-engine sequence subtitles (sub 0xE): the timeline 0x004BE080 (+2 count; per sequence entry
// lists of {start, duration, message}) at frame counter 0x004BE0A0 shows each message from its
// start for its duration (centred style 0x00258E48), then steps to the next entry.
u32 func_002593A0(u32 pkt) {
    const u32 T = 0x004BE080, C = 0x004BE0A0;
    const u32 e = W(C + 0xC);
    const u32 h = func_002507E0();
    if (GRef<u16>(T + 2) == 0) return pkt;
    const s16 msg = GRef<s16>(e + 8);
    if (!(msg < 0) && (W(C + 8) & 1) == 0) {
        const u32 now = W(C);
        u32 start = W(e);
        if (!(now < start)) {
            const s32 id = msg + GRef<u16>(h + 0x28) + 0x738;
            if (!(id < 0)) {
                const u32 m = func_002507F0(static_cast<u32>(id));
                if (m != 0xFFFFFFFFu) pkt = func_00258E48(W(m + 0xC), pkt);
            }
            start = W(e);
        }
        if (!(W(C) < start + W(e + 4))) {
            const u16 step = static_cast<u16>(GRef<u16>(C + 6) + 1);
            GRef<u16>(C + 6) = step;
            if (static_cast<s16>(step) < GRef<u16>(T + 4 + 2u * static_cast<u32>(static_cast<s32>(GRef<s16>(C + 4)))))
                GRef<u32>(C + 0xC) = W(C + 0xC) + 0x10;
            else
                GRef<u32>(C + 8) = W(C + 8) | 1;
        }
    }
    GRef<u32>(C) = W(C) + 1;
    return pkt;
}

// EE: 0x00259588
// Periodic ambient chatter (0x004BE0B0: next time, end of the effect, next message time, flags)
// against the mission timer (scene +0x1660): sky flag 1 while active (unpaused), and at the message
// time one of the five messages 0x22 + rand() % 5; the next times are re-drawn: start in
// 900 + rand() % 180 frames, effect 16 + rand() % 16, message 180 + rand() % 120.
void UpdateChatterTimers() {
    const u32 T = 0x004BE0B0;
    s32 t = GRef<s32>(Scene() + 0x1660);
    if (t == GRef<s32>(T)) {
        GRef<u32>(T + 0xC) = W(T + 0xC) | 1;
        GRef<s32>(T + 4) += t;
        GRef<s32>(T + 8) += t;
    }
    t = GRef<s32>(Scene() + 0x1660);
    if (t == GRef<s32>(T + 4)) {
        GRef<u32>(T + 0xC) = W(T + 0xC) & 0xFFFFFFFEu;
        t = GRef<s32>(Scene() + 0x1660);
    }
    if (t == GRef<s32>(T + 8)) GRef<u32>(T + 0xC) = W(T + 0xC) | 2;
    if ((W(T + 0xC) & 1) != 0 && (Ctx()->flags & 0x100) == 0) GRef<u32>(W(0x004104D0)) |= 1;
    if ((W(T + 0xC) & 2) == 0) return;
    func_00252FB8(0x22, 5, 0);
    GRef<u32>(T + 0xC) = W(T + 0xC) & 0xFFFFFFFDu;
    const s32 r1 = ee::rand();
    GRef<s32>(T) = GRef<s32>(Scene() + 0x1660) + 900 + r1 % 0xB4;
    const s32 r2 = ee::rand();
    GRef<s32>(T + 4) = r2 % 0x10 + 0x10;
    const s32 r3 = ee::rand();
    GRef<s32>(T + 8) = r3 % 0x78 + 0xB4;
}


// EE: 0x002515B8
// Radio subtitle text: lays out the glyph string s into the text texture (0x00252160, TBP 0x3A80,
// CBP 0x3C80) and draws up to 3 centred lines at y 0x7500 + line x cell height (glyph widths
// scaled by 4/5). Colours by style from the radio header 0x00250910 (normal / opening and closing
// mark / shadow pairs at +0x40.. +0xB0); the first glyph of the text and the last of the last
// line are the marks; inline icons (codes 8..11) use their own CLUT (0x3C80 + code - 7) in mode 8;
// code 0xC is drawn red in mode 4. Text in the normal colour (+0x40) gets a second shadow 0x10
// further out. Returns p (the input p when line 0 is empty).
u32 func_002515B8(u32 s, u32 p, s32 style) {
    const u32 cols = func_00250910();
    guest::GuestFrame fr(0x40);
    const u32 info = fr.Address(0), wh = fr.Address(0x10), col = fr.Address(0x20);
    const u32 out = func_00252160(p, s, info, 0x3A80, 0x3C80);
    if (GRef<u8>(info + 2) == 0) return p;
    p = out;
    u32 A, B, C, D;
    switch (style) {
        case 6: A = B = cols + 0xA0, C = D = cols + 0xB0; break;
        case 0:
        case 2: A = cols + 0x40, B = cols + 0x60, C = cols + 0x50, D = cols + 0x70; break;
        case 1:
        case 3: A = B = cols + 0x60, C = D = cols + 0x70; break;
        case 4: A = cols + 0x40, B = cols + 0x80, C = cols + 0x50, D = cols + 0x90; break;
        case 5: A = B = cols + 0x80, C = D = cols + 0x90; break;
        default: A = B = cols + 0x40, C = D = cols + 0x50; break;
    }
    const u32 lines = GRef<u8>(info);
    const u32 w = wh, h = wh + 2;
    s32 si = 0;    // string index
    u32 row = 0;   // texture row
    u32 cell_h = 0;
    for (u32 ln = 0; ln < lines; ++ln) {
        const u32 cnt = GRef<u8>(info + 2 + ln);
        const s32 first = ln == 0 ? 0 : ln == 1 ? static_cast<s32>(GRef<u8>(info + 2)) + 1
                                                : static_cast<s32>(GRef<u8>(info + 2) + GRef<u8>(info + 3)) + 2;
        u32 width = 0;
        for (u32 i = 0; i < cnt; ++i) {
            func_00251D90(GRef<u16>(s + 2u * (first + i)), w, h);
            cell_h = (static_cast<u32>(GRef<u16>(h)) << 4) & 0xFFFF;
            width = ((static_cast<u32>(GRef<u16>(w)) << 2) / 5 + width) & 0xFFFF;
        }
        s32 next_si = si + 1;
        u32 next_row = row + 1;
        width = (width << 4) & 0xFFFF;
        u32 x = 0x8000 - (width >> 1);
        const u32 y = ln * cell_h + 0x7500;
        u32 u = 0;
        func_00251DB8(w, h);
        u32 v = row * GRef<u16>(h);
        if (cnt != 0) {
            const u32 normal = cols + 0x40;
            for (u32 i = 0;;) {
                const bool mark = (ln == 0 && i == 0) || (ln == lines - 1 && i == cnt - 1);
                u32 fg, bg, cbp = 0x3C80;
                s32 mode = 2, dy = 0;
                const u32 code = GRef<u16>(s + 2u * static_cast<u32>(si));
                if (mark) {
                    fg = B;
                    bg = D;
                } else if (code - 8 < 4) {
                    cbp = (0x3C80 + code - 7) & 0xFFFF;
                    for (u32 k = 0; k < 4; ++k) GRef<u32>(col + 4 * k) = 0x80;
                    fg = col;
                    mode = 8;
                    dy = 0x30;
                    bg = C;
                } else if (code == 0xC) {
                    GRef<u32>(col) = 0xC4;
                    GRef<u32>(col + 4) = 0x14;
                    GRef<u32>(col + 0xC) = 0xA0;
                    fg = col;
                    mode = 4;
                    bg = C;
                    GRef<u32>(col + 8) = 0;
                } else {
                    fg = A;
                    bg = C;
                }
                func_00251D90(code, w, h);
                const u32 gw = GRef<u16>(w);
                const u32 sw = (((gw << 2) / 5) << 4) & 0xFFFF;
                const u32 sh = (static_cast<u32>(GRef<u16>(h)) << 4) & 0xFFFF;
                cell_h = sh;
                v &= 0xFFFF;
                const u32 gy = y + static_cast<u32>(dy);
                const u32 uu = u & 0xFFFF;
                if (fg == normal)
                    p = func_00251340(p, (x + 0x20) & 0xFFFF, (gy + 0x20) & 0xFFFF, 0x0FFFFFF0, sw, sh, uu, v, static_cast<u16>(gw),
                                      GRef<u16>(h), bg, 0x3A80, static_cast<u16>(cbp), mode);
                p = func_00251340(p, (x + 0x10) & 0xFFFF, (gy + 0x10) & 0xFFFF, 0x0FFFFFF0, sw, sh, uu, v, GRef<u16>(w), GRef<u16>(h),
                                  bg, 0x3A80, static_cast<u16>(cbp), mode);
                const u32 x0 = x & 0xFFFF;
                x += sw;
                p = func_00251340(p, x0, gy & 0xFFFF, 0x0FFFFFF0, sw, sh, uu, v, GRef<u16>(w), GRef<u16>(h), fg, 0x3A80,
                                  static_cast<u16>(cbp), mode);
                func_00251DB8(w, h);
                const s32 n = static_cast<s32>(i);
                v = row * GRef<u16>(h);
                u += GRef<u16>(w);
                if (((n < 0 ? n + 0xF : n) >> 4) << 4 == n - 0xF) {
                    u = 0;
                    row = next_row;
                    v = next_row * GRef<u16>(h);
                    ++next_row;
                }
                ++i;
                si = next_si;
                if (!(i < cnt)) break;
                ++next_si;
            }
            next_si = si + 1;
        }
        si = next_si;
        row = next_row;
    }
    return p;
}

// EE: 0x00257D08
// Speaker portrait window (who 0..0x14; others: nothing) at p: uploads the shared CLUT (last
// entry of the portrait table 0x002508A0: {base, count, entries of 16 bytes {offset, size, w, h}})
// to CBP 0x3CD0 and portrait `who` to TBP 0x3CC0, TEXFLUSH, then two sprites of it (shadow
// offset 0x18 / 0x18 at y 0x73F8, then the picture at y 0x73E0) centred at x 0x8000, 4/5 wide;
// colours from the radio header 0x00250910 (+0x60 / +0x70, speaker 3: +0xA0 / +0xB0, from 0xD on:
// +0x80 / +0x90). Returns the end of the data.
u32 func_00257D08(s32 who, u32 p) {
    const u32 hdr = func_002508A0();
    if (who < 0 || !(who < 0x15)) return p;
    const u32 ce = W(hdr + 8) + W(hdr + 4) * 16 - 0x10;
    const u32 caddr = W(hdr) + W(ce);
    fl::ZeroQ(p);
    GRef<u64>(p + 0x20) = 0x13CD000000000ull;
    GRef<u32>(p) = 0x10000005;
    GRef<u32>(p + 0xC) = 0x50000005;
    GRef<u64>(p + 0x10) = 0x1000000000008004ull;
    GRef<u64>(p + 0x18) = 0xE;
    GRef<u64>(p + 0x28) = 0x50;
    GRef<u64>(p + 0x30) = 0;
    GRef<u64>(p + 0x38) = 0x51;
    GRef<u64>(p + 0x48) = 0x52;
    GRef<u64>(p + 0x50) = 0;
    GRef<u64>(p + 0x40) = static_cast<u64>(GRef<u16>(ce + 8)) | (static_cast<u64>(GRef<u16>(ce + 0xA)) << 32);
    GRef<u64>(p + 0x58) = 0x53;
    GRef<u32>(p + 0x60) = (W(ce + 4) >> 4) | 0x30000000u;
    GRef<u32>(p + 0x64) = caddr;
    GRef<u32>(p + 0x68) = 0;
    GRef<u32>(p + 0x6C) = (W(ce + 4) >> 4) | 0x50000000u;
    const s32 idx = who < -1 ? 0x15 : who == -1 ? 0x16 : who;
    const u32 e = W(hdr + 8) + static_cast<u32>(idx) * 16;
    const u32 iaddr = W(hdr) + W(e);
    GRef<u32>(p + 0x70) = 0x10000005;
    GRef<u32>(p + 0x7C) = 0x50000005;
    GRef<u32>(p + 0x74) = 0;
    GRef<u32>(p + 0x78) = 0;
    GRef<u64>(p + 0x80) = 0x1000000000008004ull;
    GRef<u64>(p + 0x90) = 0x14083CC000000000ull;
    GRef<u64>(p + 0x98) = 0x50;
    GRef<u64>(p + 0xA8) = 0x51;
    GRef<u64>(p + 0x88) = 0xE;
    GRef<u64>(p + 0xA0) = 0;
    GRef<u64>(p + 0xB0) = static_cast<u64>(GRef<u16>(e + 8)) | (static_cast<u64>(GRef<u16>(e + 0xA)) << 32);
    GRef<u64>(p + 0xB8) = 0x52;
    GRef<u64>(p + 0xC8) = 0x53;
    GRef<u64>(p + 0xC0) = 0;
    GRef<u32>(p + 0xD0) = (W(e + 4) >> 4) | 0x30000000u;
    GRef<u32>(p + 0xD4) = iaddr;
    GRef<u32>(p + 0xD8) = 0;
    GRef<u32>(p + 0xDC) = (W(e + 4) >> 4) | 0x50000000u;
    GRef<u64>(p + 0x100) = 0;
    fl::ZeroQ(p + 0xE0);
    GRef<u32>(p + 0xE0) = 0x10000002;
    GRef<u32>(p + 0xEC) = 0x50000002;
    GRef<u64>(p + 0xF0) = 0x1000000000008001ull;
    GRef<u64>(p + 0xF8) = 0xE;
    GRef<u64>(p + 0x108) = 0x3F;
    const u32 cols = func_00250910();
    u32 shadow, pic;
    if (who < 0xD) {
        if (who == 3) {
            shadow = cols + 0xB0;
            pic = cols + 0xA0;
        } else {
            shadow = cols + 0x70;
            pic = cols + 0x60;
        }
    } else {
        shadow = cols + 0x90;
        pic = cols + 0x80;
    }
    const u64 tex0 = (0x3CC0ull | 0x2000000000000000ull) | ((0x3CD0ull << 37) | 0x665420000ull);
    const u32 tag = p + 0x110;
    u32 q = p + 0x120;
    const u32 x0 = 0x8000u - static_cast<u32>(GRef<u16>(e + 8)) * 8;
    for (u32 k = 0; k < 2; ++k) {
        GRef<u64>(q) = 0x1000000000000007ull;
        GRef<u64>(q + 8) = 0xE;
        GRef<u64>(q + 0x10) = 0;
        GRef<u64>(q + 0x18) = 0x3F;
        GRef<u64>(q + 0x20) = 0x8000000044ull;
        const u32 col = k == 0 ? shadow : pic;
        const u32 x = k == 0 ? x0 + 0x18 : x0;
        const u32 y = k == 0 ? 0x73F8u : 0x73E0u;
        GRef<u64>(q + 0x30) = 5;
        GRef<u64>(q + 0x28) = 0x42;
        GRef<u64>(q + 0x38) = 8;
        const u32 a = q + 0x40;
        GRef<u64>(a + 0x48) = 0x413;
        GRef<u64>(a) = 0x30002;
        GRef<u64>(a + 8) = 0x47;
        GRef<u64>(a + 0x40) = 0x30AB400000008002ull;
        fl::ZeroQ(a + 0x50);
        GRef<u64>(a + 0x10) = 0x131000000ull;
        GRef<u64>(a + 0x18) = 0x4E;
        GRef<u64>(a + 0x20) = tex0;
        GRef<u64>(a + 0x28) = 6;
        GRef<u64>(a + 0x30) = 0x100080060ull;
        GRef<u64>(a + 0x38) = 0x14;
        GRef<u32>(a + 0x50) = 8;
        GRef<u32>(a + 0x54) = 8;
        fl::CopyQ(a + 0x60, col);
        GRef<u32>(a + 0x70) = x;
        GRef<u32>(a + 0x7C) = 0x8000;
        GRef<u32>(a + 0x74) = y;
        GRef<u32>(a + 0x78) = 0x0FFFFFF0;
        fl::ZeroQ(a + 0x80);
        GRef<u32>(a + 0x80) = static_cast<u32>(GRef<u16>(e + 8)) * 16 - 8;
        GRef<u32>(a + 0x84) = static_cast<u32>(GRef<u16>(e + 0xA)) * 16 - 8;
        fl::CopyQ(a + 0x90, col);
        GRef<u32>(a + 0xA0) = x + ((static_cast<u32>(static_cast<s32>(GRef<u16>(e + 8)) * 4 / 5)) << 4);
        GRef<u32>(a + 0xAC) = 0;
        GRef<u32>(a + 0xA8) = 0x0FFFFFF0;
        GRef<u32>(a + 0xA4) = y + (static_cast<u32>(GRef<u16>(e + 0xA)) << 4);
        q += 0xF0;
    }
    const u32 qwc = ((q - tag) >> 4) - 1;
    fl::ZeroQ(tag);
    GRef<u32>(tag + 0xC) = qwc | 0x50000000u;
    GRef<u32>(tag) = qwc | 0x10000000u;
    return q;
}

// EE: 0x00258E48
// Bottom subtitle text: lays out the glyph string s (0x00252160, TBP 0x3A80, CBP 0x3C80) and draws
// its lines centred at x 0x8000, line l at y 0x8C00 - (lines - l) x 0x140, each glyph twice as
// shadow (radio header +0x50, at +0x20 / +0x10 with y rounded down to 0x20 / 0x10) and once in
// +0x40. Returns p (the input p when line 0 is empty).
u32 func_00258E48(u32 s, u32 p) {
    const u32 cols = func_00250910();
    guest::GuestFrame fr(0x20);
    const u32 info = fr.Address(0), wh = fr.Address(0x10);
    const u32 w = wh, h = wh + 2;
    const u32 out = func_00252160(p, s, info, 0x3A80, 0x3C80);
    if (GRef<u8>(info + 2) == 0) return p;
    p = out;
    const u32 lines = GRef<u8>(info);
    u32 si = 0, row = 0;
    for (u32 ln = 0; ln < lines; ++ln) {
        const u32 cnt = GRef<u8>(info + 2 + ln);
        const u32 first = ln == 0 ? 0u : ln == 1 ? GRef<u8>(info + 2) + 1u : GRef<u8>(info + 2) + GRef<u8>(info + 3) + 2u;
        u32 width = 0;
        for (u32 i = 0; i < cnt; ++i) {
            func_00251D90(GRef<u16>(s + 2 * (first + i)), w, h);
            width = ((static_cast<u32>(GRef<u16>(w)) << 2) / 5 + width) & 0xFFFF;
        }
        u32 next_si = si + 1;
        u32 next_row = row + 1;
        width = (width << 4) & 0xFFFF;
        func_00251DB8(w, h);
        const u32 y = 0x8C00u - (lines - ln) * 0x140;
        u32 u = 0;
        u32 v = row * GRef<u16>(h);
        u32 x = 0x8000u - (width >> 1);
        if (cnt != 0) {
            for (u32 i = 0;;) {
                v &= 0xFFFF;
                const u32 code = GRef<u16>(s + 2 * si);
                const u32 uu = u & 0xFFFF;
                func_00251D90(code, w, h);
                const u32 sw = (((static_cast<u32>(GRef<u16>(w)) << 2) / 5) << 4) & 0xFFFF;
                const u32 sh = (static_cast<u32>(GRef<u16>(h)) << 4) & 0xFFFF;
                p = func_00251340(p, (x + 0x20) & 0xFFFF, (y + 0x20) & 0xFFE0, 0x0FFFFFF0, sw, sh, uu, v, GRef<u16>(w), GRef<u16>(h),
                                  cols + 0x50, 0x3A80, 0x3C80, 2);
                p = func_00251340(p, (x + 0x10) & 0xFFFF, (y + 0x10) & 0xFFF0, 0x0FFFFFF0, sw, sh, uu, v, GRef<u16>(w), GRef<u16>(h),
                                  cols + 0x50, 0x3A80, 0x3C80, 2);
                const u32 x0 = x & 0xFFFF;
                x += sw;
                p = func_00251340(p, x0, y & 0xFFFF, 0x0FFFFFF0, sw, sh, uu, v, GRef<u16>(w), GRef<u16>(h), cols + 0x40, 0x3A80, 0x3C80, 2);
                func_00251DB8(w, h);
                const s32 n = static_cast<s32>(i);
                v = row * GRef<u16>(h);
                u += GRef<u16>(w);
                if (((n < 0 ? n + 0xF : n) >> 4) << 4 == n - 0xF) {
                    v = next_row * GRef<u16>(h);
                    row = next_row;
                    u = 0;
                    ++next_row;
                }
                ++i;
                si = next_si;
                if (!(i < cnt)) break;
                ++next_si;
            }
            ++next_si;
        }
        si = next_si;
        row = next_row;
    }
    return p;
}

// EE: 0x00258690
// Tutorial control hints (mission kind 3): uploads the hint sheet (0x00250900 image block) to TBP
// 0x3D00 / CBP 0x3DE0, draws the controller picture at (0x878, 0x779) and, for every hint bit
// requested this frame (0x004BE040, 13 hints; tables 0x0040A228.. of 13 s16 each: x, y, u, v, w,
// h), its highlight pulsing with sin(frames x C 0x0041046C / 60) (each channel (57 sin + 192) mod
// 255); a hint's frame counter (0x004BE048 [i]) restarts when its bit changes. The request mask
// moves to 0x004BE044 and is cleared. Returns the end of the data.
// fpucheck: verified 3
u32 func_00258690(u32 p) {
    const u32 h = func_00250900();
    if (W(h) == 0) return p;
    const u32 changed = W(0x004BE040) ^ W(0x004BE044);
    for (u32 i = 0; i < 0xD; ++i)
        if (((changed >> i) & 1u) != 0) GRef<u32>(0x004BE048 + 4 * i) = 0;
    p = func_002582B0(W(h), 0x3D00, 0, 0, 0x3DE0, p);
    fl::ZeroQ(p);
    GRef<u32>(p) = 0x10000008;
    GRef<u32>(p + 0xC) = 0x50000008;
    GRef<u64>(p + 0x10) = 0x1000000000008007ull;
    GRef<u64>(p + 0x18) = 0xE;
    GRef<u64>(p + 0x28) = 0x3F;
    GRef<u64>(p + 0x20) = 0;
    GRef<u64>(p + 0x30) = 0x8000000044ull;
    GRef<u64>(p + 0x38) = 0x42;
    GRef<u64>(p + 0x40) = 5;
    GRef<u64>(p + 0x48) = 8;
    GRef<u64>(p + 0x58) = 0x47;
    GRef<u64>(p + 0x50) = 0x30002;
    GRef<u64>(p + 0x60) = 0x131000000ull;
    GRef<u64>(p + 0x68) = 0x4E;
    GRef<u64>(p + 0x70) = 0x2007BC0665323D00ull;
    GRef<u64>(p + 0x78) = 6;
    GRef<u64>(p + 0x80) = 0x100080060ull;
    GRef<u64>(p + 0x88) = 0x14;
    p = func_00258418(p + 0x90, 0, 1, 0, 1, 0);
    p = func_00258550(p, 0x878, 0x779, 0xFFFFFF, 0, 0, 0x80, 0x64, 0x80, 0x80, 0x80, 0x60);
    const auto tab = [](u32 t, u32 i) { return static_cast<u32>(GRef<u16>(0x0040A228 + 0x20 * t + 2 * i)); };
    for (u32 i = 0; i < 0xD; ++i) {
        if (((W(0x004BE040) >> i) & 1u) == 0) continue;
        const u32 timer = 0x004BE048 + 4 * i;
        const f32 t = fpu::IntToFloat(GRef<s32>(timer)) * F(0x0041046C) / 60.0f;
        const u32 x = (tab(0, i) + 0x878) & 0xFFFF, y = (tab(1, i) + 0x779) & 0xFFFF;
        const u32 u = tab(2, i), v = tab(3, i), w = tab(4, i), hh = tab(5, i);
        const auto shade = [t] { return static_cast<u16>((fpu::FloatToInt(fpu::Add(ee::sinf(t) * 57.0f, 192.0f)) % 0xFF) & 0xFFFF); };
        const u16 r = shade();
        const u16 g = shade();
        const u16 b = shade();
        p = func_00258418(p, 0, 1, 0, 1, 0);
        p = func_002584A0(p, 1, 6, 0, 0, 0, 0, 1, 1);
        p = func_00258550(p, x, y, 0xFFFFFF, u, v, w, hh, r, g, b, 0x80);
        GRef<s32>(timer) = GRef<s32>(timer) + 1;
    }
    const u32 req = W(0x004BE040);
    GRef<u32>(0x004BE040) = 0;
    GRef<u32>(0x004BE044) = req;
    return p;
}
}  // namespace ac04::game
