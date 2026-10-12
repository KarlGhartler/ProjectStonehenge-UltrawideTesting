// AC04 port entry point. Startup order: command line -> paths -> log -> settings -> window ->
// renderer -> disc image check -> executable into guest memory -> audio -> input -> main loop.
// Milestone B: no game code runs yet; the window shows the test pattern through the output pass.
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <functional>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include "build_info.h"
#include "common/guest_memory.h"
#include "common/debug_hooks.h"
#include "common/guest_stack.h"
#include "game/game.h"
#include "platform/audio.h"
#include "platform/command_line.h"
#include "platform/crash.h"
#include "platform/debug/overlay.h"
#include "platform/elf_loader.h"
#include "platform/frame_pacer.h"
#include "platform/iop/iop_services.h"
#include "platform/input.h"
#include "platform/iso_image.h"
#include "platform/log.h"
#include "platform/paths.h"
#include "platform/render/renderer.h"
#include "platform/settings.h"
#include "platform/system_info.h"
#include "platform/window.h"
#include "sce/hw_registers.h"
#include "sce/runtime.h"
#include "sce/sce_host.h"

namespace {

using namespace ac04;
using namespace ac04::platform;

constexpr const char* kAppTitle = "AC04 Port";

std::string VersionText() {
    return std::string(kAppTitle) + " " + AC04_VERSION_STRING + " (" + AC04_GIT_COMMIT + ", " +
           (AC04_DEV_BUILD ? "development" : "release") + " build)";
}

// Disc image check and executable load (milestone B). Returns false when the port must exit (the
// reason has been shown and logged). `unverified` is set when a development build continues
// with --allow-unverified-iso.
// Research aid (development builds): guest RAM + scratchpad for tools/research/ramdiff.py.
// Written to the user data folder, never into the repo: it contains game data.
void DumpGuestRam(const Paths& paths, const char* name) {
    const auto dir = paths.user_data / "dumps";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const auto file = dir / name;
    if (std::FILE* f = _wfopen(file.c_str(), L"wb")) {
        std::fwrite(guest::Mem().Ram(), 1, guest::kRamSize, f);
        std::fwrite(guest::Mem().Scratchpad(), 1, guest::kScratchpadSize, f);
        std::fclose(f);
        AC04_LOG_INFO("loader", "guest RAM dumped to %s", PathToUtf8(file).c_str());
    } else {
        AC04_LOG_ERROR("loader", "cannot write %s", PathToUtf8(file).c_str());
    }
}

bool PrepareDisc(const CommandLine& cl, const Paths& paths, const Settings& settings, Window& window,
                 render::Renderer& renderer, IsoImage& image, LoadedExecutable& exe, bool& unverified) {
    unverified = false;
    const std::filesystem::path iso_path = PathFromUtf8(settings.paths.iso);
    auto fail = [&](const IsoCheck& check) {
        const std::string text = DescribeIsoProblem(check, iso_path, paths.settings_file);
        AC04_LOG_ERROR("iso", "%s", text.c_str());
        ShowErrorBox(kAppTitle, text, &window);
        return false;
    };

    IsoCheck quick = image.OpenAndQuickCheck(iso_path);
    if (quick.result == IsoCheckResult::WrongImage && cl.allow_unverified_iso) {
        AC04_LOG_WARN("iso", "not the supported image (%s); continuing (--allow-unverified-iso)", quick.detail.c_str());
        unverified = true;
    } else if (quick.result != IsoCheckResult::Ok) {
        return fail(quick);
    }
    AC04_LOG_INFO("iso", "image: %s (volume '%s')", PathToUtf8(iso_path).c_str(),
                  image.Iso().Volume().volume_id.c_str());

    if (!unverified) {
        // Full SHA-1 on a worker thread; the window keeps responding and shows the progress.
        std::atomic<bool> cancel{false}, done{false};
        std::atomic<int> percent{0};
        IsoCheck full;
        std::thread worker([&] {
            full = image.FullCheck(paths.user_data / "iso_cache.json", cl.verify_iso,
                                   [&](double p) { percent = static_cast<int>(p * 100.0); }, cancel);
            done = true;
        });
        int shown = -1;
        while (!done) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) cancel = true;
                if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) renderer.OnWindowResized();
            }
            if (percent != shown) {
                shown = percent;
                window.SetTitle(VersionText() + " - verifying disc image " + std::to_string(shown) + "%");
            }
            renderer.DrawFrame();
            std::this_thread::sleep_for(std::chrono::milliseconds(15));
        }
        worker.join();
        window.SetTitle(VersionText());
        if (full.result == IsoCheckResult::Cancelled) {
            AC04_LOG_INFO("iso", "verification cancelled by the user");
            return false;
        }
        if (full.result == IsoCheckResult::HashMismatch && cl.allow_unverified_iso) {
            AC04_LOG_WARN("iso", "checksum mismatch (%s); continuing (--allow-unverified-iso)", full.detail.c_str());
            unverified = true;
        } else if (full.result != IsoCheckResult::Ok) {
            return fail(full);
        } else {
            AC04_LOG_INFO("iso", "verified: SHA-1 %s (%s)", full.found_sha1.c_str(),
                          full.from_cache ? "cached result" : "computed now");
        }
    }

    std::string error;
    guest::Mem().Reset();
    guest::ResetStack();
    if (!LoadGameExecutable(image.Iso(), guest::Mem(), unverified, exe, error)) {
        AC04_LOG_ERROR("loader", "%s", error.c_str());
        ShowErrorBox(kAppTitle, "The game executable could not be loaded from the disc image.\n\n" + error, &window);
        return false;
    }
    for (const auto& seg : exe.segments)
        AC04_LOG_INFO("loader", "segment 0x%08X: %u bytes from the file, %u in memory", seg.vaddr,
                      seg.file_size, seg.mem_size);
    for (const auto& z : exe.zeroed) AC04_LOG_INFO("loader", "zeroed 0x%08X (+0x%X)", z.first, z.second);
    AC04_LOG_INFO("loader", "%s loaded: entry 0x%08X, gp 0x%08X, SHA-1 %s, guest sp 0x%08X",
                  SupportedImage::kExecutableName, exe.entry, exe.gp, exe.sha1.c_str(), guest::StackPointer());
    if (unverified) window.SetTitle(VersionText() + " [UNVERIFIED DISC IMAGE]");
    if (cl.dump_ram_after_load) DumpGuestRam(paths, "ram_after_load.bin");
    return true;
}

