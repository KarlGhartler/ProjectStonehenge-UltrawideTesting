// AC04 port entry point. Startup order: command line -> paths -> log -> settings -> window ->
// renderer -> disc image check -> executable into guest memory -> audio -> input -> main loop.
// Milestone B: no game code runs yet; the window shows the test pattern through the output pass.
#include <algorithm>
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <functional>
#include <random>
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
#include "platform/hash_log.h"
#include "platform/input.h"
#include "platform/input_log.h"
#include "platform/p2m2_reader.h"
#include "platform/iso_image.h"
#include "platform/log.h"
#include "platform/paths.h"
#include "platform/png_writer.h"
#include "platform/audio/spu_mixer.h"
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

// --pad-script (development builds): a pad-1 button pressed for 6 vblanks from `vblank`.
struct ScriptedPress {
    u64 vblank;
    std::uint16_t bit;
};

// The pads each game tick reads (docs/port/s5-plan.md P1). The main thread latches both pads before
// it releases a tick, so the game sees exactly one state per tick whatever the host frame rate:
// live devices in normal play; with a pad script or an input log only those (validation runs, no
// live device can leak in). Every latched tick can be written to an input log and replayed.
struct TickInputFeed;
TickInputFeed* g_feed = nullptr;

struct TickInputFeed {
    platform::InputBackend* input = nullptr;
    std::vector<ScriptedPress> script;
    platform::InputLogReader replay;
    platform::InputLogWriter record;
    platform::P2m2Reader p2m2;
    int p2m2_offset = 0;
    bool use_replay = false;
    bool use_p2m2 = false;
    bool p2m2_end_logged = false;
    platform::PadState latched[2];

    bool Live() const { return !use_replay && !use_p2m2 && script.empty(); }

