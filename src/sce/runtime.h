// Game thread and frame clock (ADR-002 option A).
//
// The translated game runs on one dedicated thread, exactly like the PS2 main thread. Its only
// timing point is the vertical blank: sceGsSyncV() parks the thread until the host releases the
// next tick. The host owns the 59.94 Hz accumulator; it releases ticks and waits until the game
// is parked again before it presents a frame, so guest memory is never touched by two threads at
// once (lockstep). Field parity alternates once per released tick, as CSR.FIELD does on the PS2.
#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

#include "common/types.h"

namespace ac04::sce {

class GameRuntime {
public:
    static GameRuntime& Get();

    // Starts `entry` (the crt0 equivalent) on the game thread. It runs until its first vsync wait.
    void Start(std::function<void()> entry);
    // Asks the game to stop at its next vsync wait and joins the thread.
    void Stop();
    bool Running() const { return running_; }

    // Host side: lets the game run `ticks` vblanks and blocks until it waits for the next one
    // (or exits). Returns false if the game thread has ended.
    bool RunTicks(u32 ticks);

    // Game side (sceGsSyncV): waits for the next vblank. Returns the field (CSR.FIELD) of the new
    // vblank. Throws GameStop when the host stops the game.
    u32 WaitVBlank();

    // Number of vblanks completed since start, and the current field (0/1).
    u64 VBlankCount() const { return vblank_count_; }
    u32 Field() const { return static_cast<u32>(vblank_count_ & 1u); }
    // Ticks consumed in the current host frame (for the overlay).
    u64 TicksConsumed() const { return ticks_consumed_; }

    struct GameStop {};

    // Interpolation hook (ADR-002): called by the host before presenting, with the last two
    // completed vblanks and the fraction of the next tick already elapsed. No-op until a renderer
    // consumes game state (presentation only, never game logic).
    using InterpolationHook = std::function<void(u64 previous_tick, u64 tick, double alpha)>;
    void SetInterpolationHook(InterpolationHook hook) { interpolation_hook_ = std::move(hook); }
    void Interpolate(double alpha) const {
        const u64 tick = vblank_count_;
        if (interpolation_hook_) interpolation_hook_(tick == 0 ? 0 : tick - 1, tick, alpha);
    }

private:
    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable cv_;
    u32 pending_ticks_ = 0;
    bool parked_ = true;
    bool stop_ = false;
    std::atomic<bool> running_{false};
    std::atomic<u64> vblank_count_{0};
    std::atomic<u64> ticks_consumed_{0};
    InterpolationHook interpolation_hook_;
};

// Fixed-step accumulator (ADR-002): converts host time into 59.94 Hz ticks, at most `max_catch_up`
// per presented frame; any excess time is dropped (the game runs slower for a moment, never
// permanently fast or slow).
class TickClock {
public:
    static constexpr double kTickSeconds = 1001.0 / 60000.0;  // NTSC field rate 59.94 Hz
    explicit TickClock(u32 max_catch_up = 4) : max_catch_up_(max_catch_up) {}

    // Adds `elapsed_seconds` of host time and returns the number of ticks to run now.
    u32 Advance(double elapsed_seconds);
    // Fraction of the next tick already elapsed (0..1), for interpolation.
    double Alpha() const { return accumulator_ / kTickSeconds; }
    u64 DroppedTicks() const { return dropped_; }

private:
    double accumulator_ = 0.0;
    u32 max_catch_up_;
    u64 dropped_ = 0;
};

// Host time per presented frame with v-sync on (ADR-002). The presentation engine shows one frame
// per display refresh, but the time the host measures between frames jitters with where the CPU
// blocks (a free swapchain image, the frames in flight): at 60 Hz the ticks released per frame came
// out 1, 0, 1, 2 instead of 1, 1, 1, 1 (in-engine cutscenes stuttered; validation.md §2aa). Each
// frame counts as one refresh; the difference from the measured time is carried and paid out in
// whole refreshes once it exceeds one refresh (a refresh really missed), so the total follows the
// host clock and the game speed is unchanged.
class RefreshSnap {
public:
    // Returns the time to give TickClock for a frame that took `elapsed_seconds`. With
    // `refresh_seconds` 0 (v-sync off, refresh rate unknown) the measured time passes unchanged.
    double Step(double elapsed_seconds, double refresh_seconds);

private:
    double debt_ = 0.0;  // measured minus counted time
};

}  // namespace ac04::sce
