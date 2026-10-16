// GS local memory and GIF parsing (src/platform/render/gs/). Synthetic data only.
#include <doctest/doctest.h>

#include <algorithm>
#include <cstring>
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
