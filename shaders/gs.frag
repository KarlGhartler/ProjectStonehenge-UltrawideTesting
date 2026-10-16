#version 450
// GS pixel pipeline subset (docs/platform/gs-subset.md): texture fetch with the GS wrap modes and
// nearest/bilinear filtering done by hand (texelFetch from the per-frame atlas or a frame-buffer
// copy), the texture function (MODULATE/DECAL/HIGHLIGHT/HIGHLIGHT2) in 8-bit integer arithmetic,
// and the alpha test. Blending is fixed-function with dual-source blending: output 0 is the colour
// and the alpha written to the frame buffer, output 1 the blend factor (As/128 or FIX/128).

layout(location = 0) in vec4 v_color;
layout(location = 1) noperspective in vec3 v_tex;

layout(location = 0, index = 0) out vec4 o_color;
layout(location = 0, index = 1) out vec4 o_factor;

layout(binding = 0) uniform sampler2D atlas;
layout(binding = 1) uniform sampler2D feedback;

layout(push_constant) uniform Params {
    vec2 target_size;
    ivec4 rect;      // decoded texels in the atlas: x, y, width, height
    ivec2 origin;    // texture coordinate of the rect's first texel
    ivec2 tex_size;  // 2^TW, 2^TH
    ivec4 region;    // MINU, MAXU, MINV, MAXV
    int flags;       // see gs_vulkan.cpp
    int aref;
    float fix;
} pc;

int Wrap(int c, int mode, int size, int mn, int mx) {
    if (mode == 0) return c & (size - 1);
    if (mode == 1) return clamp(c, 0, size - 1);
    if (mode == 2) return clamp(c, mn, mx);
    return (c & mn) | mx;
}

vec4 Fetch(ivec2 t) {
    t.x = Wrap(t.x, (pc.flags >> 12) & 3, pc.tex_size.x, pc.region.x, pc.region.y);
    t.y = Wrap(t.y, (pc.flags >> 14) & 3, pc.tex_size.y, pc.region.z, pc.region.w);
    if ((pc.flags & (1 << 16)) != 0) {
        return floor(texelFetch(feedback, t, 0) * 255.0 + 0.5);
    }
    ivec2 a = clamp(t - pc.origin, ivec2(0), pc.rect.zw - 1);
    return floor(texelFetch(atlas, pc.rect.xy + a, 0) * 255.0 + 0.5);
}

void main() {
    vec4 c = floor(v_color + 0.5);
    if ((pc.flags & 1) != 0) {
        vec2 uv = (pc.flags & 2) != 0 ? v_tex.xy : v_tex.xy / v_tex.z * vec2(pc.tex_size);
        // The GS addresses texels in fixed point with 4 fractional bits. Rounding to that grid
        // removes float interpolation error: a 1:1 sprite gives exact integers at pixel positions,
        // and 0.9999 would pick the texel on the left (seams every 16 pixels in movies).
        uv = floor(uv * 16.0 + 0.5) / 16.0;
        vec4 t;
        if ((pc.flags & (1 << 5)) != 0) {
            vec2 b = uv - 0.5;
            ivec2 i = ivec2(floor(b));
            vec2 f = floor(fract(b) * 16.0) / 16.0;  // GS: 4-bit weights
            vec4 t00 = Fetch(i), t10 = Fetch(i + ivec2(1, 0));
            vec4 t01 = Fetch(i + ivec2(0, 1)), t11 = Fetch(i + ivec2(1, 1));
            t = floor(mix(mix(t00, t10, f.x), mix(t01, t11, f.x), f.y));
        } else {
            t = Fetch(ivec2(floor(uv)));
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
    if ((pc.flags & (1 << 6)) != 0) {
        int atst = (pc.flags >> 7) & 7;
        int av = int(c.a);
        bool pass = atst == 1 || (atst == 2 && av < pc.aref) || (atst == 3 && av <= pc.aref) ||
                    (atst == 4 && av == pc.aref) || (atst == 5 && av >= pc.aref) ||
                    (atst == 6 && av > pc.aref) || (atst == 7 && av != pc.aref);
        // Flag 19: second draw of TEST.AFAIL RGB_ONLY, which keeps only the failing pixels.
        if (pass == ((pc.flags & (1 << 19)) != 0)) discard;
    }
    // Flag 18: C = Ad blending (gs_vulkan.cpp MapBlend) needs Cs / 128 against DST_ALPHA = Ad / 255.
    o_color = vec4(c.rgb / (((pc.flags & (1 << 18)) != 0) ? 128.0 : 255.0), c.a / 255.0);
    float factor = ((pc.flags >> 17) & 1) != 0 ? pc.fix / 128.0 : c.a / 128.0;
    o_factor = vec4(factor);
}
