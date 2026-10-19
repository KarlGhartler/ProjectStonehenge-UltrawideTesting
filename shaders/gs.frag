#version 450
// GS pixel pipeline subset (docs/platform/gs-subset.md): texture fetch with the GS wrap modes and
// nearest/bilinear filtering done by hand (texelFetch from the per-frame atlas or a frame-buffer
// copy), the texture function (MODULATE/DECAL/HIGHLIGHT/HIGHLIGHT2) in 8-bit integer arithmetic,
// fog (FGE), the alpha test and the destination alpha test (DATE). Blending is fixed-function with dual-source blending: output 0 is the colour
// and the alpha written to the frame buffer, output 1 the blend factor (As/128 or FIX/128).

layout(location = 0) in vec4 v_color;
layout(location = 1) noperspective in vec3 v_tex;
layout(location = 2) in float v_fog;

layout(location = 0, index = 0) out vec4 o_color;
layout(location = 0, index = 1) out vec4 o_factor;

layout(binding = 0) uniform sampler2D atlas;
layout(binding = 1) uniform sampler2D feedback;
layout(binding = 2) uniform sampler2D dst_copy;  // target region before a DATE draw (flag 22)

layout(push_constant) uniform Params {
    vec2 target_size;
    ivec4 rect;      // decoded texels in the atlas: x, y, width, height
    ivec2 origin;    // texture coordinate of the rect's first texel
    ivec2 tex_size;  // 2^TW, 2^TH
    ivec4 region;    // MINU, MAXU, MINV, MAXV
    int flags;       // see gs_vulkan.cpp
    int aref;
    float fix;
    int fogcol;      // FOGCOL (flag 20)
    int tex1;        // MMIN 0-2, mip levels 3-5, LCM 6, L 7-8, K 9-20 (signed 1/16), MMAG 21
} pc;

int Wrap(int c, int mode, int size, int mn, int mx) {
    if (mode == 0) return c & (size - 1);
    if (mode == 1) return clamp(c, 0, size - 1);
    if (mode == 2) return clamp(c, mn, mx);
    return (c & mn) | mx;
}

// Texel t of mipmap level `level`. Level 0 is the decoded rectangle (or the frame-buffer copy);
// levels 1.. are stored whole to the right of level 0, stacked top to bottom (gs_state.h
// DecodedTexture). HYPOTHESIS: the region clamp/repeat values scale with the level (>> level).
vec4 Fetch(ivec2 t, int level) {
    ivec2 size = max(pc.tex_size >> level, ivec2(1));
    t.x = Wrap(t.x, (pc.flags >> 12) & 3, size.x, pc.region.x >> level, pc.region.y >> level);
    t.y = Wrap(t.y, (pc.flags >> 14) & 3, size.y, pc.region.z >> level, pc.region.w >> level);
    if (level == 0) {
        if ((pc.flags & (1 << 16)) != 0) {
            return floor(texelFetch(feedback, t, 0) * 255.0 + 0.5);
        }
        ivec2 a = clamp(t - pc.origin, ivec2(0), pc.rect.zw - 1);
        return floor(texelFetch(atlas, pc.rect.xy + a, 0) * 255.0 + 0.5);
    }
    int y = 0;
    for (int k = 1; k < level; ++k) y += max(pc.tex_size.y >> k, 1);
    ivec2 a = clamp(t, ivec2(0), size - 1);
    return floor(texelFetch(atlas, pc.rect.xy + ivec2(pc.rect.z, y) + a, 0) * 255.0 + 0.5);
}

// Level `level` sampled at level-0 texel coordinates uv, nearest or bilinear.
vec4 Sample(vec2 uv, int level, bool linear) {
    vec2 u = uv / float(1 << level);
    if (linear) {
        vec2 b = u - 0.5;
        ivec2 i = ivec2(floor(b));
        vec2 f = floor(fract(b) * 16.0) / 16.0;  // GS: 4-bit weights
        vec4 t00 = Fetch(i, level), t10 = Fetch(i + ivec2(1, 0), level);
        vec4 t01 = Fetch(i + ivec2(0, 1), level), t11 = Fetch(i + ivec2(1, 1), level);
        return floor(mix(mix(t00, t10, f.x), mix(t01, t11, f.x), f.y));
    }
    return Fetch(ivec2(floor(u)), level);
}

