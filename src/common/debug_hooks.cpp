#include "common/debug_hooks.h"

namespace ac04::debug {
namespace {
u64 (*g_vblank_source)() = nullptr;
std::function<void()> g_first_scene_step;
}  // namespace

SceneRequestLog& SceneRequests() {
    static SceneRequestLog log;
    return log;
}

void RecordSceneRequest(SceneRequestKind kind, u16 value, u16 from_scene, u32 frame_count) {
    SceneRequestLog& log = SceneRequests();
    SceneRequest& e = log.entries[log.total % SceneRequestLog::kSize];
    e.vblank = g_vblank_source ? g_vblank_source() : 0;
    e.frame_count = frame_count;
    e.kind = kind;
    e.value = value;
    e.from_scene = from_scene;
    ++log.total;
}

void OnSceneStepEntry() {
    if (!g_first_scene_step) return;
    auto hook = std::move(g_first_scene_step);
    g_first_scene_step = nullptr;
    hook();
}

void SetFirstSceneStepHook(std::function<void()> hook) { g_first_scene_step = std::move(hook); }

void SetVBlankSource(u64 (*source)()) { g_vblank_source = source; }

}  // namespace ac04::debug