// Reloads settings.json when it changes on disk, so values can be edited while the port runs
// (there is no settings menu yet). Checked once per second.
class SettingsWatcher {
public:
    // `overrides` re-applies the session-only command-line values to the reloaded settings.
    SettingsWatcher(SettingsStore& store, std::function<void(Settings&)> overrides)
        : store_(store), overrides_(std::move(overrides)) {
        Remember();
    }

    void Poll() {
        const auto now = std::chrono::steady_clock::now();
        if (now - last_check_ < std::chrono::seconds(1)) return;
        last_check_ = now;
        std::error_code ec;
        const auto time = std::filesystem::last_write_time(store_.File(), ec);
        if (ec || time == last_time_) return;
        last_time_ = time;
        SettingsStore fresh;
        std::vector<std::string> warnings;
        fresh.Load(store_.File(), warnings);
        for (const auto& w : warnings) AC04_LOG_WARN("settings", "%s", w.c_str());
        AC04_LOG_INFO("settings", "settings.json changed on disk, reloaded");
        Settings next = fresh.Get();
        overrides_(next);
        store_.Replace(next);
    }
    void Remember() {
        std::error_code ec;
        last_time_ = std::filesystem::last_write_time(store_.File(), ec);
    }

private:
    SettingsStore& store_;
    std::function<void(Settings&)> overrides_;
    std::filesystem::file_time_type last_time_{};
    std::chrono::steady_clock::time_point last_check_{};
};

