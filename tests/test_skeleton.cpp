// Milestone C: SDK layer and runtime pieces that can be checked without the disc.
#include <doctest/doctest.h>

#include <algorithm>
#include <cstring>
#include <vector>

#include "common/ee_math.h"
#include "common/guest_memory.h"
#include "common/guest_ref.h"
#include "game/game.h"
#include "platform/iop/iop_services.h"
#include "platform/iso9660.h"
#include "sce/ee_hw.h"
#include "sce/hw_registers.h"
#include "sce/runtime.h"
#include "sce/sce.h"
#include "sce/sce_host.h"
#include "synthetic_iso.h"

using ac04::guest::GRef;
using ac04::guest::Mem;

TEST_CASE("rand: newlib LCG on the guest state word") {
    Mem().Reset();
    GRef<u32>(0x00401D74) = 0x00401000;  // _impure_ptr -> some RAM; state at +0x58
    ee::srand(1);
    CHECK(ee::rand() == 1103527590);
    CHECK(ee::rand() == 377401575);
    CHECK(ee::rand() == 662824084);
    ee::srand(123456789);
    CHECK(ee::rand() == 231794730);
    CHECK(ee::rand() == 1126946331);
    CHECK(ee::rand() == 1757975480);
}

TEST_CASE("tick clock: 59.94 Hz steps, catch-up limit, dropped time") {
    ac04::sce::TickClock clock(4);
    const double tick = ac04::sce::TickClock::kTickSeconds;
    CHECK(clock.Advance(tick * 0.5) == 0);
    CHECK(clock.Advance(tick * 0.6) == 1);
    CHECK(clock.Advance(tick * 10.0) == 4);  // at most 4 per presented frame
    CHECK(clock.DroppedTicks() == 6);
    CHECK(clock.Alpha() >= 0.0);
    CHECK(clock.Alpha() < 1.0);
}

TEST_CASE("game runtime: lockstep ticks and alternating field") {
    std::vector<u32> fields;
    auto& rt = ac04::sce::GameRuntime::Get();
    rt.Start([&fields, &rt] {
        for (;;) fields.push_back(rt.WaitVBlank());
    });
    CHECK(fields.empty());  // parked at its first vsync wait
    CHECK(rt.RunTicks(3));
    REQUIRE(fields.size() == 3);
    CHECK(fields[0] != fields[1]);
    CHECK(fields[1] != fields[2]);
    CHECK(rt.RunTicks(0));
    CHECK(fields.size() == 3);
    rt.Stop();
    CHECK_FALSE(rt.Running());
}

TEST_CASE("TBL access: {sector, size} entries per table") {
    Mem().Reset();
    // DATA.TBL buffer 0x00421880, BD.TBL buffer 0x00422880.
    GRef<u32>(0x00421880 + 8 * 5) = 0x1234;
    GRef<u32>(0x00421884 + 8 * 5) = 0x5678;
    GRef<u32>(0x00422880 + 8 * 2) = 42;
    CHECK(ac04::game::GetTblBuffer(1) == 0x00422880u);
    CHECK(ac04::game::GetTblEntrySector(0, 5) == 0x1234u);
    CHECK(ac04::game::GetTblEntrySize(0, 5) == 0x5678u);
    CHECK(ac04::game::GetTblEntrySector(1, 2) == 42u);
}

