#include "platform/settings.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <sstream>
#include <system_error>

namespace ac04::platform {
namespace {

using json = nlohmann::ordered_json;

constexpr int kFrameCaps[] = {60, 120, 144, 240, 360};

template <typename T>
struct EnumName {
    T value;
    const char* name;
};

constexpr EnumName<ScaleFilter> kFilters[] = {{ScaleFilter::Sharp, "sharp"},
                                              {ScaleFilter::Smooth, "smooth"}};
constexpr EnumName<WindowMode> kWindowModes[] = {{WindowMode::Windowed, "windowed"},
                                                 {WindowMode::Borderless, "borderless"}};
constexpr EnumName<FocusLossAudio> kFocusLoss[] = {{FocusLossAudio::KeepPlaying, "keep_playing"},
                                                   {FocusLossAudio::Mute, "mute"}};

template <typename T, size_t N>
const char* NameOf(const EnumName<T> (&table)[N], T value) {
    for (const auto& e : table)
        if (e.value == value) return e.name;
    return "?";
}

// Reads obj[key] into `out` when present and valid; otherwise keeps the default and warns.
class Reader {
public:
    Reader(const json& root, std::vector<std::string>& warnings) : root_(root), warnings_(warnings) {}

    const json* Section(const char* name) {
        auto it = root_.find(name);
        if (it == root_.end()) return nullptr;
        if (!it->is_object()) {
            Warn(std::string(name) + ": not an object, defaults used");
            return nullptr;
        }
        return &*it;
    }

    void Bool(const json* sec, const char* sec_name, const char* key, bool& out) {
        if (const json* v = Find(sec, key)) {
            if (v->is_boolean()) out = v->get<bool>();
            else Invalid(sec_name, key);
        }
    }

    void Int(const json* sec, const char* sec_name, const char* key, int& out, int lo, int hi) {
        if (const json* v = Find(sec, key)) {
            if (v->is_number_integer() && v->get<long long>() >= lo && v->get<long long>() <= hi)
                out = v->get<int>();
            else Invalid(sec_name, key);
        }
    }

    void String(const json* sec, const char* sec_name, const char* key, std::string& out) {
        if (const json* v = Find(sec, key)) {
            if (v->is_string()) out = v->get<std::string>();
            else Invalid(sec_name, key);
        }
    }

    template <typename T, size_t N>
    void Enum(const json* sec, const char* sec_name, const char* key, const EnumName<T> (&table)[N],
              T& out) {
        if (const json* v = Find(sec, key)) {
            if (v->is_string()) {
                const std::string s = v->get<std::string>();
                for (const auto& e : table) {
                    if (s == e.name) {
                        out = e.value;
                        return;
                    }
                }
            }
            Invalid(sec_name, key);
        }
    }

    void Invalid(const char* sec_name, const char* key) {
        Warn(std::string(sec_name) + "." + key + ": invalid value, default used");
    }
    void Warn(std::string text) { warnings_.push_back(std::move(text)); }

private:
    static const json* Find(const json* sec, const char* key) {
        if (!sec) return nullptr;
        auto it = sec->find(key);
        return it == sec->end() ? nullptr : &*it;
    }

