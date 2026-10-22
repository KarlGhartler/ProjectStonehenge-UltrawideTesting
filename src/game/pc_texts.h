// Save texts for a computer (port baseline, User request 2026-10-22; docs/decisions/port-baseline.md):
// the save, load and format messages refer to the PS2. After a page's font block is indexed
// (LoadFontBlock) they are reworded just enough: "memory card (PS2)" becomes "memory card", and
// the warning "...or switch off power" becomes "...or switch off the computer". The disc data is
// not changed; the messages are changed in guest memory (a message that grows is moved to the port
// menu area, port_menu.h). The unreachable hard disk drive texts are left as they are. Not applied
// in validation runs (port::Boot().pc_texts), so their RAM matches PCSX2.
#pragma once

#include "common/types.h"

namespace ac04::game {

// Rewords the `count` messages just indexed into the message table (IndexMessages).
void ApplyComputerTexts(s32 count);

}  // namespace ac04::game