    // Pads for the tick that will have vblank count `vblank` (0 = boot, before the first tick).
    void Latch(u64 vblank) {
        platform::PadState pads[2];
        if (use_p2m2) {
            const s64 frame = static_cast<s64>(vblank) + p2m2_offset;
            if (vblank == 0 || frame < 0 || !p2m2.Frame(static_cast<u32>(frame), pads)) {
                platform::NeutralPads(pads);
                if (vblank != 0 && frame >= 0 && !p2m2_end_logged) {
                    AC04_LOG_INFO("input", "PCSX2 recording ended at tick %llu (pads neutral from here)",
                                  static_cast<unsigned long long>(vblank));
                    p2m2_end_logged = true;
                }
            }
        } else if (use_replay) {
            if (!replay.Get(vblank, pads)) platform::NeutralPads(pads);
        } else if (!script.empty()) {
            if (vblank == 0) {
                pads[0] = platform::PadState{};  // as the live backend before its first update
                pads[1] = platform::PadState{};
            } else {
                platform::NeutralPads(pads);
            }
        } else {
            pads[0] = input->Port(0);
            pads[1] = input->Port(1);
        }
        for (const ScriptedPress& sp : script) {
            if (vblank >= sp.vblank && vblank < sp.vblank + 6) {
                pads[0].connected = true;
                pads[0].buttons = static_cast<std::uint16_t>(pads[0].buttons & ~sp.bit);
            }
        }
        latched[0] = pads[0];
        latched[1] = pads[1];
        if (record.IsOpen() && vblank != 0) record.Write(platform::TickInput{vblank, {pads[0], pads[1]}});
    }
};

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
    if (!renderer.Init(window.Handle(), settings.Get().presentation, cl.options.vk_validation, error)) {
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
    input.Start();

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
    // SPU2 subset mixer (ADR-004): advanced by the game thread per step, drained by the audio device.
    // Static: outlives the audio device thread and the game thread at shutdown.
    static platform::audio::SpuMixer mixer;
    audio.SetSource(&mixer);
    mixer.SetCapture(cl.options.dump_audio);
    debug::SetFreezeSim(cl.options.freeze_sim);
    sce::EeRegisters::Get().Install(guest::Mem());
    {
        sce::SceHost& host = sce::Host();
        host.iso = &disc.Iso();
        // Movie upscaling side path (ADR-006 §5): only when the stage runs (factor above 1).
        if (cl.options.movie_upscale) renderer.SetMovieUpscaleFactor(static_cast<int>(*cl.options.movie_upscale));
        host.movie_pictures = renderer.MovieUpscaleFactor() > 1;
        // --pad-script (development builds): scripted pad-1 presses, held for 6 vblanks each.
        static TickInputFeed feed;
        feed.input = &input;
        std::vector<ScriptedPress>& pad_script = feed.script;
        {
            static const std::pair<const char*, int> kNames[] = {
                {"select", 0}, {"l3", 1}, {"r3", 2}, {"start", 3}, {"up", 4}, {"right", 5}, {"down", 6}, {"left", 7},
                {"l2", 8}, {"r2", 9}, {"l1", 10}, {"r1", 11}, {"triangle", 12}, {"circle", 13}, {"cross", 14}, {"square", 15}};
            std::string spec = cl.options.pad_script;
            size_t pos = 0;
            while (pos < spec.size()) {
                const size_t end = std::min(spec.find(',', pos), spec.size());
                const std::string item = spec.substr(pos, end - pos);
                const size_t colon = item.find(':');
                bool ok = false;
                if (colon != std::string::npos) {
                    const std::string name = item.substr(colon + 1);
                    for (const auto& [n, bit] : kNames) {
                        if (name == n) {
                            pad_script.push_back({std::strtoull(item.c_str(), nullptr, 10), static_cast<std::uint16_t>(1u << bit)});
                            ok = true;
                        }
                    }
                }
                if (!ok) AC04_LOG_WARN("input", "--pad-script: cannot read '%s'", item.c_str());
                pos = end + 1;
            }
        }
        if (!cl.options.replay_input.empty()) {
            feed.use_replay = feed.replay.Open(platform::PathFromUtf8(cl.options.replay_input));
            if (feed.use_replay) {
                AC04_LOG_INFO("input", "input log %s: %zu ticks (live devices ignored)", cl.options.replay_input.c_str(),
                              feed.replay.Size());
            } else {
                AC04_LOG_ERROR("input", "cannot read the input log %s", cl.options.replay_input.c_str());
            }
        }
        if (!cl.options.replay_p2m2.empty()) {
            std::string err;
            feed.use_p2m2 = feed.p2m2.Open(platform::PathFromUtf8(cl.options.replay_p2m2), err);
            feed.p2m2_offset = cl.options.p2m2_offset;
            if (!feed.use_p2m2) {
                AC04_LOG_ERROR("input", "cannot read the PCSX2 recording %s: %s", cl.options.replay_p2m2.c_str(), err.c_str());
            } else {
                AC04_LOG_INFO("input", "PCSX2 recording %s: %u frames, game '%s', offset %d%s", cl.options.replay_p2m2.c_str(),
                              feed.p2m2.FrameCount(), feed.p2m2.GameName().c_str(), feed.p2m2_offset,
                              feed.p2m2.FromSavestate() ? " (starts from a save state: not supported, input only)" : "");
            }
        }
        if (cl.options.record_input) {
            const auto dir = paths.user_data / "inputs";
            std::error_code ec;
            std::filesystem::create_directories(dir, ec);
            if (!feed.record.Open(dir / "input.ac4i")) AC04_LOG_ERROR("input", "cannot write the input log");
        }
        if (!feed.Live())
            AC04_LOG_INFO("input", "pads from the %s only",
                          feed.use_p2m2 ? "PCSX2 recording" : (feed.use_replay ? "input log" : "pad script"));
        feed.Latch(0);
        g_feed = &feed;
        host.read_pad = [](int port) { return g_feed->latched[port & 1]; };
        host.iop_services.clear();
        const iop::IopRead iop_read = [](std::uint32_t addr, std::uint32_t size) -> const std::uint8_t* {
            auto& ram = sce::IopRam();
            if (static_cast<std::uint64_t>(addr) + size > ram.size()) return nullptr;
            return ram.data() + addr;
        };
        host.iop_services.push_back(iop::MakeSoundService(&mixer, iop_read));
        host.on_vblank = [] { mixer.AdvanceStep(); };
        if (cl.options.dump_dma_at) {
            // Development capture: one step's VIF1 stream and GIF output for tools/research/dmacap.py.
            host.dma_capture_at = *cl.options.dump_dma_at;
            host.on_dma_capture = [&paths](u64 vblank, const std::vector<u32>& vif, const std::vector<u8>& gif) {
                const auto dir = paths.user_data / "dumps";
                std::error_code ec;
                std::filesystem::create_directories(dir, ec);
                char name[64];
                std::snprintf(name, sizeof(name), "vif1_%06llu.bin", static_cast<unsigned long long>(vblank));
                if (std::FILE* f = _wfopen((dir / name).c_str(), L"wb")) {
                    std::fwrite(vif.data(), 4, vif.size(), f);
                    std::fclose(f);
                }
                std::snprintf(name, sizeof(name), "gif_%06llu.bin", static_cast<unsigned long long>(vblank));
                if (std::FILE* f = _wfopen((dir / name).c_str(), L"wb")) {
                    std::fwrite(gif.data(), 1, gif.size(), f);
                    std::fclose(f);
                }
                AC04_LOG_INFO("dma", "step capture at vblank %llu: %zu VIF1 words, %zu GIF bytes",
                              static_cast<unsigned long long>(vblank), vif.size() / 2, gif.size());
            };
        }
        host.iop_services.push_back(iop::MakeVagService(&mixer, iop_read));
        host.iop_services.push_back(iop::MakeUsbJoyService());
        // Slot 1: the port's saves folder (a formatted empty card until the first save), or a card
        // folder / no card given on the command line (development builds, validation runs).
        if (cl.options.no_memcard) {
            host.memcard.reset();
            AC04_LOG_INFO("saves", "memory card: none (--no-memcard)");
        } else if (!cl.options.memcard_dir.empty()) {
            host.memcard = std::make_unique<memcard::FolderCard>(PathFromUtf8(cl.options.memcard_dir), false);
            AC04_LOG_INFO("saves", "memory card: %s (%s)", cl.options.memcard_dir.c_str(),
                          !host.memcard->Present() ? "no card" : host.memcard->Formatted() ? "formatted" : "unformatted");
        } else {
            host.memcard = std::make_unique<memcard::FolderCard>(paths.saves, true);
        }
    }
    sce::GameRuntime& game = sce::GameRuntime::Get();
    debug::SetVBlankSource([] { return sce::GameRuntime::Get().VBlankCount(); });
    if (cl.options.dump_ram_at_first_scenestep) {
        // Runs on the game thread at the entry of the first SceneStep (docs/port/validation.md §2d).
        debug::SetFirstSceneStepHook([&paths] { DumpGuestRam(paths, "ram_first_scenestep.bin"); });
    }
    if (cl.options.dump_ram_at) {
        // RAM checkpoint (docs/port/validation-strategy.md §2.1): runs on the game thread where the
        // translated code reports the PCSX2 breakpoint address.
        const unsigned addr = *cl.options.dump_ram_at;
        const unsigned hit = cl.options.dump_ram_at_hit;
        debug::SetCheckpointHook(addr, hit, [&paths, addr, hit] {
            char name[64];
            std::snprintf(name, sizeof(name), "ram_%08X_%u.bin", addr, hit);
            DumpGuestRam(paths, name);
        });
    }
    game.Start([] { game::Entry(); });
    AC04_LOG_INFO("game", "boot reached the first vsync wait (%zu translated functions registered)",
                  guest::RegisteredGuestFunctionCount());

    AC04_LOG_INFO("startup", "ready");
    log::Flush();

    platform::RamHashLog hash_log;
    if (cl.options.hash_log) {
        const auto dir = paths.user_data / "dumps";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        if (!hash_log.Open(dir / "hashes.ac4h", guest::kRamSize))
            AC04_LOG_ERROR("loader", "cannot write the RAM hash log");
    }
    std::mt19937 stress_rng(std::random_device{}());

    sce::TickClock clock;
    auto last_time = std::chrono::steady_clock::now();
    bool game_end_logged = false;
    bool running = true;
    std::vector<unsigned> screenshot_due = cl.options.screenshot_at;
    std::sort(screenshot_due.begin(), screenshot_due.end());
    bool take_screenshot = false;
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
#if AC04_DEV_BUILD
                case SDL_EVENT_KEY_DOWN:
                    if (event.key.key == SDLK_F8 && !event.key.repeat) take_screenshot = true;
                    break;
#endif
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
        // One tick at a time: the pads are sampled and latched before each tick (s5-plan.md P1).
        for (u32 t = 0; t < ticks; ++t) {
            if (t > 0) {
                SDL_PumpEvents();
                input.Update(/*keyboard_captured=*/overlay.WantsKeyboard());
            }
            g_feed->Latch(game.VBlankCount() + 1);
            if (!game.RunTicks(1)) {
                if (!game_end_logged) AC04_LOG_WARN("game", "the game thread has ended");
                game_end_logged = true;
                break;
            }
            if (hash_log.IsOpen())
                hash_log.Write(game.VBlankCount(), guest::Mem().Ram(), guest::kRamSize,
                               guest::Mem().Scratchpad(), guest::kScratchpadSize);
        }
        // The game is parked: its GS data and the latest movie picture (side path) can be taken.
        renderer.SubmitGsSteps(sce::Dma().gs_steps, sce::Dma().gs_steps_marks, sce::Dma().gs_step_pool);
        renderer.SetDisplay(sce::Dma().dispenv[2], sce::Dma().dispenv[3]);
        {
            sce::MoviePicture& mp = sce::Dma().movie_picture;
            if (mp.serial != 0) renderer.SubmitMoviePicture(mp.serial, mp.width, mp.height, mp.rgba);
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
        // Development builds: --exit-at ends the session normally (dumps written at exit).
        if (cl.options.exit_at && game.VBlankCount() >= *cl.options.exit_at) running = false;
        // Native screenshot (development builds): F8, or --screenshot-at after the given vblanks.
        while (!screenshot_due.empty() && game.VBlankCount() >= screenshot_due.front()) {
            screenshot_due.erase(screenshot_due.begin());
            take_screenshot = true;
        }
        if (take_screenshot) {
            take_screenshot = false;
            std::vector<uint8_t> rgba;
            int w = 0, h = 0;
            if (renderer.CaptureNative(rgba, w, h)) {
                const auto dir = paths.user_data / "screenshots";
                std::error_code ec;
                std::filesystem::create_directories(dir, ec);
                char name[64];
                std::snprintf(name, sizeof(name), "native_%06llu.png", static_cast<unsigned long long>(game.VBlankCount()));
                if (!WritePng(dir / name, rgba.data(), w, h))
                    AC04_LOG_ERROR("renderer", "cannot write the screenshot %s", name);
                // --movie-upscale: the upscaling stage output of the current movie picture as well.
                if (renderer.MovieUpscaleFactor() > 1 && renderer.CaptureMovieUpscaled(rgba, w, h)) {
                    std::snprintf(name, sizeof(name), "movie_x%d_%06llu.png", renderer.MovieUpscaleFactor(),
                                  static_cast<unsigned long long>(game.VBlankCount()));
                    if (!WritePng(dir / name, rgba.data(), w, h))
                        AC04_LOG_ERROR("renderer", "cannot write the screenshot %s", name);
                }
            }
        }
        if (!renderer.DrawFrame()) {
            ShowErrorBox(kAppTitle, "The graphics device stopped working. See the session log:\n" +
                                        PathToUtf8(log::FilePath()), &window);
            break;
        }
        pacer.WaitNextFrame();
        // --stress-host: random main-thread delays, so catch-up batches vary (determinism check).
        if (cl.options.stress_host)
            std::this_thread::sleep_for(std::chrono::milliseconds(std::uniform_int_distribution<int>(0, 40)(stress_rng)));
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
    if (cl.options.dump_audio) {
        // 48 kHz stereo s16 WAV of everything the mixer produced (research only, contains game audio).
        const auto& pcm = mixer.Capture();
        const auto dir = paths.user_data / "dumps";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        std::ofstream f(dir / "audio.wav", std::ios::binary);
        const std::uint32_t bytes = static_cast<std::uint32_t>(pcm.size() * 2);
        auto u32 = [&f](std::uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
        auto u16 = [&f](std::uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
        f.write("RIFF", 4);
        u32(36 + bytes);
        f.write("WAVEfmt ", 8);
        u32(16);
        u16(1);
        u16(2);
        u32(48000);
        u32(48000 * 4);
        u16(4);
        u16(16);
        f.write("data", 4);
        u32(bytes);
        f.write(reinterpret_cast<const char*>(pcm.data()), bytes);
    }
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