    const json& root_;
    std::vector<std::string>& warnings_;
};

bool operator==(const InputSettings& a, const InputSettings& b) { return a.bindings == b.bindings; }

}  // namespace

const char* ToString(ScaleFilter v) { return NameOf(kFilters, v); }
const char* ToString(WindowMode v) { return NameOf(kWindowModes, v); }
const char* ToString(FocusLossAudio v) { return NameOf(kFocusLoss, v); }

bool IsValidFrameCap(int fps) {
    for (int c : kFrameCaps)
        if (c == fps) return true;
    return false;
}

InputSettings DefaultInputSettings() {
    // Keyboard and SDL3 gamepad defaults. Left stick on WASD, right stick on IJKL, d-pad on the
    // arrow keys. The game receives the exact PS2 pad bytes built from these (input backend).
    InputSettings s;
    auto& b = s.bindings;
    b["cross"] = {"key:Z", "pad:south"};
    b["circle"] = {"key:X", "pad:east"};
    b["square"] = {"key:C", "pad:west"};
    b["triangle"] = {"key:V", "pad:north"};
    b["l1"] = {"key:Q", "pad:leftshoulder"};
    b["r1"] = {"key:E", "pad:rightshoulder"};
    b["l2"] = {"key:1", "axis:lefttrigger+"};
    b["r2"] = {"key:3", "axis:righttrigger+"};
    b["l3"] = {"key:F", "pad:leftstick"};
    b["r3"] = {"key:H", "pad:rightstick"};
    b["start"] = {"key:Return", "pad:start"};
    b["select"] = {"key:Backspace", "pad:back"};
    b["up"] = {"key:Up", "pad:dpup"};
    b["down"] = {"key:Down", "pad:dpdown"};
    b["left"] = {"key:Left", "pad:dpleft"};
    b["right"] = {"key:Right", "pad:dpright"};
    b["lx-"] = {"key:A", "axis:leftx-"};
    b["lx+"] = {"key:D", "axis:leftx+"};
    b["ly-"] = {"key:W", "axis:lefty-"};
    b["ly+"] = {"key:S", "axis:lefty+"};
    b["rx-"] = {"key:J", "axis:rightx-"};
    b["rx+"] = {"key:L", "axis:rightx+"};
    b["ry-"] = {"key:I", "axis:righty-"};
    b["ry+"] = {"key:K", "axis:righty+"};
    return s;
}

SettingsStore::SettingsStore() {
    settings_.input = DefaultInputSettings();
}

void SettingsStore::Load(const std::filesystem::path& file, std::vector<std::string>& warnings) {
    file_ = file;
    std::error_code ec;
    if (!std::filesystem::exists(file, ec)) {
        original_text_.clear();
        settings_ = Settings{};
        settings_.input = DefaultInputSettings();
        return;
    }
    std::ifstream in(file, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    LoadFromText(ss.str(), warnings);
}

void SettingsStore::LoadFromText(const std::string& text, std::vector<std::string>& warnings) {
    Settings s;
    s.input = DefaultInputSettings();
    json root = json::parse(text, nullptr, /*allow_exceptions=*/false, /*ignore_comments=*/true);
    if (root.is_discarded() || !root.is_object()) {
        warnings.push_back("settings.json is not valid JSON; defaults used (the file is kept "
                           "until settings are saved)");
        original_text_.clear();
        settings_ = s;
        return;
    }
    original_text_ = text;
    Reader r(root, warnings);

    if (const json* p = r.Section("presentation")) {
        r.Enum(p, "presentation", "filter", kFilters, s.presentation.filter);
        r.Bool(p, "presentation", "vsync", s.presentation.vsync);
        int cap = s.presentation.frame_cap;
        r.Int(p, "presentation", "frame_cap", cap, 1, 100000);
        if (IsValidFrameCap(cap)) s.presentation.frame_cap = cap;
        else r.Invalid("presentation", "frame_cap");
        r.Enum(p, "presentation", "window_mode", kWindowModes, s.presentation.window_mode);
        r.Int(p, "presentation", "window_width", s.presentation.window_width, 320, 16384);
        r.Int(p, "presentation", "window_height", s.presentation.window_height, 240, 16384);
        int scale = s.presentation.internal_scale;
        r.Int(p, "presentation", "internal_scale", scale, 1, 16);
        if (scale != 1) {
            r.Warn("presentation.internal_scale: only 1 is supported for now; 1 used");
        }
    }
    if (const json* p = r.Section("paths")) {
        r.String(p, "paths", "iso", s.paths.iso);
    }
    if (const json* p = r.Section("audio")) {
        r.Enum(p, "audio", "on_focus_loss", kFocusLoss, s.audio.on_focus_loss);
    }
    if (const json* p = r.Section("input")) {
        auto it = p->find("bindings");
        if (it != p->end()) {
            if (!it->is_object()) {
                r.Invalid("input", "bindings");
            } else {
                for (auto& [control, value] : it->items()) {
                    if (!s.input.bindings.count(control)) {
                        r.Warn("input.bindings." + control + ": unknown PS2 control, ignored");
                        continue;
                    }
                    std::vector<std::string> list;
                    bool ok = value.is_array();
                    if (ok)
                        for (auto& e : value) {
                            if (!e.is_string()) ok = false;
                            else list.push_back(e.get<std::string>());
                        }
                    if (ok) s.input.bindings[control] = list;
                    else r.Warn("input.bindings." + control + ": invalid value, default used");
                }
            }
        }
    }
    if (const json* p = r.Section("debug")) {
        r.Bool(p, "debug", "overlay_enabled", s.debug.overlay_enabled);
        r.String(p, "debug", "overlay_toggle", s.debug.overlay_toggle);
        if (s.debug.overlay_toggle.empty()) {
            s.debug.overlay_toggle = DebugSettings{}.overlay_toggle;
            r.Invalid("debug", "overlay_toggle");
        }
    }
    settings_ = s;
}

std::string SettingsStore::ToText() const {
    json root = json::object();
    if (!original_text_.empty()) {
        json parsed = json::parse(original_text_, nullptr, false, true);
        if (parsed.is_object()) root = parsed;
    }
    const Settings& s = settings_;
    auto& p = root["presentation"];
    if (!p.is_object()) p = json::object();
    p["filter"] = ToString(s.presentation.filter);
    p["vsync"] = s.presentation.vsync;
    p["frame_cap"] = s.presentation.frame_cap;
    p["window_mode"] = ToString(s.presentation.window_mode);
    p["window_width"] = s.presentation.window_width;
    p["window_height"] = s.presentation.window_height;
    p["internal_scale"] = s.presentation.internal_scale;
    auto& pa = root["paths"];
    if (!pa.is_object()) pa = json::object();
    pa["iso"] = s.paths.iso;
    auto& a = root["audio"];
    if (!a.is_object()) a = json::object();
    a["on_focus_loss"] = ToString(s.audio.on_focus_loss);
    auto& in = root["input"];
    if (!in.is_object()) in = json::object();
    json bindings = json::object();
    for (const auto& [control, list] : s.input.bindings) bindings[control] = list;
    in["bindings"] = bindings;
    auto& d = root["debug"];
    if (!d.is_object()) d = json::object();
    d["overlay_enabled"] = s.debug.overlay_enabled;
    d["overlay_toggle"] = s.debug.overlay_toggle;
    return root.dump(2) + "\n";
}

bool SettingsStore::Save(std::string* error) const {
    if (file_.empty()) {
        if (error) *error = "no settings file path";
        return false;
    }
    // Write to a temporary file, then replace, so a crash never leaves a half-written file.
    std::filesystem::path tmp = file_;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            if (error) *error = "cannot write " + tmp.u8string();
            return false;
        }
        out << ToText();
        if (!out) {
            if (error) *error = "write failed: " + tmp.u8string();
            return false;
        }
    }
    std::error_code ec;
    std::filesystem::rename(tmp, file_, ec);
    if (ec) {
        if (error) *error = "cannot replace " + file_.u8string() + ": " + ec.message();
        return false;
    }
    return true;
}

