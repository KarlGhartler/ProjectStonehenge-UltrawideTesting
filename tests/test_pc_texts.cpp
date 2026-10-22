// Save texts for a computer (src/game/pc_texts.*): messages built in guest memory with the game's
// glyph codes, indexed as a font block's messages are, then reworded.
#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "common/guest_memory.h"
#include "common/guest_ref.h"
#include "common/port_hooks.h"
#include "game/game.h"
#include "game/pc_texts.h"
#include "game/port_menu.h"

using ac04::guest::GRef;
using ac04::guest::Mem;

namespace {

// The message font's glyph codes (pc_texts.cpp): space, digits, letters, '(' and ')'.
u16 Code(char c) {
    if (c == ' ') return 0x0D;
    if (c >= '0' && c <= '9') return static_cast<u16>(0x1D + (c - '0'));
    if (c >= 'A' && c <= 'Z') return static_cast<u16>(0x27 + (c - 'A'));
    if (c >= 'a' && c <= 'z') return static_cast<u16>(0x41 + (c - 'a'));
    if (c == '(') return 0xB1;
    if (c == ')') return 0x0E;
    return 0xA5;  // '.'
}

char Char(u16 c) {
    if (c == 0x0D) return ' ';
    if (c >= 0x1D && c <= 0x26) return static_cast<char>('0' + (c - 0x1D));
    if (c >= 0x27 && c <= 0x40) return static_cast<char>('A' + (c - 0x27));
    if (c >= 0x41 && c <= 0x5A) return static_cast<char>('a' + (c - 0x41));
    if (c == 0xB1) return '(';
    if (c == 0x0E) return ')';
    return '.';
}

// A message block {s32 count, u16 strings} at `block`: each text with its end code.
void Build(u32 block, const std::vector<std::pair<std::string, u16>>& texts) {
    GRef<s32>(block) = static_cast<s32>(texts.size());
    u32 p = block + 4;
    for (const auto& [text, end] : texts) {
        for (char c : text) {
            GRef<u16>(p) = Code(c);
            p += 2;
        }
        GRef<u16>(p) = end;
        p += 2;
    }
}

// Message `id` as text and its end code.
std::pair<std::string, u16> Read(u32 id) {
    u32 s = ac04::game::GetMessage(id);
    std::string out;
    while (GRef<u16>(s) < 0xFFFE) {
        out += Char(GRef<u16>(s));
        s += 2;
    }
    return {out, GRef<u16>(s)};
}

const std::vector<std::pair<std::string, u16>> kMessages = {
    {"Accessing memory card (PS2)...", 0xFFFE},
    {"Do not remove memory card (PS2)", 0xFFFF},
    {"or switch off power", 0xFFFE},
    {"No memory card (PS2) found in", 0xFFFF},
    {"MEMORY CARD slot 1", 0xFFFE},
    {"Saving. Do not turn power off.", 0xFFFE},  // hard disk drive text: unchanged
    {"or switch off power", 0xFFFF},
};

}  // namespace

TEST_CASE("save texts for a computer: (PS2) dropped, the power warning names the computer") {
    Mem().Reset();
    ac04::port::Boot().pc_texts = true;
    const u32 block = 0x00800000;
    Build(block, kMessages);
    const s32 count = ac04::game::IndexMessages(block);
    REQUIRE(count == static_cast<s32>(kMessages.size()));
    ac04::game::ApplyComputerTexts(count);
    CHECK(Read(0) == std::pair<std::string, u16>{"Accessing memory card...", 0xFFFE});
    CHECK(Read(1) == std::pair<std::string, u16>{"Do not remove memory card", 0xFFFF});
    CHECK(Read(2) == std::pair<std::string, u16>{"or switch off the computer", 0xFFFE});
    CHECK(Read(3) == std::pair<std::string, u16>{"No memory card found in", 0xFFFF});
    CHECK(Read(4) == std::pair<std::string, u16>{"MEMORY CARD slot 1", 0xFFFE});
    CHECK(Read(5) == std::pair<std::string, u16>{"Saving. Do not turn power off.", 0xFFFE});
    // The same longer text with another end code gets its own copy; both live in the port area.
    CHECK(Read(6) == std::pair<std::string, u16>{"or switch off the computer", 0xFFFF});
    CHECK(ac04::game::port_menu::Contains(ac04::game::GetMessage(2)));
    CHECK(ac04::game::port_menu::Contains(ac04::game::GetMessage(6)));
    CHECK(ac04::game::GetMessage(2) != ac04::game::GetMessage(6));
    // Applying it again (the page indexes the same block again) changes nothing more.
    ac04::game::ApplyComputerTexts(count);
    CHECK(Read(1) == std::pair<std::string, u16>{"Do not remove memory card", 0xFFFF});
    CHECK(Read(2) == std::pair<std::string, u16>{"or switch off the computer", 0xFFFE});
    ac04::port::Boot().pc_texts = false;
}

TEST_CASE("save texts for a computer: off in validation runs") {
    Mem().Reset();
    ac04::port::Boot().pc_texts = false;
    const u32 block = 0x00800000;
    Build(block, kMessages);
    const s32 count = ac04::game::IndexMessages(block);
    ac04::game::ApplyComputerTexts(count);
    for (s32 i = 0; i < count; ++i) CHECK(Read(static_cast<u32>(i)) == kMessages[static_cast<std::size_t>(i)]);
}