TEST_CASE("SOUND stand-in: transfer and voice-status records") {
    // VAG.IRX keys its stream voices (core 1 voices 18-23) on at init; SOUND reports them as 4.
    ac04::platform::audio::SpuMixer mixer;
    auto vag = ac04::platform::iop::MakeVagService(&mixer);
    {
        const std::uint32_t voices[4] = {0x12, 0x14, 0x16, 0x17};
        std::vector<std::uint8_t> vreply(0x100, 0);
        vag->Call(0, reinterpret_cast<const std::uint8_t*>(voices), sizeof(voices), vreply.data(), 0x100);
        std::uint32_t base = 0, ring = 0;
        std::memcpy(&base, vreply.data(), 4);
        std::memcpy(&ring, vreply.data() + 0x24, 4);
        CHECK(base == 0x0008DA40u);  // measured stream area
        CHECK(ring == 1);            // measured fno 0 ring index
    }
    mixer.AdvanceStep();  // one SPU IRQ (VagMain) per half-buffer period: at least one in a step
    {
        const std::uint32_t idle[2] = {0, 0};
        std::vector<std::uint8_t> vreply(0x100, 0);
        vag->Call(1, reinterpret_cast<const std::uint8_t*>(idle), sizeof(idle), vreply.data(), 0x100);
        std::uint32_t flags = 0, ring = 0;
        std::memcpy(&flags, vreply.data() + 0x14, 4);
        std::memcpy(&ring, vreply.data() + 0x24, 4);
        CHECK(flags == 1);  // idle (run 06: words 5-8 = 1)
        CHECK(ring == 2);   // run 06: ring index 2 while nothing streams
    }
    auto sound = ac04::platform::iop::MakeSoundService(&mixer);
    std::vector<std::uint8_t> send(0x2400, 0), reply(0x600, 0);
    auto put = [&send](std::uint32_t off, std::uint32_t v) { std::memcpy(send.data() + off, &v, 4); };
    auto get = [&reply](std::uint32_t off) {
        std::uint32_t v;
        std::memcpy(&v, reply.data() + off, 4);
        return v;
    };
    sound->Call(0, send.data(), 0x2400, reply.data(), 0x600);
    CHECK(get(0) == 2);
    // Batch: one transfer (0xA IOP -> SPU RAM, ticket 7) and one output-mode command (0xE, mode 2).
    put(0, 2);
    put(4, 0xA);
    put(4 + 4, 0x1000);    // IOP source
    put(4 + 8, 0x5A10);    // SPU destination
    put(4 + 0xC, 0x40);    // size
    put(4 + 0x10, 7);
    put(0x18, 0xE);
    put(0x1C, 2);
    sound->Call(1, send.data(), 0x2400, reply.data(), 0x600);
    CHECK(get(0) == 2);       // no voice records
    CHECK(get(4) == 10);      // transfer record
    CHECK(get(8) == 0);       // the transfer is still running: previous finished ticket
    CHECK(get(12) == 0);      // status word (BlockTransStatus >> 24)
    CHECK(get(16) == 0xF);    // voice status record
    CHECK(get(20) == 2);      // output mode
    CHECK(reply[24 + 42] == 4);  // VAG stream voices report "still sounding" (get_ch_stat)
    CHECK(reply[24 + 47] == 4);
    CHECK(reply[24 + 0] == 0);
    put(0, 0);                // next batch: empty
    sound->Call(1, send.data(), 0x2400, reply.data(), 0x600);
    CHECK(get(8) == 7);       // finished by now
    CHECK(get(12) == 0);
    // A block-transfer stop {0, 0, 0} (ticket 8) starts no DMA: its ticket is never reported
    // (main_TransBd / handler_DmaInt; PCSX2 keeps the ticket one behind after each movie).
    std::fill(send.begin(), send.end(), 0);
    put(0, 1);
    put(4, 0xA);
    put(4 + 0x10, 8);
    sound->Call(1, send.data(), 0x2400, reply.data(), 0x600);
    put(0, 0);
    sound->Call(1, send.data(), 0x2400, reply.data(), 0x600);
    sound->Call(1, send.data(), 0x2400, reply.data(), 0x600);
    CHECK(get(8) == 7);
}

TEST_CASE("sceGsSetDefDispEnv: NTSC interlaced field mode 512x224") {
    Mem().Reset();
    sceGsResetGraph(0, 1, 2, 1);
    const u32 disp = 0x00100000;
    sceGsSetDefDispEnv(disp, 1, 0x200, 0xE0, 0, 0);
    CHECK(GRef<u64>(disp) == 0x66u);
    CHECK(GRef<u64>(disp + 8) == 3u);
    CHECK(GRef<u64>(disp + 16) == 0x9000u);
    CHECK(GRef<u64>(disp + 24) == 0x1BF9FF0203227Cull);
    CHECK(GRef<u64>(disp + 32) == 0u);
}

TEST_CASE("malloc: newlib chunk sizes give the measured work heap (validation.md E-1)") {
    Mem().Reset();
    // Start the probe one 0x100 step above the measured size: 0x01A319A4 must fail (chunk
    // 0x01A319B0 > arena), 0x01A318A4 must succeed (chunk 0x01A318B0) as on the PS2.
    GRef<u32>(0x004104B0) = 0x01FFEFF0u - 0x01A319A4u - 0x1000u;
    GRef<u32>(0x004104AC) = 0;
    CHECK(ac04::game::ProbeLargestMallocSize() == 0x01A318A4u);
    const u32 block = ee::malloc(0x01A318A4u);
    CHECK(block == 0x004CC760u);
    CHECK(GRef<u32>(0x004CC75C) == 0x01A318B1u);  // newlib size word | PREV_INUSE
    CHECK(ee::malloc(16) == 0);
    ee::free(block);
    const u32 a = ee::malloc(1);
    const u32 b = ee::malloc(20);
    CHECK(a == 0x004CC760u);
    CHECK(b == 0x004CC770u);  // minimum chunk 16 bytes
    CHECK(GRef<u32>(b - 4) == 0x21u);  // 20 + 4 -> 32-byte chunk
    ee::free(a);
    ee::free(b);
}

