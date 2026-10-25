// GRAPHICS (port baseline, User approval 2026-10-20; docs/decisions/port-baseline.md): the last item
// of SCREEN SETTINGS, a view of the options page (view 3) with the port's graphics settings, built
// from the game's own menu widgets like the BRIGHTNESS view: one row per setting with its name, the
// chosen value and a row of choice squares. Up/down choose the row, left/right the value, which the
// host applies at once (common/port_hooks.h); ACCEPT also saves the values, CANCEL restores the ones
// the view opened with, the DEFAULT button sets the defaults for this display. With v-sync on the
// frame rate cap row shows "V-SYNC" and is skipped. No game state changes.
#pragma once

#include "common/types.h"

namespace ac04::game {

// Menu-tree item id of GRAPHICS as OptionsSelection reports it (outside the game's ids 0-13).
constexpr s16 kGraphicsItem = 0x40;

// The options page's column definitions with GRAPHICS appended to column 0 while the tree is built
// or its cursors are stored; the game's table is restored afterwards (the cursor stays).
class GraphicsItemPatch {
public:
    explicit GraphicsItemPatch(u32 columns);
    ~GraphicsItemPatch();
    GraphicsItemPatch(const GraphicsItemPatch&) = delete;
    GraphicsItemPatch& operator=(const GraphicsItemPatch&) = delete;

private:
    u32 columns_;
    s16 count_;
    u32 items_;
};

// Item id of a menu-tree selection with GRAPHICS in column 0: the game's ids, or kGraphicsItem.
s16 OptionsSelection(u32 tree);

// Options page object `obj` (options_page.cpp): view 3.
void EnterGraphicsView(u32 obj);
void GraphicsUpdate(u32 obj);
void DrawGraphicsView(u32 obj);
// Help lines of the view: messages 1 ("Use Left Analog Stick to adjust") and 3 (ACCEPT / CANCEL /
// DEFAULT) of the options page.
void SetGraphicsHelp(u32 mb);

}  // namespace ac04::game
