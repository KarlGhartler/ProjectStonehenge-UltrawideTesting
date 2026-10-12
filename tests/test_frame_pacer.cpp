#include <doctest/doctest.h>

#include "platform/frame_pacer.h"

using ac04::platform::FramePacer;
using namespace std::chrono;

TEST_CASE("frame pacer: deadlines advance by one interval") {
    const auto interval = duration_cast<FramePacer::Clock::duration>(duration<double>(1.0 / 60));
    const FramePacer::Clock::time_point t0 = FramePacer::Clock::time_point{} + seconds(10);
    // First frame starts now.
    auto d1 = FramePacer::NextDeadline({}, t0, interval);
    CHECK(d1 == t0);
    // On time: next deadline is exactly one interval later.
    auto d2 = FramePacer::NextDeadline(d1, t0 + milliseconds(5), interval);
    CHECK(d2 == d1 + interval);
    // Slightly late (less than one interval behind): keep the schedule.
    auto d3 = FramePacer::NextDeadline(d2, d2 + interval / 2, interval);
    CHECK(d3 == d2 + interval);
}

TEST_CASE("frame pacer: restarts after falling far behind (no burst)") {
    const auto interval = duration_cast<FramePacer::Clock::duration>(duration<double>(1.0 / 360));
    const FramePacer::Clock::time_point d = FramePacer::Clock::time_point{} + seconds(1);
    const auto now = d + milliseconds(100);
    CHECK(FramePacer::NextDeadline(d, now, interval) == now);
}
