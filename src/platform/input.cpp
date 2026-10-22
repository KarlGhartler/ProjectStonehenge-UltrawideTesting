#include "platform/input.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>

#include "platform/log.h"

namespace ac04::platform {
namespace {

// PS2 controls in a fixed order: the first 16 are the digital buttons in scePadRead bit order
// (bytes 2 and 3), the last 8 are the analog stick half-axes.
constexpr const char* kControlNames[24] = {
    "select", "l3", "r3", "start", "up", "right", "down", "left",
    "l2", "r2", "l1", "r1", "triangle", "circle", "cross", "square",
    "lx-", "lx+", "ly-", "ly+", "rx-", "rx+", "ry-", "ry+"};

// Pressure slot of each digital button (-1 = no pressure value): right left up down triangle
// circle cross square L1 R1 L2 R2.
constexpr int kPressureSlot[16] = {-1, -1, -1, -1, 2, 0, 3, 1, 10, 11, 8, 9, 4, 5, 6, 7};

// Host input names used in settings.json. Our own table (not SDL's mapping-string names), so the
// names stay stable and describe the button position, not a vendor label.
struct NamedButton {
    const char* name;
    SDL_GamepadButton button;
};
constexpr NamedButton kPadButtons[] = {
    {"south", SDL_GAMEPAD_BUTTON_SOUTH},          {"east", SDL_GAMEPAD_BUTTON_EAST},
    {"west", SDL_GAMEPAD_BUTTON_WEST},            {"north", SDL_GAMEPAD_BUTTON_NORTH},
    {"back", SDL_GAMEPAD_BUTTON_BACK},            {"guide", SDL_GAMEPAD_BUTTON_GUIDE},
    {"start", SDL_GAMEPAD_BUTTON_START},          {"leftstick", SDL_GAMEPAD_BUTTON_LEFT_STICK},
    {"rightstick", SDL_GAMEPAD_BUTTON_RIGHT_STICK}, {"leftshoulder", SDL_GAMEPAD_BUTTON_LEFT_SHOULDER},
    {"rightshoulder", SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER}, {"dpup", SDL_GAMEPAD_BUTTON_DPAD_UP},
    {"dpdown", SDL_GAMEPAD_BUTTON_DPAD_DOWN},     {"dpleft", SDL_GAMEPAD_BUTTON_DPAD_LEFT},
    {"dpright", SDL_GAMEPAD_BUTTON_DPAD_RIGHT}};

struct NamedAxis {
    const char* name;
    SDL_GamepadAxis axis;
};
constexpr NamedAxis kPadAxes[] = {
    {"leftx", SDL_GAMEPAD_AXIS_LEFTX},   {"lefty", SDL_GAMEPAD_AXIS_LEFTY},
    {"rightx", SDL_GAMEPAD_AXIS_RIGHTX}, {"righty", SDL_GAMEPAD_AXIS_RIGHTY},
    {"lefttrigger", SDL_GAMEPAD_AXIS_LEFT_TRIGGER}, {"righttrigger", SDL_GAMEPAD_AXIS_RIGHT_TRIGGER}};

// Neutral 0x7F like PCSX2's DualShock 2 (run 06: a centred stick reads 0x7F); full deflection
// reaches 0x00 and 0xFF.
std::uint8_t StickByte(float negative, float positive) {
    const float d = positive - negative;
    const float v = 127.0f + (d >= 0.0f ? d * 128.0f : d * 127.0f);
    return static_cast<std::uint8_t>(std::clamp(static_cast<int>(std::lround(v)), 0, 255));
}

}  // namespace

InputBackend::InputBackend() = default;

InputBackend::~InputBackend() {
    for (auto& pad : pads_) {
        if (pad) SDL_CloseGamepad(pad);
        pad = nullptr;
    }
}

void InputBackend::ApplySettings(const InputSettings& input) {
    for (int c = 0; c < 24; ++c) {
        Binding& b = bindings_[c];
        b.sources.clear();
        auto it = input.bindings.find(kControlNames[c]);
        if (it == input.bindings.end()) continue;
        for (const std::string& text : it->second) {
            const auto colon = text.find(':');
            const std::string kind = text.substr(0, colon);
            std::string name = colon == std::string::npos ? "" : text.substr(colon + 1);
            Source s{Source::Key, 0, +1};
            bool ok = false;
            if (kind == "key") {
                const SDL_Scancode sc = SDL_GetScancodeFromName(name.c_str());
                ok = sc != SDL_SCANCODE_UNKNOWN;
                s = {Source::Key, sc, +1};
            } else if (kind == "pad") {
                for (const auto& e : kPadButtons) {
                    if (name == e.name) {
                        s = {Source::PadButton, e.button, +1};
                        ok = true;
                    }
                }
            } else if (kind == "axis" && !name.empty()) {
                const int dir = name.back() == '-' ? -1 : +1;
                if (name.back() == '-' || name.back() == '+') name.pop_back();
                for (const auto& e : kPadAxes) {
                    if (name == e.name) {
                        s = {Source::Axis, e.axis, dir};
                        ok = true;
                    }
                }
            }
            if (ok) {
                b.sources.push_back(s);
            } else {
                AC04_LOG_WARN("input", "binding '%s' for %s not recognised, ignored", text.c_str(),
                              kControlNames[c]);
            }
        }
    }
}

void InputBackend::Start() {
    int count = 0;
    SDL_JoystickID* ids = SDL_GetJoysticks(&count);
    if (count == 0) AC04_LOG_INFO("input", "no joystick or gamepad connected (keyboard on port 1)");
    for (int i = 0; i < count; ++i) {
        const char* name = SDL_GetJoystickNameForID(ids[i]);
        const bool gamepad = SDL_IsGamepad(ids[i]);
        AC04_LOG_INFO("input", "device %d: '%s' (%s)", i, name ? name : "?",
                      gamepad ? "gamepad" : "no SDL gamepad mapping: not used");
    }
    SDL_free(ids);
    OpenGamepads();
}

void InputBackend::OpenGamepads() {
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    for (int i = 0; i < count && i < 2; ++i) {
        if (!pads_[i]) {
            pads_[i] = SDL_OpenGamepad(ids[i]);
            if (pads_[i]) AC04_LOG_INFO("input", "port %d: gamepad '%s'", i + 1, SDL_GetGamepadName(pads_[i]));
        }
    }
    SDL_free(ids);
}

void InputBackend::HandleEvent(const SDL_Event& event) {
    if (event.type == SDL_EVENT_GAMEPAD_ADDED) {
        OpenGamepads();
    } else if (event.type == SDL_EVENT_GAMEPAD_REMOVED) {
        for (int i = 0; i < 2; ++i) {
            if (pads_[i] && SDL_GetGamepadID(pads_[i]) == event.gdevice.which) {
                AC04_LOG_INFO("input", "port %d: gamepad removed", i + 1);
                SDL_CloseGamepad(pads_[i]);
                pads_[i] = nullptr;
                rumble_[i] = {};
            }
        }
    }
}

float InputBackend::Sample(const Binding& binding, SDL_Gamepad* pad, bool use_keyboard) const {
    float value = 0.0f;
    const bool* keys = use_keyboard ? SDL_GetKeyboardState(nullptr) : nullptr;
    for (const Source& s : binding.sources) {
        float v = 0.0f;
        switch (s.kind) {
            case Source::Key:
                v = (keys && keys[s.code]) ? 1.0f : 0.0f;
                break;
            case Source::PadButton:
                v = (pad && SDL_GetGamepadButton(pad, static_cast<SDL_GamepadButton>(s.code))) ? 1.0f : 0.0f;
                break;
            case Source::Axis:
                if (pad) {
                    const float a = SDL_GetGamepadAxis(pad, static_cast<SDL_GamepadAxis>(s.code)) / 32767.0f;
                    v = std::clamp(a * static_cast<float>(s.direction), 0.0f, 1.0f);
                }
                break;
        }
        value = std::max(value, v);
    }
    return value;
}

void InputBackend::SetVibration(int port, bool small_on, std::uint8_t large) {
    if (port < 0 || port > 1) return;
    SDL_Gamepad* pad = pads_[port];
    Rumble& r = rumble_[port];
    if (!pad) {
        r = {};
        return;
    }
    // The large motor is the low-frequency one; the small motor has no strength, only on/off.
    const std::uint16_t low = static_cast<std::uint16_t>(large * 257u);
    const std::uint16_t high = small_on ? 0xFFFF : 0;
    // SDL stops a rumble after its duration: running motors are sent again every 500 ms with a
    // 1 s duration, so a stalled frame loop never leaves the pad shaking for long.
    constexpr std::uint64_t kRefreshMs = 500;
    constexpr std::uint32_t kDurationMs = 1000;
    const std::uint64_t now = SDL_GetTicks();
    const bool changed = low != r.low || high != r.high;
    const bool running = low != 0 || high != 0;
    if (!changed && !(running && now - r.sent_ms >= kRefreshMs)) return;
    if (!changed && !running) return;
    if (!SDL_RumbleGamepad(pad, low, high, running ? kDurationMs : 0)) {
        log::WriteOnce("input:rumble", log::Level::Info, "input", "port %d: the gamepad has no rumble (%s)", port + 1,
                       SDL_GetError());
    } else if (running) {
        log::WriteOnce("input:rumble-on", log::Level::Info, "input", "port %d: vibration started", port + 1);
    }
    r.low = low;
    r.high = high;
    r.sent_ms = now;
}

void InputBackend::Update(bool keyboard_captured) {
    for (int port = 0; port < 2; ++port) {
        SDL_Gamepad* pad = pads_[port];
        const bool use_keyboard = port == 0 && !keyboard_captured;
        PadState& st = ports_[port];
        st.connected = port == 0 || pad != nullptr;
        st.buttons = 0xFFFF;
        st.pressure.fill(0);
        for (int c = 0; c < 16; ++c) {
            const float v = Sample(bindings_[c], pad, use_keyboard);
            // A digital press needs more than half travel (triggers, analog bindings).
            if (v > 0.5f) st.buttons &= static_cast<std::uint16_t>(~(1u << c));
            if (kPressureSlot[c] >= 0)
                st.pressure[kPressureSlot[c]] = static_cast<std::uint8_t>(std::lround(v * 255.0f));
        }
        float axes[8];
        for (int i = 0; i < 8; ++i) axes[i] = Sample(bindings_[16 + i], pad, use_keyboard);
        st.lx = StickByte(axes[0], axes[1]);
        st.ly = StickByte(axes[2], axes[3]);
        st.rx = StickByte(axes[4], axes[5]);
        st.ry = StickByte(axes[6], axes[7]);
    }
}

}  // namespace ac04::platform
