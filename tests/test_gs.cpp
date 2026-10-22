// GS local memory and GIF parsing (src/platform/render/gs/). Synthetic data only.
#include <doctest/doctest.h>

#include <algorithm>
#include <cstring>
#include <iterator>
#include <memory>
#include <random>
#include <set>
#include <vector>

#include "platform/render/gs/gs_memory.h"
#include "platform/render/gs/gs_state.h"
#include "platform/render/movie_texel_map.h"

using namespace ac04::render::gs;

namespace {

// Packet builder: 64-bit words, GIF tags and A+D pairs.
struct Packet {
    std::vector<std::uint64_t> w;
    void Tag(std::uint32_t nloop, bool eop, std::uint32_t flg, std::uint32_t nreg, std::uint64_t regs,
             bool pre = false, std::uint32_t prim = 0) {
        w.push_back(static_cast<std::uint64_t>(nloop) | static_cast<std::uint64_t>(eop) << 15 |
                    static_cast<std::uint64_t>(pre) << 46 | static_cast<std::uint64_t>(prim) << 47 |
                    static_cast<std::uint64_t>(flg) << 58 | static_cast<std::uint64_t>(nreg) << 60);
        w.push_back(regs);
    }
    void Ad(std::uint32_t reg, std::uint64_t value) {
        w.push_back(value);
        w.push_back(reg);
    }
    const std::uint8_t* Data() const { return reinterpret_cast<const std::uint8_t*>(w.data()); }
    std::size_t Bytes() const { return w.size() * 8; }
};

}  // namespace

TEST_CASE("GS memory: page and block layout") {
    // CT32: block 1 is the second 8x8 block of the first row; page 1 starts after 32 blocks.
    CHECK(GsMemory::PixelAddress(kPsmCt32, 0, 1, 8, 0) == 256);
    CHECK(GsMemory::PixelAddress(kPsmCt32, 0, 1, 0, 8) == 2 * 256);
    CHECK(GsMemory::PixelAddress(kPsmCt32, 0, 2, 64, 0) == 8192);
    CHECK(GsMemory::PixelAddress(kPsmCt32, 4, 1, 0, 0) == 4 * 256);
    // T8: 16x16 blocks, T4: 32x16 blocks (block 2 is below block 0 in T4).
    CHECK(GsMemory::PixelAddress(kPsmT8, 0, 2, 16, 0) == 256);
    CHECK(GsMemory::PixelAddress(kPsmT4, 0, 2, 32, 0) == 2 * 256);
    CHECK(GsMemory::PixelAddress(kPsmT4, 0, 2, 0, 16) == 256);
    // Every pixel of a 64x32 CT32 page maps to a distinct word inside the page.
    std::set<std::uint32_t> seen;
    for (std::uint32_t y = 0; y < 32; ++y)
        for (std::uint32_t x = 0; x < 64; ++x) seen.insert(GsMemory::PixelAddress(kPsmCt32, 0, 1, x, y));
    CHECK(seen.size() == 64 * 32);
    CHECK(*seen.rbegin() == 8192 - 4);
}

TEST_CASE("GS memory: image writes round trip per format") {
    GsMemory m;
    std::vector<std::uint8_t> t4(32 * 16 / 2);
    for (std::size_t i = 0; i < t4.size(); ++i) t4[i] = static_cast<std::uint8_t>(i * 7);
    m.WriteImage(0x100, 2, kPsmT4, 32, 16, 32, 16, 0, t4.data(), static_cast<std::uint32_t>(t4.size()));
    CHECK(m.ReadPixel(kPsmT4, 0x100, 2, 32, 16) == (t4[0] & 0xF));
    CHECK(m.ReadPixel(kPsmT4, 0x100, 2, 33, 16) == (t4[0] >> 4));
    CHECK(m.ReadPixel(kPsmT4, 0x100, 2, 32 + 5, 16 + 3) == ((t4[(3 * 32 + 5) / 2] >> 4) & 0xF));
    // A transfer split over two packets continues where the first ended.
    std::vector<std::uint8_t> ct32(4 * 4 * 4);
    for (std::size_t i = 0; i < ct32.size(); ++i) ct32[i] = static_cast<std::uint8_t>(i);
    std::uint32_t pos = m.WriteImage(0, 1, kPsmCt32, 0, 0, 4, 4, 0, ct32.data(), 32);
    CHECK(pos == 8);
    pos = m.WriteImage(0, 1, kPsmCt32, 0, 0, 4, 4, pos, ct32.data() + 32, 32);
    CHECK(pos == 16);
    CHECK(m.ReadPixel(kPsmCt32, 0, 1, 3, 3) == 0x3F3E3D3Cu);
}

