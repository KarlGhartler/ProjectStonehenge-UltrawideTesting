// Menu 3D objects and light sets (render module; docs/port/s4-inventory.md §3): the aircraft models
// of the hangar and the aircraft viewer. A menu object (0xD0 bytes) holds a model instance, its
// world matrices, the VU1 program packet it draws with, a texture and optional extras (glow model,
// effect list); DrawMenuObject builds its VIF1 packets with EmitModelInstance (model.cpp).
//
// Menu object: +0 radius, +4 triangle count, +8 node count, +0x10 full world matrix, +0x50 rotation
// matrix, +0x90 model instance, +0x94 glow model instance, +0x98 VU1 program packet (.vutext block:
// u16 qword count, then the DMA data at +0x10), +0x9C mode (0 program B, 1 program A, 2 program C),
// +0xA0 texture record, +0xA4 effect list, +0xA8 effect count, +0xAC alpha (byte), +0xB0 flags (bit
// 0: clipping off), +0xB4 ?, +0xB8 frame PSM, +0xC0 Z PSM.
//
// Light set (0x100 bytes): +0x00 negated light directions (4 x 16), +0x40 environment texture
// record, +0x50 light matrix (direction i in column... row i: x at +0x50 + 4i, y +0x60, z +0x70, w
// +0x80), +0x90 light colours (4 x 16, /128), +0xD0 ambient (/128), +0xE0 VIF UNPACK header (9
// qwords to VU 0x0C), +0xF0 DMA cnt + MSCAL.
#include "game/game.h"

#include <cstring>

#include "common/ee_math.h"
#include "common/guest_call.h"
#include "common/guest_stack.h"
#include "game/main_context.h"
#include "sce/sce.h"

