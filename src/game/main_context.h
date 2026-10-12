// MainContext (0x0029F380, 0xA20 bytes) and the game-state block (0x0029F350).
// Layout: docs/game/main-context.md (derived from every access in the code). Field names follow the
// naming convention (lower snake_case); unk_XX for fields not understood.
#pragma once

#include <cstddef>

#include "common/guest_ref.h"
#include "common/types.h"

namespace ac04::game {

using guest::ps2ptr;

// Two-buffer packet area (ctx+0x40): buf[2], index.
struct DoubleBuffer {
    ps2ptr<void> buf[2];
    u32 index;
};
static_assert(sizeof(DoubleBuffer) == 0xC, "DoubleBuffer layout");

// DMA packet list head/tail (ctx+0x50 GIF, ctx+0x60 VIF1).
struct PacketList {
    ps2ptr<void> head;
    ps2ptr<void> tail;
};
static_assert(sizeof(PacketList) == 8, "PacketList layout");

struct MainContext {
    u32 flags;                  // 0x000
    u32 req;                    // 0x004 scene request bits
    u16 scene;                  // 0x008
    u16 prev_scene;             // 0x00A
    u16 next_scene;             // 0x00C
    u16 sub;                    // 0x00E
    u16 next_sub;               // 0x010
    u16 ret_scene;              // 0x012
    u16 ret_sub;                // 0x014
    u16 unk_16;                 // 0x016
    u32 scene_work[4];          // 0x018
    u32 frame_count;            // 0x028
    u32 frame_count_unpaused;   // 0x02C
    u32 scene_frame;            // 0x030
    u32 scene_frame_unpaused;   // 0x034
    ps2ptr<void> cur_buf;       // 0x038
    u32 unk_3C;                 // 0x03C
    DoubleBuffer dbuf;          // 0x040
    u32 unk_4C;                 // 0x04C
    PacketList gif_list;        // 0x050
    u8 unk_58[8];               // 0x058
    PacketList vif1_list;       // 0x060
    u8 unk_68[8];               // 0x068
    u16 draw_w;                 // 0x070
    u16 draw_h;                 // 0x072
    u16 disp_w;                 // 0x074
    u16 disp_h;                 // 0x076
    u8 clear_rgba[4];           // 0x078
    u8 frame_psm;               // 0x07C
    u8 zbuf_psm;                // 0x07D
    u8 ztest;                   // 0x07E
    u8 draw_mode;               // 0x07F
    f32 brightness_r;           // 0x080
    f32 brightness_g;           // 0x084
    f32 brightness_b;           // 0x088
    u16 disp_flags;             // 0x08C
    u16 zbuf_page;              // 0x08E
    u64 dispenv[5];             // 0x090 sceGsDispEnv
    u8 unk_B8[8];               // 0x0B8
    u64 drawenv_pkt[28];        // 0x0C0
    u64 clear_pkt[14];          // 0x1A0
    u8 pad_state[2][0x50];      // 0x210
    u32 pad_buttons[2][4];      // 0x2B0
    u8 pad_ext[2][0x20];        // 0x2D0
    u8 unk_310[0x700];          // 0x310
    ps2ptr<void> data0_table[4];// 0xA10
};
static_assert(offsetof(MainContext, frame_count) == 0x28, "MainContext layout");
static_assert(offsetof(MainContext, cur_buf) == 0x38, "MainContext layout");
static_assert(offsetof(MainContext, dbuf) == 0x40, "MainContext layout");
static_assert(offsetof(MainContext, gif_list) == 0x50, "MainContext layout");
static_assert(offsetof(MainContext, vif1_list) == 0x60, "MainContext layout");
static_assert(offsetof(MainContext, draw_w) == 0x70, "MainContext layout");
static_assert(offsetof(MainContext, brightness_r) == 0x80, "MainContext layout");
static_assert(offsetof(MainContext, disp_flags) == 0x8C, "MainContext layout");
static_assert(offsetof(MainContext, dispenv) == 0x90, "MainContext layout");
static_assert(offsetof(MainContext, drawenv_pkt) == 0xC0, "MainContext layout");
static_assert(offsetof(MainContext, clear_pkt) == 0x1A0, "MainContext layout");
static_assert(offsetof(MainContext, pad_state) == 0x210, "MainContext layout");
static_assert(offsetof(MainContext, pad_buttons) == 0x2B0, "MainContext layout");
static_assert(offsetof(MainContext, pad_ext) == 0x2D0, "MainContext layout");
static_assert(offsetof(MainContext, data0_table) == 0xA10, "MainContext layout");
static_assert(sizeof(MainContext) == 0xA20, "MainContext size");

constexpr u32 kMainContextAddress = 0x0029F380;
constexpr u32 kGameStateAddress = 0x0029F350;

AC04_GUEST_VAR(MainContext, g_MainContext, kMainContextAddress)
AC04_GUEST_VAR(ps2ptr<MainContext>, g_pMainContext, 0x004104A8)   // gp-0x5548
AC04_GUEST_VAR(ps2ptr<u8>, g_GameState, 0x004104A4)               // gp-0x554C -> 0x0029F350

// The game always reaches the context through g_pMainContext (a load per use); Ctx() keeps that.
inline MainContext* Ctx() { return g_pMainContext().get(); }

}  // namespace ac04::game
