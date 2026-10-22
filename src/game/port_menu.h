// Port-added menu content (port baseline, User approval 2026-10-20; docs/decisions/port-baseline.md):
// the Modern control types on CONTROLLER SETUP and the GRAPHICS page under SCREEN SETTINGS. Their
// values come from common/port_hooks.h, never from settings.
//
// The game draws texts and menu items from guest addresses, so the strings and tables of these
// additions live in a port-owned guest area above the initial stack pointer (0x01FFF800-0x01FFFFFF,
// guest_stack.h): the game never reads or writes it. It is written only while one of these pages
// is open, so RAM elsewhere stays as on the PS2.
#pragma once

#include "common/types.h"

namespace ac04::game::port_menu {

constexpr u32 kAreaBase = 0x01FFF800u;
constexpr u32 kSlotSize = 0x40u;
constexpr u32 kSlots = 0x20u;

// Writes `text` (at most 63 characters, NUL-terminated) into slot `slot` and returns its address.
u32 String(u32 slot, const char* text);
// Writes `count` words into slot `slot` (at most 16) and returns its address.
u32 Words(u32 slot, const u32* words, u32 count);

}  // namespace ac04::game::port_menu
