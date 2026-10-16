#include "platform/debug/overlay.h"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "build_info.h"
#include "common/debug_hooks.h"
#include "common/guest_memory.h"
#include "platform/log.h"
#include "platform/paths.h"
#include "platform/render/renderer.h"

namespace ac04::platform {
namespace {

// MainContext fields read by the scene inspector (docs/game/main-context.md).
constexpr u32 kMainContextPointer = 0x004104A8;  // g_pMainContext
constexpr u32 kGameStatePointer = 0x004104A4;    // g_GameState

const char* const kSceneNames[8] = {"SceneBootLoader", "SceneMenuEntry (dead)", "SceneFlight", "SceneMovie (dead)",
                                    "SceneMenu", "SceneResults", "SceneStartup", "SceneHddError"};

// Read-only guest access: RAM and scratchpad only. Hardware registers are never read from the
// overlay (some reads have side effects, e.g. timer 0).
bool Peek(u32 address, void* dst, u32 size) {
    const u8* p = guest::Mem().Translate(address, size);
    if (p == nullptr) return false;
    std::memcpy(dst, p, size);
    return true;
}

template <typename T>
T PeekOr(u32 address, T fallback) {
    T v{};
    return Peek(address, &v, sizeof(T)) ? v : fallback;
}

void CheckVk(VkResult r) {
    if (r != VK_SUCCESS) log::WriteOnce("overlay:vk", log::Level::Error, "overlay", "ImGui Vulkan error %d", r);
}

void ApplySquareStyle() {
    ImGui::StyleColorsDark();
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 0.0f;
    s.ChildRounding = 0.0f;
    s.FrameRounding = 0.0f;
    s.PopupRounding = 0.0f;
    s.ScrollbarRounding = 0.0f;
    s.GrabRounding = 0.0f;
    s.TabRounding = 0.0f;
    s.WindowBorderSize = 1.0f;
    s.Colors[ImGuiCol_WindowBg].w = 0.88f;
}

}  // namespace

DebugOverlay::DebugOverlay() = default;
DebugOverlay::~DebugOverlay() { Shutdown(); }

bool DebugOverlay::Apply(const DebugSettings& settings, SDL_Window* window, render::Renderer& renderer,
                         std::string& error) {
    // Toggle: a typed character, or "key:<scancode name>".
    toggle_scancode_ = 0;
    toggle_text_ = settings.overlay_toggle.empty() ? "\\" : settings.overlay_toggle;
    if (toggle_text_.rfind("key:", 0) == 0) {
        toggle_scancode_ = SDL_GetScancodeFromName(toggle_text_.c_str() + 4);
        if (toggle_scancode_ == SDL_SCANCODE_UNKNOWN) {
            AC04_LOG_WARN("overlay", "unknown overlay toggle key '%s'; using '\\'", toggle_text_.c_str());
            toggle_text_ = "\\";
        }
    }
    if (!settings.overlay_enabled) {
        Shutdown();
        return true;
    }
    if (active_) return true;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;  // nothing written next to the executable or the working directory
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ApplySquareStyle();

    if (!ImGui_ImplSDL3_InitForVulkan(window)) {
        error = "ImGui SDL3 backend failed";
        ImGui::DestroyContext();
        return false;
    }
    render::VkContext& vk = renderer.Context();
    ImGui_ImplVulkan_InitInfo info{};
    info.ApiVersion = VK_API_VERSION_1_2;
    info.Instance = vk.instance;
    info.PhysicalDevice = vk.physical_device;
    info.Device = vk.device;
    info.QueueFamily = vk.queue_family;
    info.Queue = vk.queue;
    info.DescriptorPoolSize = 16;
    info.MinImageCount = 2;
    info.ImageCount = std::max<uint32_t>(2, renderer.SwapchainImageCount());
    info.PipelineInfoMain.RenderPass = renderer.OutputRenderPass();
    info.PipelineInfoMain.Subpass = 0;
    info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    info.CheckVkResultFn = CheckVk;
    if (!ImGui_ImplVulkan_Init(&info)) {
        error = "ImGui Vulkan backend failed";
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        return false;
    }
    renderer_ = &renderer;
    last_render_pass_ = renderer.OutputRenderPass();
    renderer.SetOverlayHook([this](VkCommandBuffer cmd) { Draw(cmd); });
    active_ = true;
    visible_ = false;
    LoadSymbols();
    AC04_LOG_INFO("overlay", "debug overlay ready (toggle '%s'); %zu symbols from %s", toggle_text_.c_str(),
                  symbols_.size(), symbols_source_.empty() ? "nowhere" : symbols_source_.c_str());
    return true;
}

void DebugOverlay::Shutdown() {
    if (!active_) return;
    if (renderer_ != nullptr) {
        vkDeviceWaitIdle(renderer_->Context().device);
        renderer_->SetOverlayHook(nullptr);
    }
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    active_ = false;
    visible_ = false;
    frame_built_ = false;
    renderer_ = nullptr;
}

bool DebugOverlay::HandleEvent(const SDL_Event& event) {
    if (!active_) return false;
    const bool toggle =
        toggle_scancode_ != 0
            ? (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat && event.key.scancode == toggle_scancode_)
            : (event.type == SDL_EVENT_TEXT_INPUT && event.text.text != nullptr && toggle_text_ == event.text.text);
    if (toggle) {
        visible_ = !visible_;
        return true;
    }
    if (!visible_) return false;
    ImGui_ImplSDL3_ProcessEvent(&event);
    const ImGuiIO& io = ImGui::GetIO();
    switch (event.type) {
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
        case SDL_EVENT_TEXT_INPUT:
            return io.WantCaptureKeyboard;
        case SDL_EVENT_MOUSE_MOTION:
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
        case SDL_EVENT_MOUSE_WHEEL:
            return io.WantCaptureMouse;
        default:
            return false;
    }
}

bool DebugOverlay::WantsKeyboard() const { return Visible() && ImGui::GetIO().WantCaptureKeyboard; }

void DebugOverlay::BuildFrame(const OverlayFrameData& data) {
    frame_built_ = false;
    if (!active_) return;
    // Frame-time history keeps running while the overlay is hidden.
    frame_ms_[frame_ms_pos_] = static_cast<float>(data.frame_seconds * 1000.0);
    frame_ms_pos_ = (frame_ms_pos_ + 1) % frame_ms_.size();
    if (!visible_) return;

    // The output render pass is recreated when the swapchain format changes.
    if (renderer_->OutputRenderPass() != last_render_pass_) {
        ImGui_ImplVulkan_PipelineInfo pi{};
        pi.RenderPass = renderer_->OutputRenderPass();
        pi.Subpass = 0;
        pi.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        ImGui_ImplVulkan_CreateMainPipeline(&pi);
        last_render_pass_ = renderer_->OutputRenderPass();
    }

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowPos(ImVec2(8, 8), ImGuiCond_FirstUseEver);
    ImGui::Begin("AC04 debug", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::TextUnformatted("Read-only panels (debug layer)");
    ImGui::Checkbox("Performance", &show_perf_);
    ImGui::SameLine();
    ImGui::Checkbox("Scene", &show_scene_);
    ImGui::SameLine();
    ImGui::Checkbox("Memory", &show_memory_);
    ImGui::SameLine();
    ImGui::Checkbox("Log", &show_log_);
    ImGui::TextDisabled("Toggle: %s", toggle_text_.c_str());
    ImGui::End();

    if (show_perf_) DrawPerformance(data);
    if (show_scene_) DrawScene();
    if (show_memory_) DrawMemory();
    if (show_log_) DrawLog();

    ImGui::Render();
    frame_built_ = true;
}

void DebugOverlay::Draw(VkCommandBuffer cmd) {
    if (!frame_built_) return;
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
    frame_built_ = false;
}

void DebugOverlay::DrawPerformance(const OverlayFrameData& data) {
    ImGui::SetNextWindowPos(ImVec2(8, 110), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(340, 0), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Performance", &show_perf_)) {
        ImGui::End();
        return;
    }
    float sum = 0.0f, worst = 0.0f;
    for (float v : frame_ms_) {
        sum += v;
        worst = std::max(worst, v);
    }
    const float avg = sum / static_cast<float>(frame_ms_.size());
    ImGui::Text("Presented: %.1f fps (avg %.2f ms, worst %.2f ms)", avg > 0.0f ? 1000.0f / avg : 0.0f, avg, worst);
    ImGui::PlotLines("##frame", frame_ms_.data(), static_cast<int>(frame_ms_.size()), static_cast<int>(frame_ms_pos_),
                     "frame ms", 0.0f, 40.0f, ImVec2(-1, 50));
    ImGui::Separator();
    ImGui::Text("Simulation: %s, 59.94 Hz fixed step", data.game_running ? "running" : "stopped");
    ImGui::Text("Steps this frame: %u   total: %llu", data.ticks_run, static_cast<unsigned long long>(data.vblank_count));
    ImGui::Text("Dropped steps (catch-up limit 4): %llu", static_cast<unsigned long long>(data.dropped_ticks));
    ImGui::Text("Interpolation alpha: %.3f", data.alpha);
    ImGui::Separator();
    ImGui::TextUnformatted("DMA intake (counted, not drawn yet)");
    ImGui::Text("GIF  ch2: %llu qw last step, %llu sends", static_cast<unsigned long long>(data.gif_qwords),
                static_cast<unsigned long long>(data.gif_sends));
    ImGui::Text("VIF1 ch1: %llu qw last step, %llu sends", static_cast<unsigned long long>(data.vif1_qwords),
                static_cast<unsigned long long>(data.vif1_sends));
    if (data.bad_chains != 0) ImGui::TextColored(ImVec4(1, 0.5f, 0.3f, 1), "Bad chains: %llu", static_cast<unsigned long long>(data.bad_chains));
    ImGui::Text("DISPFB %08llX  DISPLAY %016llX", static_cast<unsigned long long>(data.dispenv[2]),
                static_cast<unsigned long long>(data.dispenv[3]));
    ImGui::End();
}

void DebugOverlay::DrawScene() {
    ImGui::SetNextWindowPos(ImVec2(360, 8), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(380, 0), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Scene", &show_scene_)) {
        ImGui::End();
        return;
    }
    const u32 ctx = PeekOr<u32>(kMainContextPointer, 0);
    if (ctx == 0) {
        ImGui::TextUnformatted("MainContext not set up yet");
    } else {
        const u16 scene = PeekOr<u16>(ctx + 0x08, 0);
        ImGui::Text("Scene %u: %s", scene, scene < 8 ? kSceneNames[scene] : "?");
        ImGui::Text("Sub-state %u   prev scene %u", PeekOr<u16>(ctx + 0x0E, 0), PeekOr<u16>(ctx + 0x0A, 0));
        ImGui::Text("Next scene %u / sub %u   req %08X", PeekOr<u16>(ctx + 0x0C, 0), PeekOr<u16>(ctx + 0x10, 0),
                    PeekOr<u32>(ctx + 0x04, 0));
        ImGui::Text("Flags %08X", PeekOr<u32>(ctx + 0x00, 0));
        ImGui::Text("frame_count %u   unpaused %u", PeekOr<u32>(ctx + 0x28, 0), PeekOr<u32>(ctx + 0x2C, 0));
        ImGui::Text("scene_frame %u   unpaused %u", PeekOr<u32>(ctx + 0x30, 0), PeekOr<u32>(ctx + 0x34, 0));
        const u32 game = PeekOr<u32>(kGameStatePointer, 0);
        if (game != 0) ImGui::Text("Mission id (game+0x27): %u", PeekOr<u8>(game + 0x27, 0));
    }
    ImGui::Separator();
    const debug::SceneRequestLog& log = debug::SceneRequests();
    ImGui::Text("Requests (%llu total, newest first)", static_cast<unsigned long long>(log.total));
    if (ImGui::BeginTable("requests", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY,
                          ImVec2(0, 160))) {
        ImGui::TableSetupColumn("step");
        ImGui::TableSetupColumn("frame");
        ImGui::TableSetupColumn("from");
        ImGui::TableSetupColumn("request");
        ImGui::TableHeadersRow();
        const u64 n = std::min<u64>(log.total, debug::SceneRequestLog::kSize);
        for (u64 i = 0; i < n; ++i) {
            const debug::SceneRequest& e = log.entries[(log.total - 1 - i) % debug::SceneRequestLog::kSize];
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%llu", static_cast<unsigned long long>(e.vblank));
            ImGui::TableNextColumn();
            ImGui::Text("%u", e.frame_count);
            ImGui::TableNextColumn();
            ImGui::Text("%u", e.from_scene);
            ImGui::TableNextColumn();
            if (e.kind == debug::SceneRequestKind::kScene) {
                ImGui::Text("RequestScene(%u)", e.value);
            } else {
                ImGui::Text("RequestSubState(%u)", e.value);
            }
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

void DebugOverlay::LoadSymbols() {
    symbols_.clear();
    symbols_source_.clear();
    std::vector<std::filesystem::path> candidates;
#if AC04_DEV_BUILD
    candidates.push_back(std::filesystem::u8path(AC04_SOURCE_DIR) / "docs" / "symbols.csv");
#endif
    candidates.push_back(ExecutableDir() / "symbols.csv");
    for (const auto& path : candidates) {
        std::ifstream f(path);
        if (!f) continue;
        std::string line;
        std::getline(f, line);  // header: address,name,kind,status
        while (std::getline(f, line)) {
            const size_t c1 = line.find(',');
            if (c1 == std::string::npos) continue;
            const size_t c2 = line.find(',', c1 + 1);
            const std::string addr = line.substr(0, c1);
            const std::string name = line.substr(c1 + 1, c2 == std::string::npos ? std::string::npos : c2 - c1 - 1);
            char* end = nullptr;
            const unsigned long a = std::strtoul(addr.c_str(), &end, 16);
            if (end == addr.c_str() || name.empty()) continue;
            symbols_.push_back({static_cast<u32>(a), name});
        }
        std::sort(symbols_.begin(), symbols_.end(), [](const Symbol& x, const Symbol& y) { return x.address < y.address; });
        symbols_source_ = PathToUtf8(path);
        return;
    }
}

void DebugOverlay::DrawMemory() {
    ImGui::SetNextWindowPos(ImVec2(360, 330), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(640, 380), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Memory", &show_memory_)) {
        ImGui::End();
        return;
    }
    ImGui::SetNextItemWidth(220);
    const bool go = ImGui::InputTextWithHint("##goto", "address (hex) or symbol name", goto_text_, sizeof(goto_text_),
                                             ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if (ImGui::Button("Go") || go) {
        goto_error_.clear();
        const std::string text = goto_text_;
        char* end = nullptr;
        const unsigned long a = std::strtoul(text.c_str(), &end, 16);
        if (!text.empty() && end != text.c_str() && *end == 0) {
            mem_address_ = static_cast<u32>(a) & ~0xFu;
        } else {
            auto it = std::find_if(symbols_.begin(), symbols_.end(), [&](const Symbol& s) { return s.name == text; });
            if (it != symbols_.end()) {
                mem_address_ = it->address & ~0xFu;
            } else {
                goto_error_ = "not found";
            }
        }
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90);
    ImGui::Combo("##view", &mem_view_, "bytes\0u32\0f32\0");
    ImGui::SameLine();
    if (ImGui::Button("-0x100")) mem_address_ -= 0x100;
    ImGui::SameLine();
    if (ImGui::Button("+0x100")) mem_address_ += 0x100;
    if (!goto_error_.empty()) ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", goto_error_.c_str());
    if (symbols_.empty()) ImGui::TextDisabled("No symbols.csv found (development builds read docs/symbols.csv)");

    if (ImGui::BeginChild("rows", ImVec2(0, 0), ImGuiChildFlags_Borders)) {
        for (u32 row = 0; row < 32; ++row) {
            const u32 a = mem_address_ + row * 16;
            // Label: the last symbol at or before the row (within the row, or the row's owner).
            auto it = std::upper_bound(symbols_.begin(), symbols_.end(), a + 15,
                                       [](u32 v, const Symbol& s) { return v < s.address; });
            const Symbol* label = it != symbols_.begin() ? &*std::prev(it) : nullptr;
            u8 bytes[16];
            const bool ok = Peek(a, bytes, 16);
            char text[160];
            int n = std::snprintf(text, sizeof(text), "%08X  ", a);
            if (!ok) {
                std::snprintf(text + n, sizeof(text) - static_cast<size_t>(n), "-- not RAM/scratchpad --");
            } else if (mem_view_ == 0) {
                for (int i = 0; i < 16; ++i) n += std::snprintf(text + n, sizeof(text) - static_cast<size_t>(n), "%02X ", bytes[i]);
                n += std::snprintf(text + n, sizeof(text) - static_cast<size_t>(n), " ");
                for (int i = 0; i < 16; ++i) text[n++] = std::isprint(bytes[i]) ? static_cast<char>(bytes[i]) : '.';
                text[n] = 0;
            } else {
                for (int i = 0; i < 4; ++i) {
                    u32 w;
                    std::memcpy(&w, bytes + 4 * i, 4);
                    if (mem_view_ == 1) {
                        n += std::snprintf(text + n, sizeof(text) - static_cast<size_t>(n), "%08X  ", w);
                    } else {
                        float f;
                        std::memcpy(&f, &w, 4);
                        n += std::snprintf(text + n, sizeof(text) - static_cast<size_t>(n), "%14.6g ", f);
                    }
                }
            }
            ImGui::TextUnformatted(text);
            if (label != nullptr && label->address + 0x10000 > a) {
                ImGui::SameLine();
                if (label->address >= a) {
                    ImGui::TextColored(ImVec4(0.5f, 0.9f, 0.5f, 1), "%s @%08X", label->name.c_str(), label->address);
                } else {
                    ImGui::TextDisabled("%s+%X", label->name.c_str(), a - label->address);
                }
            }
        }
    }
    ImGui::EndChild();
    ImGui::End();
}

void DebugOverlay::DrawLog() {
    ImGui::SetNextWindowPos(ImVec2(8, 420), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(760, 260), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Session log", &show_log_)) {
        ImGui::End();
        return;
    }
    ImGui::SetNextItemWidth(240);
    ImGui::InputTextWithHint("##filter", "filter (module or text)", log_filter_, sizeof(log_filter_));
    if (ImGui::BeginChild("lines", ImVec2(0, 0), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar)) {
        const bool at_bottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f;
        for (const std::string& line : log::RecentLines(400)) {
            if (log_filter_[0] != 0 && line.find(log_filter_) == std::string::npos) continue;
            const bool error = line.find("] ERROR") != std::string::npos;
            const bool warn = line.find("] WARN") != std::string::npos;
            if (error) {
                ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", line.c_str());
            } else if (warn) {
                ImGui::TextColored(ImVec4(1, 0.8f, 0.3f, 1), "%s", line.c_str());
            } else {
                ImGui::TextUnformatted(line.c_str());
            }
        }
        if (at_bottom) ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();
    ImGui::End();
}

}  // namespace ac04::platform
