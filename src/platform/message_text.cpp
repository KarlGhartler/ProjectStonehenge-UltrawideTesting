#include "platform/message_text.h"

namespace ac04::platform {

std::string FormatMessageText(const MessageText& m) {
    std::string out = m.headline;
    if (!m.explanation.empty()) out += (out.empty() ? "" : "\n\n") + m.explanation;
    if (!m.steps.empty()) {
        out += "\n\nWhat to do:";
        for (std::size_t i = 0; i < m.steps.size(); ++i) out += "\n    " + std::to_string(i + 1) + ".  " + m.steps[i];
    }
    for (const auto& [label, value] : m.details) out += "\n\n" + label + ":\n    " + value;
    return out;
}

}  // namespace ac04::platform
