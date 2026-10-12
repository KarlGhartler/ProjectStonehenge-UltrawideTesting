#include "platform/render/output_layout.h"

namespace ac04::render {

Rect FitAspect(int window_width, int window_height, int aspect_w, int aspect_h) {
    Rect r;
    if (window_width <= 0 || window_height <= 0 || aspect_w <= 0 || aspect_h <= 0) return r;
    // Compare window_w/window_h with aspect_w/aspect_h in integers.
    const long long lhs = static_cast<long long>(window_width) * aspect_h;
    const long long rhs = static_cast<long long>(window_height) * aspect_w;
    if (lhs > rhs) {
        // Window is wider: full height, pillarbox.
        r.height = window_height;
        r.width = static_cast<int>(static_cast<long long>(window_height) * aspect_w / aspect_h);
    } else {
        // Window is taller (or equal): full width, letterbox.
        r.width = window_width;
        r.height = static_cast<int>(static_cast<long long>(window_width) * aspect_h / aspect_w);
    }
    r.x = (window_width - r.width) / 2;
    r.y = (window_height - r.height) / 2;
    return r;
}

}  // namespace ac04::render
