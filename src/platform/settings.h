// Port settings (ADR-005 §4, ADR-006 "Settings architecture").
// - One struct with defaults; settings.json load/save; change notification per section, so a
//   settings menu or the debug overlay can change values at run time later.
// - Missing or invalid values fall back to the default and produce a warning. Unknown keys are
//   kept when the file is saved again.
// - Game code never reads settings.
#pragma once

#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace ac04::platform {

enum class ScaleFilter { Sharp, Smooth };
enum class WindowMode { Windowed, Borderless };
enum class FocusLossAudio { KeepPlaying, Mute };

struct PresentationSettings {
    ScaleFilter filter = ScaleFilter::Sharp;
    bool vsync = true;
    int frame_cap = 360;            // presented frames per second: 60, 120, 144, 240 or 360
    WindowMode window_mode = WindowMode::Windowed;
    int window_width = 1280;        // windowed client size
    int window_height = 960;
    int internal_scale = 1;         // renderer scale factor; only 1 is accepted for now
};

struct PathSettings {
    std::string iso;                // UTF-8 path of the disc image
};

struct AudioSettings {
    FocusLossAudio on_focus_loss = FocusLossAudio::KeepPlaying;
};

struct InputSettings {
    // PS2 control name ("cross", "lx-", …) -> host inputs ("key:Z", "pad:south", "axis:leftx-").
    std::map<std::string, std::vector<std::string>> bindings;
};

struct DebugSettings {
    bool overlay_enabled = AC04_DEV_BUILD != 0;  // on by default in development builds
    std::string overlay_toggle = "\\";           // typed character (or "key:<name>")
};

struct Settings {
    PresentationSettings presentation;
    PathSettings paths;
    AudioSettings audio;
    InputSettings input;
    DebugSettings debug;
};

enum class SettingsSection { Presentation, Paths, Audio, Input, Debug };

// Values accepted by the frame cap setting.
bool IsValidFrameCap(int fps);
InputSettings DefaultInputSettings();

class SettingsStore {
public:
    using Listener = std::function<void(const Settings&)>;

    SettingsStore();

    // Loads `file`. A missing file gives the defaults (and is written by Save()). Problems are
    // appended to `warnings`; the store always ends up with a usable Settings value.
    void Load(const std::filesystem::path& file, std::vector<std::string>& warnings);
    // Parses JSON text (used by Load and by tests).
    void LoadFromText(const std::string& text, std::vector<std::string>& warnings);

    bool Save(std::string* error = nullptr) const;
    std::string ToText() const;

    const Settings& Get() const { return settings_; }

    // Changes one section and notifies its listeners.
    void Update(SettingsSection section, const std::function<void(Settings&)>& change);
    // Replaces everything (e.g. after the file changed on disk) and notifies every section whose
    // values changed.
    void Replace(const Settings& next);

    int Subscribe(SettingsSection section, Listener listener);
    void Unsubscribe(int id);

    const std::filesystem::path& File() const { return file_; }

private:
    struct Entry {
        int id;
        SettingsSection section;
        Listener listener;
    };
    void Notify(SettingsSection section);

    Settings settings_;
    std::filesystem::path file_;
    std::string original_text_;  // last loaded file, so unknown keys survive Save()
    std::vector<Entry> listeners_;
    int next_id_ = 1;
};

const char* ToString(ScaleFilter v);
const char* ToString(WindowMode v);
const char* ToString(FocusLossAudio v);

}  // namespace ac04::platform