void SettingsStore::Update(SettingsSection section, const std::function<void(Settings&)>& change) {
    change(settings_);
    Notify(section);
}

void SettingsStore::Replace(const Settings& next) {
    const Settings prev = settings_;
    settings_ = next;
    const auto& a = prev.presentation;
    const auto& b = next.presentation;
    if (a.filter != b.filter || a.vsync != b.vsync || a.frame_cap != b.frame_cap ||
        a.window_mode != b.window_mode || a.window_width != b.window_width ||
        a.window_height != b.window_height || a.internal_scale != b.internal_scale)
        Notify(SettingsSection::Presentation);
    if (prev.paths.iso != next.paths.iso) Notify(SettingsSection::Paths);
    if (prev.audio.on_focus_loss != next.audio.on_focus_loss) Notify(SettingsSection::Audio);
    if (!(prev.input == next.input)) Notify(SettingsSection::Input);
    if (prev.debug.overlay_enabled != next.debug.overlay_enabled ||
        prev.debug.overlay_toggle != next.debug.overlay_toggle)
        Notify(SettingsSection::Debug);
}

int SettingsStore::Subscribe(SettingsSection section, Listener listener) {
    listeners_.push_back({next_id_, section, std::move(listener)});
    return next_id_++;
}

void SettingsStore::Unsubscribe(int id) {
    for (auto it = listeners_.begin(); it != listeners_.end(); ++it) {
        if (it->id == id) {
            listeners_.erase(it);
            return;
        }
    }
}

void SettingsStore::Notify(SettingsSection section) {
    for (const auto& e : listeners_)
        if (e.section == section) e.listener(settings_);
}

}  // namespace ac04::platform
