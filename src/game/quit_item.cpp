// QUIT GAME in the main menu and the campaign menu (quit_item.h).
#include "game/quit_item.h"

#include "common/port_hooks.h"
#include "game/game.h"
#include "game/port_menu.h"

namespace ac04::game {

using guest::GRef;

namespace {

// Port menu area slots (port_menu.h; 0-3 are CONTROLLER SETUP's, 4-19 GRAPHICS'). The two menus
// are never up together, so they share the slots: each patch writes them again.
enum Slot : u32 {
    kSlotItems = 20,
    kSlotItemText = 21,
};

static_assert(kCampaignMenuItems <= kMainMenuItems, "QuitItemPatch's table holds the longest column");

}  // namespace

QuitItemPatch::QuitItemPatch(u32 columns, u32 items)
    : columns_(columns), count_(GRef<s16>(columns + 4)), items_(GRef<u32>(columns + 0x14)) {
    if (!port::Boot().quit_item) return;  // validation run: the PS2 menu
    u32 table[kMainMenuItems + 1];
    for (u32 i = 0; i < items; ++i) table[i] = GRef<u32>(items_ + i * 4);
    table[items] = port_menu::String(kSlotItemText, "QUIT GAME");
    GRef<s16>(columns + 4) = static_cast<s16>(items + 1);
    GRef<u32>(columns + 0x14) = port_menu::Words(kSlotItems, table, items + 1);
}

QuitItemPatch::~QuitItemPatch() {
    GRef<s16>(columns_ + 4) = count_;
    GRef<u32>(columns_ + 0x14) = items_;
}

s16 QuitItemSelection(u32 tree, u32 items) {
    const s16 id = GetMenuTreeSelection(tree);
    if (!port::Boot().quit_item) return id;
    if (id == static_cast<s16>(items)) return kQuitItem;
    return id > static_cast<s16>(items) ? static_cast<s16>(id - 1) : id;
}

}  // namespace ac04::game
