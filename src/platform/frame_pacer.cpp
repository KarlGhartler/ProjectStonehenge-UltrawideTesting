#include "platform/frame_pacer.h"

namespace ac04::platform {

void FramePacer::SetCap(int frames_per_second) {
    cap_ = frames_per_second > 0 ? frames_per_second : 0;
    deadline_ = {};
}

FramePacer::Clock::time_point FramePacer::NextDeadline(Clock::time_point previous,
                                                       Clock::time_point now,
                                                       Clock::duration interval) {
    Clock::time_point next = previous + interval;
    if (previous.time_since_epoch().count() == 0 || next + interval < now) {
        // First frame, or more than one interval late: restart the schedule.
        next = now;
    }
    return next;
}

FramePacer::Clock::time_point FramePacer::WaitNextFrame() {
    if (cap_ == 0) return Clock::now();  // no cap: v-sync paces the frames
    const auto interval = std::chrono::duration_cast<Clock::duration>(
        std::chrono::duration<double>(1.0 / cap_));
    deadline_ = NextDeadline(deadline_, Clock::now(), interval);
    if (deadline_ > Clock::now()) PreciseSleepUntil(deadline_);
    return deadline_;
}

}  // namespace ac04::platform