TEST_CASE("GS memory: ReadRect gives the ReadPixel values for every format") {
    GsMemory m;
    std::mt19937 rng(20261019);
    std::vector<std::uint8_t> image(GsMemory::kSize);
    for (auto& b : image) b = static_cast<std::uint8_t>(rng());
    m.LoadImage(image.data());
    const std::uint32_t formats[] = {kPsmCt32, kPsmCt24, kPsmCt16, kPsmCt16S, kPsmT8, kPsmT4, kPsmT8H,
                                     kPsmT4HL, kPsmT4HH, kPsmZ32, kPsmZ24, kPsmZ16, kPsmZ16S};
    for (std::uint32_t psm : formats) {
        for (int trial = 0; trial < 8; ++trial) {
            const std::uint32_t bp = rng() % 0x4000, bw = 1 + rng() % 16;
            const std::uint32_t x0 = rng() % 600, y0 = rng() % 600, w = 1 + rng() % 70, h = 1 + rng() % 40;
            std::vector<std::uint32_t> out(static_cast<std::size_t>(w) * h);
            m.ReadRect(psm, bp, bw, x0, y0, w, h, out.data());
            bool same = true;
            for (std::uint32_t y = 0; y < h; ++y)
                for (std::uint32_t x = 0; x < w; ++x)
                    same = same && out[y * w + x] == m.ReadPixel(psm, bp, bw, x0 + x, y0 + y);
            CHECK(same);
        }
    }
}

TEST_CASE("GS memory: image writes in runs equal pixel-by-pixel writes") {
    // WriteImage copies contiguous block-row runs at once; the result must equal WritePixel per
    // pixel, for unaligned rectangles, wrap-around at x 2048 and uploads split over packets.
    std::mt19937 rng(7);
    const std::uint32_t formats[] = {kPsmCt32, kPsmCt24, kPsmCt16, kPsmCt16S, kPsmT8, kPsmT4, kPsmZ32, kPsmZ16S};
    for (int iter = 0; iter < 200; ++iter) {
        const std::uint32_t psm = formats[rng() % 8];
        const int bpp = BitsPerPixel(psm);
        const std::uint32_t bp = rng() % 0x3000, bw = 1 + rng() % 16;
        const std::uint32_t sx = (iter % 10 == 0) ? 2040 + rng() % 8 : rng() % 600, sy = rng() % 400;
        const std::uint32_t w = 1 + rng() % 70, h = 1 + rng() % 20;
        std::vector<std::uint8_t> data((static_cast<std::size_t>(w) * h * bpp + 7) / 8);
        for (auto& b : data) b = static_cast<std::uint8_t>(rng());
        auto a = std::make_unique<GsMemory>();
        auto b = std::make_unique<GsMemory>();
        std::uint32_t pos = 0, off = 0;
        while (off < data.size()) {
            std::uint32_t n = 1 + rng() % 97;
            if (bpp == 4) n &= ~1u, n = n == 0 ? 2 : n;
            if (bpp >= 8) n = n / (bpp / 8) * (bpp / 8), n = n == 0 ? bpp / 8 : n;
            if (bpp == 24) n = 3 * (1 + rng() % 30);
            n = std::min<std::uint32_t>(n, static_cast<std::uint32_t>(data.size()) - off);
            pos = a->WriteImage(bp, bw, psm, sx, sy, w, h, pos, data.data() + off, n);
            off += n;
        }
        for (std::uint32_t i = 0; i < w * h; ++i) {
            std::uint32_t v = 0;
            if (bpp == 4) v = (data[i >> 1] >> ((i & 1) * 4)) & 0xF;
            else for (int k = 0; k < bpp / 8; ++k) v |= static_cast<std::uint32_t>(data[i * (bpp / 8) + k]) << (8 * k);
            b->WritePixel(psm, bp, bw, (sx + i % w) & 2047, (sy + i / w) & 2047, v);
        }
        CHECK(pos == w * h);
        CHECK(std::memcmp(a->Data(), b->Data(), GsMemory::kSize) == 0);
    }
}

