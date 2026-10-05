#include "pulse/terminal.hpp"

#include <sys/ioctl.h>
#include <unistd.h>

#include <clocale>
#include <cstdlib>
#include <cstring>
#include <langinfo.h>
#include <ostream>

namespace pulse {

TerminalSize terminal_size() {
    winsize dimensions{};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &dimensions) == 0 &&
        dimensions.ws_col > 0 && dimensions.ws_row > 0) {
        return {dimensions.ws_col, dimensions.ws_row};
    }
    return {80, 24};
}

TerminalCapabilities terminal_capabilities(const bool no_color_option) {
    const char* term = std::getenv("TERM");
    const bool legacy = term != nullptr &&
        (std::strcmp(term, "unknown") == 0 || std::strcmp(term, "vt100") == 0 ||
         std::strcmp(term, "vt102") == 0 || std::strcmp(term, "vt220") == 0);
    const bool interactive = isatty(STDOUT_FILENO) != 0 && term != nullptr &&
                             *term != '\0' && std::strcmp(term, "dumb") != 0 && !legacy;
    const char* no_color = std::getenv("NO_COLOR");
    bool unicode = false;
    if (interactive) {
        std::setlocale(LC_CTYPE, "");
        const char* codeset = nl_langinfo(CODESET);
        unicode = codeset != nullptr &&
                  (std::strstr(codeset, "UTF-8") != nullptr ||
                   std::strstr(codeset, "utf8") != nullptr);
    }
    return {interactive, interactive && !no_color_option &&
                             (no_color == nullptr || *no_color == '\0'), unicode};
}

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
    output_ << "\x1b[0m\x1b[?25h\x1b[?1049l";
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
