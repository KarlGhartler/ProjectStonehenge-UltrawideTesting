// Scene system (docs/game/scenes.md): SceneStep, the request helpers, the scene table and scene 0.
// Scenes 1-7 are stubs in the skeleton: they log once when entered and then do nothing, so the main
// loop keeps running (milestone C). They are registered at their original addresses, so SceneStep
// reaches them through the scene table in guest memory exactly as the PS2 does.
#include "game/game.h"

#include "common/debug_hooks.h"
#include "platform/log.h"
#include "sce/sce.h"

namespace ac04::game {

using guest::GRef;

constexpr u32 kSceneTable = 0x0029FDA0;  // 8 handler addresses in .data

// EE: 0x00100AB0
void RequestScene(u16 scene) {
    MainContext* ctx = Ctx();
    ctx->req = 5;
    ctx->next_scene = scene;
    debug::RecordSceneRequest(debug::SceneRequestKind::kScene, scene, ctx->scene, ctx->frame_count);
}

// EE: 0x00100AC8
void RequestSubState(u16 sub) {
    MainContext* ctx = Ctx();
    ctx->req = 2;
    ctx->next_sub = sub;
    debug::RecordSceneRequest(debug::SceneRequestKind::kSubState, sub, ctx->scene, ctx->frame_count);
}

// EE: 0x00100AF8
// Only caller: scene 3 (never requested; dead, docs/port/dead-code.md).
void SetRequestFlags(u32 req) {
    Ctx()->req = req;
}

// EE: 0x00100B08
// HYPOTHESIS: SetReturnScene. Unreferenced (dead): the only writer of ret_scene/ret_sub.
void func_00100B08(u16 scene, u16 sub) {
    MainContext* ctx = Ctx();
    ctx->ret_sub = sub;
    ctx->ret_scene = scene;
}

// EE: 0x00100B18
s32 SceneStep() {
    debug::OnSceneStepEntry();  // debug layer, record-only (RAM dump for docs/port/validation.md)
    guest::CallGuest(GRef<u32>(kSceneTable + 4u * Ctx()->scene));
    MainContext* ctx = Ctx();
    ctx->scene_frame += 1;
    if ((~ctx->flags & 0x100) != 0) ctx->scene_frame_unpaused += 1;
    u32 req = ctx->req;
    if ((req & 1) == 0) {
        ctx->req = req & 0xFFFFFFEFu;
    } else {
        ctx->flags |= 0x800;
        ctx->req = (req & 0xFFFFFFFEu) | 0x10;
        ctx->prev_scene = ctx->scene;
        ctx->scene = ctx->next_scene;
        ctx->sub = 0;
        ctx->scene_work[0] = 0;
        ctx->scene_work[1] = 0;
        ctx->scene_work[2] = 0;
        ctx->scene_work[3] = 0;
        ctx->scene_frame = 0;
        ctx->scene_frame_unpaused = 0;
    }
    req = ctx->req;
    if ((req & 2) == 0) {
        req &= 0xFFFFFFDFu;
    } else {
        ctx->sub = ctx->next_sub;
        req = (req & 0xFFFFFFFDu) | 0x20;
    }
    ctx->req = req;
    if ((ctx->req & 0x40) == 0) {
        req = ctx->req;
    } else {
        ctx->scene = ctx->ret_scene;
        ctx->req &= 0xFFFFFFBFu;
        ctx->sub = ctx->ret_sub;
        req = ctx->req;
    }
    if ((req & 4) != 0) {
        ctx->req = req & 0xFFFFFFFBu;
        sceGsSyncPath(0, 0);
        ResetDoubleBuffer(g_pMainContext().addr + offsetof(MainContext, dbuf));
        ResetPacketList(g_pMainContext().addr + offsetof(MainContext, vif1_list));
        ResetPacketList(g_pMainContext().addr + offsetof(MainContext, gif_list));
        ResetWorkHeapToMark();
    }
    return 0;
}

// EE: 0x001008B0
// HYPOTHESIS: SceneBootLoader (scene 0). State machine in gp global 0x00410494: load BD.TBL
// (file 3), the sound data DATA 0x163/0x164, stream BD entry 0 to the IOP, then RequestScene(6).
void SceneBootLoader() {
    u32& state = GRef<u32>(0x00410494);
    const u32 hdd = func_002597B8();
    switch (state) {
        case 0:
            GRef<u32>(0x00410498) = 0;
            Ctx()->flags &= 0xFFF7FFFFu;
            GRef<u8>(hdd + 0x4398) = 3;
            state = 1;
            break;
        case 1:
            GRef<u32>(0x0041049C) = QueueCdRead(1, GetCdFileLsn(3), 0, GetTblBuffer(1), 0, 1, ~0ull);
            state = 2;
            break;
        case 2:
            if (GRef<u32>(0x0041049C) <= GetCdDoneTicket(1)) {
                LoadDataEntryAsync(0x163, func_00219840());
                state = 3;
            }
            break;
        case 3:
            if (IsDataLoadDone() != 0) {
                const u32 buf = func_00219840();
                func_0021A750(buf + GRef<u32>(buf + 4));
                func_00219B48();
                func_0021CE38(1, 0xFF, 0xFF);
                SetSoundDataInputVolume(0, 0, 0);
                state = 4;
            }
            break;
        case 4:
            if (LoadSoundBankData(0x164, func_00219830()) == 1) state = 5;
            break;
        case 5: {
            // The original passes two words of a zeroed local array indexed by 0x004104A0.
            const s32 args[2] = {0, 0};
            s32& index = GRef<s32>(0x004104A0);
            if (StreamBdEntryToIop(args[index], args[index + 1]) == 1) {
                index += 1;
                if (index > 0) {
                    index = 0;
                    state = 6;
                }
            }
            break;
        }
        case 6:
            state = 7;
            break;
        case 7:
            RequestScene(6);
            break;
        default:
            break;
    }
}
AC04_GUEST_FUNCTION(0x001008B0, SceneBootLoader);

// ---- Scene stubs (skeleton) ----------------------------------------------------------------------

namespace {

template <int kScene>
void SceneStub() {
    static const char* const kNames[8] = {"SceneBootLoader", "SceneMenuEntry", "SceneFlight",
                                          "SceneMovie",      "SceneMenu",      "SceneResults",
                                          "SceneStartup",    "SceneHddError"};
    char key[32];
    std::snprintf(key, sizeof(key), "scene-stub-%d", kScene);
    log::WriteOnce(key, log::Level::Info, "game", "entered scene %d (%s): not translated yet, idling",
                   kScene, kNames[kScene]);
}

}  // namespace

// Handlers of scenes 1-7 at their original addresses (scene table 0x0029FDA0).
AC04_GUEST_FUNCTION(0x00211C58, SceneStub<1>);  // SceneMenuEntry (dead)
AC04_GUEST_FUNCTION(0x0010A030, SceneStub<2>);  // SceneFlight
AC04_GUEST_FUNCTION(0x00211CA8, SceneStub<3>);  // SceneMovie (dead)
AC04_GUEST_FUNCTION(0x002262D0, SceneStub<4>);  // SceneMenu
AC04_GUEST_FUNCTION(0x00246E18, SceneStub<5>);  // SceneResults
AC04_GUEST_FUNCTION(0x002613F0, SceneStub<6>);  // SceneStartup
AC04_GUEST_FUNCTION(0x00261680, SceneStub<7>);  // SceneHddError

}  // namespace ac04::game