TEST_CASE("GIF: PACKED sprite, REGLIST, IMAGE upload and CLUT load") {
    GsState gs;
    Packet p;
    // CLUT: 16 CT32 entries as an 8x2 image at block 0x3000.
    p.Tag(4, false, 0, 1, 0xE);
    p.Ad(0x50, static_cast<std::uint64_t>(0x3000) << 32 | 1ull << 48);  // BITBLTBUF DBP 0x3000 DBW 1 CT32
    p.Ad(0x51, 0);
    p.Ad(0x52, 8 | 2ull << 32);
    p.Ad(0x53, 0);
    p.Tag(4, false, 2, 0, 0);  // IMAGE, 4 qwords = 16 entries
    for (std::uint32_t i = 0; i < 16; i += 2)
        p.w.push_back(static_cast<std::uint64_t>(0x80000000u | i) | static_cast<std::uint64_t>(0x80000000u | (i + 1)) << 32);
    // T4 texture 4x1 at block 0x100: indices 1, 2, 3, 4.
    p.Tag(4, false, 0, 1, 0xE);
    p.Ad(0x50, static_cast<std::uint64_t>(0x100) << 32 | 2ull << 48 | static_cast<std::uint64_t>(kPsmT4) << 56);
    p.Ad(0x51, 0);
    p.Ad(0x52, 4 | 1ull << 32);
    p.Ad(0x53, 0);
    p.Tag(1, false, 2, 0, 0);
    p.w.push_back(0x4321);
    p.w.push_back(0);
    // TEX0_1 with CLD 1, then a REGLIST sprite: PRIM (sprite, textured, FST), RGBAQ, UV, XYZ2, UV, XYZ2.
    const std::uint64_t tex0 = 0x100 | 2ull << 14 | static_cast<std::uint64_t>(kPsmT4) << 20 | 2ull << 26 |
                               0ull << 30 | 1ull << 34 | static_cast<std::uint64_t>(0x3000) << 37 | 1ull << 61;
    p.Tag(2, false, 0, 1, 0xE);
    p.Ad(0x06, tex0);
    p.Ad(0x4C, 1ull << 16);  // FRAME_1: FBP 0, FBW 1
    p.Tag(1, true, 1, 6, 0x535310);
    p.w.push_back(0x116);                                   // PRIM: sprite, TME, ABE... (CTXT 0)
    p.w.push_back(0x80808080ull);                           // RGBAQ
    p.w.push_back(0);                                       // UV 0,0
    p.w.push_back(0);                                       // XYZ2 0,0
    p.w.push_back(4 * 16 | 1ull * 16 << 16);                // UV 4,1
    p.w.push_back(4 * 16 | 1ull * 16 << 16);                // XYZ2 4,1
    gs.ProcessGif(p.Data(), p.Bytes());
    auto draws = gs.TakeDraws();
    REQUIRE(draws.size() == 1);
    const DrawCommand& d = draws[0];
    CHECK(d.kind == PrimKind::kSprite);
    CHECK(d.textured);
    CHECK(d.fst);
    REQUIRE(d.vertices.size() == 2);
    CHECK(d.vertices[1].x == doctest::Approx(4.0f));
    REQUIRE(static_cast<bool>(d.texture));
    // The decoded texels are CLUT entries 1..4.
    std::uint32_t c;
    std::memcpy(&c, &d.texture->rgba[(0 - d.texture->origin_u) * 4], 4);
    CHECK(c == (0x80000000u | 1));
    std::memcpy(&c, &d.texture->rgba[(3 - d.texture->origin_u) * 4], 4);
    CHECK(c == (0x80000000u | 4));
}

TEST_CASE("GIF: CT24 IMAGE transfer with a pixel split across two packets") {
    // 8x2 CT24 image = 48 bytes = 3 qwords; sent as 1 + 2 qwords, so pixel 5 straddles the packets.
    std::vector<std::uint8_t> px(48);
    for (std::size_t i = 0; i < px.size(); ++i) px[i] = static_cast<std::uint8_t>(i + 1);
    GsState gs;
    Packet p;
    p.Tag(4, false, 0, 1, 0xE);
    p.Ad(0x50, static_cast<std::uint64_t>(0x200) << 32 | 1ull << 48 | static_cast<std::uint64_t>(kPsmCt24) << 56);
    p.Ad(0x51, 0);
    p.Ad(0x52, 8 | 2ull << 32);
    p.Ad(0x53, 0);
    p.Tag(1, false, 2, 0, 0);
    std::uint64_t q[6];
    std::memcpy(q, px.data(), 48);
    p.w.push_back(q[0]);
    p.w.push_back(q[1]);
    p.Tag(2, false, 2, 0, 0);
    for (int i = 2; i < 6; ++i) p.w.push_back(q[i]);
    gs.ProcessGif(p.Data(), p.Bytes());
    const GsMemory& m = gs.Memory();
    CHECK(m.ReadPixel(kPsmCt24, 0x200, 1, 4, 0) == 0x0F0E0Du);  // last whole pixel of packet 1
    CHECK(m.ReadPixel(kPsmCt24, 0x200, 1, 5, 0) == 0x121110u);  // bytes 16 | 17 18
    CHECK(m.ReadPixel(kPsmCt24, 0x200, 1, 7, 1) == 0x302F2Eu);  // last pixel
}

