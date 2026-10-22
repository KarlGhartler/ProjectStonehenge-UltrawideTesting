// Save texts for a computer (pc_texts.h).
#include "game/pc_texts.h"

#include <cstring>
#include <vector>

#include "common/port_hooks.h"
#include "game/game.h"
#include "game/port_menu.h"
#include "platform/log.h"

namespace ac04::game {

using guest::GRef;

namespace {

constexpr u32 kMessageTable = 0x004BCF70;  // 512 message pointers (message_window.cpp)

// Port menu area slots for the reworded messages that grew (port_menu.h; 0-21 are in use).
constexpr u32 kFirstSlot = 22;
constexpr u32 kLastSlot = 31;

// Glyph code of a character of the game's message font (decoded from the messages on the disc):
// space 0x0D, '0'-'9' 0x1D-0x26, 'A'-'Z' 0x27-0x40, 'a'-'z' 0x41-0x5A, '(' 0xB1, ')' 0x0E.
u16 Glyph(char c) {
    if (c == ' ') return 0x0D;
    if (c >= '0' && c <= '9') return static_cast<u16>(0x1D + (c - '0'));
    if (c >= 'A' && c <= 'Z') return static_cast<u16>(0x27 + (c - 'A'));
    if (c >= 'a' && c <= 'z') return static_cast<u16>(0x41 + (c - 'a'));
    if (c == '(') return 0xB1;
    return 0x0E;  // ')'
}

std::vector<u16> Glyphs(const char* text) {
    std::vector<u16> out;
    for (const char* p = text; *p != 0; ++p) out.push_back(Glyph(*p));
    return out;
}

struct Rewording {
    const char* from;
    const char* to;
};
// In this order, each wherever it occurs in a message.
constexpr Rewording kRewordings[] = {
    {" (PS2)", ""},
    {"switch off power", "switch off the computer"},
};

bool Replace(std::vector<u16>& text, const std::vector<u16>& from, const std::vector<u16>& to) {
    bool changed = false;
    for (std::size_t i = 0; i + from.size() <= text.size();) {
        if (std::equal(from.begin(), from.end(), text.begin() + static_cast<std::ptrdiff_t>(i))) {
            text.erase(text.begin() + static_cast<std::ptrdiff_t>(i), text.begin() + static_cast<std::ptrdiff_t>(i + from.size()));
            text.insert(text.begin() + static_cast<std::ptrdiff_t>(i), to.begin(), to.end());
            i += to.size();
            changed = true;
        } else {
            ++i;
        }
    }
    return changed;
}

}  // namespace

void ApplyComputerTexts(s32 count) {
    if (!port::Boot().pc_texts) return;  // validation run: the PS2 texts
    std::vector<std::vector<u16>> moved;  // reworded messages placed in the port area, by slot
    for (s32 m = 0; m < count && m < 0x200; ++m) {
        const u32 s = GRef<u32>(kMessageTable + static_cast<u32>(m) * 4);
        std::vector<u16> text;
        u32 p = s;
        while (GRef<u16>(p) < 0xFFFE) {
            text.push_back(GRef<u16>(p));
            p += 2;
        }
        const u16 end = GRef<u16>(p);  // 0xFFFE, or 0xFFFF: the next message continues it
        const std::size_t original = text.size();
        bool changed = false;
        for (const Rewording& r : kRewordings) changed |= Replace(text, Glyphs(r.from), Glyphs(r.to));
        if (!changed) continue;
        text.push_back(end);
        if (text.size() <= original + 1) {
            // Not longer: in place (the message's old tail after the new end is never read).
            for (std::size_t i = 0; i < text.size(); ++i) GRef<u16>(s + static_cast<u32>(i) * 2) = text[i];
            continue;
        }
        // Longer: into a port area slot (one per distinct text), and the message table points there.
        std::size_t k = 0;
        while (k < moved.size() && moved[k] != text) ++k;
        if (k == moved.size()) moved.push_back(text);
        const u32 slot = kFirstSlot + static_cast<u32>(k);
        if (slot > kLastSlot || text.size() > port_menu::kSlotSize / 2) {
            log::WriteOnce("pc-texts:space", log::Level::Warning, "game", "reworded save text does not fit; kept as on the PS2");
            continue;
        }
        u32 words[port_menu::kSlotSize / 4] = {};
        std::memcpy(words, text.data(), text.size() * 2);
        GRef<u32>(kMessageTable + static_cast<u32>(m) * 4) =
            port_menu::Words(slot, words, static_cast<u32>((text.size() * 2 + 3) / 4));
    }
}

}  // namespace ac04::game
