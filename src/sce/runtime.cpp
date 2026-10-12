#include "sce/runtime.h"

#include "platform/log.h"

namespace ac04::sce {

GameRuntime& GameRuntime::Get() {
    static GameRuntime runtime;
    return runtime;
}

void GameRuntime::Start(std::function<void()> entry) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = false;
        parked_ = false;
        pending_ticks_ = 0;
    }
    running_ = true;
    thread_ = std::thread([this, entry = std::move(entry)] {
        try {
            entry();
            AC04_LOG_INFO("game", "the game's main() returned");
        } catch (const GameStop&) {
            // Normal shutdown requested by the host.
        }
        std::lock_guard<std::mutex> lock(mutex_);
        running_ = false;
        parked_ = true;
        cv_.notify_all();
    });
    // Wait until the game reaches its first vsync wait (boot code before the first vblank).
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this] { return parked_; });
}

void GameRuntime::Stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
        cv_.notify_all();
    }
    if (thread_.joinable()) thread_.join();
}

bool GameRuntime::RunTicks(u32 ticks) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (!running_) return false;
    ticks_consumed_ = 0;
    if (ticks == 0) return true;
    pending_ticks_ += ticks;
    parked_ = false;
    cv_.notify_all();
    // The game consumes the ticks one vblank at a time and parks when none are left.
    cv_.wait(lock, [this] { return (parked_ && pending_ticks_ == 0) || !running_; });
    return running_;
}

u32 GameRuntime::WaitVBlank() {
    std::unique_lock<std::mutex> lock(mutex_);
    if (pending_ticks_ == 0) {
        parked_ = true;
        cv_.notify_all();
        cv_.wait(lock, [this] { return pending_ticks_ > 0 || stop_; });
    }
    if (stop_) throw GameStop{};
    --pending_ticks_;
    parked_ = false;
    ++ticks_consumed_;
    return static_cast<u32>(++vblank_count_ & 1u);
}

u32 TickClock::Advance(double elapsed_seconds) {
    accumulator_ += elapsed_seconds;
    u32 ticks = 0;
    while (accumulator_ >= kTickSeconds) {
        accumulator_ -= kTickSeconds;
        if (ticks < max_catch_up_) {
            ++ticks;
        } else {
            ++dropped_;
        }
    }
    return ticks;
}

}  // namespace ac04::sce