TEST_CASE("GIF: XYOFFSET and triangle strip kicks") {
    GsState gs;
    gs.WriteRegister(0x18, 0x8000 | 0x8000ull << 32);  // XYOFFSET_1 = (2048, 2048)
    gs.WriteRegister(0x00, 4);                         // triangle strip
    auto xy = [](int x, int y) { return static_cast<std::uint64_t>(x * 16) | static_cast<std::uint64_t>(y * 16) << 16; };
    gs.WriteRegister(0x05, xy(2048, 2048));
    gs.WriteRegister(0x05, xy(2058, 2048));
    gs.WriteRegister(0x05, xy(2048, 2058));
    gs.WriteRegister(0x05, xy(2058, 2058));
    auto draws = gs.TakeDraws();
    REQUIRE(draws.size() == 1);  // two triangles merged into one draw
    CHECK(draws[0].vertices.size() == 6);
    CHECK(draws[0].vertices[0].x == doctest::Approx(0.0f));
    CHECK(draws[0].vertices[5].y == doctest::Approx(10.0f));
}

TEST_CASE("GIF: source marks name the guest origin of an upload (movie side path)") {
    // A 16x32 CT32 column upload whose data block is marked as read from guest 0x01000800, then a
    // sprite textured from it; a second, unmarked upload to the same block clears the origin.
    GsState gs;
    auto upload = [](Packet& p) {
        p.Tag(4, false, 0, 1, 0xE);
        p.Ad(0x50, static_cast<std::uint64_t>(0x2300) << 32 | 1ull << 48);  // DBP 0x2300, DBW 1, CT32
        p.Ad(0x51, 0);
        p.Ad(0x52, 16 | 32ull << 32);
        p.Ad(0x53, 0);
        p.Tag(128, false, 2, 0, 0);  // 16 x 32 x 4 bytes = 128 qwords
        for (int i = 0; i < 256; ++i) p.w.push_back(0x8000000080000000ull);
    };
    auto sprite = [](Packet& p) {
        p.Tag(1, false, 0, 1, 0xE);
        p.Ad(0x06, 0x2300 | 1ull << 14 | 4ull << 26 | 5ull << 30);  // TEX0_1: TBP 0x2300, 16x32
        p.Tag(1, true, 1, 6, 0x535310);
        p.w.push_back(0x116);
        p.w.push_back(0x80808080ull);
        p.w.push_back(0);
        p.w.push_back(0);
        p.w.push_back(16 * 16 | 32ull * 16 << 16);
        p.w.push_back(16 * 16 | 32ull * 16 << 16);
    };
    Packet p;
    upload(p);
    const std::uint32_t data_offset = 6 * 16;  // after the A+D tag (1 + 4 qwords) and the IMAGE tag
    sprite(p);
    const std::vector<SourceMark> marks = {{data_offset, 128 * 16, 0x01000800}};
    gs.ProcessGif(p.Data(), p.Bytes(), &marks);
    auto draws = gs.TakeDraws();
    REQUIRE(draws.size() == 1);
    CHECK(draws[0].texture_guest_source == 0x01000800u);
    CHECK(draws[0].texture_guest_width == 16u);
    int x = -1, y = -1;
    // Picture 640x448 at 0x01000000: guest 0x01000800 is column 0, row 32 (32 rows x 64 bytes).
    CHECK(ac04::render::MovieTexelOrigin(0x01000800, 16, 0x01000000, 0x118000, 448, x, y));
    CHECK(x == 0);
    CHECK(y == 32);
    // Column 3 starts at 3 x 448 x 64 bytes.
    CHECK(ac04::render::MovieTexelOrigin(0x01000000 + 3 * 448 * 64, 16, 0x01000000, 0x118000, 448, x, y));
    CHECK(x == 48);
    CHECK(y == 0);
    CHECK_FALSE(ac04::render::MovieTexelOrigin(0x01000000, 32, 0x01000000, 0x118000, 448, x, y));
    CHECK_FALSE(ac04::render::MovieTexelOrigin(0x01118000, 16, 0x01000000, 0x118000, 448, x, y));

    Packet q;
    upload(q);
    sprite(q);
    gs.ProcessGif(q.Data(), q.Bytes());
    draws = gs.TakeDraws();
    REQUIRE(draws.size() == 1);
    CHECK(draws[0].texture_guest_source == 0u);
}

