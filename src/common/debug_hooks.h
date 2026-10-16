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

// RAM checkpoints (docs/port/validation-strategy.md §2.1, --dump-ram-at). Translated code calls
// OnGuestCheckpoint(address) where the PCSX2 reference breakpoint sits: at the entry of the
// function at `address`, or right before the instruction at `address` for call sites. A hook set
// with SetCheckpointHook runs once, on hit number `hit` (1 = first), then is cleared.
void OnGuestCheckpoint(u32 address);
void SetCheckpointHook(u32 address, u32 hit, std::function<void()> hook);

// --freeze-sim (S5 block A, development builds): the one switch of this layer that changes control
// flow. Scene 2 sub-states 0xA-0xC then run only the world update, cameras and draw lists of
// FlightPlayFrame (docs/port/command-line.md). Off by default; never set in Release builds.
bool FreezeSim();
void SetFreezeSim(bool on);

// The runtime sets this so hooks can stamp the vblank without depending on src/sce.
void SetVBlankSource(u64 (*source)());

}  // namespace ac04::debug