TEST_CASE("libcdvd over a synthetic ISO: search, read, sync, TOC (validation.md E-1)") {
    Mem().Reset();
    ac04::platform::Iso9660 iso;
    std::string error;
    REQUIRE(iso.Open(test::MakeTestIso(), error));
    ac04::sce::Host().iso = &iso;

    const u32 name = 0x00100000;
    const char path[] = "\\BIN\\DATA.CDP;1";
    Mem().WriteBytes(name, path, sizeof(path));
    CHECK(sceCdSearchFile(0x00200000, name) == 1);
    CHECK(GRef<u32>(0x00200000) == 22u);  // lsn
    CHECK(GRef<u32>(0x00200004) == 10u);  // size
    Mem().WriteBytes(name, "\\BIN\\data.cdp;1", 16);
    CHECK(sceCdSearchFile(0x00200000, name) == 0);  // exact case only (ADR-005)

    CHECK(sceCdRead(22, 1, 0x00300000, 0) == 1);
    CHECK(GRef<u8>(0x00300000) == '0');
    CHECK(GRef<u8>(0x00300009) == '9');
    CHECK(sceCdSync(1) == 1);  // busy for one poll
    CHECK(sceCdSync(1) == 0);

    // TOC as PCSX2 2.8.2 returns it (scenestep_first.p2s): end PSN = 0x30000 + sectors - 1.
    CHECK(sceCdGetToc(0x00400000) == 1);
    const u8 expect[24] = {0x04, 0x02, 0xF2, 0x00, 0x86, 0x72, 0, 0, 0, 0, 0, 0,
                           0x01, 0x02, 0x01, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x03, 0x00, 0x16};
    for (u32 i = 0; i < 24; ++i) CHECK(GRef<u8>(0x00400000 + i) == expect[i]);
    ac04::sce::Host().iso = nullptr;
}

TEST_CASE("sceGsResetGraph: GParam as measured in PCSX2 (validation.md E-1)") {
    Mem().Reset();
    sceGsResetGraph(0, 1, 2, 1);
    const u8 expect[8] = {0x01, 0x00, 0x02, 0x00, 0x01, 0x00, 0x1B, 0x00};
    for (u32 i = 0; i < 8; ++i) CHECK(GRef<u8>(0x003FAAA0 + i) == expect[i]);
}

TEST_CASE("SOUND stand-in: reverb mode reply carries the measured work-area address") {
    auto sound = ac04::platform::iop::MakeSoundService();
    std::vector<std::uint8_t> send(0x2400, 0), reply(0x600, 0);
    auto put = [&send](std::uint32_t off, std::uint32_t v) { std::memcpy(send.data() + off, &v, 4); };
    put(0, 1);
    put(4, 6);       // id 6: reverb mode
    send[8] = 1;     // core 1
    put(12, 2);      // mode 2
    sound->Call(1, send.data(), 0x2400, reply.data(), 0x600);
    std::uint32_t id, esa;
    std::memcpy(&id, reply.data() + 4, 4);
    std::memcpy(&esa, reply.data() + 8, 4);
    CHECK(id == 6);
    CHECK(esa == 0x001DFFF0u);  // min(core 0 mode 0 = 0x001DFFF0, core 1 mode 2 = 0x001FE0C0): PCSX2 runs 06/09/10
    sound->Call(1, send.data(), 0x2400, reply.data(), 0x600);
    std::memcpy(&esa, reply.data() + 8, 4);
    CHECK(esa == 0x001DFFF0u);  // the same after every movie end (S4 checkpoints)
    send[8] = 0;                // core 0, mode 3: the state of scenestep_first_2.p2s
    put(12, 3);
    sound->Call(1, send.data(), 0x2400, reply.data(), 0x600);
    std::memcpy(&esa, reply.data() + 8, 4);
    CHECK(esa == 0x001DB7C0u);
}

TEST_CASE("EE FPU: round to nearest as PCSX2 runs AC04 (validation.md E-2)") {
    namespace fpu = ac04::fpu;
    auto bits = [](f32 f) {
        u32 b;
        std::memcpy(&b, &f, 4);
        return b;
    };
    // cvt.s.w of the first random-table value (srand(123456789), first rand() = 231794730).
    CHECK(bits(fpu::IntToFloat(231794730)) == bits(231794730.0f));
    CHECK(bits(fpu::IntToFloat(0x7FFFFFFF)) == 0x4F000000u);  // nearest rounds up to 2^31
    // div.s: the sound volumes x / 255 measured in PCSX2 (0x0042CA50).
    CHECK(bits(fpu::Div(235.0f, 255.0f)) == 0x3F6BEBECu);
    CHECK(bits(fpu::Div(190.0f, 255.0f)) == 0x3F3EBEBFu);
    // EE rules kept: overflow clamps, denormals flush, x / 0 = signed FLT_MAX.
    CHECK(fpu::Mul(FLT_MAX, 2.0f) == FLT_MAX);
    CHECK(fpu::Mul(-FLT_MAX, 2.0f) == -FLT_MAX);
    CHECK(fpu::Mul(FLT_MIN, 0.5f) == 0.0f);
    CHECK(fpu::Div(-1.0f, 0.0f) == -FLT_MAX);
    CHECK(fpu::FloatToInt(-2.7f) == -2);  // cvt.w.s still truncates
}