namespace {

// Host->local upload of a w x h rectangle at (x, y) of buffer (bp, bw, psm); `data` holds packed
// pixels as the GIF IMAGE transfer carries them.
void Upload(Packet& p, std::uint32_t bp, std::uint32_t bw, std::uint32_t psm, std::uint32_t x, std::uint32_t y,
            std::uint32_t w, std::uint32_t h, const std::vector<std::uint8_t>& data) {
    p.Tag(4, false, 0, 1, 0xE);
    p.Ad(0x50, static_cast<std::uint64_t>(bp) << 32 | static_cast<std::uint64_t>(bw) << 48 | static_cast<std::uint64_t>(psm) << 56);
    p.Ad(0x51, static_cast<std::uint64_t>(x) << 32 | static_cast<std::uint64_t>(y) << 48);
    p.Ad(0x52, w | static_cast<std::uint64_t>(h) << 32);
    p.Ad(0x53, 0);
    std::vector<std::uint8_t> padded = data;
    padded.resize((padded.size() + 15) / 16 * 16, 0);
    // IMAGE tags of at most 0x7FFF qwords (NLOOP is 15 bits).
    for (std::size_t q = 0; q < padded.size() / 16; q += 0x7FFF) {
        const std::size_t n = std::min<std::size_t>(0x7FFF, padded.size() / 16 - q);
        p.Tag(static_cast<std::uint32_t>(n), false, 2, 0, 0);
        for (std::size_t i = q * 16; i < (q + n) * 16; i += 8) {
            std::uint64_t v;
            std::memcpy(&v, padded.data() + i, 8);
            p.w.push_back(v);
        }
    }
}

// A sprite over the whole 2^tw x 2^th texture (FST UVs), so the decode covers every texel.
void DrawTexture(Packet& p, std::uint64_t tex0, std::uint32_t size_u, std::uint32_t size_v) {
    p.Tag(2, false, 0, 1, 0xE);
    p.Ad(0x06, tex0);
    p.Ad(0x4C, 1ull << 16);
    p.Tag(1, true, 1, 6, 0x535310);
    p.w.push_back(0x116);
    p.w.push_back(0x80808080ull);
    p.w.push_back(0);
    p.w.push_back(0);
    p.w.push_back(static_cast<std::uint64_t>(size_u) * 16 | static_cast<std::uint64_t>(size_v) * 16 << 16);
    p.w.push_back(static_cast<std::uint64_t>(size_u) * 16 | static_cast<std::uint64_t>(size_v) * 16 << 16);
}

std::vector<std::uint8_t> RandomBytes(std::mt19937& rng, std::size_t n) {
    std::vector<std::uint8_t> v(n);
    for (auto& b : v) b = static_cast<std::uint8_t>(rng());
    return v;
}

}  // namespace

