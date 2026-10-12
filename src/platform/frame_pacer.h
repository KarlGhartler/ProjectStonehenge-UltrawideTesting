// Presentation frame cap (ADR-002, approved baseline: 60/120/144/240, 360 default upper limit).
// It limits presented frames only; the 59.94 Hz simulation clock is separate (milestone C).
#pragma once

#include <chrono>

namespace ac04::platform {

class FramePacer {
public:
    using Clock = std::chrono::steady_clock;

    void SetCap(int frames_per_second);
    int Cap() const { return cap_; }

    // Blocks until the next frame may start, then returns its start time.
    Clock::time_point WaitNextFrame();

    // Pure scheduling step (tested): given the previous deadline and the current time, returns the
    // next deadline. If the host fell behind by more than one interval, the schedule restarts from
    // `now` instead of trying to catch up (no burst of frames).
    static Clock::time_point NextDeadline(Clock::time_point previous, Clock::time_point now,
                                          Clock::duration interval);

private:
    int cap_ = 360;
    Clock::time_point deadline_{};
};

// OS hook: sleeps until `deadline` with sub-millisecond accuracy (platform implementation).
void PreciseSleepUntil(FramePacer::Clock::time_point deadline);
// Called once at startup: raise the timer resolution where the OS needs it.
void InitTimerResolution();

}  // namespace ac04::platform