TEST_CASE("EE FPU: add/sub mask the smaller operand as PCSX2 does (validation.md S2a-3)") {
    namespace fpu = ac04::fpu;
    auto bits = [](f32 f) {
        u32 b;
        std::memcpy(&b, &f, 4);
        return b;
    };
    // Menu fade timer (UpdateFade 0x0022C848): 30 x (1/60) reads 0x3EFFFFF8 in PCSX2 (s2a_title_sub6).
    f32 t = 0.0f;
    for (int i = 0; i < 10; ++i) t = fpu::Add(t, 1.0f / 60.0f);
    CHECK(bits(t) == 0x3E2AAAAAu);
    for (int i = 0; i < 20; ++i) t = fpu::Add(t, 1.0f / 60.0f);
    CHECK(bits(t) == 0x3EFFFFF8u);
    // Equal exponents and a difference of 1: no masking, same as IEEE.
    CHECK(bits(fpu::Add(1.5f, 1.25f)) == bits(1.5f + 1.25f));
    CHECK(bits(fpu::Sub(3.0f, 1.0f)) == bits(2.0f));
    // d >= 25: only the sign of the small operand is left.
    CHECK(fpu::Add(1.0f, 1e-9f) == 1.0f);
    CHECK(fpu::Sub(1.0f, 1e-9f) == 1.0f);
}

TEST_CASE("LerpColor: per-byte MMI blend (pmulth, psraw 12, paddsh, clamp 0..255)") {
    using ac04::game::LerpColor;
    CHECK(LerpColor(0.5f, 0, 0x808080) == 0x404040u);
    CHECK(LerpColor(1.0f, 0x765834, 0xABABAB) == 0xABABABu);
    CHECK(LerpColor(0.0f, 0x765834, 0xABABAB) == 0x765834u);
    CHECK(LerpColor(2.0f, 0, 0x80) == 0xFFu);        // clamped to 255
    CHECK(LerpColor(-1.0f, 0x10, 0) == 0x20u);       // t < 0 extrapolates away from b
    CHECK(LerpColor(0.25f, 0x101010, 0x909090) == 0x303030u);
}

TEST_CASE("IPU colour conversion: integer BT.601 method (S3)") {
    u8 px[4];
    ac04::sce::IpuCsc(16, 128, 128, px);  // black
    CHECK(px[0] == 0);
    CHECK(px[1] == 0);
    CHECK(px[2] == 0);
    CHECK(px[3] == 0x80);
    ac04::sce::IpuCsc(235, 128, 128, px);  // white: (0x95 * 219 >> 6 + 1) >> 1 = 255
    CHECK(px[0] == 255);
    CHECK(px[1] == 255);
    CHECK(px[2] == 255);
    ac04::sce::IpuCsc(100, 90, 200, px);
    // lum = 0x95 * 84 >> 6 = 195; rcr = 0xCC * 72 >> 6 = 229; gcr = -0x68 * 72 >> 6 = -117;
    // gcb = -0x32 * -38 >> 6 = 29; bcb = 0x102 * -38 >> 6 = -154 (arithmetic shifts).
    CHECK(px[0] == 212);  // (195 + 229 + 1) >> 1
    CHECK(px[1] == 54);   // (195 - 117 + 29 + 1) >> 1
    CHECK(px[2] == 21);   // (195 - 154 + 1) >> 1
}

TEST_CASE("RCNT0: a busy-wait on 0x118 H-blanks spans one vblank (MovieFrameSync, S3)") {
    auto& regs = ac04::sce::EeRegisters::Get();
    regs.OnVBlank(1);  // a field of 262 lines starts
    ac04::sce::EeHwWrite(ac04::sce::EeReg::kT0Count, 0);
    int vblanks = 0;
    u64 index = 2;
    while (ac04::sce::EeHwRead(ac04::sce::EeReg::kT0Count) < 0x118) {
        if (!regs.AdvanceLine()) {
            regs.OnVBlank(index++);
            ++vblanks;
        }
    }
    CHECK(vblanks == 1);
}
