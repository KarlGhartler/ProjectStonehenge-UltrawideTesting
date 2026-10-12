// Small math helpers on the boot path.
#include "game/game.h"

#include "common/ee_math.h"
#include "sce/sce.h"

namespace ac04::game {

using guest::GRef;

// EE: 0x00102D28
// Rotation matrix from Euler angles at `angles` (x, y, z): each wrapped into (-pi, pi] with the
// .lit4 constants 0x0040DA14.., then RotZ(z) applied to m1 -> m0, RotX(x), RotY(y).
void func_00102D28(u32 m0, u32 m1, u32 angles) {
    f32 x = GRef<f32>(angles);
    f32 y = GRef<f32>(angles + 4);
    f32 z = GRef<f32>(angles + 8);
    while (GRef<f32>(0x0040DA14) < x) x = fpu::Sub(x, GRef<f32>(0x0040DA18));
    while (x < GRef<f32>(0x0040DA1C)) x = fpu::Add(x, GRef<f32>(0x0040DA20));
    while (GRef<f32>(0x0040DA24) < y) y = fpu::Sub(y, GRef<f32>(0x0040DA28));
    while (y < GRef<f32>(0x0040DA2C)) y = fpu::Add(y, GRef<f32>(0x0040DA30));
    while (GRef<f32>(0x0040DA34) < z) z = fpu::Sub(z, GRef<f32>(0x0040DA38));
    while (z < GRef<f32>(0x0040DA3C)) z = fpu::Add(z, GRef<f32>(0x0040DA40));
    sceVu0RotMatrixZ(m0, m1, z);
    sceVu0RotMatrixX(m0, m0, x);
    sceVu0RotMatrixY(m0, m0, y);
}

// EE: 0x00104F10
// Distance between two points (x, y, z): mula/madda/madd in the order x, y, z, then sqrt.
f32 func_00104F10(u32 a, u32 b) {
    const f32 dz = fpu::Sub(GRef<f32>(a + 8), GRef<f32>(b + 8));
    const f32 dx = fpu::Sub(GRef<f32>(a), GRef<f32>(b));
    const f32 dy = fpu::Sub(GRef<f32>(a + 4), GRef<f32>(b + 4));
    f32 acc = fpu::Mul(dx, dx);
    acc = fpu::MulAdd(acc, dy, dy);
    return fpu::Sqrt(fpu::MulAdd(acc, dz, dz));
}

}  // namespace ac04::game