namespace ac04::game {

using guest::GRef;

namespace {

void Put(u32 p, u32 i, u32 v) { GRef<u32>(p + 4 * i) = v; }

void CopyQword(u32 dst, u32 src) {
    for (u32 i = 0; i < 16; i += 4) GRef<u32>(dst + i) = GRef<u32>(src + i);
}

// RGBA bytes of `rgba` as floats (cvt.s.w of each byte: gcc's unsigned conversion, whose
// halve-and-double path is never taken for bytes) scaled by 1/128.
void ColourToFloats(u32 rgba, u32 out) {
    GRef<f32>(out) = fpu::IntToFloat(static_cast<s32>(rgba & 0xFF));
    GRef<f32>(out + 4) = fpu::IntToFloat(static_cast<s32>((rgba >> 8) & 0xFF));
    GRef<f32>(out + 8) = fpu::IntToFloat(static_cast<s32>((rgba >> 16) & 0xFF));
    GRef<f32>(out + 12) = fpu::IntToFloat(static_cast<s32>(rgba >> 24));
    sceVu0ScaleVector(out, out, 0.0078125f);
}

}  // namespace

// EE: 0x00136AA0
// Clears the light matrix and colours, ambient 0.5, no texture, and the VIF header (UNPACK V4-32 9
// qwords to VU 0x0C, then a DMA cnt with MSCAL 0).
void InitLightSet(u32 ls) {
    for (u32 o = 0x50; o < 0xA0; o += 4) GRef<u32>(ls + o) = 0;
    for (int k = 0; k < 3; ++k)
        for (u32 o = 0x90; o < 0xA0; o += 4) GRef<u32>(ls + o) = 0;
    GRef<u32>(ls + 0xDC) = 0x3F000000;
    GRef<u32>(ls + 0xE0) = 0x10000009;
    GRef<u32>(ls + 0xEC) = 0x6C09800C;
    GRef<u32>(ls + 0xF0) = 0x10000000;
    GRef<u32>(ls + 0xFC) = 0x14000000;
    GRef<u32>(ls + 0x40) = 0;
    GRef<u32>(ls + 0xD0) = 0x3F000000;
    GRef<u32>(ls + 0xD4) = 0x3F000000;
    GRef<u32>(ls + 0xD8) = 0x3F000000;
    GRef<u32>(ls + 0xE4) = 0;
    GRef<u32>(ls + 0xE8) = 0;
    GRef<u32>(ls + 0xF4) = 0;
    GRef<u32>(ls + 0xF8) = 0;
}

// EE: 0x00136DA0
// Light `index`: negated direction at +index*16, direction into row `index` of the light matrix,
// colour (RGBA bytes / 128) at +0x90 + index*16.
// fpucheck: verified 4 (the add.s of gcc's unsigned convert, never taken for bytes)
void SetLight(u32 ls, u32 dir, u32 rgba, s32 index) {
    const u32 i16 = static_cast<u32>(index) * 16;
    sceVu0ScaleVector(ls + i16, dir, -1.0f);
    guest::GuestFrame frame(0x20);
    const u32 d = frame.Address(), c = d + 0x10;
    sceVu0CopyVector(d, dir);
    ColourToFloats(rgba, c);
    const u32 row = ls + static_cast<u32>(index) * 4;
    GRef<u32>(row + 0x50) = GRef<u32>(d);
    GRef<u32>(row + 0x60) = GRef<u32>(d + 4);
    GRef<u32>(row + 0x70) = GRef<u32>(d + 8);
    GRef<u32>(row + 0x80) = GRef<u32>(d + 12);
    CopyQword(ls + i16 + 0x90, c);
}

// EE: 0x00136F48
// Ambient colour (RGBA bytes / 128).
// fpucheck: verified 4 (the add.s of gcc's unsigned convert, never taken for bytes)
void SetAmbient(u32 ls, u32 rgba) {
    guest::GuestFrame frame(0x10);
    const u32 c = frame.Address();
    ColourToFloats(rgba, c);
    CopyQword(ls + 0xD0, c);
}

// EE: 0x00137068
// Environment texture: a GIM record for `gim` at TBP/CBP.
void SetLightTexture(u32 ls, u32 gim, u16 tbp, u16 cbp) {
    const u32 tex = WorkHeapAlloc(0x28, 0x10);
    GRef<u32>(ls + 0x40) = tex;
    InitGimTexture(tex, gim);
    SetTextureVramAddress(GRef<u32>(ls + 0x40), tbp, cbp);
}

// EE: 0x001370D8
// Packet into bucket `index` of `list`: twice (both VU1 buffers) the light matrix, colours and
// ambient (11 qwords from +0xE0 header, +0x50..+0xDF, +0xF0 MSCAL); with flag bit 0 and a texture,
// its upload and the A+D state (the drawing environment's TEX1/CLAMP/MIPTBP words, the camera's fog
// colour at +0x1B0, ALPHA, TEXA...). Some words of the last qwords are not written, as on the PS2.
void WriteLightSetPacket(u32 ls, u32 cam, u32 list, s32 index, u32 flags) {
    u32 p = BeginPacket();
    for (int k = 0; k < 2; ++k) {
        CopyQword(p, ls + 0xE0);
        for (u32 o = 0x50; o < 0xE0; o += 0x10) CopyQword(p + (o - 0x40), ls + o);
        CopyQword(p + 0xA0, ls + 0xF0);
        p += 0xB0;
    }
    const u32 tex = GRef<u32>(ls + 0x40);
    if (tex != 0 && (flags & 1) != 0) {
        p = BuildTextureUpload(tex, p, 2, 0x1A);
        Put(p, 1, 0);
        Put(p, 0, 0x10000009);
        Put(p, 2, 0);
        Put(p, 3, 0x50000009);
        Put(p, 4, 0x8008);  // GIF tag: NLOOP 8, EOP, A+D (top word left)
        Put(p, 5, 0x10000000);
        Put(p, 6, 0xE);
        p += 0x20;
        CopyQword(p, kMainContextAddress + 0x150);
        p += 0x10;
        CopyQword(p, kMainContextAddress + 0x170);
        p += 0x10;
        CopyQword(p, cam + 0x1B0);
        p += 0x10;
        CopyQword(p, kMainContextAddress + 0x160);
        p += 0x10;
        Put(p, 0, 0x60);
        Put(p, 2, 0x15);
        Put(p, 1, 0);
        p += 0x10;
        Put(p, 0, 0x58);
        Put(p, 2, 0x43);
        Put(p, 1, 0);
        Put(p, 3, 0);
        p += 0x10;
        Put(p, 2, 9);
        Put(p, 0, 0);
        Put(p, 1, 0);
        p += 0x10;
        Put(p, 0, 0x50000);
        Put(p, 2, 0x48);
        p += 0x10;
    }
    EndPacket(list, index, p);
}

// EE: 0x0014AE60
// Defaults: radius 100, alpha 0x80, no model/texture/effects, frame and Z formats from the context,
// VU1 program by mode (1: A, 0: B, 2: C).
void InitMenuObject(u32 obj, s32 mode) {
    GRef<u8>(obj + 0xAC) = 0x80;
    GRef<s32>(obj + 0x9C) = mode;
    GRef<u32>(obj + 0x90) = 0;
    GRef<u32>(obj + 0x94) = 0;
    GRef<u32>(obj + 0x98) = 0;
    GRef<u32>(obj + 0xA0) = 0;
    GRef<u32>(obj + 0xA4) = 0;
    GRef<u32>(obj + 0xA8) = 0;
    GRef<u32>(obj + 0xB0) = 0;
    MainContext* ctx = Ctx();
    GRef<u32>(obj + 0xB8) = ctx->frame_psm;
    GRef<u32>(obj + 0xB4) = 0;
    GRef<u32>(obj + 0xC0) = ctx->zbuf_psm;
    if (mode == 1) {
        GRef<u32>(obj + 0x98) = 0x00297C00;
    } else if (mode == 0) {
        GRef<u32>(obj + 0x98) = 0x00298AC0;
    } else if (mode == 2) {
        GRef<u32>(obj + 0x98) = 0x00299820;
    } else {
        GRef<u32>(obj + 0x98) = 0;
    }
    GRef<u32>(obj) = 0x42C80000;
}

// EE: 0x0014AEF8
// Binds model data (instance, radius, triangle and node counts), texture, glow model and effect
// list (a container of effect records, 0x40 bytes each in the pointer table).
void BindMenuObject(u32 obj, u32 model, u32 gim, u32 glow_model, u32 effects) {
    GRef<u32>(obj + 4) = 0;
    GRef<u32>(obj + 8) = 0;
    if (model != 0) {
        const u32 rec = WorkHeapAlloc(0x28, 0x10);
        BindModelData(rec, model);
        const u32 inst = WorkHeapAlloc(0x4C, 0x10);
        GRef<u32>(obj + 0x90) = inst;
        CreateModelInstance(inst, rec);
        const u32 i = GRef<u32>(obj + 0x90);
        GRef<u32>(obj + 4) = GRef<u32>(i + 0x48);
        GRef<u32>(obj) = GRef<u32>(i + 0x40);
        GRef<u32>(obj + 8) = GRef<u32>(GRef<u32>(i + 0x30) + 8);
    }
    if (gim != 0) {
        const u32 tex = WorkHeapAlloc(0x28, 0x10);
        GRef<u32>(obj + 0xA0) = tex;
        InitGimTexture(tex, gim);
    }
    if (glow_model != 0) {
        const u32 rec = WorkHeapAlloc(0x28, 0x10);
        BindModelData(rec, glow_model);
        const u32 inst = WorkHeapAlloc(0x4C, 0x10);
        GRef<u32>(obj + 0x94) = inst;
        CreateModelInstance(inst, rec);
    }
    if (effects != 0) {
        guest::GuestFrame frame(0x10);
        const u32 reader = frame.Address();
        InitContainerReader(reader, effects);
        const s32 n = GRef<s32>(reader);
        GRef<s32>(obj + 0xA8) = n;
        if (n > 0) {
            GRef<u32>(obj + 0xA4) = WorkHeapAlloc(n << 6, 0x10);
            for (s32 i = 0; i < GRef<s32>(obj + 0xA8); ++i)
                GRef<u32>(GRef<u32>(obj + 0xA4) + static_cast<u32>(i) * 4) = GetContainerItem(reader, i);
        }
    }
}

// EE: 0x0014B078
// Texture VRAM addresses: TBP into the image header, CBP into the CLUT header (if any).
void SetMenuObjectTexturePos(u32 obj, u16 tbp, u16 cbp) {
    const u32 tex = GRef<u32>(obj + 0xA0);
    if (tex == 0) return;
    const u32 clut = GRef<u32>(tex + 8);
    GRef<u16>(GRef<u32>(tex + 4)) = tbp;
    if (clut != 0) GRef<u16>(clut) = cbp;
}

// EE: 0x0014B0A0
// Part value `channel` = v on the model and the glow model.
void SetMenuObjectPart(f32 v, u32 obj, s32 channel) {
    if (GRef<u32>(obj + 0x90) != 0) SetModelPartValue(v, GRef<u32>(obj + 0x90), channel);
    if (GRef<u32>(obj + 0x94) != 0) SetModelPartValue(v, GRef<u32>(obj + 0x94), channel);
}

// EE: 0x0014B168
// Node matrices of the model and the glow model from the identity (only dirty nodes: flag 1).
void UpdateMenuObject(u32 obj) {
    guest::GuestFrame frame(0x50);
    const u32 m = frame.Address(), idx = m + 0x40;
    SetUnitMatrixVu0(m);
    if (GRef<u32>(obj + 0x90) != 0) {
        GRef<u32>(idx) = 0;
        UpdateModelHierarchy(GRef<u32>(obj + 0x90), idx, m, m, 1);
    }
    if (GRef<u32>(obj + 0x94) != 0) {
        GRef<u32>(idx) = 0;
        UpdateModelHierarchy(GRef<u32>(obj + 0x94), idx, m, m, 1);
    }
}

// EE: 0x0014B1D0
// World matrices from rotation angles `rot` and translation `trans`.
void SetMenuObjectPose(u32 obj, u32 rot, u32 trans) {
    const u32 r = obj + 0x50;
    SetUnitMatrixVu0(r);
    RotateMatrix(r, r, rot);
    sceVu0TransMatrix(obj + 0x10, r, trans);
}

// EE: 0x0014B238
// World matrices given directly: rotation `rotation`, full `full`.
void SetMenuObjectMatrices(u32 obj, u32 rotation, u32 full) {
    sceVu0CopyMatrix(obj + 0x50, rotation);
    sceVu0CopyMatrix(obj + 0x10, full);
}

// EE: 0x0014B280
// Uploads the object's VU1 program: a DMA ref to the .vutext block into bucket `index`.
void UploadMenuObjectProgram(u32 obj, u32 list, s32 index) {
    const u32 prog = GRef<u32>(obj + 0x98);
    if (prog != 0) AddBucketRef(list, index, prog + 0x10, GRef<u16>(prog));
}

// EE: 0x0014B2A8
// Texture packet into bucket `index`: with flag bit 0 the texture upload, then TEX0, and twice the
// TEX0 high word state at VU 0x1C (PSM; with a CLUT also CBP and CPSM) with an MSCAL 0.
void WriteMenuObjectTexture(u32 obj, u32 list, s32 index, u32 flags) {
    u32 p = BeginPacket();
    u32 tex = GRef<u32>(obj + 0xA0);
    if (tex != 0) {
        if ((flags & 1) != 0) {
            p = BuildTextureUpload(tex, p, 1, 0);
            tex = GRef<u32>(obj + 0xA0);
        }
        p = WriteTex0Tex1(tex, p);
        tex = GRef<u32>(obj + 0xA0);
        const u32 clut = GRef<u32>(tex + 8);
        for (int k = 0; k < 2; ++k, p += 0x30) {
            Put(p, 0, 0x10000001);
            Put(p, 1, 0);
            Put(p, 2, 0);
            Put(p, 3, 0x6C01C01C);  // UNPACK V4-32 1 -> 0x1C (TOPS)
            if (clut == 0) {
                Put(p, 5, 0);
                Put(p, 6, 0x16);
                Put(p, 4, static_cast<u32>(GRef<u8>(GRef<u32>(tex + 4) + 3)) << 20);
            } else {
                Put(p, 4, static_cast<u32>(GRef<u8>(GRef<u32>(tex + 4) + 3)) << 20);
                Put(p, 6, 0x16);
                Put(p, 5, static_cast<u32>(GRef<u16>(clut)) << 5 | static_cast<u32>(GRef<u8>(clut + 3)) << 19 | 0x20000000);
            }
            Put(p, 8, 0x10000000);
            Put(p, 9, 0);
            Put(p, 10, 0);
            Put(p, 11, 0x14000000);  // MSCAL 0
        }
    }
    EndPacket(list, index, p);
}

// EE: 0x0014B3F8
// Camera packet into bucket `index`, twice: `vec` at VU 0x1E, and the camera's world->screen and
// world->clip matrices (+0xE0..+0x15F) at VU 0, with an MSCAL 0.
void WriteMenuObjectCamera(u32 obj, u32 cam, u32 list, s32 index, u32 vec) {
    (void)obj;
    u32 p = BeginPacket();
    for (int k = 0; k < 2; ++k, p += 0xC0) {
        Put(p, 0, 0x10000001);
        Put(p, 1, 0);
        Put(p, 2, 0);
        Put(p, 3, 0x6C01801E);
        CopyQword(p + 0x10, vec);
        Put(p, 8, 0x10000009);
        Put(p, 9, 0);
        Put(p, 10, 0);
        Put(p, 11, 0x6C088000);
        for (u32 q = 0; q < 8; ++q) CopyQword(p + 0x30 + q * 16, cam + 0xE0 + q * 16);
        Put(p, 0x2C, 0);
        Put(p, 0x2D, 0);
        Put(p, 0x2E, 0);
        Put(p, 0x2F, 0x14000000);
    }
    EndPacket(list, index, p);
}

// EE: 0x0014B5A0
// Draws the object into bucket `index` of `list`: TEST (Z test 0x58... A+D) and the model's
// opaque parts (mode 8/0xC by the clipping flag); mode 2 also ALPHA (alpha +0xAC) and the
// translucent parts in two passes (0xA/0xE, 9/0xD). Then the effect list into `effect_list` (bucket
// 0 and 0x3FF state packets around DrawSpecialObject), and for mode 2 the glow pass.
void DrawMenuObject(u32 obj, u32 cam, u32 list, s32 index, u32 effect_list) {
    guest::GuestFrame frame(0xB0);
    const u32 sm = frame.Address(), rm = sm + 0x40, origin = sm + 0x80, axis = sm + 0x90;
    const u32 full = obj + 0x10, rot = obj + 0x50;
    for (u32 o = 0; o < 0x10; o += 4) GRef<u32>(origin + o) = 0;
    GRef<u32>(origin + 0xC) = 0x3F800000;
    u32 p = BeginPacket();
    Put(p, 0, 0x10000002);
    Put(p, 1, 0);
    Put(p, 2, 0x11000000);  // FLUSH
    Put(p, 3, 0x50000002);
    Put(p, 4, 0x8001);
    Put(p, 5, 0x10000000);
    Put(p, 6, 0xE);
    Put(p, 8, 0x58);
    Put(p, 9, 0);
    Put(p, 10, 0x43);
    Put(p, 11, 0);
    const bool no_clip = (GRef<u32>(obj + 0xB0) & 1) != 0;
    p = EmitModelInstance(GRef<u32>(obj + 0x90), p + 0x30, cam, full, rot, no_clip ? 8 : 0xC);
    if (GRef<s32>(obj + 0x9C) == 2) {
        Put(p, 0, 0x10000002);
        Put(p, 1, 0);
        Put(p, 2, 0x11000000);
        Put(p, 3, 0x50000002);
        Put(p, 4, 0x8001);
        Put(p, 5, 0x10000000);
        Put(p, 6, 0xE);
        Put(p, 8, 0x68);
        Put(p, 10, 0x43);
        Put(p, 9, GRef<u8>(obj + 0xAC));
        Put(p, 11, 0);
        const u32 q = EmitModelInstance(GRef<u32>(obj + 0x90), p + 0x30, cam, full, rot, no_clip ? 10 : 0xE);
        p = EmitModelInstance(GRef<u32>(obj + 0x90), q, cam, full, rot, no_clip ? 9 : 0xD);
    }
    Put(p, 0, 0x10000000);
    Put(p, 1, 0);
    Put(p, 2, 0x11000000);
    Put(p, 3, 0);
    EndPacket(list, index, p + 0x10);
    if (GRef<u32>(obj + 0xA4) != 0) {
        // Effect list (never set in S4; DrawSpecialObject belongs to S5f).
        p = BeginPacket();
        Put(p, 0, 0x10000002);
        Put(p, 1, 0);
        Put(p, 2, 0);
        Put(p, 3, 0x50000002);
        Put(p, 4, 0x8001);
        Put(p, 5, 0x10000000);
        Put(p, 6, 0xE);
        CopyQword(p + 0x20, kMainContextAddress + 0xE0);
        Put(p, 9, 1);
        EndPacket(effect_list, 0, p + 0x30);
        u32 inst = GRef<u32>(obj + 0x90);
        if (GRef<u32>(GRef<u32>(inst + 0x30) + 0xC) != 0) {
            for (u32 i = 0;;) {
                const u32 fx = GRef<u32>(GRef<u32>(obj + 0xA4) + i * 4);
                const u32 node = GRef<u16>(GRef<u32>(GRef<u32>(inst + 0x30) + 0x24) + i * 2);
                GRef<u32>(axis) = 0;
                GRef<u32>(axis + 4) = 0;
                GRef<u32>(axis + 8) = 0x3F800000;
                GRef<u32>(axis + 12) = 0x3F800000;
                ++i;
                sceVu0MulMatrix(sm, full, GRef<u32>(inst + 0x10) + node * 0x40);
                const u32 inst2 = GRef<u32>(obj + 0x90);
                const u32 node2 = GRef<u16>(GRef<u32>(GRef<u32>(inst2 + 0x30) + 0x24) + (i - 1) * 2);
                sceVu0MulMatrix(rm, rot, GRef<u32>(inst2 + 0x14) + node2 * 0x40);
                sceVu0ApplyMatrix(axis, rm, axis);
                func_001456F8(fx + 0x20, axis);
                sceVu0ApplyMatrix(fx, sm, origin);
                guest::CallGuest(0x001E0A98, cam, effect_list, fx);
                inst = GRef<u32>(obj + 0x90);
                if (GRef<u32>(GRef<u32>(inst + 0x30) + 0xC) <= i) break;
            }
        }
        p = BeginPacket();
        Put(p, 1, 0);
        Put(p, 0, 0x10000002);
        Put(p, 2, 0);
        Put(p, 3, 0x50000002);
        Put(p, 4, 0x8001);
        Put(p, 5, 0x10000000);
        Put(p, 6, 0xE);
        CopyQword(p + 0x20, kMainContextAddress + 0xE0);
        EndPacket(effect_list, 0x3FF, p + 0x30);
    }
    if (GRef<s32>(obj + 0x9C) == 2 && GRef<u32>(obj + 0x94) != 0) {
        // Glow pass (program D): the glow model is never bound in S4.
        guest::CallGuest(0x00142460, GRef<u32>(obj + 0x94), cam, list, index, full, 0x2500u, 0x2A00u, rot);
    }
}

}  // namespace ac04::game
