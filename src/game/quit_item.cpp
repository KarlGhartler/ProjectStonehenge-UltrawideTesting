// QUIT GAME in the main menu (quit_item.h).
#include "game/quit_item.h"

#include "common/port_hooks.h"
#include "game/game.h"
#include "game/port_menu.h"

namespace ac04::game {

using guest::GRef;

namespace {

// Port menu area slots (port_menu.h; 0-3 are CONTROLLER SETUP's, 4-19 GRAPHICS').
enum Slot : u32 {
    kSlotItems = 20,
    kSlotItemText = 21,
};

constexpr u32 kItemCount = 9;  // column 0 of the main menu on the PS2: NEW GAME ... OPTIONS

}  // namespace

QuitItemPatch::QuitItemPatch(u32 columns)
    : columns_(columns), count_(GRef<s16>(columns + 4)), items_(GRef<u32>(columns + 0x14)) {
    if (!port::Boot().quit_item) return;  // validation run: the PS2 menu
    u32 items[kItemCount + 1];
    for (u32 i = 0; i < kItemCount; ++i) items[i] = GRef<u32>(items_ + i * 4);
    items[kItemCount] = port_menu::String(kSlotItemText, "QUIT GAME");
    GRef<s16>(columns + 4) = static_cast<s16>(kItemCount + 1);
    GRef<u32>(columns + 0x14) = port_menu::Words(kSlotItems, items, kItemCount + 1);
}

QuitItemPatch::~QuitItemPatch() {
    GRef<s16>(columns_ + 4) = count_;
    GRef<u32>(columns_ + 0x14) = items_;
}

s16 MainMenuSelection(u32 tree) {
    const s16 id = GetMenuTreeSelection(tree);
    if (!port::Boot().quit_item) return id;
    if (id == static_cast<s16>(kItemCount)) return kQuitItem;
    return id > static_cast<s16>(kItemCount) ? static_cast<s16>(id - 1) : id;
}

}  // namespace ac04::game
