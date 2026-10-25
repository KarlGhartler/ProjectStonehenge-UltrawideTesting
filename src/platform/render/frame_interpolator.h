// Frame interpolation (ADR-002 "Interpolation design"; approved baseline, docs/decisions/port-baseline.md):
// with more presented frames than game steps (displays above 60 Hz, or v-sync off with a higher
// frame cap), each presented frame shows the 3D world between the last two completed steps.
//
// The game's 3D goes through the VU1 programs (CPU reimplementations, vu1/vu1_programs.h): every
// program run reads matrices and vertex records from VU1 memory and kicks GIF packets. The VIF
// records each run of a step (vu1::RunLog: its output range in the step's GIF stream, its program
// and entry, and the VU memory it reads). A run of the newest step is matched with a run of the
// step before it by the guest address its vertex records came from (the game's model and terrain
// data, the same every step), the terrain cell's origin, and the occurrence of that key in the step.
// At presentation time the newest step's GIF stream is rebuilt with every matched run executed
// again on its own VU memory with the matrices blended between the two steps (screen, clip and
// normal matrices of the model programs, the camera matrices and position of the terrain program,
// the node matrix of the glow program); everything else (2D, HUD, effects the EE projects itself,
// unmatched runs) is the newest step's. A run whose projected origin moves implausibly far is not
// blended; when the camera jumps (a terrain cell moves on screen by more than that) the whole step
// is shown unblended (cuts).
//
// Presentation only: guest memory is never read or written here, and the game's steps are unchanged.
#pragma once

#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#include "platform/render/gs/gs_source_mark.h"
#include "platform/render/vu1/vif1.h"

namespace ac04::render {

class FrameInterpolator {
public:
    FrameInterpolator();
    ~FrameInterpolator();

    // The newest completed step: its GIF stream, source marks and program runs. The step before it
    // becomes the one blended from.
    void PushStep(const std::vector<std::uint8_t>& gif, const std::vector<gs::SourceMark>& marks, vu1::RunLog& runs);
    // Forgets both steps (interpolation turned off, or a scene change the host knows of).
    void Reset();

    // The newest step's GIF stream with its matched runs repeated at `alpha` (0 = the previous
    // step's matrices, 1 = the newest step's). False when there is nothing to blend (fewer than two
    // steps, no matched run, or a camera cut); the caller then draws the step as it is.
    bool Build(double alpha, std::vector<std::uint8_t>& gif, std::vector<gs::SourceMark>& marks);

    // Statistics of the last Build (debug overlay, logs): runs blended, runs in the step.
    std::uint32_t BlendedRuns() const { return blended_; }
    std::uint32_t StepRuns() const { return static_cast<std::uint32_t>(cur_.runs.runs.size()); }

private:
    struct Step {
        bool valid = false;
        std::vector<std::uint8_t> gif;
        std::vector<gs::SourceMark> marks;
        vu1::RunLog runs;
        std::vector<int> match;          // per run: the matched run of the previous step, or -1
        std::vector<std::uint8_t> moved;  // per matched run: only its position is blended (it turned too far)
    };
    void Match();
    void RunJobs(std::size_t first, std::size_t end, vu1::Vu1State& vu, float a);
    void WorkerLoop(std::size_t w);

    Step prev_, cur_;
    bool cut_ = false;  // the camera jumped between the two steps
    std::unique_ptr<vu1::Vu1State> vu_;
    std::uint32_t blended_ = 0;
    // Build: the matched runs repeated (indices into cur_.runs.runs, stream order), the output of
    // each and the size changes for the marks; kept with their capacity.
    std::vector<std::uint32_t> jobs_;
    std::vector<std::vector<std::uint8_t>> outs_;
    std::vector<std::pair<std::uint32_t, std::int64_t>> shifts_;
    // Helper threads of Build, each with its own VU memory; they start on a new generation_.
    std::vector<std::thread> workers_;
    std::vector<std::unique_ptr<vu1::Vu1State>> worker_vu_;
    std::mutex mutex_;
    std::condition_variable start_cv_, done_cv_;
    std::uint64_t generation_ = 0;
    std::size_t pending_ = 0;
    float alpha_ = 0.0f;
    bool quit_ = false;
};

}  // namespace ac04::render
