// Aircraft viewer (hangar background; docs/port/s4-inventory.md §2): the hangar room, the selected
// aircraft in its three colours with its floor reflection and shadow, the special weapons, the
// ceiling light glows, and the 3D camera.
//
// Viewer object (0x9D0 bytes, *0x00410EDC): +0x10 data load queue, +0x220 DATA 0xCD items (font
// block, two hangar models, the light texture +0x22C, the ceiling glow packet +0x230), +0x234
// aircraft model file (DATA 0xCD + aircraft, 2 items: +0x234 model, +0x238 shadow), +0x23C special
// weapon model files (8 bytes each), +0x260 fade (+0x26C level), +0x270 camera, +0x430 look-at
// (+0x4A0 VU1 parameter vector), +0x460 light parameters, +0x4B0/+0x580 hangar menu objects,
// +0x650/+0x750/+0x850 light sets (room, mirrored, aircraft), +0x950 aircraft record, +0x954 special
// weapon records, +0x958 help line message box, +0x97C fade state, +0x980 load state (0 queue
// font, 1 loading, 2 shown, 3 change pending, 4 reload, 5 loading, 6 building), +0x984 aircraft,
// +0x988 colour, +0x98C/+0x990 special weapon list/count, +0x994 special count of the built set,
// +0x998 build step, +0x99C.., +0x9A4 FBA/alpha word of the pass state, +0x9AC/+0x9AE highlight
// masks (bit 0 aircraft, bits 1.. special weapons), +0x9B0 pending colour, +0x9B4 reflection off,
// +0x9B8 part mode, +0x9BC/+0x9C0.
//
// Model record (0x30): +0 position, +0x10 rotation, +0x20 objects (aircraft: 3 colours x {normal,
// mirrored} 0xD0 each, shadow at +0x4E0; special weapon: normal, mirrored, shadow), +0x24 kind.
#include "game/game.h"

#include "common/ee_math.h"
#include "common/guest_stack.h"
#include "common/vu0_math.h"
#include "game/main_context.h"
#include "sce/sce.h"

