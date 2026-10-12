#include <doctest/doctest.h>

#include <nlohmann/json.hpp>

#include "platform/settings.h"

using namespace ac04::platform;

TEST_CASE("settings: defaults") {
    SettingsStore store;
    std::vector<std::string> warnings;
    store.LoadFromText("{}", warnings);
    CHECK(warnings.empty());
    const Settings& s = store.Get();
    CHECK(s.presentation.filter == ScaleFilter::Sharp);
    CHECK(s.presentation.vsync);
    CHECK(s.presentation.frame_cap == 360);
    CHECK(s.presentation.window_mode == WindowMode::Windowed);
    CHECK(s.presentation.internal_scale == 1);
    CHECK(s.audio.on_focus_loss == FocusLossAudio::KeepPlaying);
    CHECK(s.debug.overlay_toggle == "\\");
    CHECK(s.input.bindings.at("cross").size() == 2);
}

TEST_CASE("settings: valid values are read") {
    SettingsStore store;
    std::vector<std::string> warnings;
    store.LoadFromText(R"({"presentation":{"filter":"smooth","vsync":false,"frame_cap":144,
        "window_mode":"borderless"},"paths":{"iso":"C:\\x.iso"},"audio":{"on_focus_loss":"mute"},
        "debug":{"overlay_enabled":true,"overlay_toggle":"key:F12"},
        "input":{"bindings":{"cross":["key:Space"]}}})",
                       warnings);
    CHECK(warnings.empty());
    const Settings& s = store.Get();
    CHECK(s.presentation.filter == ScaleFilter::Smooth);
    CHECK_FALSE(s.presentation.vsync);
    CHECK(s.presentation.frame_cap == 144);
    CHECK(s.presentation.window_mode == WindowMode::Borderless);
    CHECK(s.paths.iso == "C:\\x.iso");
    CHECK(s.audio.on_focus_loss == FocusLossAudio::Mute);
    CHECK(s.debug.overlay_enabled);
    CHECK(s.debug.overlay_toggle == "key:F12");
    CHECK(s.input.bindings.at("cross") == std::vector<std::string>{"key:Space"});
    CHECK(s.input.bindings.at("circle").size() == 2);  // untouched controls keep their default
}

TEST_CASE("settings: invalid values fall back to defaults with warnings") {
    SettingsStore store;
    std::vector<std::string> warnings;
    store.LoadFromText(R"({"presentation":{"filter":"lanczos","frame_cap":75,"internal_scale":2,
        "vsync":"yes"},"input":{"bindings":{"turbo":["key:T"]}}})",
                       warnings);
    CHECK(warnings.size() == 5);
    const Settings& s = store.Get();
    CHECK(s.presentation.filter == ScaleFilter::Sharp);
    CHECK(s.presentation.frame_cap == 360);
    CHECK(s.presentation.internal_scale == 1);
    CHECK(s.presentation.vsync);
}

TEST_CASE("settings: broken JSON gives defaults") {
    SettingsStore store;
    std::vector<std::string> warnings;
    store.LoadFromText("{ not json", warnings);
    CHECK(warnings.size() == 1);
    CHECK(store.Get().presentation.frame_cap == 360);
}

TEST_CASE("settings: unknown keys survive a save") {
    SettingsStore store;
    std::vector<std::string> warnings;
    store.LoadFromText(R"({"mods":{"x":1},"presentation":{"future_option":true}})", warnings);
    const auto out = nlohmann::json::parse(store.ToText());
    CHECK(out["mods"]["x"] == 1);
    CHECK(out["presentation"]["future_option"] == true);
    CHECK(out["presentation"]["filter"] == "sharp");
}

TEST_CASE("settings: change notification per section") {
    SettingsStore store;
    int presentation_calls = 0, audio_calls = 0;
    store.Subscribe(SettingsSection::Presentation, [&](const Settings&) { ++presentation_calls; });
    const int id = store.Subscribe(SettingsSection::Audio, [&](const Settings&) { ++audio_calls; });
    store.Update(SettingsSection::Presentation, [](Settings& s) { s.presentation.filter = ScaleFilter::Smooth; });
    CHECK(presentation_calls == 1);
    CHECK(audio_calls == 0);

    Settings next = store.Get();
    next.audio.on_focus_loss = FocusLossAudio::Mute;
    store.Replace(next);  // only the audio section changed
    CHECK(presentation_calls == 1);
    CHECK(audio_calls == 1);

    store.Unsubscribe(id);
    next.audio.on_focus_loss = FocusLossAudio::KeepPlaying;
    store.Replace(next);
    CHECK(audio_calls == 1);
}

TEST_CASE("settings: frame caps") {
    CHECK(IsValidFrameCap(60));
    CHECK(IsValidFrameCap(120));
    CHECK(IsValidFrameCap(144));
    CHECK(IsValidFrameCap(240));
    CHECK(IsValidFrameCap(360));
    CHECK_FALSE(IsValidFrameCap(0));
    CHECK_FALSE(IsValidFrameCap(75));
}