int Run(int argc, char** argv) {
    const CommandLineResult cl = ParseCommandLine(argc, argv);
    if (cl.options.show_help || !cl.errors.empty()) {
        std::string text;
        for (const auto& e : cl.errors) text += "Error: " + e + "\n";
        if (!text.empty()) text += "\n";
        text += CommandLineHelp();
        std::fputs(text.c_str(), stdout);
        if (!AC04_DEV_BUILD) ShowInfoBox(kAppTitle, text);
        return cl.errors.empty() ? 0 : 2;
    }
    if (cl.options.show_version) {
        std::puts(VersionText().c_str());
        if (!AC04_DEV_BUILD) ShowInfoBox(kAppTitle, VersionText());
        return 0;
    }

    InstallCrashHandler();
    InitTimerResolution();

    Paths paths;
    std::string error;
    if (!ResolvePaths(PathFromUtf8(cl.options.user_data_dir), paths, error)) {
        ShowErrorBox(kAppTitle, "The port cannot create its data folder.\n\n" + error);
        return 1;
    }
    log::Open(paths.logs);
    AC04_LOG_INFO("startup", "%s", VersionText().c_str());
    AC04_LOG_INFO("startup", "OS: %s", OperatingSystemDescription().c_str());
    AC04_LOG_INFO("startup", "user data: %s", PathToUtf8(paths.user_data).c_str());

    SettingsStore settings;
    {
        std::vector<std::string> warnings;
        settings.Load(paths.settings_file, warnings);
        for (const auto& w : warnings) AC04_LOG_WARN("settings", "%s", w.c_str());
        if (!std::filesystem::exists(paths.settings_file) || !warnings.empty()) {
            // First run, or problems found: write a complete file (unknown keys are kept).
            if (!settings.Save(&error)) AC04_LOG_ERROR("settings", "cannot save settings.json: %s", error.c_str());
        }
    }
    // Session-only overrides from the command line (not saved).
    const auto apply_overrides = [&cl](Settings& s) {
        if (cl.options.debug_overlay.has_value()) s.debug.overlay_enabled = *cl.options.debug_overlay;
        if (!cl.options.iso_path.empty()) s.paths.iso = cl.options.iso_path;
    };
    {
        Settings s = settings.Get();
        apply_overrides(s);
        settings.Replace(s);
    }
    AC04_LOG_INFO("startup", "settings in effect:\n%s", settings.ToText().c_str());

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
        AC04_LOG_ERROR("startup", "SDL_Init failed: %s", SDL_GetError());
        ShowErrorBox(kAppTitle, std::string("Initialisation failed: ") + SDL_GetError());
        return 1;
    }

    Window window;
    if (!window.Create(VersionText(), settings.Get().presentation, error)) {
        AC04_LOG_ERROR("startup", "%s", error.c_str());
        ShowErrorBox(kAppTitle, error);
        return 1;
    }

    render::Renderer renderer;
    if (!renderer.Init(window.Handle(), settings.Get().presentation, error)) {
        AC04_LOG_ERROR("renderer", "%s", error.c_str());
        ShowErrorBox(kAppTitle, "The renderer could not start.\n\n" + error, &window);
        return 1;
    }

    IsoImage disc;
    LoadedExecutable executable;
    bool unverified_disc = false;
    if (!PrepareDisc(cl.options, paths, settings.Get(), window, renderer, disc, executable, unverified_disc)) {
        renderer.Shutdown();
        window.Destroy();
        SDL_Quit();
        log::Close();
        return 1;
    }

    AudioOutput audio;
    if (audio.Open(error)) {
        AC04_LOG_INFO("audio", "output: %s, 48000 Hz stereo", audio.DeviceName().c_str());
    } else {
        // The port keeps running without sound.
        AC04_LOG_ERROR("audio", "%s", error.c_str());
    }
    audio.ApplySettings(settings.Get().audio);

    InputBackend input;
    input.ApplySettings(settings.Get().input);

    FramePacer pacer;
    pacer.SetCap(settings.Get().presentation.frame_cap);

    // Change notifications: the platform modules follow settings.json at run time.
    settings.Subscribe(SettingsSection::Presentation, [&](const Settings& s) {
        renderer.ApplySettings(s.presentation);
        window.ApplyMode(s.presentation);
        pacer.SetCap(s.presentation.frame_cap);
        AC04_LOG_INFO("settings", "presentation: filter %s, vsync %s, cap %d, %s",
                      ToString(s.presentation.filter), s.presentation.vsync ? "on" : "off",
                      s.presentation.frame_cap, ToString(s.presentation.window_mode));
    });
    settings.Subscribe(SettingsSection::Audio, [&](const Settings& s) { audio.ApplySettings(s.audio); });
    settings.Subscribe(SettingsSection::Input, [&](const Settings& s) { input.ApplySettings(s.input); });
    // Debug overlay (optional debug layer, milestone D): created only when enabled in settings.
    DebugOverlay overlay;
    if (!overlay.Apply(settings.Get().debug, window.Handle(), renderer, error)) {
        AC04_LOG_ERROR("overlay", "debug overlay unavailable: %s", error.c_str());
    }
    settings.Subscribe(SettingsSection::Debug, [&](const Settings& s) {
        std::string overlay_error;
        if (!overlay.Apply(s.debug, window.Handle(), renderer, overlay_error))
            AC04_LOG_ERROR("overlay", "debug overlay unavailable: %s", overlay_error.c_str());
    });
    SettingsWatcher watcher(settings, apply_overrides);

    // Game thread (ADR-002): SDK layer wired to the disc, the pads and the IOP stand-ins, then the
    // crt0 equivalent runs until its first vsync wait.
    sce::EeRegisters::Get().Install(guest::Mem());
    {
        sce::SceHost& host = sce::Host();
        host.iso = &disc.Iso();
        host.read_pad = [&input](int port) { return input.Port(port); };
        host.iop_services.clear();
        host.iop_services.push_back(iop::MakeSoundService());
        host.iop_services.push_back(iop::MakeVagService());
        host.iop_services.push_back(iop::MakeUsbJoyService());
    }
    sce::GameRuntime& game = sce::GameRuntime::Get();
    debug::SetVBlankSource([] { return sce::GameRuntime::Get().VBlankCount(); });
    if (cl.options.dump_ram_at_first_scenestep) {
        // Runs on the game thread at the entry of the first SceneStep (docs/port/validation.md §2d).
        debug::SetFirstSceneStepHook([&paths] { DumpGuestRam(paths, "ram_first_scenestep.bin"); });
    }
    game.Start([] { game::Entry(); });
    AC04_LOG_INFO("game", "boot reached the first vsync wait (%zu translated functions registered)",
                  guest::RegisteredGuestFunctionCount());

    AC04_LOG_INFO("startup", "ready");
    log::Flush();

    sce::TickClock clock;
    auto last_time = std::chrono::steady_clock::now();
    bool game_end_logged = false;
    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_EVENT_QUIT:
                case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                    running = false;
                    break;
                case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                case SDL_EVENT_WINDOW_RESTORED:
                    renderer.OnWindowResized();
                    break;
                case SDL_EVENT_WINDOW_FOCUS_GAINED:
                    audio.OnFocusChanged(true);
                    break;
                case SDL_EVENT_WINDOW_FOCUS_LOST:
                    audio.OnFocusChanged(false);
                    break;
                default:
                    break;
            }
            overlay.HandleEvent(event);
            input.HandleEvent(event);
        }
        watcher.Poll();
        input.Update(/*keyboard_captured=*/overlay.WantsKeyboard());
        // Fixed 59.94 Hz simulation: release the ticks due since the last frame (catch-up <= 4);
        // the game thread runs them and parks again before the frame is presented.
        const auto now = std::chrono::steady_clock::now();
        const double elapsed = std::chrono::duration<double>(now - last_time).count();
        last_time = now;
        const u32 ticks = clock.Advance(elapsed);
        if (!game.RunTicks(ticks) && !game_end_logged) {
            AC04_LOG_WARN("game", "the game thread has ended");
            game_end_logged = true;
        }
        game.Interpolate(clock.Alpha());
        if (overlay.Active()) {
            const sce::DmaStats& dma = sce::Dma();
            OverlayFrameData od;
            od.frame_seconds = elapsed;
            od.ticks_run = ticks;
            od.vblank_count = game.VBlankCount();
            od.dropped_ticks = clock.DroppedTicks();
            od.alpha = clock.Alpha();
            od.game_running = game.Running();
            od.gif_qwords = dma.frame_qwords_gif;
            od.vif1_qwords = dma.frame_qwords_vif1;
            od.gif_sends = dma.sends[2];
            od.vif1_sends = dma.sends[1];
            od.bad_chains = dma.bad_chains;
            od.dispenv = dma.dispenv;
            overlay.BuildFrame(od);
        }
        if (!renderer.DrawFrame()) {
            ShowErrorBox(kAppTitle, "The graphics device stopped working. See the session log:\n" +
                                        PathToUtf8(log::FilePath()), &window);
            break;
        }
        pacer.WaitNextFrame();
    }

    // Remember the windowed size for the next start.
    if (settings.Get().presentation.window_mode == WindowMode::Windowed) {
        int w = 0, h = 0;
        window.WindowedSize(w, h);
        const auto& p = settings.Get().presentation;
        if (w > 0 && h > 0 && (w != p.window_width || h != p.window_height)) {
            SettingsStore saved;
            std::vector<std::string> ignored;
            saved.Load(paths.settings_file, ignored);  // file values, without session overrides
            Settings s = saved.Get();
            s.presentation.window_width = w;
            s.presentation.window_height = h;
            saved.Replace(s);
            if (!saved.Save(&error)) AC04_LOG_ERROR("settings", "cannot save settings.json: %s", error.c_str());
        }
    }

    game.Stop();
    overlay.Shutdown();
    AC04_LOG_INFO("game", "stopped after %llu vblanks (%llu ticks dropped)",
                  static_cast<unsigned long long>(game.VBlankCount()),
                  static_cast<unsigned long long>(clock.DroppedTicks()));
    renderer.Shutdown();
    audio.Close();
    window.Destroy();
    SDL_Quit();
    AC04_LOG_INFO("startup", "clean exit");
    log::Close();
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    return Run(argc, argv);
}
