// Minimal debug overlay (milestone D, docs/port/debug-overlay.md). Part of the optional debug layer:
// created only when settings.json "debug.overlay_enabled" is true (default on in development builds),
// opened and closed with the toggle character ("\" by default, or "key:<SDL scancode name>").
//
// Panels (all read-only, "Sim: no"): performance and frame timing, scene inspector with the
// RequestScene/RequestSubState log, memory viewer at PS2 addresses with docs/symbols.csv names,
// live session log.
//
// Drawn by the output pass after the 4:3 game image, at host resolution, into the swapchain image
// (Renderer overlay hook). Guest memory is read only while the game thread is parked (ADR-002).
#pragma once

#include <array>
#include <memory>
#include <string>
#include <vector>

#include "common/types.h"
#include "platform/settings.h"

union SDL_Event;
struct SDL_Window;
typedef struct VkCommandBuffer_T* VkCommandBuffer;

namespace ac04::render {
class Renderer;
}

namespace ac04::platform {

// Values the host loop hands to the overlay each presented frame.
struct OverlayFrameData {
    double frame_seconds = 0.0;   // host time since the previous presented frame
    u32 ticks_run = 0;            // simulation steps run before this frame
    u64 vblank_count = 0;         // simulation steps since boot
    u64 dropped_ticks = 0;        // ticks dropped by the catch-up limit (all time)
    double alpha = 0.0;           // interpolation fraction
    bool game_running = false;
    // DMA intake (milestone C: counted, not drawn).
    u64 gif_qwords = 0, vif1_qwords = 0;  // in the last simulation step
    u64 gif_sends = 0, vif1_sends = 0;    // all time
    u64 bad_chains = 0;
    std::array<u64, 5> dispenv{};          // PMODE, SMODE2, DISPFB, DISPLAY, BGCOLOR
};

class DebugOverlay {
public:
    DebugOverlay();
    ~DebugOverlay();

    // Creates the ImGui context and backends when `settings.overlay_enabled`; otherwise does
    // nothing and every other call is a no-op. Can be called again after a settings change.
    bool Apply(const DebugSettings& settings, SDL_Window* window, render::Renderer& renderer, std::string& error);
    void Shutdown();

    // Returns true when the event was consumed by the overlay (the toggle, or keyboard/mouse input
    // while the overlay has focus).
    bool HandleEvent(const SDL_Event& event);
    // The keyboard belongs to the overlay (not forwarded to the game's pad mapping).
    bool WantsKeyboard() const;

    // Builds the UI for this frame; call before Renderer::DrawFrame.
    void BuildFrame(const OverlayFrameData& data);

    bool Active() const { return active_; }
    bool Visible() const { return active_ && visible_; }

private:
    void Draw(VkCommandBuffer cmd);
    void DrawPerformance(const OverlayFrameData& data);
    void DrawScene();
    void DrawMemory();
    void DrawLog();
    void LoadSymbols();

    struct Symbol {
        u32 address;
        std::string name;
    };

    bool active_ = false;
    bool visible_ = false;
    bool frame_built_ = false;
    std::string toggle_text_ = "\\";
    int toggle_scancode_ = 0;   // non-zero when the toggle is "key:<name>"
    render::Renderer* renderer_ = nullptr;
    void* last_render_pass_ = nullptr;

    bool show_perf_ = true, show_scene_ = true, show_memory_ = false, show_log_ = false;
    std::array<float, 240> frame_ms_{};
    size_t frame_ms_pos_ = 0;

    std::vector<Symbol> symbols_;   // sorted by address
    std::string symbols_source_;
    u32 mem_address_ = 0x0029F380;  // g_MainContext
    int mem_view_ = 0;              // 0 bytes, 1 u32, 2 f32
    char goto_text_[64] = {};
    std::string goto_error_;
    char log_filter_[64] = {};
};

}  // namespace ac04::platform
