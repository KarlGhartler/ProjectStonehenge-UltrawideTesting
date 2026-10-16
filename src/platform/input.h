// Input backend (approved baseline: rebinding). Host keyboard and SDL3 gamepads are mapped onto
// the exact DualShock 2 data that scePadRead returns; the game's own ConvertPadInput is unchanged.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "platform/settings.h"

union SDL_Event;
struct SDL_Gamepad;

namespace ac04::platform {

// One controller in PS2 terms.
struct PadState {
    bool connected = false;
    // Digital buttons, active-low as in the scePadRead buffer bytes 2–3:
    //   byte 2: select L3 R3 start up right down left (bits 0..7)
    //   byte 3: L2 R2 L1 R1 triangle circle cross square (bits 0..7)
    std::uint16_t buttons = 0xFFFF;
    std::uint8_t rx = 0x7F, ry = 0x7F, lx = 0x7F, ly = 0x7F;  // neutral as in PCSX2
    // Pressure (0 = released, 255 = full) in scePadRead order:
    // right left up down triangle circle cross square L1 R1 L2 R2.
    std::array<std::uint8_t, 12> pressure{};
};

class InputBackend {
public:
    InputBackend();
    ~InputBackend();

    // Logs every joystick SDL sees (name, gamepad mapping yes/no) and opens the gamepads (call once after SDL_Init).
    void Start();
    void ApplySettings(const InputSettings& input);
    void HandleEvent(const SDL_Event& event);  // gamepad hot-plug
    // Samples keyboard and gamepads; keyboard is ignored while `keyboard_captured` (overlay focus).
    void Update(bool keyboard_captured);

    const PadState& Port(int port) const { return ports_[port]; }

private:
    struct Source {
        enum Kind { Key, PadButton, Axis } kind;
        int code;         // SDL_Scancode / SDL_GamepadButton / SDL_GamepadAxis
        int direction;    // axis: +1 or -1
    };
    struct Binding {
        std::vector<Source> sources;
    };
    void OpenGamepads();
    float Sample(const Binding& binding, SDL_Gamepad* pad, bool use_keyboard) const;

    std::array<PadState, 2> ports_{};
    std::array<SDL_Gamepad*, 2> pads_{};
    std::array<Binding, 24> bindings_{};  // indexed by Control
};

}  // namespace ac04::platform
