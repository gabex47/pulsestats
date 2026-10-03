#include "pulse/terminal.hpp"

#include <ostream>

namespace pulse {

TerminalScreen::TerminalScreen(std::ostream& output) : output_(output) {
    // The alternate screen preserves the shell's scrollback and cursor position.
    output_ << "\x1b[?1049h\x1b[?25l";
    output_.flush();
}

TerminalScreen::~TerminalScreen() {
    // A transient stream error must not suppress the restoration attempt.
    if (!output_) {
        output_.clear();
    }
    output_ << "\x1b[?25h\x1b[?1049l";
    output_.flush();
}

void TerminalScreen::draw(const std::string_view frame) {
    output_ << "\x1b[H";
    std::string_view remaining = frame;
    while (!remaining.empty()) {
        const auto end = remaining.find('\n');
        output_ << "\x1b[2K" << remaining.substr(0, end);
        if (end == std::string_view::npos) {
            break;
        }
        remaining.remove_prefix(end + 1);
        if (!remaining.empty()) {
            output_ << "\x1b[E";
        }
    }
    output_ << "\x1b[J";
    output_.flush();
}

}  // namespace pulse