namespace ac04::game {

using guest::GRef;

namespace {

AC04_GUEST_VAR(u32, g_pMenuFade, 0x00410E88)

constexpr u32 kSpr = 0x70000000;  // scratchpad work area (matrices and vectors)

f32 K(u32 address) { return GRef<f32>(address); }

void StoreVf0(u32 a) {  // sqc2 vf0: (0, 0, 0, 1)
    GRef<u32>(a) = 0;
    GRef<u32>(a + 4) = 0;
    GRef<u32>(a + 8) = 0;
    GRef<u32>(a + 12) = 0x3F800000;
}

void CopyQword(u32 dst, u32 src) {
    for (u32 i = 0; i < 16; i += 4) GRef<u32>(dst + i) = GRef<u32>(src + i);
}

u64 Sext(u32 v) { return static_cast<u64>(static_cast<s64>(static_cast<s32>(v))); }

}  // namespace

// EE: 0x0022DF70
// Special weapon list of the viewer's aircraft.
void SetViewerSpecialList(u32 v) {
    const u32 list = GetAircraftSpecialWeapons(GRef<s32>(v + 0x984));
    GRef<u32>(v + 0x98C) = list;
    GRef<u32>(v + 0x990) = GRef<u32>(list);
}

// EE: 0x0022DFA8
// Fades, the 3D camera (640 or the 16:9 width x 480, distance 920 ...), the look-at, aircraft (1-21,
// else 2) and colour (0-2, else 0), special list, masks, the VU1 parameter vector (128, 255, 0, 0)
// and the lights (ambient 0x101210; two lights 0x707070 and 0x282828).
void InitAircraftViewer(u32 v, s32 aircraft, u32 colour) {
    const u32 cam = v + 0x270;
    GRef<u32>(v + 0x97C) = 0;
    GRef<u16>(g_pMenuFade() + 0x30) = 1;
    GRef<u16>(g_pMenuFade() + 0x32) = 0;
    InitFade(v + 0x260, 0);
    InitFade(g_pMenuFade(), 0);
    StartFade(0.5f, g_pMenuFade(), 0);
    Init2dCamera(cam);
    f32 width = 640.0f;
    if ((Ctx()->flags & 0x10000) != 0) width = K(0x00410164);
    SetCameraProjection(width, 480.0f, 920.0f, 50.0f, 16384.0f, cam);
    SetCameraViewport(1.0f, K(0x00410168), cam, 0, 0, static_cast<s16>(Ctx()->draw_w), static_cast<s16>(Ctx()->draw_h));
    ResetCameraLookAt(v + 0x430);
    ResetViewerLookAt(v + 0x430);
    if (static_cast<u32>(aircraft - 1) >= 0x15) aircraft = 2;
    if (colour >= 3) colour = 0;
    GRef<s32>(v + 0x984) = aircraft;
    GRef<u32>(v + 0x988) = colour;
    SetViewerSpecialList(v);
    GRef<u16>(v + 0x9AE) = 0xFFFF;
    GRef<f32>(v + 0x99C) = K(0x0041016C);
    GRef<u32>(v + 0x9A4) = 0x80000000;
    GRef<u16>(v + 0x9AC) = 0xFFFF;
    GRef<f32>(v + 0x9A0) = 32768.0f;
    for (u32 i = 0; i < 16; i += 4) GRef<u32>(v + 0x4A0 + i) = 0;
    GRef<f32>(v + 0x4A0) = 32768.0f;
    GRef<f32>(v + 0x4A4) = 255.0f;
    InitLights(v + 0x460, 0x80101210);
    SetLightDirection(K(0x00410170), 0.0f, v + 0x460, 0, 0x80707070);
    SetLightDirection(K(0x00410174), 0.0f, v + 0x460, 1, 0x80282828);
    GRef<f32>(v + 0x9BC) = 1.0f;
    GRef<f32>(v + 0x9C0) = 0.0f;
    GRef<u32>(v + 0x9B8) = 0;
    GRef<u32>(v + 0x9A8) = 0;
    GRef<u32>(v + 0x9B4) = 0;
}

// EE: 0x0022E1A8
// Queues DATA 0xCD (5 items: font block, hangar models, light texture, glow packet).
void QueueViewerData(u32 v) {
    ResetMenuDataLoad(v + 0x10);
    QueueMenuDataLoad(v + 0x10, 0xCD, 5, v + 0x220);
}

// EE: 0x0022E1F0
// Queues the aircraft model file (DATA 0xCD + aircraft, 2 items).
void QueueViewerAircraft(u32 v) {
    s32 a = GRef<s32>(v + 0x984);
    if (static_cast<u32>(a - 1) > 0x14) a = 1;
    QueueMenuDataLoad(v + 0x10, static_cast<u16>(a + 0xCD), 2, v + 0x234);
}

// EE: 0x0022E218
// Queues the special weapon model files (DATA 0xE4 + id, 2 items each; ids above 0x22 load 0xE4).
void QueueViewerSpecials(u32 v) {
    const s32 n = GRef<s32>(v + 0x990);
    GRef<s32>(v + 0x994) = n;
    const u32 ids = GRef<u32>(v + 0x98C) + 4;
    for (s32 i = 0; i < n; ++i) {
        u32 id = GRef<u8>(ids + static_cast<u32>(i));
        if (id > 0x22) id = 0;
        QueueMenuDataLoad(v + 0x10, static_cast<u16>(id + 0xE4), 2, v + 0x23C + static_cast<u32>(i) * 8);
    }
}

// EE: 0x0022E2B0
// Load state machine: 0 queue the room data; 1 wait, then font, help line box, room objects, heap
// mark, queue the aircraft; 4 (aircraft change) back to the heap mark and queue again; 5 wait; 6
// build over four frames (record and colour 0, colour 1, colour 2, special weapons; then shown).
void UpdateViewerLoad(u32 v) {
    switch (GRef<u32>(v + 0x980)) {
        case 0:
            QueueViewerData(v);
            GRef<u32>(v + 0x980) = 1;
            break;
        case 1:
            if (UpdateMenuDataLoad(v + 0x10) >= 0) return;
            LoadFontBlock(GRef<u32>(v + 0x220));
            InitMessageBox(v + 0x958);
            CreateHangarObjects(v);
            PushWorkHeapMark();
            QueueViewerAircraft(v);
            QueueViewerSpecials(v);
            GRef<u32>(v + 0x980) = 5;
            break;
        case 4:
            PopWorkHeapMark();
            PushWorkHeapMark();
            SetViewerSpecialList(v);
            GRef<u16>(v + 0x9AC) = GRef<u16>(v + 0x9AE);
            QueueViewerAircraft(v);
            QueueViewerSpecials(v);
            GRef<u32>(v + 0x980) = 5;
            break;
        case 5:
            if (UpdateMenuDataLoad(v + 0x10) < 0) {
                GRef<s32>(v + 0x998) = 0;
                GRef<u32>(v + 0x980) = 6;
            }
            return;
        case 6: {
            const s32 step = GRef<s32>(v + 0x998);
            if (step == 0) {
                CreateAircraftRecord(v);
                CreateAircraftColour(v, 0);
            } else if (step == 1) {
                CreateAircraftColour(v, 1);
            } else if (step == 2) {
                CreateAircraftColour(v, 2);
            } else if (step == 3) {
                CreateSpecialRecords(v, GRef<s32>(v + 0x994));
                GRef<u32>(v + 0x980) = 2;
            }
            GRef<s32>(v + 0x998) = GRef<s32>(v + 0x998) + 1;
            break;
        }
        default:
            break;
    }
}

// EE: 0x0022E450
// The two hangar room objects at the origin, the room and aircraft light sets (the aircraft set with
// the light texture at TBP 0x3400 / CBP 0x3F08), and the mirrored set (lights flipped in Y).
void CreateHangarObjects(u32 v) {
    CreateMenuObject(v + 0x4B0, GRef<u32>(v + 0x224), 0, 0);
    StoreVf0(kSpr + 0x40);
    StoreVf0(kSpr + 0x50);
    SetMenuObjectPose(v + 0x4B0, kSpr + 0x40, kSpr + 0x50);
    CreateMenuObject(v + 0x580, GRef<u32>(v + 0x228), 0, 2);
    const u32 lights = v + 0x460;
    SetMenuObjectPose(v + 0x580, kSpr + 0x40, kSpr + 0x50);
    InitLightSet(v + 0x650);
    ApplyLights(v + 0x650, lights);
    InitLightSet(v + 0x850);
    ApplyLights(v + 0x850, lights);
    SetLightTexture(v + 0x850, GRef<u32>(v + 0x22C), 0x3400, 0x3F08);
    InitLightSet(v + 0x750);
    sceVu0UnitMatrix(kSpr);
    GRef<f32>(kSpr + 0x14) = -1.0f;
    ApplyLightsTransformed(v + 0x750, lights, kSpr);
}

// EE: 0x0022E580
// Aircraft record (kind 1) at the origin with its object block (0x5B0) and the shadow object
// (mode 2) from the shadow model.
void CreateAircraftRecord(u32 v) {
    const u32 rec = WorkHeapAlloc(0x30, 0x10);
    GRef<u32>(rec + 0x24) = 1;
    GRef<u32>(v + 0x950) = rec;
    StoreVf0(kSpr + 0x40);
    StoreVf0(kSpr + 0x50);
    CopyQword(rec + 0x10, kSpr + 0x40);
    CopyQword(rec, kSpr + 0x50);
    const u32 objs = WorkHeapAlloc(0x5B0, 0x10);
    GRef<u32>(rec + 0x20) = objs;
    CreateMenuObject(objs + 0x4E0, GRef<u32>(v + 0x238), 0, 2);
    GRef<f32>(kSpr + 0x40) = 0.0f;
    GRef<f32>(kSpr + 0x48) = 0.0f;
    SetMenuObjectPose(objs + 0x4E0, kSpr + 0x40, kSpr + 0x50);
}

// EE: 0x0022E660
// Colour `colour` (0-2, else 0) of the aircraft: the normal object (mode 2, texture item colour + 1)
// and the mirrored one (Y flipped, mode 0) for the floor reflection; parts by mode; node matrices.
void CreateAircraftColour(u32 v, u32 colour) {
    const u32 c = colour < 3 ? colour : 0;
    const u32 rec = GRef<u32>(v + 0x950);
    StoreVf0(kSpr + 0x40);
    StoreVf0(kSpr + 0x50);
    BuildModelMatrices(kSpr + 0x60, kSpr + 0xA0, kSpr + 0x40, kSpr + 0x50);
    const u32 obj = GRef<u32>(rec + 0x20) + c * 0xD0;
    CreateMenuObject(obj, GRef<u32>(v + 0x234), c + 1, 2);
    SetMenuObjectMatrices(obj, kSpr + 0x60, kSpr + 0xA0);
    sceVu0UnitMatrix(kSpr);
    GRef<f32>(kSpr + 0x14) = -1.0f;
    BuildModelMatrices(kSpr + 0x60, kSpr + 0xA0, kSpr + 0x40, kSpr + 0x50);
    sceVu0MulMatrix(kSpr + 0x60, kSpr + 0x60, kSpr);
    sceVu0MulMatrix(kSpr + 0xA0, kSpr + 0xA0, kSpr);
    const u32 mirror = GRef<u32>(rec + 0x20) + c * 0xD0 + 0x270;
    CreateMenuObject(mirror, GRef<u32>(v + 0x234), c + 1, 0);
    SetMenuObjectMatrices(mirror, kSpr + 0x60, kSpr + 0xA0);
    SetAircraftParts(0.0f, rec, static_cast<s32>(c), GRef<s32>(v + 0x984), GRef<u32>(v + 0x9B8));
    FinishAircraftObjects(rec, static_cast<s32>(c));
}

// EE: 0x0022E800
// Special weapon records (kind 2), at the positions of table 0x0039D870 (x, z per slot): normal
// (mode 2) and mirrored (mode 0) objects from the weapon model, and the shadow object.
void CreateSpecialRecords(u32 v, s32 count) {
    if (count < 0) return;
    u32 rec = WorkHeapAlloc(count * 0x30, 0x10);
    GRef<u32>(v + 0x954) = rec;
    for (s32 i = 0; i < count; ++i, rec += 0x30) {
        GRef<u32>(rec + 0x24) = 2;
        StoreVf0(kSpr + 0x40);
        StoreVf0(kSpr + 0x50);
        const u32 pos = 0x0039D870 + static_cast<u32>(i) * 8;
        GRef<u32>(kSpr + 0x50) = GRef<u32>(pos);
        GRef<u32>(kSpr + 0x58) = GRef<u32>(pos + 4);
        CopyQword(rec + 0x10, kSpr + 0x40);
        CopyQword(rec, kSpr + 0x50);
        const u32 objs = WorkHeapAlloc(0x270, 0x10);
        GRef<u32>(rec + 0x20) = objs;
        const u32 file = GRef<u32>(v + 0x23C + static_cast<u32>(i) * 8);
        CreateMenuObject(objs, file, 0, 2);
        CreateMenuObject(GRef<u32>(rec + 0x20) + 0xD0, file, 0, 0);
        CreateMenuObject(GRef<u32>(rec + 0x20) + 0x1A0, GRef<u32>(v + 0x240 + static_cast<u32>(i) * 8), 0, 2);
    }
}

// EE: 0x0022E978
// Part values of colour `colour` by part mode (jump table 0x00407070): 0 gear down (part 0x1F = 1,
// part 0x13 = 0), 1/2 all 0, 3/4 unchanged, 5/6 part 0x1F = clamp(v * k + k0), 7 part 0x13 =
// clamp(v, 0, 1) (also on the shadow object).
void SetAircraftParts(f32 value, u32 rec, s32 colour, s32 aircraft, u32 mode) {
    (void)aircraft;
    const u32 objs = GRef<u32>(rec + 0x20);
    const u32 normal = objs + static_cast<u32>(colour) * 0xD0;
    const u32 mirror = objs + static_cast<u32>(colour) * 0xD0 + 0x270;
    const u32 shadow = objs + 0x4E0;
    if (mode >= 8) return;
    switch (mode) {
        case 0:
            SetMenuObjectPart(1.0f, normal, 0x1F);
            SetMenuObjectPart(1.0f, mirror, 0x1F);
            SetMenuObjectPart(0.0f, normal, 0x13);
            SetMenuObjectPart(0.0f, mirror, 0x13);
            SetMenuObjectPart(0.0f, shadow, 0x13);
            break;
        case 1:
        case 2:
            SetMenuObjectPart(0.0f, normal, 0x1F);
            SetMenuObjectPart(0.0f, mirror, 0x1F);
            SetMenuObjectPart(0.0f, normal, 0x13);
            SetMenuObjectPart(0.0f, mirror, 0x13);
            SetMenuObjectPart(0.0f, shadow, 0x13);
            break;
        case 5:
        case 6: {
            const f32 lo = K(0x0041017C);
            f32 t = fpu::Add(fpu::Mul(value, K(0x00410178)), lo);
            if (t < lo) {
                t = lo;
            } else {
                const f32 hi = K(0x00410180);
                if (hi < t) t = hi;
            }
            SetMenuObjectPart(t, normal, 0x1F);
            SetMenuObjectPart(t, mirror, 0x1F);
            break;
        }
        case 7: {
            f32 t = value;
            if (t < 0.0f) {
                t = 0.0f;
            } else if (1.0f < t) {
                t = 1.0f;
            }
            SetMenuObjectPart(t, normal, 0x13);
            SetMenuObjectPart(t, mirror, 0x13);
            SetMenuObjectPart(t, shadow, 0x13);
            break;
        }
        default:
            break;
    }
}

// EE: 0x0022EBD0
// Node matrices of the colour's normal and mirrored objects and the shadow object.
void FinishAircraftObjects(u32 rec, s32 colour) {
    UpdateMenuObject(GRef<u32>(rec + 0x20) + static_cast<u32>(colour) * 0xD0);
    UpdateMenuObject(GRef<u32>(rec + 0x20) + static_cast<u32>(colour) * 0xD0 + 0x270);
    UpdateMenuObject(GRef<u32>(rec + 0x20) + 0x4E0);
}

// EE: 0x0022EC28
// Render state packet into bucket `index` (GIF A+D, 6 registers): TEX1 0x60, register 0x3D =
// `fba`, by pass (0 opaque: TEST 0x3000D, ZBUF with Z mask off; 1 / 2: TEST 0x7000D / 0x7040D,
// ZBUF masked; other passes leave these two words), CLAMP 0, ALPHA 0x44.
void WriteViewerPassState(u32 list, s32 index, u32 fba, s32 pass) {
    const u32 p = Ctx()->cur_buf.addr;
    const u32 t = p + 0x10;
    GRef<u64>(t) = 0x1000000000008006ull;
    GRef<u64>(t + 8) = 0xE;
    GRef<u64>(t + 0x10) = 0x60;
    GRef<u64>(t + 0x18) = 0x14;
    GRef<u64>(t + 0x20) = fba;
    GRef<u64>(t + 0x28) = 0x3D;
    if (pass >= 0 && pass <= 2) {
        u64 test, zbuf;
        if (pass == 0) {
            test = 0x3000D;
            zbuf = 0x1310000DCull;
        } else {
            test = pass == 1 ? 0x7000D : 0x7040D;
            zbuf = 0x310000DC;
        }
        GRef<u64>(t + 0x30) = test;
        GRef<u64>(t + 0x38) = 0x47;
        GRef<u64>(t + 0x40) = zbuf;
        GRef<u64>(t + 0x48) = 0x4E;
    }
    GRef<u64>(t + 0x68) = 0x42;
    GRef<u64>(t + 0x58) = 8;
    GRef<u64>(t + 0x60) = 0x44;
    GRef<u64>(t + 0x50) = 0;
    const u32 end = CloseDmaPacket(p, t + 0x70);
    LinkPacket(list, index, p, end);
}

// EE: 0x0022ED70
// First hangar room object with the room light set.
void DrawHangarRoom(u32 list, s32 index, u32 v) {
    const u32 obj = v + 0x4B0, cam = v + 0x270;
    UploadMenuObjectProgram(obj, list, index);
    WriteLightSetPacket(v + 0x650, cam, list, index, 0);
    WriteMenuObjectCamera(obj, cam, list, index, v + 0x4A0);
    WriteMenuObjectTexture(obj, list, index, 1);
    DrawMenuObject(obj, cam, list, index, 0);
}

// EE: 0x0022EE30
// Second hangar room object (mode 2) with the room light set.
void DrawHangarRoomGlass(u32 list, s32 index, u32 v) {
    const u32 obj = v + 0x580, cam = v + 0x270;
    UploadMenuObjectProgram(obj, list, index);
    WriteLightSetPacket(v + 0x650, cam, list, index, 0);
    WriteMenuObjectCamera(obj, cam, list, index, v + 0x4A0);
    WriteMenuObjectTexture(obj, list, index, 1);
    DrawMenuObject(obj, cam, list, index, 0);
}

// EE: 0x0022EEF0
// The aircraft in its colour, lit by the aircraft light set turned towards the camera, with the
// light texture.
void DrawAircraft(u32 list, s32 index, u32 v) {
    const u32 cam = v + 0x270;
    u32 c = GRef<u32>(v + 0x988);
    const u32 rec = GRef<u32>(v + 0x950);
    if (c > 2) c = 0;
    const u32 obj = GRef<u32>(rec + 0x20) + c * 0xD0;
    UploadMenuObjectProgram(obj, list, index);
    WriteMenuObjectCamera(obj, cam, list, index, v + 0x4A0);
    WriteMenuObjectTexture(obj, list, index, 1);
    BuildShadowMatrix(kSpr, cam, rec);
    ApplyLightsTransformed(v + 0x850, v + 0x460, kSpr);
    WriteLightSetPacket(v + 0x850, cam, list, index, 1);
    BuildModelMatrices(kSpr + 0x60, kSpr + 0xA0, rec + 0x10, rec);
    sceVu0MulMatrix(kSpr + 0x60, kSpr + 0x60, kSpr);
    SetMenuObjectMatrices(obj, kSpr + 0x60, kSpr + 0xA0);
    DrawMenuObject(obj, cam, list, index, 0);
}

// EE: 0x0022F048
// The aircraft's floor reflection (mirrored object, mirrored light set).
void DrawAircraftReflection(u32 list, s32 index, u32 v) {
    const u32 cam = v + 0x270;
    u32 c = GRef<u32>(v + 0x988);
    if (c > 2) c = 0;
    const u32 obj = GRef<u32>(GRef<u32>(v + 0x950) + 0x20) + c * 0xD0 + 0x270;
    UploadMenuObjectProgram(obj, list, index);
    WriteLightSetPacket(v + 0x750, cam, list, index, 0);
    WriteMenuObjectCamera(obj, cam, list, index, v + 0x4A0);
    WriteMenuObjectTexture(obj, list, index, 1);
    DrawMenuObject(obj, cam, list, index, 0);
}

// EE: 0x0022F130
// The aircraft's shadow object with the room light set.
void DrawAircraftShadow(u32 list, s32 index, u32 v) {
    const u32 cam = v + 0x270;
    const u32 obj = GRef<u32>(GRef<u32>(v + 0x950) + 0x20) + 0x4E0;
    UploadMenuObjectProgram(obj, list, index);
    WriteLightSetPacket(v + 0x650, cam, list, index, 0);
    WriteMenuObjectCamera(obj, cam, list, index, v + 0x4A0);
    WriteMenuObjectTexture(obj, list, index, 1);
    DrawMenuObject(obj, cam, list, index, 0);
}

// EE: 0x0022F200
// The special weapons whose bit is set in `mask` (bit 0 = first), as DrawAircraft.
void DrawSpecials(u32 list, s32 index, u32 v, u32 mask) {
    if (mask == 0) return;
    u32 rec = GRef<u32>(v + 0x954);
    const u32 cam = v + 0x270;
    u32 obj = GRef<u32>(rec + 0x20);
    UploadMenuObjectProgram(obj, list, index);
    WriteMenuObjectCamera(obj, cam, list, index, v + 0x4A0);
    const s32 n = GRef<s32>(v + 0x994);
    if (n <= 0) return;
    for (s32 i = 0;;) {
        if ((mask & 1) != 0) {
            obj = GRef<u32>(rec + 0x20);
            WriteMenuObjectTexture(obj, list, index, 1);
            CopyQword(kSpr + 0x40, rec + 0x10);
            CopyQword(kSpr + 0x50, rec);
            BuildShadowMatrix(kSpr, cam, kSpr + 0x50);
            ApplyLightsTransformed(v + 0x850, v + 0x460, kSpr);
            WriteLightSetPacket(v + 0x850, cam, list, index, 1);
            BuildModelMatrices(kSpr + 0x60, kSpr + 0xA0, kSpr + 0x40, kSpr + 0x50);
            sceVu0MulMatrix(kSpr + 0x60, kSpr + 0x60, kSpr);
            SetMenuObjectMatrices(obj, kSpr + 0x60, kSpr + 0xA0);
            DrawMenuObject(obj, cam, list, index, 0);
        }
        rec += 0x30;
        mask >>= 1;
        ++i;
        if (i >= n || mask == 0) break;
    }
}

// EE: 0x0022F3D8
// The special weapons' floor reflections (mirrored objects).
void DrawSpecialReflections(u32 list, s32 index, u32 v, u32 mask) {
    if (mask == 0) return;
    u32 rec = GRef<u32>(v + 0x954);
    sceVu0UnitMatrix(kSpr);
    const u32 cam = v + 0x270;
    const u32 first = GRef<u32>(rec + 0x20);
    GRef<f32>(kSpr + 0x14) = -1.0f;
    UploadMenuObjectProgram(first + 0xD0, list, index);
    WriteLightSetPacket(v + 0x750, cam, list, index, 0);
    WriteMenuObjectCamera(first + 0xD0, cam, list, index, v + 0x4A0);
    const s32 n = GRef<s32>(v + 0x994);
    if (n <= 0) return;
    for (s32 i = 0;;) {
        if ((mask & 1) != 0) {
            const u32 obj = GRef<u32>(rec + 0x20) + 0xD0;
            WriteMenuObjectTexture(obj, list, index, 1);
            CopyQword(kSpr + 0x40, rec + 0x10);
            CopyQword(kSpr + 0x50, rec);
            BuildModelMatrices(kSpr + 0x60, kSpr + 0xA0, kSpr + 0x40, kSpr + 0x50);
            sceVu0MulMatrix(kSpr + 0x60, kSpr + 0x60, kSpr);
            sceVu0MulMatrix(kSpr + 0xA0, kSpr + 0xA0, kSpr);
            SetMenuObjectMatrices(obj, kSpr + 0x60, kSpr + 0xA0);
            DrawMenuObject(obj, cam, list, index, 0);
        }
        rec += 0x30;
        mask >>= 1;
        ++i;
        if (i >= n || mask == 0) break;
    }
}

// EE: 0x0022F5B0
// The special weapons' shadow objects.
void DrawSpecialShadows(u32 list, s32 index, u32 v, u32 mask) {
    if (mask == 0) return;
    u32 rec = GRef<u32>(v + 0x954);
    const u32 cam = v + 0x270;
    const u32 first = GRef<u32>(rec + 0x20);
    UploadMenuObjectProgram(first + 0x1A0, list, index);
    WriteLightSetPacket(v + 0x650, cam, list, index, 0);
    WriteMenuObjectCamera(first + 0x1A0, cam, list, index, v + 0x4A0);
    const s32 n = GRef<s32>(v + 0x994);
    if (n <= 0) return;
    for (s32 i = 0;;) {
        if ((mask & 1) != 0) {
            const u32 obj = GRef<u32>(rec + 0x20) + 0x1A0;
            WriteMenuObjectTexture(obj, list, index, 1);
            CopyQword(kSpr + 0x40, rec + 0x10);
            CopyQword(kSpr + 0x50, rec);
            BuildModelMatrices(kSpr + 0x60, kSpr + 0xA0, kSpr + 0x40, kSpr + 0x50);
            SetMenuObjectMatrices(obj, kSpr + 0x60, kSpr + 0xA0);
            DrawMenuObject(obj, cam, list, index, 0);
        }
        rec += 0x30;
        mask >>= 1;
        ++i;
        if (i >= n || mask == 0) break;
    }
}

// EE: 0x0022F750
// Camera from the look-at, its matrices, and the world->screen and world->clip matrices scaled by 10
// (the hangar models are in tenths).
void UpdateViewerCamera3D(u32 cam, u32 look) {
    SetCameraLookAt(cam, look);
    UpdateCameraMatrices(cam);
    BuildScaleMatrix(10.0f, 10.0f, 10.0f, kSpr);
    sceVu0MulMatrix(cam + 0xE0, cam + 0xE0, kSpr);
    sceVu0MulMatrix(cam + 0x120, cam + 0x120, kSpr);
}

// EE: 0x0022F7B8
// Clamps the look-at eye and target into the box 0x0039D890..0x0039D8A0.
void ClampViewerLookAt(u32 look) {
    ClampVector(look, 0x0039D890, 0x0039D8A0);
    ClampVector(look + 0x10, 0x0039D890, 0x0039D8A0);
}

// EE: 0x0022F810
// Ceiling light glows into bucket `index`: the glow packet (DMA call to +0x230), a scissored draw
// environment, then rows of 9/8 lights (5 rows) at height `y`: each light projected with the
// camera (VU0, perspective by Q), kept when its centre and both corners lie in the 4096-pixel range
// with z, w >= 0; its size `size` x the camera scale over the distance; colour by distance
// (LerpColor); a clipped textured sprite (BuildClippedSpriteStripsUv). `alpha` x 128 is the FBA/
// alpha word.
// fpucheck: verified 4
void DrawCeilingLights(f32 y, f32 size, f32 alpha, u32 list, s32 index, u32 v) {
    const u32 cam = v + 0x270;
    const u32 start = Ctx()->cur_buf.addr;
    const u32 pkt = WriteDmaCallTag(start, GRef<u32>(v + 0x230));
    u32 p = BuildMenuDrawEnv(pkt + 0x10, 0x280, 0x1C0);
    const s32 a = fpu::FloatToInt(fpu::Mul(alpha, 128.0f));
    const s32 h = GRef<s16>(cam + 0x18E) / 2, w = GRef<s16>(cam + 0x18C) / 2;
    const s32 cy = fpu::FloatToInt(GRef<f32>(cam + 0x17C));
    const s32 cx = fpu::FloatToInt(GRef<f32>(cam + 0x178));
    const s32 y0 = (cy - h) << 4, y1 = (cy + h) << 4, x0 = (cx - w) << 4, x1 = (cx + w) << 4;
    GRef<u64>(p) = 0x11AB400000000003ull;
    GRef<u64>(p + 8) = 0xE;
    GRef<u64>(p + 0x10) = 0x1310000DCull;
    GRef<u64>(p + 0x18) = 0x4F;
    GRef<u64>(p + 0x30) = static_cast<u64>(static_cast<u32>(a)) << 32 | 0x68;
    GRef<u64>(p + 0x20) = 0x7000D;
    GRef<u64>(p + 0x28) = 0x48;
    GRef<u64>(p + 0x38) = 0x43;
    p += 0x40;
    GRef<u64>(p) = 0x2400000000008001ull;
    GRef<u64>(p + 8) = 0x17;
    GRef<u64>(p + 0x10) = 0x2006000199306D00ull;
    GRef<u64>(p + 0x18) = 0x80808080;
    GRef<u64>(kSpr + 0xE0) = Sext(static_cast<u32>(x0)) | Sext(static_cast<u32>(y0)) << 16;
    GRef<u64>(kSpr + 0xE8) = Sext(static_cast<u32>(x1)) | Sext(static_cast<u32>(y1)) << 16;
    p += 0x20;
    for (u32 i = 0; i < 16; i += 4) GRef<u32>(kSpr + 0x40 + i) = 0;
    GRef<f32>(kSpr + 0x40) = fpu::Mul(fpu::Mul(GRef<f32>(cam + 0x180), size), GRef<f32>(cam + 0x168));
    GRef<f32>(kSpr + 0x44) = fpu::Mul(fpu::Mul(GRef<f32>(cam + 0x184), size), GRef<f32>(cam + 0x168));
    const vu0::Vec4 scale{GRef<f32>(kSpr + 0x40), GRef<f32>(kSpr + 0x44), GRef<f32>(kSpr + 0x48), GRef<f32>(kSpr + 0x4C)};
    const u32 ms = v + 0x350;  // camera world->screen (+0xE0)
    const vu0::Vec4 m[4] = {
        {GRef<f32>(ms), GRef<f32>(ms + 4), GRef<f32>(ms + 8), GRef<f32>(ms + 12)},
        {GRef<f32>(ms + 0x10), GRef<f32>(ms + 0x14), GRef<f32>(ms + 0x18), GRef<f32>(ms + 0x1C)},
        {GRef<f32>(ms + 0x20), GRef<f32>(ms + 0x24), GRef<f32>(ms + 0x28), GRef<f32>(ms + 0x2C)},
        {GRef<f32>(ms + 0x30), GRef<f32>(ms + 0x34), GRef<f32>(ms + 0x38), GRef<f32>(ms + 0x3C)}};
    StoreVf0(kSpr + 0x40);
    GRef<f32>(kSpr + 0x44) = y;
    GRef<f32>(kSpr + 0x40) = K(0x00410184);
    // Packs the 12.4 x, y of an ftoi4 result as the MMI code tests it: non-zero when x or y >= 4096 px
    // or negative.
    auto out_of_range = [](s32 fx, s32 fy) {
        return ((static_cast<u32>(fx) >> 16) | ((static_cast<u32>(fy) >> 16) << 16)) != 0;
    };
    for (s32 row = 0; row < 5; ++row) {
        s32 count;
        if ((row & 1) != 0) {
            GRef<f32>(kSpr + 0x48) = K(0x00410188);
            count = 8;
        } else {
            GRef<f32>(kSpr + 0x48) = K(0x0041018C);
            count = 9;
        }
        for (s32 i = 0; i < count; ++i) {
            const vu0::Vec4 pt{GRef<f32>(kSpr + 0x40), GRef<f32>(kSpr + 0x44), GRef<f32>(kSpr + 0x48), GRef<f32>(kSpr + 0x4C)};
            vu0::Vec4 c = vu0::MulMatrix(m[0], m[1], m[2], m[3], pt);
            const f32 q = vu0::Div(1.0f, c.w);
            c.x = vu0::Mul(c.x, q);
            c.y = vu0::Mul(c.y, q);
            c.z = vu0::Mul(c.z, q);
            const s32 ix = vu0::FloatToInt4(c.x), iy = vu0::FloatToInt4(c.y);
            const s32 iz = vu0::FloatToInt4(c.z), iw = vu0::FloatToInt4(c.w);
            bool skip = out_of_range(ix, iy) || iz < 0 || iw < 0;
            if (!skip) {
                const f32 dist = vu0::Add(0.0f, c.w);
                const f32 level = fpu::Sub(K(0x00410194), fpu::Mul(dist, K(0x00410190)));
                vu0::Vec4 d{vu0::Mul(scale.x, level), vu0::Mul(scale.y, level), vu0::Mul(scale.z, level), vu0::Mul(scale.w, level)};
                d.x = vu0::Mul(d.x, q);
                d.y = vu0::Mul(d.y, q);
                const vu0::Vec4 lo{vu0::Sub(c.x, d.x), vu0::Sub(c.y, d.y), vu0::Sub(c.z, d.z), vu0::Sub(c.w, d.w)};
                const vu0::Vec4 hi{vu0::Add(c.x, d.x), vu0::Add(c.y, d.y), vu0::Add(c.z, d.z), vu0::Add(c.w, d.w)};
                const s32 l[4] = {vu0::FloatToInt4(lo.x), vu0::FloatToInt4(lo.y), vu0::FloatToInt4(lo.z), vu0::FloatToInt4(lo.w)};
                const s32 r[4] = {vu0::FloatToInt4(hi.x), vu0::FloatToInt4(hi.y), vu0::FloatToInt4(hi.z), vu0::FloatToInt4(hi.w)};
                skip = out_of_range(l[0], l[1]) || out_of_range(r[0], r[1]);
                if (!skip) {
                    u64 rgba;
                    if (1.0f < level) {
                        rgba = LerpColor(fpu::Mul(fpu::Sub(K(0x00410198), level), K(0x0041019C)), 0, Sext(0x80808080));
                    } else {
                        rgba = LerpColor(level, 0x60606060, Sext(0x80808080));
                    }
                    if (rgba != 0) {
                        GRef<u64>(p) = 0x2400000000008001ull;  // REGLIST {PRIM, RGBAQ}
                        GRef<u64>(p + 8) = 0x10;
                        GRef<u64>(p + 0x10) = 0x356;
                        GRef<u64>(p + 0x18) = rgba & 0xFFFFFFFFull;
                        p += 0x20;
                        // x, y (16 bits each) and z >> 3 (logical) in the upper word, as pextuw builds it.
                        auto xyz = [](const s32 e[4]) {
                            return static_cast<u64>((static_cast<u32>(e[0]) & 0xFFFF) | (static_cast<u32>(e[1]) & 0xFFFF) << 16) |
                                   static_cast<u64>(static_cast<u32>(e[2]) >> 3) << 32;
                        };
                        p = BuildClippedSpriteStripsUv(p, xyz(l), xyz(r), 0x00080008, 0x03F803F8, GRef<u64>(kSpr + 0xE0),
                                                       GRef<u64>(kSpr + 0xE8));
                    }
                }
            }
            GRef<f32>(kSpr + 0x48) = fpu::Add(GRef<f32>(kSpr + 0x48), K(0x004101A0));
        }
        GRef<f32>(kSpr + 0x40) = fpu::Add(GRef<f32>(kSpr + 0x40), K(0x004101A4));
    }
    const u32 end = CloseDmaPacket(pkt, p);
    LinkPacket(list, index, start, end);
}

// EE: 0x0022FCD0
// Background: a draw environment and a full-screen sprite (PRIM sprite, Z 0x...) clearing the frame
// and Z buffer.
void ClearViewerBackground(u32 list, s32 index) {
    const u32 start = Ctx()->cur_buf.addr;
    const u32 p = BuildMenuDrawEnv(start + 0x10, 0x280, 0x1C0);
    GRef<u64>(p + 8) = 0xE;
    GRef<u64>(p + 0x10) = 0xFF000000000A00DCull;
    GRef<u64>(p + 0x18) = 0x4D;
    GRef<u64>(p) = 0x1103400000000003ull;
    GRef<u64>(p + 0x28) = 0x19;
    GRef<u64>(p + 0x38) = 1;
    GRef<u64>(p + 0x20) = 0;
    GRef<u64>(p + 0x30) = 0;
    const u32 q = BuildSpriteStrips(p + 0x40, 0, 0x1C002800);
    const u32 end = CloseDmaPacket(start, q);
    LinkPacket(list, index, start, end);
}

// EE: 0x0022FDA0
// The viewer scene into `list`: with `parts` != 0 the background and the five pass states, then the
// room (bit 0) with its ceiling lights (bit 1), the aircraft (bit 2, when highlighted: reflection,
// shadow unless off, aircraft) and the special weapons (bit 3); always the black fade (bucket 6).
void DrawViewerScene(u32 list, u32 v, u32 parts) {
    u32 mask = GRef<u16>(v + 0x9AC);
    const s32 no_shadow = GRef<s32>(v + 0x9B4);
    if (parts != 0) {
        ClearViewerBackground(list, 0);
        WriteViewerPassState(list, 0, GRef<u32>(v + 0x9A4), 2);
        WriteViewerPassState(list, 1, GRef<u32>(v + 0x9A4), 1);
        WriteViewerPassState(list, 2, GRef<u32>(v + 0x9A4), 1);
        WriteViewerPassState(list, 3, GRef<u32>(v + 0x9A4), 0);
        WriteViewerPassState(list, 4, GRef<u32>(v + 0x9A4), 1);
        if ((parts & 1) != 0) {
            DrawHangarRoom(list, 0, v);
            DrawHangarRoomGlass(list, 2, v);
            if ((parts & 2) != 0) {
                DrawCeilingLights(K(0x004101A8), 260.0f, K(0x004101AC), list, 0, v);
                DrawCeilingLights(K(0x004101B0), 240.0f, 1.0f, list, 4, v);
            }
        }
        if ((parts & 4) != 0 && (mask & 1) != 0) {
            DrawAircraftReflection(list, 1, v);
            if (no_shadow == 0) DrawAircraftShadow(list, 3, v);
            DrawAircraft(list, 4, v);
        }
        mask >>= 1;
        if ((parts & 8) != 0) {
            DrawSpecialReflections(list, 1, v, mask);
            if (no_shadow == 0) DrawSpecialShadows(list, 3, v, mask);
            DrawSpecials(list, 4, v, mask);
        }
    }
    DrawBlackFade(GRef<f32>(v + 0x26C), list, 6);
}

// EE: 0x0022FF78
// Fade, loading, the fade-in once loaded and the menu fade is done, and the camera.
void UpdateAircraftViewer(u32 v) {
    UpdateFade(v + 0x260);
    UpdateViewerLoad(v);
    s32 state = GRef<s32>(v + 0x97C);
    if (state == 1) {
        if ((GRef<u32>(v + 0x260) & 4) != 0) {
            state = 2;
            GRef<s32>(v + 0x97C) = 2;
        }
    } else if (state == 0 && GRef<u32>(v + 0x980) > 1 && (GRef<u32>(g_pMenuFade()) & 4) != 0) {
        StartFade(0.5f, v + 0x260, 0);
        GRef<s32>(v + 0x97C) = 1;
        state = 1;
    }
    if (state != 0) UpdateViewerCamera3D(v + 0x270, v + 0x430);
}

// EE: 0x00230048
// Shown: everything; while (re)loading: the room only.
void DrawAircraftViewer(u32 list, u32 v) {
    const u32 state = GRef<u32>(v + 0x980);
    if (state == 2) {
        DrawViewerScene(list, v, 0xF);
    } else if (state > 1 && state < 7) {
        DrawViewerScene(list, v, 3);
    }
}

// EE: 0x00230080
// Another aircraft (and pending colour) while shown: reload.
void SetAircraftViewerAircraft(u32 v, s32 aircraft, u32 colour) {
    if (GRef<u32>(v + 0x980) - 2u < 2) {
        GRef<s32>(v + 0x984) = aircraft;
        GRef<u32>(v + 0x980) = 4;
        GRef<u32>(v + 0x9B0) = colour;
    }
}

// EE: 0x002300A8
// Default look-at: eye (0, k, 1600), target (0, -160, 240), up.
void ResetViewerLookAt(u32 look) {
    GRef<f32>(look + 4) = K(0x004101B4);
    GRef<u32>(look + 8) = 0x44C80000;
    GRef<u32>(look + 0x14) = 0xC3200000;
    GRef<u32>(look + 0x18) = 0x43700000;
    GRef<u32>(look + 0x20) = 0x44660000;
    GRef<u32>(look + 0x24) = 0;
    GRef<u32>(look) = 0;
    GRef<u32>(look + 0x10) = 0;
    GRef<u32>(look + 0x28) = 0;
    GRef<u32>(look + 0x2C) = 0;
}

// EE: 0x002300F8
// Creates the viewer (0x9D0 bytes): clear colour (16, 16, 16), menu draw mode 2 with Z test in both
// drawing environments, menu fade requests cleared, init, load state 0, the DATA item table cleared.
u32 CreateAircraftViewer(s32 aircraft, u32 colour) {
    MainContext* ctx = Ctx();
    ctx->clear_rgba[0] = 0x10;
    ctx->clear_rgba[1] = 0x10;
    ctx->clear_rgba[2] = 0x10;
    ctx->clear_rgba[3] = 0x80;
    BuildDrawEnvPackets();
    Ctx()->draw_mode = 2;
    const u32 fade = g_pMenuFade();
    ctx = Ctx();
    ctx->drawenv_pkt[4] = 0x1310000DCull;
    GRef<u16>(fade + 0x32) = 0;
    ctx->drawenv_pkt[0x14] = 0x1310000DCull;
    GRef<u16>(fade + 0x30) = 0;
    const u32 v = WorkHeapAlloc(0x9D0, 0x10);
    InitAircraftViewer(v, aircraft, colour);
    GRef<u32>(v + 0x980) = 0;
    for (u32 o = 0x25C; o >= 0x220; o -= 4) GRef<u32>(v + o) = 0;
    return v;
}

// EE: 0x002301C8
// Aircraft (1-21, else 2) and colour (0-2, else 0) without reloading. Not called in the ELF.
void SetAircraftViewerSelection(u32 v, s32 aircraft, u32 colour) {
    s32 a = 2;
    if (static_cast<u32>(aircraft - 1) < 0x15) a = aircraft;
    if (colour > 2) colour = 0;
    GRef<s32>(v + 0x984) = a;
    GRef<u32>(v + 0x988) = colour;
    SetViewerSpecialList(v);
}

}  // namespace ac04::game
