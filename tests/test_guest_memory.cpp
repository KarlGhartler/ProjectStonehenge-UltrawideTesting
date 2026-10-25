#include <doctest/doctest.h>

#include "common/guest_call.h"
#include "common/guest_memory.h"
#include "common/guest_stack.h"
#include "common/ps2ptr.h"
#include "platform/log.h"

using namespace ac04::guest;

TEST_CASE("guest memory: RAM aliases map to the same bytes") {
    GuestMemory m;
    m.Write<u32>(0x00123450, 0xDEADBEEF);
    CHECK(m.Read<u32>(0x20123450) == 0xDEADBEEF);  // uncached
    CHECK(m.Read<u32>(0x30123450) == 0xDEADBEEF);  // uncached accelerated
    CHECK(m.Read<u32>(0x80123450) == 0xDEADBEEF);  // kseg0
    CHECK(m.Read<u32>(0xA0123450) == 0xDEADBEEF);  // kseg1
    m.Write<u16>(0x21FFFFFE, 0x1234);              // last halfword of RAM via the alias
    CHECK(m.Read<u16>(0x01FFFFFE) == 0x1234);
    CHECK(GuestMemory::RamOffset(0x30000010) == 0x10);
    CHECK(GuestMemory::RamOffset(0x70000000) == -1);
}

TEST_CASE("guest memory: scratchpad is separate from RAM") {
    GuestMemory m;
    m.Write<u32>(0x70000000, 0x11223344);
    m.Write<u32>(0x70003FFC, 0x55667788);
    CHECK(m.Read<u32>(0x70000000) == 0x11223344);
    CHECK(m.Read<u32>(0x70003FFC) == 0x55667788);
    CHECK(m.Read<u32>(0x00000000) == 0);
    CHECK(m.Translate(0x70003FFE, 4) == nullptr);  // crosses the end of the scratchpad
}

TEST_CASE("guest memory: invalid addresses read 0 and are counted") {
    GuestMemory m;
    const u64 before = m.InvalidAccessCount();
    CHECK(m.Read<u32>(0x02000000) == 0);  // past the end of RAM
    m.Write<u32>(0x40000000, 1);          // unmapped segment
    CHECK(m.Read<u32>(0x10000000) == 0);  // hardware without a handler
    CHECK(m.InvalidAccessCount() == before + 3);
}

TEST_CASE("guest memory: an invalid page is logged once, with the kind of access and the host caller") {
    GuestMemory m;
    m.TranslateOrDummy(0x0C3C0010, 4)[0] = 7;  // GRef-style access: read or write
    m.TranslateOrDummy(0x0C3C0020, 4);         // same 64 KiB page: not logged again
    int lines = 0;
    for (const std::string& line : ac04::log::RecentLines(50)) {
        if (line.find("0x0C3C00") == std::string::npos) continue;
        ++lines;
        CHECK(line.find("invalid access (read or write) of 4 bytes at guest address 0x0C3C0010") != std::string::npos);
        CHECK(line.find(" from ") != std::string::npos);
        CHECK(line.find("+0x") != std::string::npos);  // module+offset of the calling code
    }
    CHECK(lines == 1);
}

TEST_CASE("guest memory: hardware handlers") {
    struct Counter : HardwareHandler {
        u32 last_write = 0;
        void Read(u32 address, void* dst, u32 size) override {
            const u32 v = address ^ 0xFFFFFFFFu;
            std::memcpy(dst, &v, size < 4 ? size : 4);
        }
        void Write(u32, const void* src, u32 size) override { std::memcpy(&last_write, src, size < 4 ? size : 4); }
    } timer;
    GuestMemory m;
    m.MapHardware(0x10000000, 0x100, &timer);
    CHECK(m.Read<u32>(0x10000000) == 0xEFFFFFFFu);
    CHECK(m.Read<u32>(0xB0000010) == (0x10000010u ^ 0xFFFFFFFFu));  // kseg1 alias of the register
    m.Write<u32>(0x10000010, 0x83);
    CHECK(timer.last_write == 0x83);
}

struct TestNode {
    u32 value;
    ps2ptr<TestNode> next;
};
static_assert(sizeof(TestNode) == 8, "guest struct layout");

TEST_CASE("ps2ptr: dereference, arithmetic, linked structs") {
    Mem().Reset();
    ps2ptr<TestNode> a(0x00400000), b(0x00400100);
    a->value = 1;
    a->next = b;
    b->value = 2;
    b->next = ps2ptr<TestNode>();
    int sum = 0;
    for (ps2ptr<TestNode> p = a; p; p = p->next) sum += static_cast<int>(p->value);
    CHECK(sum == 3);
    CHECK(Mem().Read<u32>(0x00400004) == 0x00400100);  // stored as a 32-bit guest address

    ps2ptr<u32> words(0x00500000);
    words[0] = 10;
    words[3] = 40;
    CHECK((words + 3).addr == 0x0050000C);
    CHECK(*(words + 3) == 40);
    CHECK((words + 3) - words == 3);
    ps2ptr<void> v(0x00500000);
    CHECK((v + 5).addr == 0x00500005);
    CHECK(v.cast<u32>()[0] == 10);
}

TEST_CASE("ps2ptr: invalid address fails safely") {
    const u64 before = Mem().InvalidAccessCount();
    ps2ptr<TestNode> bad(0x05000000);  // beyond the 32 MiB of RAM
    CHECK(bad->value == 0);
    bad->value = 7;                    // discarded
    CHECK(bad->value == 0);
    CHECK(Mem().InvalidAccessCount() > before);
}

TEST_CASE("guest stack: frames nest and are 16-byte aligned") {
    ResetStack();
    CHECK(StackPointer() == kInitialStackPointer);
    {
        GuestFrame outer(20);
        CHECK(outer.Address() == kInitialStackPointer - 32);
        CHECK(outer.Address() % 16 == 0);
        {
            GuestFrame inner(1);
            CHECK(inner.Address() == outer.Address() - 16);
        }
        CHECK(StackPointer() == outer.Address());
    }
    CHECK(StackPointer() == kInitialStackPointer);
}

namespace {
s32 AddMixed(s32 a, f32 x, u32 b, f32 y) { return a + static_cast<s32>(x) + static_cast<s32>(b) + static_cast<s32>(y * 10.0f); }
f32 Half(f32 x) { return x * 0.5f; }
int g_touched = 0;
void Touch(ps2ptr<u32> p) { g_touched = static_cast<int>(*p); }
}  // namespace

AC04_GUEST_FUNCTION(0x7FFF0010, AddMixed);
AC04_GUEST_FUNCTION(0x7FFF0020, Half);
AC04_GUEST_FUNCTION(0x7FFF0030, Touch);

TEST_CASE("guest call: registered functions with EE argument passing") {
    CHECK(IsGuestFunctionRegistered(0x7FFF0010));
    CHECK(CallGuest<s32>(0x7FFF0010, s32(-5), 2.5f, u32(7), 0.5f) == -5 + 2 + 7 + 5);
    CHECK(CallGuest<f32>(0x7FFF0020, 3.0f) == doctest::Approx(1.5f));
    Mem().Write<u32>(0x00600000, 99);
    CallGuest(0x7FFF0030, ps2ptr<u32>(0x00600000));
    CHECK(g_touched == 99);
}

TEST_CASE("guest call: unknown target fails safely") {
    CHECK_FALSE(IsGuestFunctionRegistered(0x7FFF00F0));
    CHECK(CallGuest<u32>(0x7FFF00F0, u32(1)) == 0);
}