void main() {
    // Flag 22: destination alpha test. The pixel is drawn only when bit 7 of the stored alpha equals
    // DATM (flag 23).
    if ((pc.flags & (1 << 22)) != 0) {
        float da = floor(texelFetch(dst_copy, ivec2(gl_FragCoord.xy), 0).a * 255.0 + 0.5);
        if ((da >= 128.0) != ((pc.flags & (1 << 23)) != 0)) discard;
    }
    vec4 c = floor(v_color + 0.5);
    if ((pc.flags & 1) != 0) {
        bool fst = (pc.flags & 2) != 0;
        vec2 uv = fst ? v_tex.xy : v_tex.xy / v_tex.z * vec2(pc.tex_size);
        // The GS addresses texels in fixed point with 4 fractional bits. Rounding to that grid
        // removes float interpolation error: a 1:1 sprite gives exact integers at pixel positions,
        // and 0.9999 would pick the texel on the left (seams every 16 pixels in movies).
        uv = floor(uv * 16.0 + 0.5) / 16.0;
        // Filter choice (TEX1): LOD = K with LCM = 1, else log2(1/|Q|) * 2^L + K. LOD <= 0 magnifies
        // (MMAG); above, MMIN: 0 nearest, 1 linear, 2/4 nearest/linear in the nearest mip level
        // (LOD rounded), 3/5 nearest/linear in the two nearest levels, blended (trilinear). LOD is
        // clamped to the decoded levels (MXL).
        int mmin = pc.tex1 & 7;
        int levels = (pc.tex1 >> 3) & 7;
        float k = float((pc.tex1 << 11) >> 20) / 16.0;
        float q = fst ? 1.0 : abs(v_tex.z);
        float lod = ((pc.tex1 >> 6) & 1) != 0 ? k : log2(1.0 / max(q, 1e-30)) * float(1 << ((pc.tex1 >> 7) & 3)) + k;
        bool min_linear = mmin == 1 || mmin == 4 || mmin == 5;
        vec4 t;
        if (lod <= 0.0) {
            t = Sample(uv, 0, ((pc.tex1 >> 21) & 1) != 0);
        } else if (mmin < 2 || levels == 0) {
            t = Sample(uv, 0, min_linear);
        } else {
            float l = min(lod, float(levels));
            if (mmin == 2 || mmin == 4) {
                t = Sample(uv, min(int(floor(l + 0.5)), levels), min_linear);
            } else {
                int li = int(floor(l));
                if (li >= levels) {
                    t = Sample(uv, levels, min_linear);
                } else {
                    t = floor(mix(Sample(uv, li, min_linear), Sample(uv, li + 1, min_linear), l - float(li)));
                }
            }
        }
        int tfx = (pc.flags >> 2) & 3;
        bool tcc = (pc.flags & (1 << 4)) != 0;
        vec3 rgb;
        float a;
        if (tfx == 1) {  // DECAL
            rgb = t.rgb;
            a = tcc ? t.a : c.a;
        } else {
            rgb = floor(t.rgb * c.rgb / 128.0);
            if (tfx >= 2) rgb += c.a;  // HIGHLIGHT, HIGHLIGHT2
            if (tfx == 0) a = tcc ? floor(t.a * c.a / 128.0) : c.a;
            else if (tfx == 2) a = tcc ? t.a + c.a : c.a;
            else a = tcc ? t.a : c.a;
        }
        c = vec4(min(rgb, 255.0), min(a, 255.0));
    }
    // Flag 20: fog (PRIM.FGE). The colour moves toward FOGCOL as F falls (F = 255: no fog); alpha is
    // not fogged. F is interpolated across the primitive.
    if ((pc.flags & (1 << 20)) != 0) {
        vec3 fc = vec3(pc.fogcol & 0xFF, (pc.fogcol >> 8) & 0xFF, (pc.fogcol >> 16) & 0xFF);
        c.rgb = floor(mix(fc, c.rgb, clamp(v_fog, 0.0, 255.0) / 255.0));
    }
    if ((pc.flags & (1 << 6)) != 0) {
        int atst = (pc.flags >> 7) & 7;
        int av = int(c.a);
        bool pass = atst == 1 || (atst == 2 && av < pc.aref) || (atst == 3 && av <= pc.aref) ||
                    (atst == 4 && av == pc.aref) || (atst == 5 && av >= pc.aref) ||
                    (atst == 6 && av > pc.aref) || (atst == 7 && av != pc.aref);
        // Flag 19: second draw of TEST.AFAIL RGB_ONLY, which keeps only the failing pixels.
        if (pass == ((pc.flags & (1 << 19)) != 0)) discard;
    }
    // Flag 25: PRIM.AA1 without ABE blends with As = coverage, 1.0 inside the primitive.
    float factor = ((pc.flags >> 17) & 1) != 0 ? pc.fix / 128.0 : (((pc.flags >> 25) & 1) != 0 ? 1.0 : c.a / 128.0);
    // Flag 24: 16-bit frame buffer (CT16/CT16S). The GS stores the colour's top 5 bits and alpha
    // bit 7; the RGBA8 target keeps them as multiples of 8 and 0x80/0 (gs_conv.comp reads them back).
    if ((pc.flags & (1 << 24)) != 0) c = vec4(floor(c.rgb / 8.0) * 8.0, c.a >= 128.0 ? 128.0 : 0.0);
    // Flag 18: C = Ad blending (gs_vulkan.cpp MapBlend) needs Cs / 128 against DST_ALPHA = Ad / 255.
    o_color = vec4(c.rgb / (((pc.flags & (1 << 18)) != 0) ? 128.0 : 255.0), c.a / 255.0);
    o_factor = vec4(factor);
}
