// Sequence character engine (slice S5i, EE 0x00220228-0x00225840): the skinned character models of
// the in-engine sequences (docs/subsystems/mission-flow.md "In-engine sequences", rendering.md
// "Sequence character engine"): creation from the sequence data, motion evaluation and blending,
// bone / light / shadow matrices, VU0 micro skinning (program S through src/platform/vu0) and the
// GIF strip packets.
#include "game/game.h"

#include <cmath>
#include <cstring>

#include "common/ee_math.h"
#include "common/guest_stack.h"
#include "common/vu0_math.h"
#include "game/flight_common.h"
#include "platform/vu0/vu0_hle.h"
#include "sce/ee_hw.h"
#include "sce/sce.h"

namespace ac04::game {

using guest::GRef;
using namespace fl;

// Sequence character engine (EE 0x00220228-0x00225840): the skinned character models of the
// in-engine sequences. Engine globals: 0x00365F00 (+0x14/+0x18/+0x1C/+0x30/+0x34 DMA channel
// handles 0, 1, 2, 8, 9), 0x00365F40 world -> screen matrix, 0x00365F80 view -> screen matrix,
// 0x00365FC0 normal light matrix, 0x00366000 light colour matrix, 0x00366040 page table (512 bytes,
// one per 4 KB page of the 2 MB work area *0x00410D50), draw lists *0x00410D40 / *0x00410D44, fog
// colour *0x00410D70, fog scale *0x00410D74, fog far *0x00410D78.
// Character (0xF0 bytes + parts): +0..+0xC scale (1.0), +0x10 motion state, +0x20 light colour
// matrix, +0x60 normal light matrix, +0xA0 part count, +0xA4 texture set, +0xAC (index 2 block),
// +0xB8 part flags block (u8 at +0x50 + part -> part +0x152), +0xC0 (f32 data +8), +0xC4 parts
// (0x160 bytes each), +0xC8 next character, +0xCC head character, +0xD4 part-set bit, +0xD8 head
// mask, +0xDC, +0xDE, +0xE0 morph block, +0xE4 attached set, +0xE8, +0xEC.

// EE: 0x002252B0
// Offset table entry n of a block: base + (word base[n + 1] & ~3).
u32 func_002252B0(u32 base, s32 n) {
    return base + (W(base + 4 + static_cast<u32>(n) * 4) & 0xFFFFFFFCu);
}

// EE: 0x002252D0
// Rounds v up for alignment n: v when v % n == 0, else v + 16 - v % n (the step is 16 whatever n,
// as the original; every caller passes 16). n == 0 is a trap (break 7) in the original.
u32 func_002252D0(u32 v, u32 n) {
    const u32 r = v % n;
    if (r == 0) return v;
    return v + 0x10 - r;
}

// EE: 0x002254A0
// Clears the 512-byte page table 0x00366040.
void func_002254A0() {
    for (s32 i = 0x1FF; i >= 0; --i) GRef<u8>(0x00366040 + static_cast<u32>(i)) = 0;
}

// EE: 0x002254D8
// Rounds up to a multiple of 4 KB.
u32 func_002254D8(u32 v) {
    const u32 r = v & 0xFFF;
    if (r == 0) return v;
    return v + 0x1000 - r;
}

// EE: 0x00225570
// Allocates size bytes (rounded up to 4 KB pages) from the work area *0x00410D50: first fit over the
// page table 0x00366040; the pages taken get the count of pages left in the run (n, n - 1, ...,
// 1). Returns the address, or `size` itself when no run fits (as the original). The run test does
// not stop at the end of the table, and with size 0 it scans until a used byte (as the original).
u32 func_00225570(u32 size) {
    const u32 pages = func_002254D8(size) >> 12;
    const u32 table = 0x00366040;
    u32 t = 0;
    while (static_cast<s32>(t) < 0x200) {
        if (GRef<s8>(table + t) != 0) {
            ++t;
            continue;
        }
        bool fit = true;
        for (u32 k = 0;; ++k) {
            if (GRef<s8>(table + t + k) != 0) {
                t = t + k + 1;
                fit = false;
                break;
            }
            if (k == pages - 1) break;
        }
        if (!fit) continue;
        for (u32 k = 0; k < pages; ++k) GRef<u8>(table + t + k) = static_cast<u8>(pages - k);
        return (t << 12) + W(0x00410D50);
    }
    return size;
}

// EE: 0x00220308
// Builds character c from model data `data` (+2 part count, +8 f32, part records of 0x30 bytes
// from +0x10): parts array after the 0xF0-byte header (16-aligned), scale 1.0, the default light
// matrices (0x00366000 -> +0x20, 0x00365FC0 -> +0x60), +0xDE = 1. Part (0x160 bytes) from its
// record r by type (r+0): types 0, 1 and 3 copy the record fields (+0x140 type, +0x142 (types 1 / 3:
// r+0xA), +0x144 r+0xC, +0x146 r+0xE, +0x148 r+8, +0x14A r+4, +0x10C data, +0x110 / +0x114 /
// +0x128 / +0x12C data + r+0x18 / +0x1C / +0x14 / +0x10, types 1 / 3 also +0x108 data + r+0x20; the
// TEX0 qword r+0x28 -> +0x100); every part gets +0x14E = 0, +0x13C = 0. The original also tracks
// the minimum of bits 37-50 of the parts' TEX0 in a register that is never used (not reproduced:
// no effect). Then the part draw state (0x002206C0). Returns the size used (16-aligned end - c).
u32 func_00220308(u32 c, u32 data) {
    const u32 parts = func_002252D0(c + 0xF0, 0x10);
    GRef<u32>(c + 0xC4) = parts;
    GRef<u32>(c + 0xA0) = GRef<u16>(data + 2);
    GRef<f32>(c + 0xC0) = GRef<f32>(data + 8);
    GRef<f32>(c + 0xC) = 1.0f;
    GRef<f32>(c + 0) = 1.0f;
    GRef<f32>(c + 4) = 1.0f;
    GRef<f32>(c + 8) = 1.0f;
    sceVu0CopyMatrix(c + 0x20, 0x00366000);
    sceVu0CopyMatrix(c + 0x60, 0x00365FC0);
    GRef<u16>(c + 0xDE) = 1;
    for (s32 i = 0; i < GRef<s32>(c + 0xA0); ++i) {
        const u32 r = data + 0x10 + static_cast<u32>(i) * 0x30;
        const u32 p = W(c + 0xC4) + static_cast<u32>(i) * 0x160;
        const u16 type = GRef<u16>(r);
        if (type == 0) {
            GRef<u16>(p + 0x140) = type;
            GRef<u16>(p + 0x144) = GRef<u16>(r + 0xC);
            GRef<u32>(p + 0x128) = data + W(r + 0x14);
            GRef<u32>(p + 0x12C) = data + W(r + 0x10);
            GRef<u16>(p + 0x146) = GRef<u16>(r + 0xE);
            GRef<u32>(p + 0x110) = data + W(r + 0x18);
            GRef<u32>(p + 0x114) = data + W(r + 0x1C);
            GRef<u16>(p + 0x148) = GRef<u16>(r + 8);
            GRef<u32>(p + 0x10C) = data;
            GRef<u16>(p + 0x14A) = GRef<u16>(r + 4);
            GRef<u64>(p + 0x100) = GRef<u64>(r + 0x28);
        } else if (type == 1 || type == 3) {
            GRef<u16>(p + 0x140) = type;
            GRef<u16>(p + 0x142) = GRef<u16>(r + 0xA);
            GRef<u32>(p + 0x108) = data + W(r + 0x20);
            GRef<u32>(p + 0x128) = data + W(r + 0x14);
            GRef<u16>(p + 0x144) = GRef<u16>(r + 0xC);
            GRef<u32>(p + 0x114) = data + W(r + 0x1C);
            GRef<u32>(p + 0x12C) = data + W(r + 0x10);
            GRef<u16>(p + 0x146) = GRef<u16>(r + 0xE);
            GRef<u32>(p + 0x110) = data + W(r + 0x18);
            GRef<u32>(p + 0x10C) = data;
            GRef<u16>(p + 0x148) = GRef<u16>(r + 8);
            GRef<u16>(p + 0x14A) = GRef<u16>(r + 4);
            GRef<u64>(p + 0x100) = GRef<u64>(r + 0x28);
        }
        GRef<u16>(p + 0x14E) = 0;
        GRef<u32>(p + 0x13C) = 0;
    }
    func_002206C0(c);
    return func_002252D0(parts + W(c + 0xA0) * 0x160, 0x10) - c;
}

// EE: 0x00220590
// Binds motion data `motion` to character c with work memory at `work`. A null motion (word 0)
// shares the head character's motion state and uses no memory. Else the motion state (0x218
// bytes, 16-aligned) at work: +0 motion, +8 = 1, +0xA = motion+0 (count); header 0 bound with the
// bone array after it (0x002217E8); the root bone pointer +0xC taken from the head's motion state,
// that root bone's +0..+0x18 cleared and +0x1C = 1.0; motion 1 started (0x002221B0). Returns the
// size used.
u32 func_00220590(u32 work, u32 c, u32 motion) {
    if (W(motion) == 0) {
        GRef<u32>(c + 0x10) = W(W(c + 0xCC) + 0x10);
        return 0;
    }
    const u32 ms = func_002252D0(work, 0x10);
    GRef<u32>(c + 0x10) = ms;
    u32 end = func_002252D0(ms + 0x218, 0x10);
    GRef<u16>(ms + 8) = 1;
    GRef<u32>(ms) = motion;
    GRef<u16>(ms + 0xA) = GRef<u16>(motion);
    func_002217E8(W(c + 0x10), func_002252B0(motion, 0), end);
    const u32 n = static_cast<u32>(GRef<s16>(ms + 0x20) + 1);
    GRef<u32>(ms + 0xC) = W(W(W(c + 0xCC) + 0x10) + 0xC);
    end = func_002252D0(n * 0xB0 + end, 0x10);
    const u32 root = W(ms + 0xC);
    GRef<u32>(root + 0xC) = 0;
    GRef<f32>(root + 0x1C) = 1.0f;
    GRef<u32>(root + 0x10) = 0;
    GRef<u32>(root + 0x14) = 0;
    GRef<u32>(root + 0x18) = 0;
    GRef<u32>(root + 0) = 0;
    GRef<u32>(root + 4) = 0;
    GRef<u32>(root + 8) = 0;
    func_002221B0(ms, GRef<s16>(ms + 8));
    return end - work;
}

// EE: 0x002206C0
// Part draw state: for each part, the register list at +0x10 (count +0x150 = 7 when done):
// TEX0_2 = part+0x100, TEX1_2 = 0x60, TEST_2 = 0x515AD, ALPHA_2 = 0x7F00000044, CLAMP_2 = 0, FOGCOL
// = fog colour *0x00410D70 (low word only; the high word is left as it was, as the original), and
// at +0 the A+D GIF tag for those 6 registers (NLOOP = count - 1).
void func_002206C0(u32 c) {
    for (s32 i = 0; i < GRef<s32>(c + 0xA0); ++i) {
        const u32 p = W(c + 0xC4) + static_cast<u32>(i) * 0x160;
        GRef<u16>(p + 0x150) = 1;
        GRef<u64>(p + 0x10) = GRef<u64>(p + 0x100);
        GRef<u64>(p + 0x18) = 7;
        GRef<u64>(p + 0x20) = 0x60;
        GRef<u64>(p + 0x28) = 0x15;
        GRef<u64>(p + 0x30) = 0x515AD;
        GRef<u64>(p + 0x38) = 0x48;
        GRef<u64>(p + 0x40) = 0x7F00000044ull;
        GRef<u64>(p + 0x48) = 0x43;
        GRef<u64>(p + 0x50) = 0;
        GRef<u64>(p + 0x58) = 9;
        GRef<u32>(p + 0x60) = W(0x00410D70);
        GRef<u64>(p + 0x68) = 0x3D;
        GRef<u16>(p + 0x150) = 7;
        GRef<u64>(p) = 0x1000000000000006ull;
        GRef<u64>(p + 8) = 0xE;
    }
}

// EE: 0x00220228
// Draws character c and its linked characters (+0xC8) whose part-set bit (+0xD4) is in the head
// mask (+0xD8, s16): shadow and bones (0x00221440), the texture upload of the set (+0xA4 entry 0
// via 0x00104338, when +0xDE == 1), the skinned parts (0x002236A0) and, when the head has an
// attached set (+0xE4), a reset of `list` bucket 0 (0x001132E0) and that set (0x00223830).
void func_00220228(u32 c, u32 chars, u32 list) {
    const s32 mask = GRef<s16>(c + 0xD8);
    GRef<u32>(0x00410D40) = chars;
    GRef<u32>(0x00410D44) = list;
    for (u32 s = c; s != 0; s = W(s + 0xC8)) {
        if ((static_cast<u32>(mask) & W(s + 0xD4)) == 0) continue;
        func_00221440(s);
        if (GRef<s16>(s + 0xDE) == 1) func_00104338(W(0x00410D40), 0, func_002252B0(W(s + 0xA4), 0), 0);
        func_002236A0(s);
        if (W(c + 0xE4) != 0) {
            func_001132E0(list, 0, 0x80);
            func_00223830(W(c + 0xE4));
        }
    }
}

// EE: 0x00220948
void func_00220948(u32 m) { sceVu0CopyMatrix(0x00365F40, m); }

// EE: 0x00220958
void func_00220958(u32 m) { sceVu0CopyMatrix(0x00365F80, m); }

// EE: 0x00220A40
// Normal light matrix m -> +0x60 of character c and every linked character.
void func_00220A40(u32 c, u32 m) {
    sceVu0CopyMatrix(c + 0x60, m);
    for (u32 s = W(c + 0xC8); s != 0; s = W(s + 0xC8)) sceVu0CopyMatrix(s + 0x60, m);
}

// EE: 0x00220A98
// Light colour matrix m -> +0x20 of character c and every linked character.
void func_00220A98(u32 c, u32 m) {
    sceVu0CopyMatrix(c + 0x20, m);
    for (u32 s = W(c + 0xC8); s != 0; s = W(s + 0xC8)) sceVu0CopyMatrix(s + 0x20, m);
}

// EE: 0x00220B00
// Fog: colour -> *0x00410D70, far -> *0x00410D78, scale 255 / (far - near) -> *0x00410D74.
// fpucheck: 1 sub
void func_00220B00(u32 colour, f32 near_z, f32 far_z) {
    const f32 d = fpu::Sub(far_z, near_z);
    GRef<f32>(0x00410D78) = far_z;
    GRef<u32>(0x00410D70) = colour;
    GRef<f32>(0x00410D74) = 1.0f / d * 255.0f;
}

// EE: 0x00220B40
// First character after the head (from head +0xCC... of c: c+0xCC's chain) whose part-set bit
// lies inside its head's mask: walks s = c+0xCC, s+0xC8, ...; returns s when (s+0xD4 &
// (s+0xCC)+0xD8) == s+0xD4, else 0.
u32 func_00220B40(u32 c) {
    for (u32 s = W(c + 0xCC); s != 0; s = W(s + 0xC8)) {
        const u32 bit = W(s + 0xD4);
        if ((bit & W(W(s + 0xCC) + 0xD8)) == bit) return s;
    }
    return 0;
}

// EE: 0x00220BF0
// Attached set of object o (+0xE4): built from `set` (0x00220C40), or cleared when set is 0.
void func_00220BF0(u32 o, u32 set) {
    GRef<u32>(o + 0xE4) = set != 0 ? func_00220C40(set) : 0;
}

// EE: 0x00220C38
void func_00220C38(u32 o, f32 v) { GRef<f32>(o + 0xEC) = v; }

// EE: 0x00220C40
// Creates the characters of a set block (`set`+0 = count n, entries via 0x002252B0; each entry:
// 0 model data, 1 texture set, 2 block, 3 motion, 4 part flags when the entry count >= 5). The
// size is summed first (header + parts per model; motion state + bones for entry 0 or a
// non-null motion; 0x20 for a morph block when set entry 0's block 2 is non-null), one page run
// is allocated (0x00225570), then the head (+0xCC = itself) and the n - 1 linked characters are
// built (0x00220308, 0x00220590); later characters inherit the head texture set when theirs is
// empty; +0xD4 = 1 << index, head +0xD8 = 1. The morph block (s8 = 0x20) is cleared and given to
// all; part +0x152 from the part flags (s8) or 0. With n >= 4 the original also looks up entry 3
// of the set and discards it (no effect).
u32 func_00220C40(u32 set) {
    const s32 n = GRef<s32>(set);
    bool morph = false;
    u32 size = 0;
    for (s32 i = 0; i < n; ++i) {
        const u32 e = func_002252B0(set, i);
        size = (size + 0xF) & 0xFFFFFFF0u;
        size = func_002252D0(size + 0xF0, 0x10);
        size = func_002252D0(size + GRef<u16>(func_002252B0(e, 0) + 2) * 0x160u, 0x10);
        const u32 m = func_002252B0(e, 3);
        if (W(m) != 0) {
            const s32 bones = GRef<s16>(func_002252B0(m, 0));
            size = ((size + 0x227) & 0xFFFFFFF0u) + static_cast<u32>(bones + 1) * 0xB0;
        } else if (i == 0) {
            size = ((size + 0x227) & 0xFFFFFFF0u) + 0xB0;
        }
    }
    if (W(func_002252B0(func_002252B0(set, 0), 2)) != 0) {
        morph = true;
        size = ((size + 0xF) & 0xFFFFFFF0u) + 0x20;
    }
    const u32 c = func_00225570(size);
    GRef<u32>(c + 0xCC) = c;
    GRef<u32>(c + 0xC8) = 0;
    u32 prev = c;
    u32 e = func_002252B0(set, 0);
    u32 work = func_002252D0(c + func_00220308(c, func_002252B0(e, 0)), 0x10);
    if (W(func_002252B0(e, 3)) != 0) work = func_002252D0(work + func_00220590(work, c, func_002252B0(e, 3)), 0x10);
    GRef<u32>(c + 0xE4) = 0;
    GRef<u32>(c + 0xEC) = 0;
    GRef<u32>(c + 0xE8) = 0;
    GRef<u32>(c + 0xA4) = func_002252B0(e, 1) == 0 ? 0 : func_002252B0(e, 1);
    GRef<u32>(c + 0xAC) = func_002252B0(e, 2) == 0 ? 0 : func_002252B0(e, 2);
    const auto flags = [](u32 s, u32 entry) {
        if (GRef<s32>(entry) < 5) {
            GRef<u32>(s + 0xB8) = 0;
            return;
        }
        const u32 f = func_002252B0(entry, 4);
        GRef<u32>(s + 0xB8) = W(f) != 0 ? f : 0;
    };
    flags(c, e);
    GRef<u32>(c + 0xD4) = 1;
    GRef<u32>(c + 0xD8) = 1;
    GRef<u16>(c + 0xDC) = 0;
    for (s32 i = 1; i < n; ++i) {
        e = func_002252B0(set, i);
        const u32 s = work;
        GRef<u32>(prev + 0xC8) = s;
        GRef<u32>(s + 0xC8) = 0;
        GRef<u32>(s + 0xCC) = c;
        work = func_002252D0(s + func_00220308(s, func_002252B0(e, 0)), 0x10);
        work = func_002252D0(work + func_00220590(work, s, func_002252B0(e, 3)), 0x10);
        const u32 tex = func_002252B0(e, 1);
        GRef<u32>(s + 0xA4) = W(tex) != 0 ? tex : W(c + 0xA4);
        const u32 b2 = func_002252B0(e, 2);
        GRef<u32>(s + 0xAC) = W(b2) != 0 ? b2 : 0;
        flags(s, e);
        prev = s;
        GRef<u32>(s + 0xD4) = 1u << i;
    }
    if (morph) {
        if (c == 0) return c;
        for (u32 s = c; s != 0;) {
            GRef<u32>(s + 0xE0) = work;
            s = W(s + 0xC8);
            GRef<u64>(work) = 0;
            GRef<u32>(work + 8) = 0;
        }
    } else {
        if (c == 0) return c;
        for (u32 s = c; s != 0; s = W(s + 0xC8)) GRef<u32>(s + 0xE0) = 0;
    }
    for (u32 s = c; s != 0; s = W(s + 0xC8)) {
        const u32 f = W(s + 0xB8);
        for (s32 i = 0; i < GRef<s32>(s + 0xA0); ++i)
            GRef<s16>(W(s + 0xC4) + static_cast<u32>(i) * 0x160 + 0x152) = f != 0 ? GRef<s8>(f + static_cast<u32>(i) + 0x50) : 0;
    }
    return c;
}

// EE: 0x00221110
// Default sequence camera: sceVu0CameraMatrix(0x00365F40, position (0, 0, 0, 1), z (0, 0, 1, 1),
// y (0, 1, 0, 1)); sceVu0ViewScreenMatrix(0x00365F80, scrz 640, ax 1, ay 1, cx 2048, cy 2048, zmin
// 1, zmax K 0x0040FFFC, nearz 1, farz K 0x0040FFF8).
void func_00221110() {
    guest::GuestFrame frame(0x30);
    const u32 p = frame.Address(), z = p + 0x10, y = p + 0x20;
    GRef<f32>(p + 0xC) = 1.0f;
    GRef<f32>(z + 8) = 1.0f;
    GRef<f32>(z + 0xC) = 1.0f;
    GRef<f32>(y + 4) = 1.0f;
    GRef<f32>(y + 0xC) = 1.0f;
    GRef<u32>(p) = 0;
    GRef<u32>(p + 4) = 0;
    GRef<u32>(p + 8) = 0;
    GRef<u32>(z) = 0;
    GRef<u32>(z + 4) = 0;
    GRef<u32>(y) = 0;
    GRef<u32>(y + 8) = 0;
    sceVu0CameraMatrix(0x00365F40, p, z, y);
    sceVu0ViewScreenMatrix(0x00365F80, 640.0f, 1.0f, 1.0f, 2048.0f, 2048.0f, 1.0f, GRef<f32>(0x0040FFFC), 1.0f,
                           GRef<f32>(0x0040FFF8));
}

// EE: 0x002211C8
// Default sequence lights: directions (0.5, 0.5, 0), (-0.5, -0.5, 0), (0, 0, 0) into the normal
// light matrix 0x00365FC0; colours (K 0x00410004 x3), (K 0x00410008 x3), (K 0x00410000 x3) and
// ambient (K 0x00410000 x3), w 0, into the light colour matrix 0x00366000.
void func_002211C8() {
    guest::GuestFrame frame(0x70);
    const u32 sp = frame.Address();
    GRef<f32>(sp + 4) = 0.5f;
    GRef<f32>(sp + 0x14) = -0.5f;
    GRef<f32>(sp + 0) = 0.5f;
    GRef<u32>(sp + 8) = 0;
    GRef<u32>(sp + 0xC) = 0;
    GRef<f32>(sp + 0x10) = -0.5f;
    for (u32 o = 0x18; o < 0x30; o += 4) GRef<u32>(sp + o) = 0;
    sceVu0NormalLightMatrix(0x00365FC0, sp, sp + 0x10, sp + 0x20);
    const f32 k2 = GRef<f32>(0x00410000), k0 = GRef<f32>(0x00410004), k1 = GRef<f32>(0x00410008);
    const auto put = [sp](u32 o, f32 k) {
        GRef<f32>(sp + o) = k;
        GRef<f32>(sp + o + 4) = k;
        GRef<f32>(sp + o + 8) = k;
        GRef<u32>(sp + o + 0xC) = 0;
    };
    put(0x30, k0);
    put(0x40, k1);
    put(0x50, k2);
    put(0x60, k2);
    sceVu0LightColorMatrix(0x00366000, sp + 0x30, sp + 0x40, sp + 0x50, sp + 0x60);
}

// EE: 0x00225698
// Sequence model engine init: DMA channel handles (0 -> +0x14, 1 -> +0x18, 2 -> +0x1C, 9 -> +0x34,
// 8 -> +0x30 of 0x00365F00), default camera and lights, work AllocQwords(0x10000) (start rounded
// up to 4 KB) -> *0x00410D50, the texture-upload template (0x002213C0, 0xDDB09E00) and the draw-env
// template (0x002212A0, 0x40), page table cleared, fog (colour 0x808080, near 4000, far K
// 0x00410048). The work is 0x10000 qwords (1 MB); the page table covers 2 MB.
void InitSequenceModelEngine() {
    const u32 e = 0x00365F00;
    GRef<u32>(e + 0x14) = sceDmaGetChan(0);
    GRef<u32>(e + 0x18) = sceDmaGetChan(1);
    GRef<u32>(e + 0x1C) = sceDmaGetChan(2);
    GRef<u32>(e + 0x34) = sceDmaGetChan(9);
    GRef<u32>(e + 0x30) = sceDmaGetChan(8);
    func_00221110();
    func_002211C8();
    const u32 w = AllocQwords(0x10000);
    GRef<u32>(0x00410D50) = w;
    GRef<u32>(0x00410D50) = func_002254D8(w);
    func_002213C0(0xFFFFFFFFDDB09E00ull);  // lui: the 64-bit register value is sign-extended
    func_002212A0(0x40);
    func_002254A0();
    func_00220B00(0x808080, 4000.0f, GRef<f32>(0x00410048));
}

// EE: 0x002212A0
// Draw-environment template 0x00384340 (count *0x00410D68 = 9 qwords): A+D tag (NLOOP 8), TEST_2 =
// 0x5C003, ALPHA_2 = 0xA9 with FIX = alpha, FBA_2 = 0, RGBAQ = 0x50505050, then the main-context
// register qwords +0x150, +0x180, +0x170, +0x160.
void func_002212A0(u32 alpha) {
    const u32 t = 0x00384340;
    const u32 ctx = W(0x004104A8);
    GRef<u64>(t + 0x10) = 0x5C003;
    GRef<u64>(t + 0x18) = 0x48;
    GRef<u64>(t + 0x20) = (static_cast<u64>(alpha & 0xFF) << 32) | 0xA9;
    GRef<u64>(t + 0x28) = 0x43;
    GRef<u64>(t + 0x38) = 0x4B;
    GRef<u64>(t + 0x40) = 0x50505050;
    GRef<u64>(t + 0x48) = 1;
    GRef<u32>(0x00410D68) = 5;
    GRef<u64>(t + 0x30) = 0;
    CopyQ(t + 0x50, ctx + 0x150);
    const u32 order[3] = {0x180, 0x170, 0x160};
    for (u32 o : order) {
        const u32 n = W(0x00410D68) + 1;
        GRef<u32>(0x00410D68) = n;
        CopyQ(t + n * 0x10, W(0x004104A8) + o);
    }
    const u32 n = W(0x00410D68);
    GRef<u64>(t + 8) = 0xE;
    GRef<u64>(t) = static_cast<u64>(static_cast<s64>(static_cast<s32>(n))) | 0x1000000000000000ull;
    GRef<u32>(0x00410D68) = n + 1;
}

// EE: 0x002213C0
// Texture template 0x00384240 (count *0x00410D60 = 5, last qword pointer *0x00410D64 = +0x40): A+D
// tag (NLOOP 4), TEX0_2 = tex0 (64-bit), TEX1_2 = 0x60, TEST_2 = 0x50003, ALPHA_2 = 0x7F00000068.
void func_002213C0(u64 tex0) {
    const u32 t = 0x00384240;
    GRef<u64>(t) = 0x1000000000000004ull;
    GRef<u64>(t + 0x10) = tex0;
    GRef<u64>(t + 8) = 0xE;
    GRef<u64>(t + 0x18) = 7;
    GRef<u64>(t + 0x20) = 0x60;
    GRef<u64>(t + 0x28) = 0x15;
    GRef<u64>(t + 0x30) = 0x50003;
    GRef<u64>(t + 0x38) = 0x48;
    GRef<u64>(t + 0x40) = 0x7F00000068ull;
    GRef<u64>(t + 0x48) = 0x43;
    GRef<u32>(0x00410D64) = t + 0x40;
    GRef<u32>(0x00410D60) = 5;
}

// Motion state (0x218 bytes, at character +0x10): +0 motion data, +4 f32 1.0, +8 current motion,
// +0xA motion count, +0xC root bone (0xB0 bytes: +0 rotation, +0x10 position, +0x20 matrix, +0xA8
// -1 when set by 0x00222800), +0x10 bones (0xB0 bytes: +0x20 matrix, +0x50 position, +0xA4 parent,
// +0xA8 accumulated flag), +0x14 header, +0x18 / +0x1C header tables, +0x20 bone count, +0x22
// active slot, +0x26 blend slot, +0x210 end flag (1 clamped at the end, 2 wrapped). Slot k (4, at
// +0x130 + 0x38k... fields below relative to ms + 0x38k): +0x130 time, +0x134 length, +0x138 speed,
// +0x13C blend weight, +0x148 blend frames, +0x150 (u64 -1), +0x158 motion header, +0x15C clamp
// (else wrap), +0x15E state (0 free, 1 active, 2 blending in, 4 to clear), +0x160 sync group, +0x162,
// +0x164.

// EE: 0x002217E8
// Binds motion header `hdr` to motion state ms with bone memory `work`: +0x18 / +0x1C = hdr + its
// table offsets +4 / +8, +4 = 1.0, +0x20 = bone count hdr+0, +0x14 = hdr, the 4 slot states
// cleared, root bone +0xC = work, bones +0x10 = work + 0xB0, bone parents (+0xA4) from the s16
// table hdr+0xC (-1 -> none).
void func_002217E8(u32 ms, u32 hdr, u32 work) {
    GRef<u32>(ms + 0x18) = hdr + W(hdr + 4);
    GRef<u32>(ms + 0x1C) = hdr + W(hdr + 8);
    GRef<f32>(ms + 4) = 1.0f;
    GRef<u16>(ms + 0x20) = GRef<u16>(hdr);
    GRef<u32>(ms + 0x14) = hdr;
    for (s32 k = 3; k >= 0; --k) GRef<u16>(ms + 0x15E + static_cast<u32>(k) * 0x38) = 0;
    GRef<u32>(ms + 0xC) = work;
    GRef<u32>(ms + 0x10) = work + 0xB0;
    const u32 bones = work + 0xB0;
    for (s32 i = 0; i < GRef<s16>(ms + 0x20); ++i) {
        const s32 parent = GRef<s16>(hdr + 0xC + static_cast<u32>(i) * 2);
        GRef<u32>(bones + static_cast<u32>(i) * 0xB0 + 0xA4) = parent == -1 ? 0 : bones + static_cast<u32>(parent) * 0xB0;
    }
}

// EE: 0x002221B0
// Starts motion n on motion state ms: +8 = n, all slot states cleared, then the first free slot
// (always slot 0) set up from motion header h = entry n of the motion data: state 1, sync group
// h+0x4A, +0x150 = -1, clamp flag h+0x48, header h, length (float)h+2 (minus 1.0 when clamped;
// 0 and sync group 0 when not positive), +0x164 = +0x162 = 0, active slot +0x22, speed and blend
// weight 1.0, time 0.
// fpucheck: 1 sub (clamped)
void func_002221B0(u32 ms, s16 n) {
    GRef<s16>(ms + 8) = n;
    const u32 h = func_002252B0(W(ms), n);
    for (s32 k = 3; k >= 0; --k) GRef<u16>(ms + 0x15E + static_cast<u32>(k) * 0x38) = 0;
    for (s32 k = 0; k < 4; ++k) {
        const u32 s = ms + static_cast<u32>(k) * 0x38;
        if (GRef<s16>(s + 0x15E) != 0) continue;
        GRef<u16>(s + 0x15E) = 1;
        GRef<u16>(s + 0x160) = GRef<u16>(h + 0x4A);
        GRef<u64>(s + 0x150) = 0xFFFFFFFFFFFFFFFFull;
        const u16 clamp = GRef<u16>(h + 0x48);
        GRef<u32>(s + 0x158) = h;
        GRef<u16>(s + 0x15C) = clamp;
        f32 len = fpu::IntToFloat(GRef<s16>(h + 2));
        GRef<f32>(s + 0x134) = len;
        if (clamp != 0) GRef<f32>(s + 0x134) = fpu::Sub(len, 1.0f);
        if (GRef<f32>(s + 0x134) <= 0.0f) {
            GRef<u32>(s + 0x134) = 0;
            GRef<u16>(s + 0x160) = 0;
        }
        GRef<u16>(s + 0x164) = 0;
        GRef<u16>(s + 0x162) = 0;
        GRef<s16>(ms + 0x22) = static_cast<s16>(k);
        GRef<f32>(s + 0x13C) = 1.0f;
        GRef<f32>(s + 0x138) = 1.0f;
        GRef<u32>(s + 0x130) = 0;
        return;
    }
}

// EE: 0x00221FA0
// Starts motion n on the motion state of the first visible character of c's set (0x00220B40),
// when the character has bones: when entries n and n + 1 of the motion data are the same block
// (no such motion), or n == 0, prints "Error!! This motiondata does not exist" (0x00406AE0).
void func_00221FA0(u32 c, s32 n) {
    const u32 s = func_00220B40(c);
    if (s == 0) return;
    if (GRef<s16>(W(c + 0x10) + 0x20) == 0) return;
    const u32 a = func_002252B0(W(W(s + 0x10)), n);
    const u32 b = func_002252B0(W(W(s + 0x10)), n + 1);
    if (a != b && n != 0) {
        func_002221B0(W(s + 0x10), static_cast<s16>(n));
        return;
    }
    ee::printf(0x00406AE0, nullptr, 0);
}

// EE: 0x00223518
// Sets the time of the active slot of the first visible character of c's set to t. Clamped
// motion: time >= length -> length (end flag +0x210 = 1 when the slot is active), below 0 -> 0.
// Wrapping motion: subtracts / adds the length until 0 <= time < length (end flag 2 when active).
// A zero length with t >= 0 loops forever, as the original.
// fpucheck: 2 (sub and add loops)
void func_00223518(u32 c, f32 t) {
    if (func_00220B40(c) == 0) return;
    const u32 ms = W(func_00220B40(c) + 0x10);
    const u32 s = ms + static_cast<u32>(GRef<s16>(ms + 0x22)) * 0x38;
    GRef<f32>(s + 0x130) = t;
    if (GRef<s16>(s + 0x15C) != 0) {
        const f32 len = GRef<f32>(s + 0x134);
        if (len <= t) {
            GRef<f32>(s + 0x130) = len;
            if (GRef<s16>(s + 0x15E) == 1) GRef<u16>(ms + 0x210) = 1;
        }
        if (GRef<f32>(s + 0x130) < 0.0f) GRef<u32>(s + 0x130) = 0;
        return;
    }
    if (GRef<f32>(s + 0x134) <= t) {
        if (GRef<s16>(s + 0x15E) == 1) GRef<u16>(ms + 0x210) = 2;
        const f32 len = GRef<f32>(s + 0x134);
        f32 v;
        do {
            v = fpu::Sub(GRef<f32>(s + 0x130), len);
            GRef<f32>(s + 0x130) = v;
        } while (!(v < len));
    }
    if (GRef<f32>(s + 0x130) < 0.0f) {
        if (GRef<s16>(s + 0x15E) == 1) GRef<u16>(ms + 0x210) = 2;
        const f32 len = GRef<f32>(s + 0x134);
        f32 v;
        do {
            v = fpu::Add(len, GRef<f32>(s + 0x130));
            GRef<f32>(s + 0x130) = v;
        } while (!(0.0f <= v));
    }
}

// EE: 0x00221758
// Bone light matrices: 0x00369240[i] = character normal light matrix (+0x60) x bone i matrix.
void func_00221758(u32 c) {
    for (s32 i = 0; i < GRef<s16>(W(c + 0x10) + 0x20); ++i)
        sceVu0MulMatrix(0x00369240 + static_cast<u32>(i) * 0x40, c + 0x60,
                        W(W(c + 0x10) + 0x10) + static_cast<u32>(i) * 0xB0 + 0x20);
}

// EE: 0x00221C30
// Accumulates a bone matrix down its parent chain: walks up (+0xA4) while the bone is not
// accumulated (+0xA8 == 0) and has a parent (a root is marked accumulated), then multiplies each
// pushed bone by its parent's matrix from the top down (+0x20 = parent +0x20 x own) and marks it.
void func_00221C30(u32 bone) {
    u32 chain[64];  // sp+0 (0x100 bytes)
    s32 n = 0;
    u32 b = bone;
    for (;;) {
        if (W(b + 0xA4) == 0) {
            GRef<u32>(b + 0xA8) = 1;
            break;
        }
        if (W(b + 0xA8) != 0) break;
        chain[n++] = b;
        b = W(b + 0xA4);
    }
    u32 m = b + 0x20;
    for (s32 k = n - 1; k >= 0; --k) {
        const u32 s = chain[k];
        sceVu0MulMatrix(s + 0x20, m, s + 0x20);
        GRef<u32>(s + 0xA8) = 1;
        m = s + 0x20;
    }
}

// EE: 0x00221CF0
// m = unit, rotated by the rotation vector r (sceVu0RotMatrix), translated by t.
void func_00221CF0(u32 m, u32 r, u32 t) {
    sceVu0UnitMatrix(m);
    sceVu0RotMatrix(m, m, r);
    sceVu0TransMatrix(m, m, t);
}

// EE: 0x00221D48
// m = unit rotated about Z, X, Y (r.z, r.x, r.y), translated by t.
void func_00221D48(u32 m, u32 r, u32 t) {
    sceVu0UnitMatrix(m);
    sceVu0RotMatrixZ(m, m, GRef<f32>(r + 8));
    sceVu0RotMatrixX(m, m, GRef<f32>(r));
    sceVu0RotMatrixY(m, m, GRef<f32>(r + 4));
    sceVu0TransMatrix(m, m, t);
}

// EE: 0x00221DC0
// Order Y, Z, X.
void func_00221DC0(u32 m, u32 r, u32 t) {
    sceVu0UnitMatrix(m);
    sceVu0RotMatrixY(m, m, GRef<f32>(r + 4));
    sceVu0RotMatrixZ(m, m, GRef<f32>(r + 8));
    sceVu0RotMatrixX(m, m, GRef<f32>(r));
    sceVu0TransMatrix(m, m, t);
}

// EE: 0x00221E38
// Order Y, X, Z.
void func_00221E38(u32 m, u32 r, u32 t) {
    sceVu0UnitMatrix(m);
    sceVu0RotMatrixY(m, m, GRef<f32>(r + 4));
    sceVu0RotMatrixX(m, m, GRef<f32>(r));
    sceVu0RotMatrixZ(m, m, GRef<f32>(r + 8));
    sceVu0TransMatrix(m, m, t);
}

// EE: 0x00221EB0
// Order X, Z, Y.
void func_00221EB0(u32 m, u32 r, u32 t) {
    sceVu0UnitMatrix(m);
    sceVu0RotMatrixX(m, m, GRef<f32>(r));
    sceVu0RotMatrixZ(m, m, GRef<f32>(r + 8));
    sceVu0RotMatrixY(m, m, GRef<f32>(r + 4));
    sceVu0TransMatrix(m, m, t);
}

// EE: 0x00221F28
// Order X, Y, Z.
void func_00221F28(u32 m, u32 r, u32 t) {
    sceVu0UnitMatrix(m);
    sceVu0RotMatrixX(m, m, GRef<f32>(r));
    sceVu0RotMatrixY(m, m, GRef<f32>(r + 4));
    sceVu0RotMatrixZ(m, m, GRef<f32>(r + 8));
    sceVu0TransMatrix(m, m, t);
}

// EE: 0x00222800
// Root transform of character c: root bone (motion state +0xC) rotation +0 = rot, w 0, position
// +0x10 = pos, w 1.0, +0xA8 = -1 (set: 0x00222FA0 leaves it), matrix +0x20 built in rotation order
// `order` (jump table 0x00406B10: 0 sceVu0RotMatrix, 1 ZXY, 2 YZX, 3 YXZ, 4 XZY, 5 XYZ); other
// orders leave the matrix unchanged.
void func_00222800(u32 c, u32 rot, u32 pos, u32 order) {
    const u32 r = W(W(c + 0x10) + 0xC);
    GRef<f32>(r) = GRef<f32>(rot);
    GRef<s32>(r + 0xA8) = -1;
    GRef<f32>(r + 4) = GRef<f32>(rot + 4);
    GRef<u32>(r + 0xC) = 0;
    GRef<f32>(r + 8) = GRef<f32>(rot + 8);
    GRef<f32>(r + 0x10) = GRef<f32>(pos);
    GRef<f32>(r + 0x14) = GRef<f32>(pos + 4);
    GRef<f32>(r + 0x1C) = 1.0f;
    GRef<f32>(r + 0x18) = GRef<f32>(pos + 8);
    switch (order) {
    case 0: func_00221CF0(r + 0x20, r, r + 0x10); break;
    case 1: func_00221D48(r + 0x20, r, r + 0x10); break;
    case 2: func_00221DC0(r + 0x20, r, r + 0x10); break;
    case 3: func_00221E38(r + 0x20, r, r + 0x10); break;
    case 4: func_00221EB0(r + 0x20, r, r + 0x10); break;
    case 5: func_00221F28(r + 0x20, r, r + 0x10); break;
    default: break;
    }
}

// EE: 0x002228F8
// Scale matrix: m = diag(s.x, s.y, s.z, 1).
void func_002228F8(u32 m, u32 s) {
    for (u32 o = 0; o < 0x40; o += 4) GRef<u32>(m + o) = 0;
    GRef<f32>(m) = GRef<f32>(s);
    GRef<f32>(m + 0x14) = GRef<f32>(s + 4);
    GRef<f32>(m + 0x28) = GRef<f32>(s + 8);
    GRef<f32>(m + 0x3C) = 1.0f;
}

// EE: 0x00225840
// Blends matrices a and b by t (row k = a_k (1 - t) + b_k t; VU0 macro, `vmulaw` / `vmaddx`) and
// re-orthonormalises: X = r0.xyz / |r0.xyz| (w r0.w), Y = r1 - X (X . r1) normalised (w kept),
// Z = X x Y (w 0), translation row r3 as blended; r2 is not used.
void func_00225840(u32 out, u32 a, u32 b, f32 t) {
    const auto ld = [](u32 p) { return vu0::Vec4{GRef<f32>(p), GRef<f32>(p + 4), GRef<f32>(p + 8), GRef<f32>(p + 12)}; };
    const f32 u = vu0::Sub(1.0f, t);
    vu0::Vec4 r[4];
    for (u32 k = 0; k < 4; ++k) {
        const vu0::Vec4 va = ld(a + k * 0x10), vb = ld(b + k * 0x10);
        r[k] = {vu0::MulAdd(vu0::Mul(va.x, u), vb.x, t), vu0::MulAdd(vu0::Mul(va.y, u), vb.y, t),
                vu0::MulAdd(vu0::Mul(va.z, u), vb.z, t), vu0::MulAdd(vu0::Mul(va.w, u), vb.w, t)};
    }
    const auto len2 = [](const vu0::Vec4& v) {
        return vu0::Add(vu0::Add(vu0::Mul(v.x, v.x), vu0::Mul(v.y, v.y)), vu0::Mul(v.z, v.z));
    };
    f32 q = vu0::Div(1.0f, vu0::Sqrt(len2(r[0])));
    const vu0::Vec4 x{vu0::Mul(r[0].x, q), vu0::Mul(r[0].y, q), vu0::Mul(r[0].z, q), vu0::Add(0.0f, r[0].w)};
    const f32 d = vu0::Add(vu0::Add(vu0::Mul(r[1].x, x.x), vu0::Mul(r[1].y, x.y)), vu0::Mul(r[1].z, x.z));
    const vu0::Vec4 e{vu0::Sub(r[1].x, vu0::Mul(x.x, d)), vu0::Sub(r[1].y, vu0::Mul(x.y, d)),
                      vu0::Sub(r[1].z, vu0::Mul(x.z, d)), vu0::Sub(r[1].w, vu0::Mul(x.w, d))};
    q = vu0::Div(1.0f, vu0::Sqrt(len2(e)));
    const vu0::Vec4 y{vu0::Mul(e.x, q), vu0::Mul(e.y, q), vu0::Mul(e.z, q), vu0::Add(0.0f, e.w)};
    const vu0::Vec4 z{vu0::Sub(vu0::Mul(x.y, y.z), vu0::Mul(y.y, x.z)), vu0::Sub(vu0::Mul(x.z, y.x), vu0::Mul(y.z, x.x)),
                      vu0::Sub(vu0::Mul(x.x, y.y), vu0::Mul(y.x, x.y)), 0.0f};
    const auto st = [](u32 p, const vu0::Vec4& v) {
        GRef<f32>(p) = v.x;
        GRef<f32>(p + 4) = v.y;
        GRef<f32>(p + 8) = v.z;
        GRef<f32>(p + 12) = v.w;
    };
    st(out, x);
    st(out + 0x10, y);
    st(out + 0x20, z);
    st(out + 0x30, r[3]);
}

namespace {

// Interpolation search of the s16 key frames keys[0..n] (keys[n] the end) for `frame`: the key
// index s with keys[s] <= frame < keys[s + 1], guessing linearly between the bounds (start: 0 / 0
// and n / `frames`). Signed integer division; a zero divisor traps (break 7) in the original.
s32 FindKey(u32 keys, s32 n, s32 frame, s32 frames) {
    s32 lo = 0, lo_frame = 0, hi = n, hi_frame = frames;
    s32 rel = frame;
    s32 s = n * frame / frames;
    for (;;) {
        if (frame < GRef<s16>(keys + static_cast<u32>(s) * 2)) {
            hi = s;
            hi_frame = GRef<s16>(keys + static_cast<u32>(s) * 2);
        } else if (frame < GRef<s16>(keys + static_cast<u32>(s) * 2 + 2)) {
            return s;
        } else {
            lo = s + 1;
            lo_frame = GRef<s16>(keys + static_cast<u32>(lo) * 2);
            rel = frame - lo_frame;
        }
        s = lo + (hi - lo) * rel / (hi_frame - lo_frame);
    }
}

}  // namespace

// EE: 0x00222A18
// Evaluates slot k of motion state ms at its time (+0x130) into bone matrices at `out` (0x40
// each; the callers use scratchpad 0x70000000 / 0x70001000). Track block T = header +
// header+0x18: +2 bone count, +4 frame count, +8 root offset (xyz), +0x14 position scale, key data
// from +0x18. Frame f = (s16)(s32)time. Per bone, positions: n, keys[n + 1], n s16 triples;
// the key at f (no interpolation) as integers x scale; root bones (parent < 0) get the root
// offset added. Then per bone, rotations: n, keys[n + 1], n s16 triples (angles x 2pi / 65536,
// K 0x00410010 / 0x00410014): when the fraction t = (time - k0) / (k1 - k0) exceeds 1e-4 (K
// 0x0041000C) the rotation matrices of key s and s + 1 (wrapping to 0) are blended (0x00225840),
// else key s alone. Finally each matrix is translated by its bone position.
// fpucheck: 2 sub
void func_00222A18(u32 out, u32 ms, s32 k) {
    guest::GuestFrame frame(0x490);
    const u32 sp = frame.Address();
    const u32 offset = sp, ivec = sp + 0x10, fvec = sp + 0x20, ikeys = sp + 0x30, fkeys = sp + 0x40, pos = sp + 0x50,
              tmp = sp + 0x450;
    const u32 slot = ms + static_cast<u32>(k) * 0x38;
    u32 parents = W(ms + 0x14) + 0xC;
    const u32 mh = W(slot + 0x158);
    const f32 time = GRef<f32>(slot + 0x130);
    const s32 f = static_cast<s16>(fpu::FloatToInt(time));
    const u32 t = mh + W(mh + 0x18);
    const s32 bones = GRef<s16>(t + 2);
    const s32 frames = GRef<s16>(t + 4);
    GRef<f32>(offset) = GRef<f32>(t + 8);
    const f32 pscale = GRef<f32>(t + 0x14);
    GRef<s32>(ivec + 0xC) = 1;
    GRef<u32>(ikeys + 0xC) = 0;
    GRef<f32>(offset + 4) = GRef<f32>(t + 0xC);
    GRef<u32>(offset + 0xC) = 0;
    GRef<f32>(offset + 8) = GRef<f32>(t + 0x10);
    u32 d = t + 0x18;
    for (s32 b = 0; b < bones; ++b) {
        const s32 n = GRef<s16>(d);
        d += 2;
        const s32 s = FindKey(d, n, f, frames);
        d += static_cast<u32>(n) * 2 + 2;
        const u32 v = d + static_cast<u32>(s) * 6;
        GRef<s32>(ivec) = GRef<s16>(v);
        GRef<s32>(ivec + 4) = GRef<s16>(v + 2);
        GRef<s32>(ivec + 8) = GRef<s16>(v + 4);
        sceVu0ITOF0Vector(fvec, ivec);
        const u32 p = pos + static_cast<u32>(b) * 0x10;
        sceVu0ScaleVectorXYZ(p, fvec, pscale);
        if (GRef<s16>(parents) < 0) sceVu0AddVector(p, p, offset);
        parents += 2;
        d += static_cast<u32>(n) * 6;
    }
    for (s32 b = 0; b < bones; ++b) {
        const s32 n = GRef<s16>(d);
        d += 2;
        const s32 s = FindKey(d, n, f, frames);
        const u32 key = d + static_cast<u32>(s) * 2;
        GRef<s32>(ikeys) = GRef<s16>(key);
        GRef<s32>(ikeys + 4) = GRef<s16>(key + 2);
        sceVu0ITOF0Vector(fkeys, ikeys);
        const f32 k0 = GRef<f32>(fkeys);
        const f32 frac = fpu::Sub(time, k0) / fpu::Sub(GRef<f32>(fkeys + 4), k0);
        d += static_cast<u32>(n) * 2 + 2;
        const u32 m = out + static_cast<u32>(b) * 0x40;
        const auto angles = [&](s32 i, f32 scale) {
            const u32 v = d + static_cast<u32>(i) * 6;
            GRef<s32>(ikeys) = GRef<s16>(v);
            GRef<s32>(ikeys + 4) = GRef<s16>(v + 2);
            GRef<s32>(ikeys + 8) = GRef<s16>(v + 4);
            sceVu0ITOF0Vector(fkeys, ikeys);
            sceVu0ScaleVectorXYZ(fkeys, fkeys, scale);
        };
        if (GRef<f32>(0x0041000C) < frac) {
            const f32 scale = GRef<f32>(0x00410010);
            angles(s, scale);
            sceVu0UnitMatrix(m);
            sceVu0RotMatrix(m, m, fkeys);
            const s32 s1 = s + 1 == n ? 0 : s + 1;
            angles(s1, scale);
            sceVu0UnitMatrix(tmp);
            sceVu0RotMatrix(tmp, tmp, fkeys);
            func_00225840(m, m, tmp, frac);
        } else {
            angles(s, GRef<f32>(0x00410014));
            sceVu0UnitMatrix(m);
            sceVu0RotMatrix(m, m, fkeys);
        }
        d += static_cast<u32>(n) * 6;
    }
    for (s32 b = 0; b < bones; ++b)
        sceVu0TransMatrix(out + static_cast<u32>(b) * 0x40, out + static_cast<u32>(b) * 0x40, pos + static_cast<u32>(b) * 0x10);
}

// EE: 0x00222FA0
// Motion update of character c: slots grouped by state (0-5, up to 4 each); the first active slot
// (state 1) -> +0x22, evaluated into scratchpad 0x70000000; when a slot is blending in (state 2,
// first one -> +0x26): its time synced to the active fraction when both share a non-zero sync
// group, evaluated into 0x70001000 and blended into the active matrices by its weight +0x13C. The
// active time advances by its speed; the blending slot's time by its own speed, its weight losing
// weight / frames and its frame count +0x148 decreasing (speed > 0) or increasing (speed < 0) by
// the active speed; when the count reaches <= 0 that slot and all state-4 slots are freed. Then
// every slot of states 1-4 is clamped or wrapped (end flag +0x210 reset per slot, set to 1 / 2 as
// 0x00223518). Bone matrices = the evaluated ones, accumulated down the hierarchy (0x00221C30),
// the root matrix built from its rotation and position unless set by 0x00222800 (+0xA8 < 0), and
// every bone multiplied by the root.
// fpucheck: verified 9 (the two speed-sign branches of the blend: 3 ops each in the original, 4 shared calls here)
void func_00222FA0(u32 c) {
    const u32 ms = W(c + 0x10);
    u32 count[6] = {0, 0, 0, 0, 0, 0};  // sp+0..+0x14
    u32 list[6][4] = {};                // sp+0x20 (states 0-5, 4 entries)
    for (u32 k = 0; k < 4; ++k) {
        const s32 st = GRef<s16>(ms + k * 0x38 + 0x15E);
        list[st][count[st]] = k;
        ++count[st];
    }
    const u32 act = list[1][0];
    const auto slot = [ms](u32 k) { return ms + k * 0x38; };
    GRef<s16>(ms + 0x22) = static_cast<s16>(act);
    const f32 phase = GRef<f32>(slot(act) + 0x130) / GRef<f32>(slot(act) + 0x134);
    u32 blend = 0;
    if (count[2] != 0) {
        blend = list[2][0];
        GRef<s16>(ms + 0x26) = static_cast<s16>(blend);
        const s16 g = GRef<s16>(slot(act) + 0x160);
        const f32 len = GRef<f32>(slot(blend) + 0x134);
        if (g != 0 && g == GRef<s16>(slot(blend) + 0x160)) GRef<f32>(slot(blend) + 0x130) = len * phase;
    }
    func_00222A18(0x70000000, ms, static_cast<s32>(act));
    if (count[2] != 0) {
        func_00222A18(0x70001000, ms, static_cast<s32>(blend));
        if (count[2] != 0)
            for (s32 i = 0; i < GRef<s16>(ms + 0x20); ++i)
                func_00225840(0x70000000 + static_cast<u32>(i) * 0x40, 0x70000000 + static_cast<u32>(i) * 0x40,
                              0x70001000 + static_cast<u32>(i) * 0x40, GRef<f32>(slot(blend) + 0x13C));
    }
    {
        const u32 a = slot(act);
        const f32 speed = GRef<f32>(a + 0x138);
        GRef<f32>(a + 0x130) = fpu::Add(GRef<f32>(a + 0x130), speed);
        if (count[2] != 0) {
            const u32 b = slot(blend);
            if (0.0f < speed || speed < 0.0f) {
                const f32 w = GRef<f32>(b + 0x13C), frames = GRef<f32>(b + 0x148);
                const f32 step = w / frames;
                GRef<f32>(b + 0x130) = fpu::Add(GRef<f32>(b + 0x130), GRef<f32>(b + 0x138));
                GRef<f32>(b + 0x13C) = fpu::Sub(w, step);
                GRef<f32>(b + 0x148) = 0.0f < speed ? fpu::Sub(frames, GRef<f32>(a + 0x138)) : fpu::Add(frames, GRef<f32>(a + 0x138));
            }
            if (GRef<f32>(b + 0x148) <= 0.0f) {
                GRef<u16>(b + 0x15E) = 0;
                for (u32 i = 0; i < count[4]; ++i) GRef<u16>(slot(list[4][i]) + 0x15E) = 0;
            }
        }
    }
    for (u32 st = 1; st < 5; ++st) {
        for (u32 i = 0; i < count[st]; ++i) {
            GRef<u16>(ms + 0x210) = 0;
            const u32 s = slot(list[st][i]);
            if (GRef<s16>(s + 0x15C) != 0) {
                const f32 len = GRef<f32>(s + 0x134);
                if (len <= GRef<f32>(s + 0x130)) {
                    GRef<f32>(s + 0x130) = len;
                    if (GRef<s16>(s + 0x15E) == 1) GRef<u16>(ms + 0x210) = 1;
                }
                if (GRef<f32>(s + 0x130) < 0.0f) GRef<u32>(s + 0x130) = 0;
                continue;
            }
            if (GRef<f32>(s + 0x134) <= GRef<f32>(s + 0x130)) {
                if (GRef<s16>(s + 0x15E) == 1) GRef<u16>(ms + 0x210) = 2;
                f32 v;
                do {
                    const f32 len = GRef<f32>(s + 0x134);
                    v = fpu::Sub(GRef<f32>(s + 0x130), len);
                    GRef<f32>(s + 0x130) = v;
                    if (v < len) break;
                } while (true);
            }
            if (GRef<f32>(s + 0x130) < 0.0f) {
                if (GRef<s16>(s + 0x15E) == 1) GRef<u16>(ms + 0x210) = 2;
                const f32 len = GRef<f32>(s + 0x134);
                f32 v;
                do {
                    v = fpu::Add(len, GRef<f32>(s + 0x130));
                    GRef<f32>(s + 0x130) = v;
                } while (!(0.0f <= v));
            }
        }
    }
    for (s32 i = 0; i < GRef<s16>(ms + 0x20); ++i) {
        sceVu0CopyMatrix(W(ms + 0x10) + static_cast<u32>(i) * 0xB0 + 0x20, 0x70000000 + static_cast<u32>(i) * 0x40);
        GRef<u32>(W(ms + 0x10) + static_cast<u32>(i) * 0xB0 + 0xA8) = 0;
    }
    for (s32 i = 0; i < GRef<s16>(ms + 0x20); ++i) func_00221C30(W(ms + 0x10) + static_cast<u32>(i) * 0xB0);
    const u32 root = W(ms + 0xC);
    if (GRef<s32>(root + 0xA8) >= 0) func_00221CF0(root + 0x20, root, root + 0x10);
    for (s32 i = 0; i < GRef<s16>(ms + 0x20); ++i) {
        const u32 m = W(ms + 0x10) + static_cast<u32>(i) * 0xB0 + 0x20;
        sceVu0MulMatrix(m, W(ms + 0xC) + 0x20, m);
    }
}

// EE: 0x00221440
// Per drawn character: the motion update when the head's +0xDC is 0 (0x00222FA0), the bone light
// matrices (0x00221758), then (0x00221488) the bone and shadow matrices: model matrix M = root x
// (scale(c+0) x root^-1); a drop-shadow matrix S onto y = 0 for the light direction (first
// column of the light matrix +0x60), with the plane factor 1 / +0xE8 (or 1 / (root matrix
// translation y (+0x54) + +0xEC) when +0xE8 == 0); S = world->screen x S, T = view->screen x S. Per bone i (positions
// from header table +0x1C, 0x10 each, w 0): B = bone matrix with translation = its own
// translation (+0x50) - bone matrix x p (3 sub) and then M x B -> 0x0036A240[i]; world->screen x B -> 0x00367240[i];
// view->screen x that -> 0x00366240[i]; world->screen x bone matrix -> 0x00368240[i]; T x B ->
// 0x0036B240[i].
// fpucheck: 1 add, 3 sub
void func_00221440(u32 c) {
    if (GRef<s16>(W(c + 0xCC) + 0xDC) == 0) func_00222FA0(c);
    func_00221758(c);
    guest::GuestFrame frame(0xE0);
    const u32 sp = frame.Address();
    const u32 v = sp, m = sp + 0x10, s = sp + 0x50, t = sp + 0x90, lp = sp + 0xD0;
    const u32 ms = W(c + 0x10);
    func_002228F8(m, c);
    sceVu0InversMatrix(s, W(ms + 0xC) + 0x20);
    sceVu0MulMatrix(m, m, s);
    sceVu0MulMatrix(m, W(ms + 0xC) + 0x20, m);
    const f32 e8 = GRef<f32>(c + 0xE8);
    GRef<f32>(lp) = GRef<f32>(c + 0x60);
    GRef<f32>(lp + 4) = GRef<f32>(c + 0x70);
    GRef<f32>(lp + 8) = GRef<f32>(c + 0x80);
    GRef<f32>(lp + 0xC) = GRef<f32>(c + 0x90);
    u32 pp = W(ms + 0x1C);
    if (e8 == 0.0f) {
        const f32 h = fpu::Add(GRef<f32>(W(W(c + 0x10) + 0xC) + 0x54), GRef<f32>(c + 0xEC));
        sceVu0DropShadowMatrix(s, lp, 0.0f, 1.0f / h, 0.0f, 0);
    } else {
        sceVu0DropShadowMatrix(s, lp, 0.0f, 1.0f / e8, 0.0f, 0);
    }
    sceVu0MulMatrix(s, 0x00365F40, s);
    sceVu0MulMatrix(t, 0x00365F80, s);
    for (s32 i = 0; i < GRef<s16>(ms + 0x20); ++i) {
        const u32 bone = W(ms + 0x10) + static_cast<u32>(i) * 0xB0;
        const u32 o = static_cast<u32>(i) * 0x40;
        GRef<f32>(v) = GRef<f32>(pp);
        GRef<f32>(v + 4) = GRef<f32>(pp + 4);
        GRef<u32>(v + 0xC) = 0;
        GRef<f32>(v + 8) = GRef<f32>(pp + 8);
        pp += 0x10;
        sceVu0ApplyMatrix(v, bone + 0x20, v);
        const u32 b = 0x0036A240 + o;
        sceVu0CopyMatrix(b, bone + 0x20);
        GRef<f32>(b + 0x30) = fpu::Sub(GRef<f32>(bone + 0x50), GRef<f32>(v));
        GRef<f32>(b + 0x34) = fpu::Sub(GRef<f32>(bone + 0x54), GRef<f32>(v + 4));
        GRef<f32>(b + 0x38) = fpu::Sub(GRef<f32>(bone + 0x58), GRef<f32>(v + 8));
        sceVu0MulMatrix(b, m, b);
        sceVu0MulMatrix(0x00367240 + o, 0x00365F40, b);
        sceVu0MulMatrix(0x00366240 + o, 0x00365F80, 0x00367240 + o);
        sceVu0MulMatrix(0x00368240 + o, 0x00365F40, bone + 0x20);
        sceVu0MulMatrix(0x0036B240 + o, t, b);
    }
}

// Part draw buffers: 0x0036C240 screen positions (XYZF2 qwords), 0x00374240 colours (RGBAQ),
// 0x0037C240 environment-map UVs; bone matrix tables (0x40 per bone): 0x00366240 bone -> screen,
// 0x00367240 bone -> world-screen, 0x00368240 world-screen x bone (environment), 0x00369240 bone
// light, 0x0036A240 model, 0x0036B240 shadow -> screen.
// VU0 (program S, docs/subsystems/vu-programs.md): the EE side loads VU0 registers (lqc2 / ctc2),
// VIF0 DMA chains in scratchpad 0x70002200 unpack the vertex data into VU0 data memory, and
// vcallms runs one program entry per vertex (vu0hle::).

// EE: 0x00225048
// Skinning batch registers: vf12-vf15 = light colour matrix (4 qwords at a), vf11 = colour scale,
// vf10 = scales, vf9 = clip bounds, vf7 = fog, vf8 (not read by the program), VI04 / VI05 = the low
// 16 bits of kick / mask.
void func_00225048(u32 a, u32 colour, u32 scales, u32 bounds, u32 fog, u32 v8, u32 kick, u32 mask) {
    kick &= 0xFFFF;
    mask &= 0xFFFF;
    for (u32 k = 0; k < 4; ++k) vu0hle::LoadVf(12 + k, a + k * 0x10);
    vu0hle::LoadVf(11, colour);
    vu0hle::LoadVf(10, scales);
    vu0hle::LoadVf(9, bounds);
    vu0hle::LoadVf(7, fog);
    vu0hle::LoadVf(8, v8);
    vu0hle::SetVi(4, kick);
    vu0hle::SetVi(5, mask);
}

// EE: 0x00225088
// vf16-vf19 = matrix m.
void func_00225088(u32 m) {
    for (u32 k = 0; k < 4; ++k) vu0hle::LoadVf(16 + k, m + k * 0x10);
}

// EE: 0x002250A0
// vf16-vf31 = matrices m0..m3.
void func_002250A0(u32 m0, u32 m1, u32 m2, u32 m3) {
    const u32 m[4] = {m0, m1, m2, m3};
    for (u32 j = 0; j < 4; ++j)
        for (u32 k = 0; k < 4; ++k) vu0hle::LoadVf(16 + j * 4 + k, m[j] + k * 0x10);
}

namespace {

// The vcallms loops: per call one quadword vf1 (qmfc2.I waits for the program) stored at out.
// n is decremented before the test: n == 0 runs 2^32 times, as the original.
void RunEntry(u32 pair, u32 out, u32 n) {
    do {
        vu0hle::CallMs(pair);
        vu0hle::StoreVf(1, out);
        out += 0x10;
        --n;
    } while (n != 0);
}

}  // namespace

// EE: 0x002250E8
void func_002250E8(u32 out, u32 a, u32 n) {
    vu0hle::SetVi(1, a);
    RunEntry(0x00, out, n);
}

// EE: 0x00225110
void func_00225110(u32 out, u32 a, u32 w, u32 n) {
    vu0hle::SetVi(1, a);
    vu0hle::SetVi(3, w);
    RunEntry(0x02, out, n);
}

// EE: 0x00225140
void func_00225140(u32 out, u32 a, u32 b, u32 w, u32 n) {
    vu0hle::SetVi(1, a);
    vu0hle::SetVi(2, b);
    vu0hle::SetVi(3, w);
    RunEntry(0x06, out, n);
}

// EE: 0x00225178
void func_00225178(u32 out, u32 a, u32 n) {
    vu0hle::SetVi(1, a);
    RunEntry(0x08, out, n);
}

// EE: 0x002251A8
void func_002251A8(u32 out, u32 a, u32 w, u32 n) {
    vu0hle::SetVi(1, a);
    vu0hle::SetVi(3, w);
    RunEntry(0x0A, out, n);
}

// EE: 0x002251D8
void func_002251D8(u32 out, u32 a, u32 b, u32 w, u32 n) {
    vu0hle::SetVi(1, a);
    vu0hle::SetVi(2, b);
    vu0hle::SetVi(3, w);
    RunEntry(0x0E, out, n);
}

// EE: 0x00225210
void func_00225210(u32 out, u32 a, u32 n) {
    vu0hle::SetVi(1, a);
    RunEntry(0x10, out, n);
}

// EE: 0x00225240
void func_00225240(u32 out, u32 a, u32 w, u32 n) {
    vu0hle::SetVi(1, a);
    vu0hle::SetVi(3, w);
    RunEntry(0x12, out, n);
}

// EE: 0x00225270
void func_00225270(u32 out, u32 a, u32 b, u32 w, u32 n) {
    vu0hle::SetVi(1, a);
    vu0hle::SetVi(2, b);
    vu0hle::SetVi(3, w);
    RunEntry(0x16, out, n);
}

// EE: 0x00223CC0
// Waits while the DMA channel (register block from sceDmaGetChan) is busy (CHCR STR).
void func_00223CC0(u32 chan) {
    while ((sce::EeDmaReadChcr(chan) & 0x100) != 0) {
    }
}

// EE: 0x00223CE8
// Starts a chain transfer on the channel: TADR = tag, QWC = 0, CHCR = (CHCR & ~0xC) | 0x104
// (chain mode, STR; the other bits, TTE included, kept).
void func_00223CE8(u32 chan, u32 tag) {
    const u32 chcr = sce::EeDmaReadChcr(chan);
    sce::EeDmaWriteTadr(chan, tag);
    sce::EeDmaWriteQwc(chan, 0);
    sce::EeDmaWriteChcr(chan, (chcr & 0xFFFFFFF3u) | 0x104);
}

// EE: 0x00225408
// Uploads VU0 program S (.vutext 0x0029C200, a VIF0 chain with MPG) by VIF0 DMA: sync channel 0,
// D0_QWC = 0, D0_TADR = 0x0029C200, D_STAT = 1 (clears the channel 0 interrupt status), FlushCache(0),
// D0_CHCR = 0x145 (chain, TTE, STR), sync.
void UploadVu0Microcode() {
    const u32 e = 0x00365F00;
    sceDmaSync(W(e + 0x14), 0, 0);
    sce::EeHwWrite(sce::EeReg::kD0Qwc, 0);
    sce::EeHwWrite(sce::EeReg::kD0Tadr, 0x0029C200u & 0x0FFFFFFFu);
    sce::EeHwWrite(sce::EeReg::kDmacStat, 1);
    FlushCache(0);
    sce::EeHwWrite(sce::EeReg::kD0Chcr, 0x145);
    sceDmaSync(W(e + 0x14), 0, 0);
}

namespace {

// The common register set-up of the skinning functions: light colour matrix (character +0x20);
// vf11 = (255, 255, 255, 2048); vf10 = vf8 = scales (character +0xC0, then k1, z, w as given); vf9 =
// (k0, k0, 32, k2); vf7 = fog (255, 0, *0x00410D74, *0x00410D78) in 0x00384460; VI04 = 0x8000,
// VI05 = 0xD0.
void SkinRegisters(u32 c, u32 sp, f32 k0, f32 k1, f32 k2, bool zw, f32 z, f32 w) {
    const u32 fog = 0x00384460;
    GRef<f32>(fog) = 255.0f;
    GRef<f32>(sp) = GRef<f32>(c + 0xC0);
    GRef<f32>(sp + 4) = k1;
    GRef<f32>(sp + 0x14) = k0;
    GRef<f32>(sp + 0x18) = 32.0f;
    GRef<f32>(sp + 0x1C) = k2;
    GRef<f32>(sp + 0x2C) = 2048.0f;
    GRef<f32>(fog + 8) = GRef<f32>(0x00410D74);
    GRef<f32>(fog + 0xC) = GRef<f32>(0x00410D78);
    if (zw) {
        GRef<f32>(sp + 8) = z;
        GRef<f32>(sp + 0xC) = w;
    } else {
        GRef<u32>(sp + 8) = 0;
        GRef<u32>(sp + 0xC) = 0;
    }
    GRef<f32>(sp + 0x10) = k0;
    GRef<f32>(sp + 0x20) = 255.0f;
    GRef<f32>(sp + 0x24) = 255.0f;
    GRef<f32>(sp + 0x28) = 255.0f;
    GRef<u32>(fog + 4) = 0;
    func_00225048(c + 0x20, sp + 0x20, sp, sp + 0x10, fog, sp, 0x8000, 0xD0);
}

// Scratchpad DMA tags for VIF0 (0x70002200 + 0x10 k): word 0 = qwc | id, word 1 = address, word 3 =
// the VIF code sent with the tag (TTE).
void PutTag(u32 tag, u32 w0, u32 addr, u32 vif) {
    GRef<u32>(tag) = w0;
    GRef<u32>(tag + 0xC) = vif;
    GRef<u32>(tag + 4) = addr;
}

void ClearTags() {
    for (u32 k = 0; k < 4; ++k) {
        GRef<u64>(0x70002200 + k * 0x10) = 0;
        GRef<u64>(0x70002208 + k * 0x10) = 0;
    }
}

// Quadwords for n elements of `bytes` bytes, rounded up (s32 arithmetic as the original).
s32 Qwords(s32 n, s32 bytes) {
    const s32 b = n * bytes;
    return b / 16 + ((b & 0xF) != 0 ? 1 : 0);
}

// Bone indices of a weight group (4 s16 at rec, negative -> 0).
void GroupBones(u32 rec, s32 bones[4]) {
    for (u32 k = 0; k < 4; ++k) {
        const s32 b = GRef<s16>(rec + k * 2);
        bones[k] = b >= 0 ? b : 0;
    }
}

void LoadBoneMatrices(u32 table, const s32 bones[4]) {
    func_002250A0(table + static_cast<u32>(bones[0]) * 0x40, table + static_cast<u32>(bones[1]) * 0x40,
                  table + static_cast<u32>(bones[2]) * 0x40, table + static_cast<u32>(bones[3]) * 0x40);
}

}  // namespace

// EE: 0x00224180
// Skins a rigid part (type 0) on VU0 in batches of up to 128 vertices: positions (V3-16 from
// part +0x110, 0x300 bytes per batch) through bone 0 (0x00366240) with entry 0x00 -> xyz; normals
// (V3-16 from +0x114) through the bone light matrix (0x00369240) with entry 0x08 -> colours; when
// part +0x152: the environment UVs through 0x00368240 with entry 0x10 -> env. Constants K
// 0x00410018 / 0x0041001C / 0x00410020.
void func_00224180(u32 c, u32 part, u32 xyz, u32 col, u32 env) {
    guest::GuestFrame frame(0x30);
    const u32 sp = frame.Address();
    ClearTags();
    SkinRegisters(c, sp, GRef<f32>(0x00410018), GRef<f32>(0x0041001C), GRef<f32>(0x00410020), false, 0.0f, 0.0f);
    s32 left = GRef<u16>(part + 0x144);
    u32 pos = W(part + 0x110), nrm = W(part + 0x114);
    s32 n = 0x80, qwc = 0x30;
    while (left != 0) {
        if (left < 0x81) {
            n = left;
            qwc = Qwords(left, 6);
        }
        const u32 unpack = (static_cast<u32>(n) << 16) | 0x69000000u;
        const u32 ch0 = W(0x00365F14);
        PutTag(0x70002200, static_cast<u32>(qwc), pos, unpack);
        func_00223CE8(ch0, 0xF0002200);
        func_00225088(0x00366240);
        func_00223CC0(ch0);
        func_002250E8(xyz, 0, static_cast<u32>(n));
        xyz += static_cast<u32>(n) * 0x10;
        PutTag(0x70002230, static_cast<u32>(qwc), nrm, unpack);
        func_00223CE8(ch0, 0xF0002230);
        func_00225088(0x00369240);
        func_00223CC0(ch0);
        func_00225178(col, 0, static_cast<u32>(n));
        col += static_cast<u32>(n) * 0x10;
        if (GRef<s16>(part + 0x152) != 0) {
            func_00225088(0x00368240);
            func_00225210(env, 0, static_cast<u32>(n));
            env += static_cast<u32>(n) * 0x10;
        }
        pos += 0x300;
        nrm += 0x300;
        left -= n;
    }
}

// EE: 0x00224410
// Skins a weighted part (type 1): per weight group (part +0x142 groups of 0x20 bytes at +0x108: 4
// s16 bones, +8 count, +0x10 / +0x14 / +0x18 offsets of positions (from +0x110), weights (from the
// model data +0x10C) and normals (from +0x114)), in batches of up to 64: positions (V3-16, VU 0) and
// weights (V4-8 unsigned, VU 0x80) through the 4 bone matrices (0x00366240) with entry 0x02 ->
// xyz; normals through 0x00369240 with entry 0x0A -> colours; env (part +0x152) through 0x00368240
// with entry 0x12. Constants K 0x00410024 / 0x00410028 / 0x0041002C.
void func_00224410(u32 c, u32 part, u32 xyz, u32 col, u32 env) {
    guest::GuestFrame frame(0x30);
    const u32 sp = frame.Address();
    ClearTags();
    SkinRegisters(c, sp, GRef<f32>(0x00410024), GRef<f32>(0x00410028), GRef<f32>(0x0041002C), false, 0.0f, 0.0f);
    u32 rec = W(part + 0x108);
    for (s32 g = 0; g < GRef<u16>(part + 0x142); ++g, rec += 0x20) {
        u32 pos = W(part + 0x110) + W(rec + 0x10);
        u32 nrm = W(part + 0x114) + W(rec + 0x18);
        u32 wgt = W(rec + 0x14) + W(part + 0x10C);
        s32 bones[4];
        GroupBones(rec, bones);
        s32 left = GRef<u16>(rec + 8);
        s32 n = 0x40, qwc6 = 0x18, qwc4 = 0x10;
        while (left != 0) {
            if (left < 0x41) {
                n = left;
                qwc4 = Qwords(left, 4);
                qwc6 = Qwords(left, 6);
            }
            const u32 v3 = (static_cast<u32>(n) << 16) | 0x69000000u;
            const u32 v4 = (static_cast<u32>(n) << 16) | 0x6E004080u;
            const u32 ch0 = W(0x00365F14);
            PutTag(0x70002200, static_cast<u32>(qwc6) | 0x30000000u, pos, v3);
            PutTag(0x70002210, static_cast<u32>(qwc4), wgt, v4);
            func_00223CE8(ch0, 0xF0002200);
            LoadBoneMatrices(0x00366240, bones);
            func_00223CC0(ch0);
            func_00225110(xyz, 0, 0x80, static_cast<u32>(n));
            xyz += static_cast<u32>(n) * 0x10;
            PutTag(0x70002230, static_cast<u32>(qwc6), nrm, v3);
            func_00223CE8(ch0, 0xF0002230);
            LoadBoneMatrices(0x00369240, bones);
            sceDmaSync(ch0, 0, 0);
            func_002251A8(col, 0, 0x80, static_cast<u32>(n));
            col += static_cast<u32>(n) * 0x10;
            if (GRef<s16>(part + 0x152) != 0) {
                LoadBoneMatrices(0x00368240, bones);
                func_00225240(env, 0, 0x80, static_cast<u32>(n));
                env += static_cast<u32>(n) * 0x10;
            }
            wgt += 0x100;
            nrm += 0x180;
            left -= n;
            pos += 0x180;
        }
    }
}

// EE: 0x00224848
// Skins a morphing part (type 3): as type 1 with two key shapes A / B (positions from part +0x118 /
// +0x120, normals from +0x11C / +0x124, group offsets +0x10 / +0x18) blended by t = part +0x138:
// vf10 = (character +0xC0, K 0x00410030, 1 - t (computed in double: __extendsfdf2, __subdf3,
// __truncdfsf2), t); positions A (VU 0), B (VU 0x40) and weights (VU 0x80) with entry 0x06; normals
// with entry 0x0E; env with entry 0x16. Constants K 0x00410034 / 0x00410038. The +0x118..+0x124
// fields are set only by dead code (0x00223E70, docs/port/dead-code.md).
void func_00224848(u32 c, u32 part, u32 xyz, u32 col, u32 env) {
    guest::GuestFrame frame(0x30);
    const u32 sp = frame.Address();
    ClearTags();
    const f32 t = GRef<f32>(part + 0x138);
    const f32 u = static_cast<f32>(1.0 - static_cast<double>(t));
    SkinRegisters(c, sp, GRef<f32>(0x00410034), GRef<f32>(0x00410030), GRef<f32>(0x00410038), true, u,
                  GRef<f32>(part + 0x138));
    u32 rec = W(part + 0x108);
    for (s32 g = 0; g < GRef<u16>(part + 0x142); ++g, rec += 0x20) {
        u32 pa = W(part + 0x118) + W(rec + 0x10);
        u32 pb = W(part + 0x120) + W(rec + 0x10);
        u32 na = W(part + 0x11C) + W(rec + 0x18);
        u32 nb = W(part + 0x124) + W(rec + 0x18);
        u32 wgt = W(rec + 0x14) + W(part + 0x10C);
        s32 bones[4];
        GroupBones(rec, bones);
        s32 left = GRef<u16>(rec + 8);
        s32 n = 0x40, qwc6 = 0x18, qwc4 = 0x10;
        while (left != 0) {
            if (left < 0x41) {
                n = left;
                qwc4 = Qwords(left, 4);
                qwc6 = Qwords(left, 6);
            }
            const u32 ref6 = static_cast<u32>(qwc6) | 0x30000000u;
            const u32 va = (static_cast<u32>(n) << 16) | 0x69000000u;
            const u32 vb = (static_cast<u32>(n) << 16) | 0x40u | 0x69000000u;
            const u32 vw = (static_cast<u32>(n) << 16) | 0x6E004080u;
            const u32 ch0 = W(0x00365F14);
            PutTag(0x70002200, ref6, pa, va);
            PutTag(0x70002210, ref6, pb, vb);
            PutTag(0x70002220, static_cast<u32>(qwc4), wgt, vw);
            func_00223CE8(ch0, 0xF0002200);
            LoadBoneMatrices(0x00366240, bones);
            sceDmaSync(ch0, 0, 0);
            func_00225140(xyz, 0, 0x40, 0x80, static_cast<u32>(n));
            xyz += static_cast<u32>(n) * 0x10;
            PutTag(0x70002200, ref6, na, va);
            PutTag(0x70002210, static_cast<u32>(qwc6), nb, vb);
            func_00223CE8(ch0, 0xF0002200);
            LoadBoneMatrices(0x00369240, bones);
            sceDmaSync(ch0, 0, 0);
            func_002251D8(col, 0, 0x40, 0x80, static_cast<u32>(n));
            col += static_cast<u32>(n) * 0x10;
            if (GRef<s16>(part + 0x152) != 0) {
                LoadBoneMatrices(0x00368240, bones);
                func_00225270(env, 0, 0x40, 0x80, static_cast<u32>(n));
                env += static_cast<u32>(n) * 0x10;
            }
            pa += 0x180;
            pb += 0x180;
            na += 0x180;
            nb += 0x180;
            wgt += 0x100;
            left -= n;
        }
    }
}

// EE: 0x00224D50
// Shadow positions of a part: per weight group as type 1 (positions and weights) through the
// shadow matrices 0x0036B240 with entry 0x02 -> xyz. Constants K 0x0041003C / 0x00410040 /
// 0x00410044.
void func_00224D50(u32 c, u32 part, u32 xyz) {
    guest::GuestFrame frame(0x30);
    const u32 sp = frame.Address();
    ClearTags();
    SkinRegisters(c, sp, GRef<f32>(0x0041003C), GRef<f32>(0x00410040), GRef<f32>(0x00410044), false, 0.0f, 0.0f);
    u32 rec = W(part + 0x108);
    for (s32 g = 0; g < GRef<u16>(part + 0x142); ++g, rec += 0x20) {
        u32 pos = W(part + 0x110) + W(rec + 0x10);
        u32 wgt = W(rec + 0x14) + W(part + 0x10C);
        s32 bones[4];
        GroupBones(rec, bones);
        s32 left = GRef<u16>(rec + 8);
        s32 n = 0x40, qwc6 = 0x18, qwc4 = 0x10;
        while (left != 0) {
            if (left < 0x41) {
                n = left;
                qwc4 = Qwords(left, 4);
                qwc6 = Qwords(left, 6);
            }
            const u32 ch0 = W(0x00365F14);
            PutTag(0x70002200, static_cast<u32>(qwc6) | 0x30000000u, pos, (static_cast<u32>(n) << 16) | 0x69000000u);
            PutTag(0x70002210, static_cast<u32>(qwc4), wgt, (static_cast<u32>(n) << 16) | 0x6E004080u);
            pos += 0x180;
            wgt += 0x100;
            left -= n;
            func_00223CE8(ch0, 0xF0002200);
            LoadBoneMatrices(0x0036B240, bones);
            func_00223CC0(ch0);
            func_00225110(xyz, 0, 0x80, static_cast<u32>(n));
            xyz += static_cast<u32>(n) * 0x10;
        }
    }
}

// EE: 0x002236A0
// Skins the parts of character c, last to first, by type (0 -> 0x00224180, 1 -> 0x00224410, 3 ->
// 0x00224848, others not skinned), then emits each part's strips (0x002238E0) and, when part
// +0x152, the environment pass (0x00223B80). Buffers: xyz 0x0036C240, colours 0x00374240, env
// 0x0037C240.
void func_002236A0(u32 c) {
    const u32 xyz = 0x0036C240, col = 0x00374240, env = 0x0037C240;
    for (s32 i = GRef<s32>(c + 0xA0) - 1; i >= 0; --i) {
        const u32 p = W(c + 0xC4) + static_cast<u32>(i) * 0x160;
        const u16 type = GRef<u16>(p + 0x140);
        const u32 uv = W(p + 0x128);
        const u32 strips = W(p + 0x12C);
        if (type == 0)
            func_00224180(c, p, xyz, col, env);
        else if (type == 1)
            func_00224410(c, p, xyz, col, env);
        else if (type == 3)
            func_00224848(c, p, xyz, col, env);
        const u32 n = GRef<u16>(p + 0x14A);
        func_002238E0(c, i, strips, uv, col, xyz, env, n);
        if (GRef<s16>(W(c + 0xC4) + static_cast<u32>(i) * 0x160 + 0x152) != 0)
            func_00223B80(c, i, strips, uv, col, xyz, env, GRef<u16>(p + 0x14A));
    }
}

// EE: 0x00223830
// Shadow of set `set`: per part, last to first, the shadow positions (0x00224D50 into
// 0x0036C240) and the shadow strips (0x00223A58).
void func_00223830(u32 set) {
    for (s32 i = GRef<s32>(set + 0xA0) - 1; i >= 0; --i) {
        const u32 p = W(set + 0xC4) + static_cast<u32>(i) * 0x160;
        const u32 strips = W(p + 0x12C);
        func_00224D50(set, p, 0x0036C240);
        func_00223A58(set, i, strips, 0x0036C240, GRef<u16>(p + 0x14A));
    }
}

namespace {

// Strip walk shared by the three emitters: strips of 8-byte vertex entries {u16 a, u16 b, u16 c,
// u16 count/flag}; a strip's first entry +6 is its vertex count. After a strip the s16 +6 of its
// last entry decides: >= 0 -> next strip regardless of the strip counter, < 0 -> next strip while
// the counter (decremented) is positive (as the original). `kick` counts down the vertices that
// get ADC (bit 15 of the XYZF2 w) after a vertex flagged by the program (set to 3 by a flagged
// vertex, kept across strips).
template <typename Vertex>
u32 WalkStrips(u32 q, u32 e, s32 strips, u64 tag_hi, u64 regs, Vertex vertex) {
    if (strips <= 0) return q;
    s32 kick = 0;
    for (;;) {
        u32 v = GRef<u16>(e + 6);
        GRef<u64>(q + 8) = regs;
        GRef<u64>(q) = static_cast<u64>(v | 0x8000u) | tag_hi;
        q += 0x10;
        u32 n = GRef<u16>(e + 6);
        const s32 next = strips - 1;
        if (static_cast<s32>(n) > 0) {
            do {
                q = vertex(q, e, kick);
                --n;
                e += 8;
            } while (static_cast<s32>(n) > 0);
        }
        strips = next;
        if (GRef<s16>(e - 2) >= 0) continue;
        if (next > 0) continue;
        return q;
    }
}

}  // namespace

// EE: 0x002238E0
// Emits part i of c into the drawing list *0x00410D40 bucket 0: the part register list (+0x150
// qwords from part +0), then per strip a PACKED GIF tag (PRE, PRIM 0x37C: textured, fogged,
// blended tri-strip, context 2; NREG 3: RGBAQ, UV, XYZF2) and per vertex the colour (entry +0 of
// `col`), the UV (8 bytes of entry +2 of `uv`, the upper half of the qword left as it was) and the
// position (entry +4 of `xyz`); a vertex whose position w has bit 15 (out of bounds) starts a run
// of 3 vertices with ADC set. The packet is closed (CloseDmaPacket) and linked (LinkPacket).
void func_002238E0(u32 c, s32 i, u32 strips, u32 uv, u32 col, u32 xyz, u32 env, u32 n) {
    (void)env;
    const u32 start = Ctx()->cur_buf.addr;
    const u32 part = W(c + 0xC4) + static_cast<u32>(i) * 0x160;
    u32 q = start + 0x10;
    for (s32 k = 0; k < GRef<s16>(W(c + 0xC4) + static_cast<u32>(i) * 0x160 + 0x150); ++k) {
        CopyQ(q, part + static_cast<u32>(k) * 0x10);
        q += 0x10;
    }
    q = WalkStrips(q, strips, static_cast<s32>(n), 0xC6F9ull << 46, 0x431, [&](u32 o, u32 e, s32& kick) {
        CopyQ(o, col + static_cast<u32>(GRef<u16>(e)) * 0x10);
        o += 0x10;
        GRef<u64>(o) = GRef<u64>(uv + static_cast<u32>(GRef<u16>(e + 2)) * 8);
        o += 0x10;
        const u32 p = xyz + static_cast<u32>(GRef<u16>(e + 4)) * 0x10;
        CopyQ(o, p);
        if ((W(p + 0xC) & 0x8000) != 0) kick = 3;
        if (kick != 0) {
            GRef<u32>(o + 0xC) = 0x8000;
            --kick;
        }
        return o + 0x10;
    });
    LinkPacket(W(0x00410D40), 0, start, CloseDmaPacket(start, q));
}

// EE: 0x00223A58
// Shadow strips of part i into list *0x00410D44 bucket 0: the draw-environment template
// (0x00384340, *0x00410D68 qwords), per strip a PACKED tag (PRE, PRIM 0x344: blended tri-strip,
// context 2; NREG 1: XYZF2) and per vertex the position (entry +4 of `xyz`), ADC as 0x002238E0.
void func_00223A58(u32 c, s32 i, u32 strips, u32 xyz, u32 n) {
    (void)c;
    (void)i;
    const u32 start = Ctx()->cur_buf.addr;
    u32 q = start + 0x10;
    for (s32 k = 0; k < GRef<s32>(0x00410D68); ++k) {
        CopyQ(q, 0x00384340 + static_cast<u32>(k) * 0x10);
        q += 0x10;
    }
    q = WalkStrips(q, strips, static_cast<s32>(n), 0x8D12ull << 45, 4, [&](u32 o, u32 e, s32& kick) {
        const u32 p = xyz + static_cast<u32>(GRef<u16>(e + 4)) * 0x10;
        CopyQ(o, p);
        if ((W(p + 0xC) & 0x8000) != 0) kick = 3;
        if (kick != 0) {
            GRef<u32>(o + 0xC) = 0x8000;
            --kick;
        }
        return o + 0x10;
    });
    LinkPacket(W(0x00410D44), 0, start, CloseDmaPacket(start, q));
}

// EE: 0x00223B80
// Environment pass of part i into list *0x00410D44 bucket 0: the texture template (0x00384240,
// *0x00410D60 qwords), per strip a PACKED tag (PRE, PRIM 0x35C: textured, blended tri-strip, context
// 2; NREG 3: NOP, UV, XYZF2) and per vertex a NOP qword (not written, as the original), the UV (8
// bytes of entry +0 of `env`) and the position (entry +4 of `xyz`); here any non-zero position w
// starts the ADC run.
void func_00223B80(u32 c, s32 i, u32 strips, u32 uv, u32 col, u32 xyz, u32 env, u32 n) {
    (void)c;
    (void)i;
    (void)uv;
    (void)col;
    const u32 start = Ctx()->cur_buf.addr;
    u32 q = start + 0x10;
    for (s32 k = 0; k < GRef<s32>(0x00410D60); ++k) {
        CopyQ(q, 0x00384240 + static_cast<u32>(k) * 0x10);
        q += 0x10;
    }
    q = WalkStrips(q, strips, static_cast<s32>(n), 0xC6B9ull << 46, 0x43F, [&](u32 o, u32 e, s32& kick) {
        o += 0x10;
        GRef<u64>(o) = GRef<u64>(env + static_cast<u32>(GRef<u16>(e)) * 0x10);
        o += 0x10;
        const u32 p = xyz + static_cast<u32>(GRef<u16>(e + 4)) * 0x10;
        CopyQ(o, p);
        if (W(p + 0xC) != 0) kick = 3;
        if (kick != 0) {
            GRef<u32>(o + 0xC) = 0x8000;
            --kick;
        }
        return o + 0x10;
    });
    LinkPacket(W(0x00410D44), 0, start, CloseDmaPacket(start, q));
}

// ---- Dead character-engine functions (slice S8; dead-code.md: no reference, or only from dead
// code: the motion transition variants 0x00222050 / 0x00222100, the vertex morph 0x00223D08, the
// free path 0x00220BA0, the matrix interpolation 0x002219D8 and the devkit file loader 0x00225308).

namespace {

vu0::Vec4 LoadQ(u32 p) { return {GRef<f32>(p), GRef<f32>(p + 4), GRef<f32>(p + 8), GRef<f32>(p + 12)}; }

void StoreQ(u32 p, const vu0::Vec4& v) {
    GRef<f32>(p) = v.x;
    GRef<f32>(p + 4) = v.y;
    GRef<f32>(p + 8) = v.z;
    GRef<f32>(p + 12) = v.w;
}

// VU max: orders the float bit patterns (-0 below +0).
f32 VuMax(f32 a, f32 b) {
    if (a == b) return std::signbit(a) ? b : a;
    return a < b ? b : a;
}

u32 FBits(f32 v) {
    u32 b;
    std::memcpy(&b, &v, sizeof(b));
    return b;
}

f32 FFromBits(u32 b) {
    f32 v;
    std::memcpy(&v, &b, sizeof(v));
    return v;
}

// Sign bit test of a float as an integer register (bgez / bltz on its bits: -0 counts as negative).
bool SignBit(f32 v) { return std::signbit(v); }

// Bit mask of the part indices of a morph block: OR of 1 << (u16 +0x10 + i x 0x20) (32-bit shifts,
// sign-extended into the 64-bit mask as the EE does).
u64 MorphPartMask(u32 block) {
    u64 mask = 0;
    const u32 n = GRef<u16>(block + 2);
    for (u32 i = 0; i < n; ++i) {
        const u32 bit = 1u << (GRef<u16>(block + 0x10 + i * 0x20) & 0x1F);
        mask |= static_cast<u64>(static_cast<s64>(static_cast<s32>(bit)));
    }
    return mask;
}

// Morph block selection: entry `a` of the morph table when its first dword is set, else entry `b`
// when set, else none (0).
u32 SelectMorphBlock(u32 table, s32 a, s32 b) {
    const u32 ea = func_002252B0(table, a);
    const u32 eb = func_002252B0(table, b);
    if (GRef<u64>(ea) != 0) return ea;
    if (GRef<u64>(eb) != 0) return eb;
    return 0;
}

}  // namespace

// EE: 0x002208E0
void func_002208E0() {}

// EE: 0x002208E8
// Sets the position (+0, a quadword) of character c and of every character linked after it (+0xC8).
void func_002208E8(u32 c, u32 pos) {
    for (; c != 0; c = W(c + 0xC8)) CopyQ(c, pos);
}

// EE: 0x00220918
// Sets u16 +0xDE on character c and on every character linked after it.
void func_00220918(u32 c, u16 v) {
    for (; c != 0; c = W(c + 0xC8)) GRef<u16>(c + 0xDE) = v;
}

// EE: 0x00220968
// World position of bone `bone` of character c: bone point +0x50 through scale x inverse(root) x
// root (the root matrix of the motion state), w 1.
void func_00220968(u32 c, s32 bone, u32 out) {
    guest::GuestFrame frame(0x90);
    const u32 m = frame.Address();
    const u32 inv = frame.Address(0x40);
    const u32 v = frame.Address(0x80);
    func_002228F8(m, c);
    sceVu0InversMatrix(inv, W(W(c + 0x10) + 0xC) + 0x20);
    sceVu0MulMatrix(m, m, inv);
    sceVu0MulMatrix(m, W(W(c + 0x10) + 0xC) + 0x20, m);
    sceVu0ApplyMatrix(v, m, W(W(c + 0x10) + 0x10) + static_cast<u32>(bone) * 0xB0 + 0x50);
    GRef<f32>(out) = GRef<f32>(v);
    GRef<f32>(out + 0xC) = 1.0f;
    GRef<f32>(out + 4) = GRef<f32>(v + 4);
    GRef<f32>(out + 8) = GRef<f32>(v + 8);
}

// EE: 0x00220AF0
// Head mask +0xD8.
void func_00220AF0(u32 c, s16 mask) { GRef<s32>(c + 0xD8) = mask; }

// EE: 0x00220B80
// First character from c on (+0xC8 chain) whose part-set bit (+0xD4) lies inside `mask` (no end test,
// as the original).
u32 func_00220B80(u32 c, u32 mask) {
    for (;;) {
        const u32 bit = W(c + 0xD4);
        if ((mask & bit) == bit) return c;
        c = W(c + 0xC8);
    }
}

// EE: 0x00220BA0
// Frees the pages of character c and of its attached set (+0xE4).
void func_00220BA0(u32 c) {
    if (c == 0) return;
    if (W(c + 0xE4) != 0) func_00225648(W(c + 0xE4));
    func_00225648(c);
}

// EE: 0x002213B0
// Stores the texture-upload template pointer (dword 0x00384250).
void func_002213B0(u32 p) { GRef<u64>(0x00384250) = static_cast<u64>(static_cast<s64>(static_cast<s32>(p))); }

// EE: 0x002218C0
// Binds motion data to motion state ms as 0x002217E8 with the two tables given (`t1`, `t2`) and the
// bone parents from the s16 table hdr+2 (+4 is not set).
void func_002218C0(u32 ms, u32 hdr, u32 t1, u32 t2, u32 work) {
    GRef<u32>(ms + 0x18) = t1;
    GRef<u32>(ms + 0x1C) = t2;
    GRef<u16>(ms + 0x20) = GRef<u16>(hdr);
    GRef<u32>(ms + 0x14) = hdr;
    for (s32 k = 3; k >= 0; --k) GRef<u16>(ms + 0x15E + static_cast<u32>(k) * 0x38) = 0;
    GRef<u32>(ms + 0xC) = work;
    GRef<u32>(ms + 0x10) = work + 0xB0;
    const u32 bones = work + 0xB0;
    for (s32 i = 0; i < GRef<s16>(ms + 0x20); ++i) {
        const s32 parent = GRef<s16>(hdr + 2 + static_cast<u32>(i) * 2);
        GRef<u32>(bones + static_cast<u32>(i) * 0xB0 + 0xA4) = parent == -1 ? 0 : bones + static_cast<u32>(parent) * 0xB0;
    }
}

// EE: 0x00221978
// out = translate(rotation `rot` (Euler, sceVu0RotMatrix), `pos`).
void func_00221978(u32 rot, u32 pos, u32 out) {
    guest::GuestFrame frame(0x40);
    const u32 m = frame.Address();
    sceVu0UnitMatrix(m);
    sceVu0RotMatrix(m, m, rot);
    sceVu0TransMatrix(out, m, pos);
}

// EE: 0x002219D8
// Interpolates from matrix a towards b by t: the rotation between them as axis and angle
// (0x00225C38), then 0x00225970.
void func_002219D8(u32 out, u32 a, u32 b, f32 t) {
    guest::GuestFrame frame(0x10);
    func_00225C38(frame.Address(), a, b);
    func_00225970(out, a, b, frame.Address(), t);
}

// EE: 0x00221A40
// Blends the `n` matrices of a and b (0x40 bytes each) whose bit is set in `mask` into out: copies a
// when t < 0.01, b when t > 0.99 (compared as doubles, libgcc __cmpdf2 with the constants
// 0x00406AD0 / 0x00406AD8), else 0x00225840.
void func_00221A40(u32 out, u32 a, u32 b, s32 n, u64 mask, f32 t) {
    const auto rodata_double = [](u32 addr) {
        const u64 bits = GRef<u64>(addr);
        double d;
        std::memcpy(&d, &bits, sizeof(d));
        return d;
    };
    const double td = static_cast<double>(t);
    if (td < rodata_double(0x00406AD0)) {
        for (; 0 < n; --n, mask >>= 1, a += 0x40, out += 0x40) {
            if ((mask & 1) != 0) sceVu0CopyMatrix(out, a);
        }
        return;
    }
    if (rodata_double(0x00406AD8) < td) {
        for (; 0 < n; --n, mask >>= 1, b += 0x40, out += 0x40) {
            if ((mask & 1) != 0) sceVu0CopyMatrix(out, b);
        }
        return;
    }
    for (; 0 < n; --n, mask >>= 1, a += 0x40, b += 0x40, out += 0x40) {
        if ((mask & 1) != 0) func_00225840(out, a, b, t);
    }
}

// EE: 0x00221BB0
// Copies the x, y, z of the first three rows of m (w kept).
void func_00221BB0(u32 out, u32 m) {
    for (u32 r = 0; r < 3; ++r) {
        GRef<u64>(out + r * 0x10) = GRef<u64>(m + r * 0x10);
        GRef<u32>(out + r * 0x10 + 8) = GRef<u32>(m + r * 0x10 + 8);
    }
}

// EE: 0x00221BE8
// out = inverse(a) x b.
void func_00221BE8(u32 out, u32 a, u32 b) {
    guest::GuestFrame frame(0x40);
    sceVu0InversMatrix(frame.Address(), a);
    sceVu0MulMatrix(out, frame.Address(), b);
}

// EE: 0x00222050
// Starts motion n with the transition rule of 0x002222C0 on the first visible character of c's set
// (as 0x00221FA0).
void func_00222050(u32 c, s32 n) {
    const u32 s = func_00220B40(c);
    if (s == 0) return;
    if (GRef<s16>(W(c + 0x10) + 0x20) == 0) return;
    const u32 a = func_002252B0(W(W(s + 0x10)), n);
    const u32 b = func_002252B0(W(W(s + 0x10)), n + 1);
    if (a != b && n != 0) {
        func_002222C0(W(s + 0x10), static_cast<s16>(n));
        return;
    }
    ee::printf(0x00406AE0, nullptr, 0);  // "Error!! This motiondata doesn't exist.\n"
}

// EE: 0x00222100
// The same with transition n of the table +0x12C (0x002224E8).
void func_00222100(u32 c, s32 n) {
    const u32 s = func_00220B40(c);
    if (s == 0) return;
    if (GRef<s16>(W(c + 0x10) + 0x20) == 0) return;
    const u32 a = func_002252B0(W(W(s + 0x10)), n);
    const u32 b = func_002252B0(W(W(s + 0x10)), n + 1);
    if (a != b && n != 0) {
        func_002224E8(W(s + 0x10), n);
        return;
    }
    ee::printf(0x00406AE0, nullptr, 0);
}

// EE: 0x002222C0
// Motion change with blending: active slots of motion n's header with a blend time (+0x50) and
// +0x4C go to state 2 (blending out, weight 1, +0x164 - 2 when >= 3; +0x26 = that slot), the others
// stop; then the first free slot starts motion n (as 0x002221B0, weight 0, blend frames of the
// outgoing slot +0x148 from +0x4C or a quarter of the length, frame sync with the outgoing slot).
// fpucheck: 1 sub (clamped length)
void func_002222C0(u32 ms, s16 n) {
    GRef<s16>(ms + 8) = n;
    const u32 h = func_002252B0(W(ms), n);
    for (s32 k = 0; k < 4; ++k) {
        const u32 s = ms + static_cast<u32>(k) * 0x38;
        const s16 st = GRef<s16>(s + 0x15E);
        if (st == 1) {
            if (GRef<s16>(h + 0x50) == 0 || GRef<s16>(h + 0x4C) == 0) {
                GRef<u16>(s + 0x15E) = 0;
                continue;
            }
            GRef<u16>(s + 0x15E) = 2;
            GRef<f32>(s + 0x13C) = 1.0f;
            GRef<u16>(s + 0x162) = 0;
            if (!(GRef<s16>(s + 0x164) < 3)) GRef<u16>(s + 0x164) = static_cast<u16>(GRef<u16>(s + 0x164) - 2);
            GRef<s16>(ms + 0x26) = static_cast<s16>(k);
        } else if (st == 2) {
            GRef<u16>(s + 0x15E) = 0;
        }
    }
    for (s32 k = 0; k < 4; ++k) {
        const u32 s = ms + static_cast<u32>(k) * 0x38;
        if (GRef<s16>(s + 0x15E) != 0) continue;
        GRef<u16>(s + 0x15E) = 1;
        const u16 clamp = GRef<u16>(h + 0x48);
        GRef<u64>(s + 0x150) = 0xFFFFFFFFFFFFFFFFull;
        GRef<u16>(s + 0x15C) = clamp;
        GRef<f32>(s + 0x138) = 1.0f;
        GRef<u32>(s + 0x158) = h;
        GRef<u16>(s + 0x160) = GRef<u16>(h + 0x4A);
        const f32 len = fpu::IntToFloat(GRef<s16>(h + 2));
        GRef<f32>(s + 0x134) = len;
        if (clamp != 0) GRef<f32>(s + 0x134) = fpu::Sub(len, 1.0f);
        if (GRef<f32>(s + 0x134) <= 0.0f) {
            GRef<u32>(s + 0x134) = 0;
            GRef<u16>(s + 0x160) = 0;
        }
        GRef<u32>(s + 0x13C) = 0;
        GRef<u16>(s + 0x162) = GRef<u16>(h + 0x50);
        GRef<u32>(s + 0x130) = 0;
        GRef<u16>(s + 0x164) = GRef<s16>(h + 0x52) != 0 ? static_cast<u16>(GRef<u16>(h + 0x52) + 2) : 0;
        const s16 frames = GRef<s16>(h + 0x4C);
        const f32 blend = frames < 0 ? GRef<f32>(s + 0x134) * 0.25f : fpu::IntToFloat(frames);
        const u16 out_slot = GRef<u16>(ms + 0x26);
        GRef<f32>(ms + static_cast<u32>(GRef<s16>(ms + 0x26)) * 0x38 + 0x148) = blend;
        const s16 group = GRef<s16>(s + 0x160);
        if (group != 0) {
            const u32 o = ms + static_cast<u32>(static_cast<s16>(out_slot)) * 0x38;
            if (GRef<s16>(o + 0x160) == group)
                GRef<f32>(s + 0x130) = GRef<f32>(s + 0x134) * (GRef<f32>(o + 0x130) / GRef<f32>(o + 0x134));
        }
        GRef<s16>(ms + 0x22) = static_cast<s16>(k);
        return;
    }
}

// EE: 0x002224E8
// Motion change by transition n of the table +0x12C (0x18 bytes: motion, clamp, sync group, -, -,
// in / out blend frames, frame +0xA, +0xC): slots in state 3 stop, the first free slot starts the
// motion in state 3 (weight 0, +0x140 = 1, blend frames +0x144 / +0x148 from the entry or a quarter
// of the length), frame sync with the active slot.
// fpucheck: 1 sub (clamped length)
void func_002224E8(u32 ms, s32 n) {
    for (u32 k = 0; k < 4; ++k) {
        const u32 s = ms + k * 0x38;
        if (GRef<s16>(s + 0x15E) == 3) GRef<u16>(s + 0x15E) = 0;
    }
    for (s32 k = 0; k < 4; ++k) {
        const u32 s = ms + static_cast<u32>(k) * 0x38;
        if (GRef<s16>(s + 0x15E) != 0) continue;
        const u32 e = W(ms + 0x12C) + static_cast<u32>(n) * 0x18;
        GRef<u16>(s + 0x15E) = 3;
        GRef<u64>(s + 0x150) = 0xFFFFFFFFFFFFFFFFull;
        GRef<f32>(s + 0x138) = 1.0f;
        GRef<u16>(s + 0x15C) = GRef<u16>(e + 2);
        GRef<u16>(ms + 8) = GRef<u16>(e);
        const u32 h = func_002252B0(W(ms), GRef<s16>(e));
        GRef<u32>(s + 0x158) = h;
        const u32 e2 = W(ms + 0x12C) + static_cast<u32>(n) * 0x18;
        GRef<u16>(s + 0x160) = GRef<u16>(e2 + 4);
        const f32 len = fpu::IntToFloat(GRef<s16>(h + 2));
        GRef<f32>(s + 0x134) = len;
        if (GRef<s16>(s + 0x15C) != 0) GRef<f32>(s + 0x134) = fpu::Sub(len, 1.0f);
        if (GRef<f32>(s + 0x134) <= 0.0f) {
            GRef<f32>(s + 0x134) = 0.0f;
            GRef<u16>(s + 0x160) = 0;
        }
        GRef<f32>(s + 0x130) = 0.0f;
        GRef<u16>(s + 0x162) = GRef<u16>(e2 + 0xA);
        GRef<f32>(s + 0x140) = 1.0f;
        GRef<f32>(s + 0x13C) = 0.0f;
        GRef<u16>(s + 0x164) = GRef<s16>(e2 + 0xC) != 0 ? static_cast<u16>(GRef<u16>(e2 + 0xC) + 2) : 0;
        const s16 in_frames = GRef<s16>(e2 + 6);
        GRef<f32>(s + 0x144) = in_frames < 0 ? GRef<f32>(s + 0x134) * 0.25f : fpu::IntToFloat(in_frames);
        const s16 out_frames = GRef<s16>(e2 + 8);
        GRef<f32>(s + 0x148) = out_frames < 0 ? GRef<f32>(s + 0x134) * 0.25f : fpu::IntToFloat(out_frames);
        const s16 group = GRef<s16>(s + 0x160);
        if (group != 0) {
            const u32 o = ms + static_cast<u32>(GRef<s16>(ms + 0x22)) * 0x38;
            if (GRef<s16>(o + 0x160) == group)
                GRef<f32>(s + 0x130) = GRef<f32>(s + 0x134) * (GRef<f32>(o + 0x130) / GRef<f32>(o + 0x134));
        }
        GRef<s16>(ms + 0x22) = static_cast<s16>(k);
        return;
    }
}

// EE: 0x00222720
// End flag (+0x210) of the motion state of the first visible character of c's set (0 if none).
s16 func_00222720(u32 c) {
    const u32 s = func_00220B40(c);
    if (s == 0) return 0;
    return GRef<s16>(W(s + 0x10) + 0x210);
}

// EE: 0x00222750
// Length (+0x134) of the active slot of the first visible character of c's set (0 if none).
f32 func_00222750(u32 c) {
    if (func_00220B40(c) == 0) return 0.0f;
    const u32 ms = W(func_00220B40(c) + 0x10);
    return GRef<f32>(ms + static_cast<u32>(GRef<s16>(ms + 0x22)) * 0x38 + 0x134);
}

// EE: 0x002227A8
// Time (+0x130) of that slot.
f32 func_002227A8(u32 c) {
    if (func_00220B40(c) == 0) return 0.0f;
    const u32 ms = W(func_00220B40(c) + 0x10);
    return GRef<f32>(ms + static_cast<u32>(GRef<s16>(ms + 0x22)) * 0x38 + 0x130);
}

// EE: 0x002228E0
// Root record of c's motion state: not evaluated (+0xA8 = -1), matrix = m.
void func_002228E0(u32 c, u32 m) {
    const u32 root = W(W(c + 0x10) + 0xC);
    GRef<s32>(root + 0xA8) = -1;
    sceVu0CopyMatrix(root + 0x20, m);
}

// EE: 0x00222950
// Orthonormalises m's rows 0-2 from src (Gram-Schmidt: X = |src0|, Y = |src1 - X (X . src1)|, Z = X x Y)
// and copies src's row 3.
void func_00222950(u32 m, u32 src) {
    guest::GuestFrame frame(0x10);
    const u32 tmp = frame.Address();
    sceVu0Normalize(m, src);
    const f32 d = sceVu0InnerProduct(src + 0x10, m);
    sceVu0ScaleVector(tmp, m, d);
    sceVu0SubVector(tmp, src + 0x10, tmp);
    sceVu0Normalize(m + 0x10, tmp);
    sceVu0OuterProduct(m + 0x20, m, m + 0x10);
    sceVu0CopyVector(m + 0x30, src + 0x30);
}

// EE: 0x002229F0
// Clears `n` quadwords (at least one).
void func_002229F0(u32 p, s32 n) {
    do {
        ZeroQ(p);
        p += 0x10;
        --n;
    } while (n != 0);
}

// EE: 0x00223680
// Speed (+0x138) of the active slot of c's motion state.
void func_00223680(u32 c, f32 v) {
    const u32 ms = W(c + 0x10);
    GRef<f32>(ms + static_cast<u32>(GRef<s16>(ms + 0x22)) * 0x38 + 0x138) = v;
}

// EE: 0x00223D08
// Vertex morph of character c for this frame (0x00223E70); when no part was morphed the morph parts
// go back to rigid (0x00223E20); the morphed-part mask is cleared.
void func_00223D08(u32 c) {
    func_00223E70(c);
    if (GRef<u64>(W(c + 0xE0)) == 0) func_00223E20(c);
    GRef<u64>(W(c + 0xE0)) = 0;
}

// EE: 0x00223D50
void func_00223D50() {}

// EE: 0x00223D60
// Part mask of morph block `a` (or `b` when `a` is empty) of c's morph table +0xAC; 0 when none.
u64 func_00223D60(u32 c, s32 a, s32 b) {
    if (W(c + 0xAC) == 0) return 0;
    const u32 block = SelectMorphBlock(W(c + 0xAC), a, b);
    if (block == 0) return 0;
    return MorphPartMask(block);
}

// EE: 0x00223E20
// Parts of type 3 (morph) go back to type 1.
void func_00223E20(u32 c) {
    for (s32 k = 0; k < GRef<s32>(c + 0xA0); ++k) {
        const u32 part = W(c + 0xC4) + static_cast<u32>(k) * 0x160;
        if (GRef<u16>(part + 0x140) == 3) GRef<u16>(part + 0x140) = 1;
    }
}

// EE: 0x00223E70
// Morph keys of the active motion slot at its current frame (record table header +0x14: count, then
// per frame `count` triples {block a, block b, weight / 255}) through 0x00223F58.
void func_00223E70(u32 c) {
    const u32 ms = W(c + 0x10);
    const u32 slot = ms + static_cast<u32>(GRef<s16>(ms + 0x22)) * 0x38;
    const u32 frame = fpu::FixUnsSfSi(GRef<f32>(slot + 0x130));
    const u32 h = W(ms + static_cast<u32>(GRef<s16>(ms + 0x22)) * 0x38 + 0x158);
    const u32 off = W(h + 0x14);
    if (off == 0) return;
    u32 p = h + off;
    const u32 count = GRef<u8>(p);
    p += frame * count * 3 + 1;
    if (count == 0) return;
    for (u32 k = 0; k < count; ++k, p += 3) {
        const f32 w = static_cast<f32>(static_cast<s32>(GRef<u8>(p + 2))) / 255.0f;
        func_00223F58(c, GRef<u8>(p), GRef<u8>(p + 1), w);
    }
}

// EE: 0x00223F58
// Morph between blocks `a` and `b` of the morph table with weight w: when none of its parts were
// morphed this frame (mask +0xE0), for every character of c's set in the head mask the parts of the
// block become type 3 with their two key vertex lists (+0x118 / +0x11C from a, +0x120 / +0x124 from
// b) and weight +0x138.
void func_00223F58(u32 c, s32 a, s32 b, f32 w) {
    u32 ea = func_002252B0(W(c + 0xAC), a);
    u32 eb = func_002252B0(W(c + 0xAC), b);
    const auto pick = [](u32& first, u32& second) {
        if (GRef<u64>(first) != 0) {
            if (GRef<u64>(second) == 0) second = first;
            return true;
        }
        if (GRef<u64>(second) == 0) return false;
        first = second;
        return true;
    };
    if (!pick(ea, eb)) return;
    const u64 mask = MorphPartMask(ea);
    const u32 acc = W(c + 0xE0);
    if ((GRef<u64>(acc) & mask) != 0) return;
    GRef<u64>(acc) = GRef<u64>(acc) | mask;
    for (u32 s = c; s != 0;) {
        const u32 next = W(s + 0xC8);
        if ((W(c + 0xD8) & W(s + 0xD4)) == 0) {
            s = next;
            continue;
        }
        if (W(s + 0xAC) == 0) return;
        u32 sa = func_002252B0(W(s + 0xAC), a);
        u32 sb = func_002252B0(W(s + 0xAC), b);
        if (!pick(sa, sb)) return;
        const u32 parts = W(s + 0xC4);
        for (u32 i = 0; i < GRef<u16>(sa + 2); ++i) {
            const u32 ka = sa + 0x10 + i * 0x20;
            const u32 kb = sb + 0x10 + i * 0x20;
            const u32 pa = parts + GRef<u16>(ka) * 0x160u;
            GRef<u16>(pa + 0x140) = 3;
            GRef<u32>(pa + 0x118) = sa + W(ka + 8);
            GRef<u32>(pa + 0x11C) = sa + W(ka + 0xC);
            GRef<f32>(pa + 0x138) = w;
            const u32 pb = parts + GRef<u16>(kb) * 0x160u;
            GRef<u32>(pb + 0x124) = sb + W(kb + 0xC);
            GRef<u32>(pb + 0x120) = sb + W(kb + 8);
        }
        s = next;
    }
}

// EE: 0x00225308
// Devkit file loader: reads file `name` from the host ("sim..." names become "host0:" + name+4)
// into `buf`; returns its size, or -1 ("File not found.").
s32 func_00225308(u32 name, u32 buf) {
    const u32 path = 0x0042D0C0;
    if (ee::strncmp(name, 0x00410DA8 /* "sim" */, 3) == 0) {
        GRef<u32>(path) = W(0x00410DB0);  // "host0:"
        GRef<u16>(path + 4) = GRef<u16>(0x00410DB4);
        GRef<u8>(path + 6) = GRef<u8>(0x00410DB6);
        ee::strcat(path, name + 4);
    } else {
        ee::strcpy(path, name);
    }
    const s32 fd = sceOpen(path, 1);
    if (fd < 0) {
        ee::printf(0x00406B28, nullptr, 0);  // "File not found.\n"
        return -1;
    }
    const s32 size = sceLseek(fd, 0, 2);
    sceLseek(fd, 0, 0);
    sceRead(fd, buf, size);
    sceClose(fd);
    FlushCache(0);
    return size;
}

// EE: 0x002254F8
// Prints and returns the number of used pages of the page table ("UseMem= %x/%x  MaxUnit=%x").
s32 func_002254F8() {
    s32 used = 0;
    u32 last = 0;
    for (u32 i = 0; i < 0x200; ++i) {
        if (GRef<s8>(0x00366040 + i) == 0) continue;
        ++used;
        if (last < i) last = i;
    }
    const u64 args[3] = {static_cast<u64>(used), 0x200, last};
    ee::printf(0x00406B40, args, 3);
    return used;
}

// EE: 0x00225648
// Frees the page run starting at `addr` (the run length is the first page's entry).
void func_00225648(u32 addr) {
    u32 e = 0x00366040 + ((addr - W(0x00410D50)) >> 12);
    s32 n = GRef<s8>(e);
    if (!(0 < n)) return;
    do {
        GRef<u8>(e) = 0;
        --n;
        ++e;
    } while (n != 0);
}

// EE: 0x00225750
// 0x00225840 for each of `n` matrix pairs (0x40 bytes apart) while the low 16 bits of the shifted
// mask are not all clear (the test is `mask & 0xFFFF`, then mask >>= 1; at least one pass).
void func_00225750(u32 out, u32 a, u32 b, s32 n, u64 mask, f32 t) {
    do {
        const u64 test = mask & 0xFFFF;
        mask >>= 1;
        if (test != 0) func_00225840(out, a, b, t);
        out += 0x40;
        a += 0x40;
        b += 0x40;
        --n;
    } while (n != 0);
}

// EE: 0x00225908
// Linear blend of the four rows of a and b by t (VU0 macro: a (1 - t) + b t), no re-orthonormalising.
void func_00225908(u32 out, u32 a, u32 b, f32 t) {
    const f32 u = vu0::Sub(1.0f, t);
    for (u32 k = 0; k < 4; ++k) {
        const vu0::Vec4 va = LoadQ(a + k * 0x10), vb = LoadQ(b + k * 0x10);
        StoreQ(out + k * 0x10, {vu0::MulAdd(vu0::Mul(va.x, u), vb.x, t), vu0::MulAdd(vu0::Mul(va.y, u), vb.y, t),
                                vu0::MulAdd(vu0::Mul(va.z, u), vb.z, t), vu0::MulAdd(vu0::Mul(va.w, u), vb.w, t)});
    }
}

// EE: 0x00225970
// out = rotation(axis q.xyz, angle q.w x t) x a (translation cleared), translation = a's + (b's -
// a's) x t. q.w is scaled in place.
void func_00225970(u32 out, u32 a, u32 b, u32 q, f32 t) {
    guest::GuestFrame frame(0x90);
    const u32 rot = frame.Address();
    const u32 m = frame.Address(0x40);
    const u32 pos = frame.Address(0x80);
    GRef<f32>(q + 0xC) = GRef<f32>(q + 0xC) * t;
    func_00225A88(rot, q);
    sceVu0CopyMatrix(m, a);
    GRef<f32>(pos) = GRef<f32>(a + 0x30);
    GRef<f32>(pos + 4) = GRef<f32>(a + 0x34);
    GRef<f32>(pos + 8) = GRef<f32>(a + 0x38);
    GRef<f32>(pos + 0xC) = GRef<f32>(a + 0x3C);
    GRef<u32>(m + 0x30) = 0;
    GRef<u32>(m + 0x34) = 0;
    GRef<u32>(m + 0x38) = 0;
    GRef<u32>(m + 0x3C) = 0;
    sceVu0MulMatrix(out, rot, m);
    for (u32 i = 0; i < 4; ++i) {
        const f32 p = GRef<f32>(pos + i * 4);
        GRef<f32>(out + 0x30 + i * 4) = fpu::Add(p, fpu::Sub(GRef<f32>(b + 0x30 + i * 4), p) * t);
    }
}

// EE: 0x00225A88
// Rotation matrix about axis q.xyz by angle q.w (polynomial sine / cosine 0x002260D0 / 0x00226130):
// c I + s [axis]x + (1 - c) axis axis^T in rows 0-2, row 3 of the unit matrix.
// fpucheck: verified 10 (1 sub, 9 adds)
void func_00225A88(u32 m, u32 q) {
    guest::GuestFrame frame(0x40);
    const u32 xx = frame.Address();
    const u32 yy = frame.Address(0x10);
    const u32 zz = frame.Address(0x20);
    const u32 sv = frame.Address(0x30);
    sceVu0UnitMatrix(m);
    const f32 s = func_002260D0(GRef<f32>(q + 0xC));
    sceVu0ScaleVector(sv, q, s);
    const f32 c = func_00226130(GRef<f32>(q + 0xC));
    sceVu0ScaleVector(xx, q, GRef<f32>(q));
    sceVu0ScaleVector(yy, q, GRef<f32>(q + 4));
    sceVu0ScaleVector(zz, q, GRef<f32>(q + 8));
    const f32 sx = GRef<f32>(sv), sy = GRef<f32>(sv + 4), sz = GRef<f32>(sv + 8);
    GRef<f32>(m) = c;
    GRef<f32>(m + 0x14) = c;
    GRef<f32>(m + 0x28) = c;
    const f32 k = fpu::Sub(1.0f, c);
    GRef<f32>(m + 8) = -sy;
    GRef<f32>(m + 0x10) = -sz;
    GRef<f32>(m + 0x20) = sy;
    GRef<f32>(m + 0x24) = -sx;
    GRef<f32>(m + 4) = sz;
    GRef<f32>(m + 0x18) = sx;
    sceVu0ScaleVector(xx, xx, k);
    sceVu0ScaleVector(yy, yy, k);
    sceVu0ScaleVector(zz, zz, k);
    for (u32 r = 0; r < 3; ++r) {
        for (u32 i = 0; i < 3; ++i) {
            const u32 at = m + r * 0x10 + i * 4;
            GRef<f32>(at) = fpu::Add(GRef<f32>(at), GRef<f32>(xx + r * 0x10 + i * 4));
        }
    }
}

// EE: 0x00225C38
// Rotation from matrix a to matrix b as axis and angle (out.xyz axis, out.w angle; all zero when the
// rotation is too small, K 0x0041004C): M = a^T(3x3) b, the axis from the largest cross product of
// two rows of M - I, the angle from the rotated basis through an atan polynomial (0x00384470).
// VU0 macro code with MMI transposes; the zero tests read the registers as integers (a negative
// cosine also selects the |q| > 1 branch, -0 counts as negative), as the original.
void func_00225C38(u32 out, u32 a, u32 b) {
    using vu0::Vec4;
    const Vec4 r0 = LoadQ(a), r1 = LoadQ(a + 0x10), r2 = LoadQ(a + 0x20);
    const Vec4 c0{r0.x, r1.x, r2.x, 0.0f}, c1{r0.y, r1.y, r2.y, 0.0f}, c2{r0.z, r1.z, r2.z, 0.0f};
    const Vec4 b0 = LoadQ(b), b1 = LoadQ(b + 0x10), b2 = LoadQ(b + 0x20), b3 = LoadQ(b + 0x30);
    const auto comb = [&](const Vec4& v) { return vu0::MulMatrix(b0, b1, b2, b3, v); };
    Vec4 m0 = comb(c0), m1 = comb(c1), m2 = comb(c2), m3 = comb({0.0f, 0.0f, 0.0f, 0.0f});
    Vec4 v1 = m0, v2 = m1, v3 = m2;
    v1.x = vu0::Sub(v1.x, 1.0f);
    v2.y = vu0::Sub(v2.y, 1.0f);
    v3.z = vu0::Sub(v3.z, 1.0f);
    const auto cr = [](f32 ay, f32 bz, f32 by, f32 az) { return vu0::Sub(vu0::Mul(ay, bz), vu0::Mul(by, az)); };
    // Components of the three cross products, regrouped as the original does (vmr32).
    const f32 c23x = cr(v2.y, v3.z, v3.y, v2.z), c23y = cr(v2.z, v3.x, v3.z, v2.x), c23z = cr(v2.x, v3.y, v3.x, v2.y);
    const f32 c31x = cr(v3.y, v1.z, v1.y, v3.z), c31y = cr(v3.z, v1.x, v1.z, v3.x), c31z = cr(v3.x, v1.y, v1.x, v3.y);
    const f32 c12x = cr(v1.y, v2.z, v2.y, v1.z), c12y = cr(v1.z, v2.x, v2.z, v1.x), c12z = cr(v1.x, v2.y, v2.x, v1.y);
    const f32 f1a = VuMax(VuMax(std::fabs(c23z), std::fabs(c23x)), std::fabs(c23y));
    const f32 f2 = VuMax(VuMax(std::fabs(c31x), std::fabs(c31y)), std::fabs(c31z));
    const f32 f3 = VuMax(VuMax(std::fabs(c12y), std::fabs(c12z)), std::fabs(c12x));
    f32 f1 = f1a;
    Vec4 p, q;
    if (f1 < f2) {
        f1 = f2;
        if (f1 < f3) {
            f1 = f3;
            p = v1;
            q = v2;
        } else {
            p = v3;
            q = v1;
        }
    } else if (f1 < f3) {
        f1 = f3;
        p = v1;
        q = v2;
    } else {
        p = v2;
        q = v3;
    }
    p.w = 1.0f;
    if (f1 < GRef<f32>(0x0041004C)) {
        StoreQ(out, {0.0f, 0.0f, 0.0f, 0.0f});
        return;
    }
    const auto cross = [&](const Vec4& x, const Vec4& y) {
        return Vec4{cr(x.y, y.z, y.y, x.z), cr(x.z, y.x, y.z, x.x), cr(x.x, y.y, y.x, x.y), 0.0f};
    };
    const auto unit = [](const Vec4& v) {
        const f32 l2 = vu0::Add(vu0::Add(vu0::Mul(v.x, v.x), vu0::Mul(v.y, v.y)), vu0::Mul(v.z, v.z));
        const f32 k = vu0::Div(1.0f, vu0::Sqrt(l2));
        return Vec4{vu0::Mul(v.x, k), vu0::Mul(v.y, k), vu0::Mul(v.z, k), 0.0f};
    };
    const Vec4 axis = unit(cross(p, q));
    const Vec4 e0 = unit(axis);
    const Vec4 e1 = unit(p);
    const Vec4 e2 = unit(cross(e0, e1));
    const Vec4 e3{0.0f, 0.0f, 0.0f, 1.0f};
    // n_k = M e_k, then the basis' transpose times n_k.
    const auto mrow = [&](const Vec4& v) { return vu0::MulMatrix(m0, m1, m2, m3, v); };
    const Vec4 n0 = mrow(e0), n1 = mrow(e1), n2 = mrow(e2), n3 = mrow(e3);
    const Vec4 t0{e0.x, e1.x, e2.x, e3.x}, t1{e0.y, e1.y, e2.y, e3.y}, t2{e0.z, e1.z, e2.z, e3.z}, t3{e0.w, e1.w, e2.w, e3.w};
    (void)n0;
    (void)n2;
    (void)n3;
    const Vec4 k1 = vu0::MulMatrix(t0, t1, t2, t3, n1);
    const f32 s = k1.z;  // sine
    const f32 c = k1.y;  // cosine
    const Vec4 coef = LoadQ(0x00384470);
    f32 angle;
    if (FBits(s) == 0) {
        angle = 0.0f;
    } else if (FBits(c) == 0) {
        angle = FFromBits(0x3FC90FDBu);  // pi / 2
    } else {
        const f32 r = std::fabs(vu0::Add(0.0f, vu0::Div(s, c)));
        const f32 over = VuMax(vu0::Sub(r, 1.0f), 0.0f);
        const bool big = FBits(over) != 0 || SignBit(c);
        const f32 u = big ? vu0::Add(0.0f, vu0::Div(vu0::Add(vu0::Sub(0.0f, r), 1.0f), vu0::Add(r, 1.0f))) : r;
        const f32 m = vu0::Sub(0.0f, vu0::Mul(u, u));
        f32 term = u;
        f32 sum = u;
        const f32 k[4] = {coef.x, coef.y, coef.z, coef.w};
        for (u32 i = 0; i < 4; ++i) {
            term = vu0::Mul(term, m);
            sum = vu0::Add(sum, vu0::Mul(term, k[i]));
        }
        angle = big ? vu0::Sub(FFromBits(0x3F490FD8u), sum) : sum;
    }
    if (SignBit(c)) angle = vu0::Sub(FFromBits(0x40490FD8u), angle);
    if (SignBit(s)) angle = vu0::Sub(0.0f, angle);
    StoreQ(out, {axis.x, axis.y, axis.z, vu0::Mul(1.0f, angle)});
}

// EE: 0x002260D0
// Polynomial sine on VU0: x + x(-x^2)c0 + ... (4 terms, coefficients 0x00384480).
f32 func_002260D0(f32 x) {
    const vu0::Vec4 k = LoadQ(0x00384480);
    const f32 m = vu0::Sub(0.0f, vu0::Mul(x, x));
    const f32 c[4] = {k.x, k.y, k.z, k.w};
    f32 term = x;
    f32 sum = x;
    for (u32 i = 0; i < 4; ++i) {
        term = vu0::Mul(vu0::Mul(term, m), c[i]);
        sum = vu0::Add(sum, term);
    }
    return sum;
}

// EE: 0x00226130
// Polynomial cosine on VU0: 1 + (-x^2)c0 + ... (4 terms, coefficients 0x00384490).
f32 func_00226130(f32 x) {
    const vu0::Vec4 k = LoadQ(0x00384490);
    const f32 m = vu0::Sub(0.0f, vu0::Mul(x, x));
    const f32 c[4] = {k.x, k.y, k.z, k.w};
    f32 term = vu0::Add(0.0f, 1.0f);
    f32 sum = term;
    for (u32 i = 0; i < 4; ++i) {
        term = vu0::Mul(vu0::Mul(term, m), c[i]);
        sum = vu0::Add(sum, term);
    }
    return sum;
}

AC04_GUEST_FUNCTION(0x00222050, func_00222050);
AC04_GUEST_FUNCTION(0x00222100, func_00222100);

}  // namespace ac04::game
