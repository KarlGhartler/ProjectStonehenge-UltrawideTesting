// QUIT GAME (port baseline, User approval 2026-10-22; docs/decisions/port-baseline.md): the last
// item of the main menu's first column, after OPTIONS, drawn by the game's own menu tree like the
// other items. Selecting it fades the menu and the screen out like the game's own exits and then
// asks the host to end the session (common/port_hooks.h), as closing the window does. The help line
// stays empty on it (the game's help texts are messages of the page's font block). No game state
// changes. Not added in validation runs (port::Boot().quit_item), so their RAM matches PCSX2.
#pragma once

#include "common/types.h"

namespace ac04::game {

// Menu-tree item id of QUIT GAME as MainMenuSelection reports it (outside the game's ids 0-0x23).
constexpr s16 kQuitItem = 0x40;

// The main menu's column definitions with QUIT GAME appended to column 0 while the tree is built
// or its cursors are stored; the game's table is restored afterwards (the cursor stays).
class QuitItemPatch {
public:
    explicit QuitItemPatch(u32 columns);
    ~QuitItemPatch();
    QuitItemPatch(const QuitItemPatch&) = delete;
    QuitItemPatch& operator=(const QuitItemPatch&) = delete;

private:
    u32 columns_;
    s16 count_;
    u32 items_;
};

// Item id of a main menu selection with QUIT GAME in column 0: the game's ids, or kQuitItem.
s16 MainMenuSelection(u32 tree);

}  // namespace ac04::game
