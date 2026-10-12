#include "platform/window.h"

#include <SDL3/SDL.h>

#include "platform/log.h"

namespace ac04::platform {

Window::~Window() { Destroy(); }

bool Window::Create(const std::string& title, const PresentationSettings& presentation,
                    std::string& error) {
    const SDL_WindowFlags flags = SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN;
    window_ = SDL_CreateWindow(title.c_str(), presentation.window_width, presentation.window_height,
                               flags);
    if (!window_) {
        error = std::string("SDL_CreateWindow failed: ") + SDL_GetError();
        return false;
    }
    SDL_SetWindowMinimumSize(window_, 320, 240);
    ApplyMode(presentation);
    SDL_ShowWindow(window_);
    // Text input delivers typed characters (layout-aware), needed for the overlay toggle key.
    SDL_StartTextInput(window_);
    return true;
}

void Window::Destroy() {
    if (window_) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }
}

void Window::ApplyMode(const PresentationSettings& presentation) {
    if (!window_) return;
    // Only touch the window when its mode or configured size changed, so other presentation
    // changes (filter, v-sync, cap) never undo a size the user set by dragging.
    const bool mode_changed = !applied_ || presentation.window_mode != mode_;
    const bool size_changed = presentation.window_width != applied_width_ ||
                              presentation.window_height != applied_height_;
    if (presentation.window_mode == WindowMode::Borderless) {
        if (mode_changed) {
            // Null display mode = borderless "fullscreen desktop" window, no video mode change.
            SDL_SetWindowFullscreenMode(window_, nullptr);
            SDL_SetWindowFullscreen(window_, true);
        }
    } else {
        if (mode_changed) SDL_SetWindowFullscreen(window_, false);
        if (mode_changed || size_changed) {
            SDL_SetWindowSize(window_, presentation.window_width, presentation.window_height);
        }
    }
    mode_ = presentation.window_mode;
    applied_width_ = presentation.window_width;
    applied_height_ = presentation.window_height;
    applied_ = true;
    SDL_SyncWindow(window_);
}

void Window::SetTitle(const std::string& title) {
    if (window_) SDL_SetWindowTitle(window_, title.c_str());
}

void Window::PixelSize(int& width, int& height) const {
    width = height = 0;
    if (window_) SDL_GetWindowSizeInPixels(window_, &width, &height);
}

void Window::WindowedSize(int& width, int& height) const {
    width = height = 0;
    if (window_) SDL_GetWindowSize(window_, &width, &height);
}

void ShowErrorBox(const std::string& title, const std::string& text, const Window* window) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title.c_str(), text.c_str(),
                             window ? window->Handle() : nullptr);
}

void ShowInfoBox(const std::string& title, const std::string& text, const Window* window) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, title.c_str(), text.c_str(),
                             window ? window->Handle() : nullptr);
}

}  // namespace ac04::platform