TEST_CASE("texture decode: re-decoding only the written blocks equals a full decode") {
    struct Case {
        std::uint32_t psm, bpp;
    };
    for (const Case c : {Case{kPsmT8, 8}, Case{kPsmT4, 4}, Case{kPsmCt32, 32}}) {
        CAPTURE(c.psm);
        std::mt19937 rng(c.psm * 7 + 1);
        const std::uint32_t tbp = 0x1010, tbw = 2, size = 128;  // not page aligned: blocks cross pages
        const std::uint32_t cbp = 0x3000;
        const bool paletted = c.bpp < 16;
        const std::uint64_t tex0 = tbp | static_cast<std::uint64_t>(tbw) << 14 | static_cast<std::uint64_t>(c.psm) << 20 |
                                   7ull << 26 | 7ull << 30 | 1ull << 34 |
                                   (paletted ? static_cast<std::uint64_t>(cbp) << 37 | 1ull << 61 : 0);
        const auto clut = RandomBytes(rng, 256 * 4);
        const auto full = RandomBytes(rng, size * size * c.bpp / 8);
        // Patches: one crossing block edges, one outside the texture in the same pages, one large.
        struct Patch {
            std::uint32_t bp, x, y, w, h;
        };
        const Patch patches[] = {{tbp, 5, 7, 40, 21}, {tbp, 0, 130, 32, 8}, {tbp, 0, 0, 128, 100}};
        std::vector<std::vector<std::uint8_t>> patch_data;
        for (const Patch& q : patches) patch_data.push_back(RandomBytes(rng, q.w * q.h * c.bpp / 8 + 8));

        GsState incremental;
        std::shared_ptr<const DecodedTexture> previous;
        {
            Packet p;
            if (paletted) Upload(p, cbp, 1, kPsmCt32, 0, 0, 16, 16, clut);
            Upload(p, tbp, tbw, c.psm, 0, 0, size, size, full);
            DrawTexture(p, tex0, size, size);
            incremental.ProcessGif(p.Data(), p.Bytes());
            const auto first = incremental.TakeDraws();
            REQUIRE(first.size() == 1);
            previous = first[0].texture;
        }
        for (std::size_t k = 0; k < std::size(patches); ++k) {
            const Patch& q = patches[k];
            CAPTURE(k);
            Packet p;
            std::vector<std::uint8_t> bytes(patch_data[k].begin(), patch_data[k].begin() + q.w * q.h * c.bpp / 8);
            Upload(p, q.bp, tbw, c.psm, q.x, q.y, q.w, q.h, bytes);
            DrawTexture(p, tex0, size, size);
            incremental.ProcessGif(p.Data(), p.Bytes());
            const auto draws = incremental.TakeDraws();
            REQUIRE(draws.size() == 1);
            REQUIRE(static_cast<bool>(draws[0].texture));

            // The same memory built from scratch and decoded once.
            GsState fresh;
            Packet f;
            if (paletted) Upload(f, cbp, 1, kPsmCt32, 0, 0, 16, 16, clut);
            Upload(f, tbp, tbw, c.psm, 0, 0, size, size, full);
            for (std::size_t j = 0; j <= k; ++j) {
                const Patch& r = patches[j];
                std::vector<std::uint8_t> b(patch_data[j].begin(), patch_data[j].begin() + r.w * r.h * c.bpp / 8);
                Upload(f, r.bp, tbw, c.psm, r.x, r.y, r.w, r.h, b);
            }
            DrawTexture(f, tex0, size, size);
            fresh.ProcessGif(f.Data(), f.Bytes());
            const auto fresh_draws = fresh.TakeDraws();
            REQUIRE(fresh_draws.size() == 1);
            REQUIRE(static_cast<bool>(fresh_draws[0].texture));
            CHECK(draws[0].texture->width == fresh_draws[0].texture->width);
            CHECK(draws[0].texture->rgba == fresh_draws[0].texture->rgba);
            // A write outside the texture's blocks keeps the decode (same texture, no new decode).
            if (k == 1) CHECK(draws[0].texture.get() == previous.get());
            previous = draws[0].texture;
        }
    }
}

TEST_CASE("texture decode: a decode stays valid until a block it reads is written") {
    std::mt19937 rng(11);
    GsState gs;
    const std::uint32_t tbp = 0x400, tbw = 2;  // 128 x 128 T8: two 8 KiB pages (blocks tbp to tbp + 63)
    const std::uint32_t cbp = 0x3000;
    const std::uint64_t tex0 = tbp | static_cast<std::uint64_t>(tbw) << 14 | static_cast<std::uint64_t>(kPsmT8) << 20 |
                               7ull << 26 | 7ull << 30 | 1ull << 34 | static_cast<std::uint64_t>(cbp) << 37 | 1ull << 61;
    auto draw = [&](Packet& p) {
        DrawTexture(p, tex0, 128, 128);
        gs.ProcessGif(p.Data(), p.Bytes());
        const auto draws = gs.TakeDraws();
        REQUIRE(draws.size() == 1);
        return draws[0].texture;
    };
    Packet first;
    Upload(first, cbp, 1, kPsmCt32, 0, 0, 16, 16, RandomBytes(rng, 256 * 4));
    Upload(first, tbp, tbw, kPsmT8, 0, 0, 128, 128, RandomBytes(rng, 128 * 128));
    const auto a = draw(first);
    Packet same;  // nothing written
    CHECK(draw(same).get() == a.get());
    Packet elsewhere;  // the next page, not part of the texture
    Upload(elsewhere, tbp + 64, tbw, kPsmT8, 0, 0, 16, 16, RandomBytes(rng, 16 * 16));
    CHECK(draw(elsewhere).get() == a.get());
    Packet unchanged;  // the same bytes again: no block changes
    {
        std::vector<std::uint8_t> bytes(16 * 16);
        for (std::uint32_t y = 0; y < 16; ++y)
            for (std::uint32_t x = 0; x < 16; ++x) bytes[y * 16 + x] = static_cast<std::uint8_t>(gs.Memory().ReadPixel(kPsmT8, tbp, tbw, x, y));
        Upload(unchanged, tbp, tbw, kPsmT8, 0, 0, 16, 16, bytes);
    }
    CHECK(draw(unchanged).get() == a.get());
    Packet inside;  // one texel of the texture
    Upload(inside, tbp, tbw, kPsmT8, 64, 64, 1, 1, {static_cast<std::uint8_t>(~gs.Memory().ReadPixel(kPsmT8, tbp, tbw, 64, 64))});
    const auto b = draw(inside);
    CHECK(b.get() != a.get());
    CHECK(b->serial != a->serial);
}

