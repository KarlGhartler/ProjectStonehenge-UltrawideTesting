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
    SDL_SyncWindow(window_);
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(window_, &w, &h);
    AC04_LOG_INFO("window", "%s, %dx%d pixels", ToString(presentation.window_mode), w, h);
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
    if (presentation.window_mode == WindowMode::Fullscreen) {
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
    if ((mode_changed || size_changed) && (SDL_GetWindowFlags(window_) & SDL_WINDOW_HIDDEN) == 0) {
        int w = 0, h = 0;
        SDL_GetWindowSizeInPixels(window_, &w, &h);
        AC04_LOG_INFO("window", "%s, %dx%d pixels", ToString(presentation.window_mode), w, h);
    }
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

void ShowErrorBox(const std::string& title, const MessageText& text, const Window* window) {
    ShowErrorBox(title, FormatMessageText(text), window);
}

int ShowChoiceBox(const std::string& title, const MessageText& text, const std::vector<std::string>& buttons,
                  bool error, const Window* window) {
    const std::string body = FormatMessageText(text);
    std::vector<SDL_MessageBoxButtonData> data(buttons.size());
    for (std::size_t i = 0; i < buttons.size(); ++i) {
        data[i].buttonID = static_cast<int>(i);
        data[i].text = buttons[i].c_str();
        data[i].flags = 0;
        if (i == 0) data[i].flags |= SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT;
        if (i + 1 == buttons.size()) data[i].flags |= SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT;
    }
    SDL_MessageBoxData box{};
    box.flags = (error ? SDL_MESSAGEBOX_ERROR : SDL_MESSAGEBOX_INFORMATION) | SDL_MESSAGEBOX_BUTTONS_LEFT_TO_RIGHT;
    box.window = window ? window->Handle() : nullptr;
    box.title = title.c_str();
    box.message = body.c_str();
    box.numbuttons = static_cast<int>(data.size());
    box.buttons = data.data();
    int pressed = -1;
    if (!SDL_ShowMessageBox(&box, &pressed)) return -1;
    return pressed;
}

std::string ChooseFile(const Window* window, const std::string& filter_name, const std::string& extension) {
    struct State {
        bool done = false;
        std::string path;
    } state;
    const SDL_DialogFileFilter filter{filter_name.c_str(), extension.c_str()};
    SDL_ShowOpenFileDialog(
        [](void* user, const char* const* files, int) {
            auto* s = static_cast<State*>(user);
            if (files != nullptr && files[0] != nullptr) s->path = files[0];
            s->done = true;
        },
        &state, window ? window->Handle() : nullptr, &filter, 1, nullptr, false);
    // The dialog reports through the callback; on some systems only while events are pumped.
    while (!state.done) {
        SDL_PumpEvents();
        SDL_Delay(10);
    }
    return state.path;
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
