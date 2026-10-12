// Read-only hooks from translated game code into the optional debug layer (docs/port/debug-overlay.md).
// They only record what happened; they never change game state. The overlay reads them on the host
// thread while the game thread is parked (ADR-002 lockstep), so no locking is needed.
#pragma once

#include <array>
#include <functional>

#include "common/types.h"

namespace ac04::debug {

enum class SceneRequestKind : u8 { kScene, kSubState };

struct SceneRequest {
    u64 vblank = 0;        // runtime vblank count when requested
    u32 frame_count = 0;   // MainContext frame_count at that time
    SceneRequestKind kind = SceneRequestKind::kScene;
    u16 value = 0;         // requested scene or sub-state
    u16 from_scene = 0;    // scene that made the request
};

struct SceneRequestLog {
    static constexpr size_t kSize = 64;
    std::array<SceneRequest, kSize> entries{};
    u64 total = 0;  // number of requests recorded; the newest is entries[(total - 1) % kSize]
};

SceneRequestLog& SceneRequests();
void RecordSceneRequest(SceneRequestKind kind, u16 value, u16 from_scene, u32 frame_count);

// Called at the entry of SceneStep (0x00100B18), on the game thread. A hook registered with
// SetFirstSceneStepHook runs once, on the first call, then is cleared (--dump-ram-at-first-scenestep).
void OnSceneStepEntry();
void SetFirstSceneStepHook(std::function<void()> hook);

// The runtime sets this so hooks can stamp the vblank without depending on src/sce.
void SetVBlankSource(u64 (*source)());

}  // namespace ac04::debug