TEST_CASE("texture decode: the GPU ops replayed on a copy of local memory give the CPU decode") {
    // GsState in kBoth mode emits the GPU work (memory updates, then decodes) and decodes on the CPU
    // as well. Replaying the ops in order on a second memory, as the backend does on the GPU, and
    // decoding each texture from that memory at its op (DecodeOnCpu, the reference of
    // gs_decode.comp) must give each texture's CPU decode, although the stream rewrites the texture
    // memory between draws (the terrain case).
    struct Case {
        std::uint32_t psm, bpp, cpsm;
    };
    for (const Case c : {Case{kPsmT8, 8, 0}, Case{kPsmT4, 4, 0}, Case{kPsmT4, 4, 2}, Case{kPsmCt32, 32, 0},
                         Case{kPsmCt24, 24, 0}, Case{kPsmCt16, 16, 0}, Case{kPsmCt16S, 16, 0}}) {
        CAPTURE(c.psm);
        CAPTURE(c.cpsm);
        std::mt19937 rng(c.psm * 13 + c.cpsm);
        GsState gs;
        gs.SetTextureDecode(TextureDecode::kBoth);
        const std::uint32_t tbp = 0x1010, tbw = 2, size = 64, cbp = 0x3000;
        const bool paletted = c.bpp < 16;
        // TEXA: TA0 0x40, AEM on, TA1 0x90 (CT24, CT16 and 16-bit palettes use it).
        const std::uint64_t texa = 0x40 | 1ull << 15 | 0x90ull << 32;
        const std::uint64_t tex0 = tbp | static_cast<std::uint64_t>(tbw) << 14 | static_cast<std::uint64_t>(c.psm) << 20 |
                                   6ull << 26 | 6ull << 30 | 1ull << 34 |
                                   (paletted ? static_cast<std::uint64_t>(cbp) << 37 | static_cast<std::uint64_t>(c.cpsm) << 51 | 1ull << 61 : 0);
        Packet setup;
        setup.Tag(1, false, 0, 1, 0xE);
        setup.Ad(0x3B, texa);
        if (paletted) {
            if (c.cpsm == 0) Upload(setup, cbp, 1, kPsmCt32, 0, 0, 16, 16, RandomBytes(rng, 256 * 4));
            else Upload(setup, cbp, 1, kPsmCt16, 0, 0, 16, 16, RandomBytes(rng, 256 * 2));
        }
        Upload(setup, tbp, tbw, c.psm, 0, 0, size, size, RandomBytes(rng, size * size * c.bpp / 8));
        gs.ProcessGif(setup.Data(), setup.Bytes());
        std::vector<std::shared_ptr<const DecodedTexture>> drawn;
        for (int round = 0; round < 6; ++round) {
            Packet p;
            // Rewrite part of the texture (and, every other round, an unrelated page), then draw.
            const std::uint32_t x = static_cast<std::uint32_t>(rng() % 48), y = static_cast<std::uint32_t>(rng() % 48);
            Upload(p, tbp, tbw, c.psm, x, y, 16, 16, RandomBytes(rng, 16 * 16 * c.bpp / 8));
            if (round % 2) Upload(p, 0x2000, 2, kPsmCt32, 0, 0, 16, 16, RandomBytes(rng, 16 * 16 * 4));
            DrawTexture(p, tex0, size, size);
            gs.ProcessGif(p.Data(), p.Bytes());
            const auto draws = gs.TakeDraws();
            REQUIRE(draws.size() == 1);
            REQUIRE(static_cast<bool>(draws[0].texture));
            drawn.push_back(draws[0].texture);
        }
        GsMemory gpu;  // starts zero, as the GPU copy
        std::vector<std::uint8_t> image(GsMemory::kSize, 0);
        std::size_t decodes = 0;
        for (const TextureOp& op : gs.TakeTextureOps()) {
            if (!op.decode) {
                REQUIRE(op.bytes.size() == op.blocks.size() * 256);
                for (std::size_t i = 0; i < op.blocks.size(); ++i)
                    std::memcpy(&image[static_cast<std::size_t>(op.blocks[i]) * 256], &op.bytes[i * 256], 256);
                gpu.LoadImage(image.data());
                continue;
            }
            DecodedTexture replay = *op.decode;
            DecodeOnCpu(gpu, replay);
            CHECK(replay.rgba == op.decode->rgba);
            ++decodes;
        }
        CHECK(decodes == drawn.size());
    }
}

