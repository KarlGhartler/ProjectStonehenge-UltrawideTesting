#include "platform/render/frame_interpolator.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>

#include "platform/render/vu1/vu1_programs.h"

namespace ac04::render {

namespace {

using vu1::RunRecord;

// A projected point that moves further than this between two steps (pixels) is not blended: two
// different objects were matched, or the camera cut.
constexpr float kMaxJump = 160.0f;
// A run whose matrix turns by more than this between two steps (cos 25°) has only its position
// blended: blending matrices that far apart distorts the shape (the propellers turn about 30-60° per
// step); with the newest orientation and a blended position the part stays on its parent.
constexpr float kMinColumnCos = 0.906f;

// Cosine of the angle between column `c` (xyz) of the matrices at `a` and `b`.
float ColumnCos(const std::vector<std::uint32_t>& ma, std::size_t a, const std::vector<std::uint32_t>& mb, std::size_t b, int c);

bool Blendable(std::uint32_t source) {
    return source == vu1::kProgramA || source == vu1::kProgramB || source == vu1::kProgramC ||
           source == vu1::kProgramE || source == vu1::kProgramT || source == vu1::kProgramD;
}

// Qwords of the run's VU memory that are blended (matrix columns and positions), per program.
struct Range {
    std::uint32_t first, count;
};
int BlendRanges(std::uint32_t source, Range out[2]) {
    if (source == vu1::kProgramT) {
        out[0] = {0, 8};   // world -> screen, world -> clip
        out[1] = {14, 1};  // camera position (the morph distance)
        return 2;
    }
    if (source == vu1::kProgramD) {
        out[0] = {0, 4};  // node matrix
        return 1;
    }
    out[0] = {0, 12};  // screen, clip and normal matrices (each with the node's world matrix)
    return 1;
}

float F(const std::vector<std::uint32_t>& mem, std::size_t word) {
    float f;
    std::memcpy(&f, &mem[word], 4);
    return f;
}

// Screen position (pixels) of point (x, y, z, 1) under the matrix whose columns are qwords 0-3 at
// `base` words; false behind the camera.
bool Project(const std::vector<std::uint32_t>& mem, std::size_t base, float x, float y, float z, float& sx, float& sy) {
    float p[4];
    for (int r = 0; r < 4; ++r)
        p[r] = F(mem, base + r) * x + F(mem, base + 4 + r) * y + F(mem, base + 8 + r) * z + F(mem, base + 12 + r);
    if (!(p[3] > 1e-6f)) return false;
    sx = p[0] / p[3];
    sy = p[1] / p[3];
    return std::isfinite(sx) && std::isfinite(sy);
}

float ColumnCos(const std::vector<std::uint32_t>& ma, std::size_t a, const std::vector<std::uint32_t>& mb, std::size_t b, int c) {
    float dot = 0.0f, na = 0.0f, nb = 0.0f;
    for (int r = 0; r < 3; ++r) {
        const float x = F(ma, a + static_cast<std::size_t>(c) * 4 + r), y = F(mb, b + static_cast<std::size_t>(c) * 4 + r);
        dot += x * y;
        na += x * x;
        nb += y * y;
    }
    if (!(na > 0.0f) || !(nb > 0.0f)) return 1.0f;
    return dot / std::sqrt(na * nb);
}

std::uint64_t Mix(std::uint64_t h, std::uint64_t v) {
    h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
    return h;
}

// Match key of a run: its program, where its vertex records came from, and for a terrain cell its
// origin (qword 12: x, 0, z, 1).
// Word index of TOP-relative qword q of a run in its log.
std::size_t Rel(const RunRecord& r, std::uint32_t q) { return r.mem_offset + (RunRecord::kAbsQwords + q) * 4; }

std::uint64_t Key(const vu1::RunLog& log, const RunRecord& r) {
    std::uint64_t h = Mix(r.source, r.pc);
    if (r.source == vu1::kProgramT && r.rel_qwords > 23) {
        // Terrain cells are built every step from the cell data: the cell's origin and its vertex
        // count and class (qword 23) identify it.
        h = Mix(h, log.mem[Rel(r, 12)]);
        h = Mix(h, log.mem[Rel(r, 12) + 2]);
        h = Mix(h, log.mem[Rel(r, 23)] & 0xFFFF);
    } else {
        h = Mix(h, r.vertex_source);
    }
    return h;
}

}  // namespace

FrameInterpolator::FrameInterpolator() : vu_(std::make_unique<vu1::Vu1State>()) {}
FrameInterpolator::~FrameInterpolator() = default;

void FrameInterpolator::Reset() {
    prev_.valid = false;
    cur_.valid = false;
    cut_ = false;
}

void FrameInterpolator::PushStep(const std::vector<std::uint8_t>& gif, const std::vector<gs::SourceMark>& marks,
                                 vu1::RunLog& runs) {
    std::swap(prev_, cur_);
    cur_.valid = true;
    cur_.gif.assign(gif.begin(), gif.end());
    cur_.marks = marks;
    std::swap(cur_.runs, runs);
    runs.Clear();
    Match();
}

void FrameInterpolator::Match() {
    cut_ = false;
    cur_.match.assign(cur_.runs.runs.size(), -1);
    cur_.moved.assign(cur_.runs.runs.size(), 0);
    if (!prev_.valid) return;
    // Runs of the previous step by key, in order (the n-th run with a key matches the n-th).
    std::unordered_map<std::uint64_t, std::vector<int>> by_key;
    by_key.reserve(prev_.runs.runs.size());
    for (std::size_t i = 0; i < prev_.runs.runs.size(); ++i) {
        const RunRecord& r = prev_.runs.runs[i];
        if (r.gif_size == 0 || !Blendable(r.source)) continue;
        by_key[Key(prev_.runs, r)].push_back(static_cast<int>(i));
    }
    std::unordered_map<std::uint64_t, std::size_t> used;
    used.reserve(by_key.size());
    for (std::size_t i = 0; i < cur_.runs.runs.size(); ++i) {
        const RunRecord& r = cur_.runs.runs[i];
        if (r.gif_size == 0 || !Blendable(r.source)) continue;
        const std::uint64_t k = Key(cur_.runs, r);
        auto it = by_key.find(k);
        if (it == by_key.end()) continue;
        std::size_t& n = used[k];
        if (n >= it->second.size()) continue;
        const int j = it->second[n++];
        const RunRecord& p = prev_.runs.runs[static_cast<std::size_t>(j)];
        if (p.rel_qwords < 16 || r.rel_qwords < 16) continue;
        // Plausibility: the projected origin (models: the node's origin; terrain: the cell's
        // origin under the camera) must not jump.
        const std::size_t cb = Rel(r, 0), pb = Rel(p, 0);
        float cx = 0.0f, cy = 0.0f, px = 0.0f, py = 0.0f;
        bool ok;
        if (r.source == vu1::kProgramT) {
            const float ox = F(cur_.runs.mem, cb + 48), oz = F(cur_.runs.mem, cb + 50);
            ok = Project(cur_.runs.mem, cb, ox, 0.0f, oz, cx, cy) && Project(prev_.runs.mem, pb, ox, 0.0f, oz, px, py);
            if (ok && std::hypot(cx - px, cy - py) > kMaxJump) cut_ = true;
        } else {
            ok = Project(cur_.runs.mem, cb, 0.0f, 0.0f, 0.0f, cx, cy) && Project(prev_.runs.mem, pb, 0.0f, 0.0f, 0.0f, px, py);
        }
        if (!ok || std::hypot(cx - px, cy - py) > kMaxJump) continue;
        if (r.source != vu1::kProgramT) {
            // The node's orientation (the normal matrix, qwords 8-10 of the model programs; the
            // node matrix of the glow program) must not turn too far.
            const std::size_t mo = r.source == vu1::kProgramD ? 0 : 8;
            bool turned = false;
            for (int c = 0; c < 3 && !turned; ++c)
                turned = ColumnCos(cur_.runs.mem, cb + mo * 4, prev_.runs.mem, pb + mo * 4, c) < kMinColumnCos;
            cur_.moved[i] = turned ? 1 : 0;
        }
        cur_.match[i] = j;
    }
}

bool FrameInterpolator::Build(double alpha, std::vector<std::uint8_t>& gif, std::vector<gs::SourceMark>& marks) {
    blended_ = 0;
    if (!cur_.valid || !prev_.valid || cut_) return false;
    const float a = static_cast<float>(std::clamp(alpha, 0.0, 1.0));
    gif.clear();
    gif.reserve(cur_.gif.size() + cur_.gif.size() / 8);
    // (end of a replaced range in the step, size change up to there), for the marks.
    std::vector<std::pair<std::uint32_t, std::int64_t>> shifts;
    std::int64_t shift = 0;
    std::uint32_t pos = 0;
    vu1::Vu1State& vu = *vu_;
    for (std::size_t i = 0; i < cur_.runs.runs.size(); ++i) {
        const int j = cur_.match[i];
        if (j < 0) continue;
        const RunRecord& r = cur_.runs.runs[i];
        const RunRecord& p = prev_.runs.runs[static_cast<std::size_t>(j)];
        if (r.gif_offset < pos || r.gif_offset + r.gif_size > cur_.gif.size()) continue;
        gif.insert(gif.end(), cur_.gif.begin() + pos, cur_.gif.begin() + r.gif_offset);
        // The run's VU memory as it was, with the blended qwords.
        std::memcpy(vu.mem.data(), &cur_.runs.mem[r.mem_offset], RunRecord::kAbsQwords * 16);
        std::memcpy(&vu.mem[r.top * 4], &cur_.runs.mem[Rel(r, 0)], static_cast<std::size_t>(r.rel_qwords) * 16);
        Range ranges[2];
        int n = BlendRanges(r.source, ranges);
        if (cur_.moved[i]) {
            // Position only: the translation columns of the screen and clip matrices (the glow
            // program: of its node matrix).
            ranges[0] = {3, 1};
            ranges[1] = {7, 1};
            n = r.source == vu1::kProgramD ? 1 : 2;
        }
        for (int k = 0; k < n; ++k) {
            for (std::uint32_t q = ranges[k].first; q < ranges[k].first + ranges[k].count; ++q) {
                if (q >= r.rel_qwords || q >= p.rel_qwords) continue;
                for (int c = 0; c < 4; ++c) {
                    const float from = F(prev_.runs.mem, Rel(p, q) + static_cast<std::uint32_t>(c));
                    const float to = F(cur_.runs.mem, Rel(r, q) + static_cast<std::uint32_t>(c));
                    const float v = (1.0f - a) * from + a * to;  // exactly `to` at a = 1
                    std::memcpy(&vu.mem[(r.top + q) * 4 + static_cast<std::uint32_t>(c)], &v, 4);
                }
            }
        }
        vu.top = r.top;
        vu.itop = r.itop;
        const std::size_t before = gif.size();
        std::uint32_t next = 0;
        vu1::RunVu1Program(vu, r.source, r.pc, gif, next);
        pos = r.gif_offset + r.gif_size;
        shift += static_cast<std::int64_t>(gif.size() - before) - static_cast<std::int64_t>(r.gif_size);
        shifts.emplace_back(pos, shift);
        ++blended_;
    }
    if (blended_ == 0) return false;
    gif.insert(gif.end(), cur_.gif.begin() + pos, cur_.gif.end());
    marks = cur_.marks;
    for (gs::SourceMark& m : marks) {
        std::int64_t s = 0;
        for (const auto& [end, total] : shifts) {
            if (end > m.offset) break;
            s = total;
        }
        m.offset = static_cast<std::uint32_t>(static_cast<std::int64_t>(m.offset) + s);
    }
    return true;
}

}  // namespace ac04::render
