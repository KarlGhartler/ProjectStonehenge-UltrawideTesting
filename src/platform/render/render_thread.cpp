#include "platform/render/render_thread.h"

#include <utility>

namespace ac04::render {

void RenderThread::Start(Work work) {
    if (thread_.joinable()) return;
    work_ = std::move(work);
    stop_ = false;
    busy_ = false;
    thread_ = std::thread([this] { Loop(); });
}

FrameJob RenderThread::Submit(FrameJob job) {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this] { return !busy_; });
    FrameJob done = std::move(job_);
    job_ = std::move(job);
    busy_ = true;
    cv_.notify_all();
    return done;
}

void RenderThread::WaitIdle() {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this] { return !busy_; });
}

FrameJob RenderThread::Stop() {
    if (!thread_.joinable()) return {};
    {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] { return !busy_; });
        stop_ = true;
        cv_.notify_all();
    }
    thread_.join();
    return std::move(job_);
}

void RenderThread::Loop() {
    std::unique_lock<std::mutex> lock(mutex_);
    for (;;) {
        cv_.wait(lock, [this] { return busy_ || stop_; });
        if (!busy_) return;  // stop_ with nothing in flight
        // The main thread does not touch job_ while busy_ is set.
        lock.unlock();
        work_(job_);
        lock.lock();
        busy_ = false;
        cv_.notify_all();
    }
}

}  // namespace ac04::render
