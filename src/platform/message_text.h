// Text of the port's message boxes in one layout (platform/window.h shows them): a one-line
// headline, a short explanation, numbered steps under "What to do:" and labelled details (a path or a
// reason, each value on its own indented line), separated by blank lines. Empty parts are left out.
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace ac04::platform {

struct MessageText {
    std::string headline;
    std::string explanation;
    std::vector<std::string> steps;
    std::vector<std::pair<std::string, std::string>> details;  // label, value
};

// The plain text of `m` in the layout above (also used for the log).
std::string FormatMessageText(const MessageText& m);

}  // namespace ac04::platform
