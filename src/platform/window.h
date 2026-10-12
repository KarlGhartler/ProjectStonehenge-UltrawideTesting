// Main window (SDL3): windowed or borderless (ADR-006 / approved baseline).
#pragma once

#include <string>

#include "platform/settings.h"

struct SDL_Window;

namespace ac04::platform {

class Window {
public:
    ~Window();

    bool Create(const std::string& title, const PresentationSettings& presentation, std::string& error);
    void Destroy();

    void ApplyMode(const PresentationSettings& presentation);
    void SetTitle(const std::string& title);

    // Drawable size in pixels (may differ from the window size on high-DPI screens).
    void PixelSize(int& width, int& height) const;
    // Current windowed client size (for saving it in settings).
    void WindowedSize(int& width, int& height) const;

    SDL_Window* Handle() const { return window_; }

private:
    SDL_Window* window_ = nullptr;
    WindowMode mode_ = WindowMode::Windowed;
    bool applied_ = false;
    int applied_width_ = 0, applied_height_ = 0;
};

// Modal message box with an OK button. `window` may be null. Used for startup errors.
void ShowErrorBox(const std::string& title, const std::string& text, const Window* window = nullptr);
void ShowInfoBox(const std::string& title, const std::string& text, const Window* window = nullptr);

}  // namespace ac04::platform