TEST_CASE("texture decode: the decoded-texture cache stays within its byte budget") {
    // 512 x 512 CT32 textures (1 MiB each) at different block pointers, more than the budget holds.
    std::mt19937 rng(5);
    GsState gs;
    const std::uint32_t tbw = 8;
    {
        Packet p;
        Upload(p, 0, tbw, kPsmCt32, 0, 0, 512, 512, RandomBytes(rng, 512 * 512 * 4));
        gs.ProcessGif(p.Data(), p.Bytes());
    }
    const std::size_t entries = GsState::kTextureCacheBytes / (512 * 512 * 4) + 64;
    std::size_t peak = 0;
    for (std::size_t i = 0; i < entries; ++i) {
        const std::uint64_t tbp = (i * 32) & 0x3FFF;
        const std::uint64_t tex0 = tbp | static_cast<std::uint64_t>(tbw) << 14 | 9ull << 26 | 9ull << 30 | 1ull << 34;
        Packet p;
        DrawTexture(p, tex0, 512, 512);
        gs.ProcessGif(p.Data(), p.Bytes());
        REQUIRE(gs.TakeDraws().size() == 1);
        peak = std::max(peak, gs.TextureCacheBytes());
    }
    CHECK(peak <= GsState::kTextureCacheBytes + 512 * 512 * 4);
    CHECK(gs.TextureCacheBytes() < peak);  // the budget was reached and the cache emptied
}

TEST_CASE("anti-aliasing point: a mark becomes a draw-list entry between the 3D and the HUD") {
    // Two untextured sprites with the same state into FRAME_1 = FBP 0, FBW 8; a kAntiAlias mark
    // between them. Without the mark they merge into one draw; with it the list is sprite, point,
    // sprite, and the point carries FRAME_1 and the view's rectangle as SCISSOR.
    auto sprite = [](Packet& p, std::uint32_t x) {
        p.Tag(1, false, 0, 1, 0xE);
        p.Ad(0x00, 6);  // PRIM sprite
        p.Tag(2, false, 1, 1, 0x5);  // REGLIST: XYZ2 twice
        p.w.push_back((static_cast<std::uint64_t>(x) << 4) | (static_cast<std::uint64_t>(10) << 4 << 16));
        p.w.push_back((static_cast<std::uint64_t>(x + 8) << 4) | (static_cast<std::uint64_t>(20) << 4 << 16));
    };
    Packet p;
    p.Tag(2, false, 0, 1, 0xE);
    p.Ad(0x4C, 0x00080000ull);  // FRAME_1: FBP 0, FBW 8, CT32
    p.Ad(0x40, (511ull << 16) | (447ull << 48));
    sprite(p, 10);
    const std::uint32_t split = static_cast<std::uint32_t>(p.Bytes());
    sprite(p, 40);
    {
        GsState gs;
        gs.ProcessGif(p.Data(), p.Bytes());
        CHECK(gs.TakeDraws().size() == 1);
    }
    std::vector<SourceMark> marks(1);
    marks[0].offset = split;
    marks[0].kind = SourceMark::kAntiAlias;
    marks[0].rect[0] = 0;
    marks[0].rect[1] = 0;
    marks[0].rect[2] = 511;
    marks[0].rect[3] = 447;
    GsState gs;
    gs.ProcessGif(p.Data(), p.Bytes(), &marks);
    const auto draws = gs.TakeDraws();
    REQUIRE(draws.size() == 3);
    CHECK_FALSE(draws[0].aa_point);
    CHECK(draws[1].aa_point);
    CHECK(draws[1].vertices.empty());
    CHECK(draws[1].frame == 0x00080000ull);
    CHECK(draws[1].scissor == ((511ull << 16) | (447ull << 48)));
    CHECK_FALSE(draws[2].aa_point);
    CHECK(draws[2].vertices.size() == 2);
    // A movie source mark (kSource) at the same place adds nothing to the draw list.
    marks[0].kind = SourceMark::kSource;
    marks[0].bytes = 16;
    GsState plain;
    plain.ProcessGif(p.Data(), p.Bytes(), &marks);
    CHECK(plain.TakeDraws().size() == 1);
}
