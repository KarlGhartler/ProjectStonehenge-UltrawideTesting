#include "common/debug_hooks.h"

namespace ac04::debug {
namespace {
u64 (*g_vblank_source)() = nullptr;
std::function<void()> g_first_scene_step;
struct Checkpoint {
    u32 address = 0;
    u32 hit = 0;
    u32 seen = 0;
    std::function<void()> hook;
};
Checkpoint g_checkpoint;
}  // namespace

void OnGuestCheckpoint(u32 address) {
    Checkpoint& c = g_checkpoint;
    if (!c.hook || c.address != address) return;
    if (++c.seen < c.hit) return;
    auto hook = std::move(c.hook);
    c.hook = nullptr;
    hook();
}

void SetCheckpointHook(u32 address, u32 hit, std::function<void()> hook) {
    g_checkpoint = Checkpoint{address, hit == 0 ? 1 : hit, 0, std::move(hook)};
}

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

namespace {
bool g_freeze_sim = false;
AllocHook g_alloc_hook = nullptr;
int g_heap_fill = -1;
}  // namespace

int HeapFill() { return g_heap_fill; }
void SetHeapFill(int value) { g_heap_fill = value; }

#if defined(_MSC_VER)
extern "C" char __ImageBase;  // MSVC linker symbol: start of the executable image
#endif

void SetAllocHook(AllocHook hook) { g_alloc_hook = hook; }
AllocHook GetAllocHook() { return g_alloc_hook; }
u64 ImageRelative(const void* host_address) {
#if defined(_MSC_VER)
    return static_cast<u64>(reinterpret_cast<const char*>(host_address) - &__ImageBase);
#else
    return reinterpret_cast<u64>(host_address);  // other compilers: absolute address (no image base)
#endif
}

bool FreezeSim() { return g_freeze_sim; }
void SetFreezeSim(bool on) { g_freeze_sim = on; }

namespace {
BootMission g_boot_mission;
}  // namespace

const BootMission& GetBootMission() { return g_boot_mission; }
void SetBootMission(const BootMission& m) { g_boot_mission = m; }

}  // namespace ac04::debug
