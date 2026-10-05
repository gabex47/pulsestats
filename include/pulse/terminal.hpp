#pragma once

#include <cstddef>
#include <iosfwd>
#include <string_view>

namespace pulse {

struct TerminalSize {
    std::size_t columns;
    std::size_t rows;
};

struct TerminalCapabilities {
    bool interactive;
    bool color;
    bool unicode;
};

TerminalSize terminal_size();
TerminalCapabilities terminal_capabilities(bool no_color_option);

class TerminalScreen {
public:
    explicit TerminalScreen(std::ostream& output);
    ~TerminalScreen();

    TerminalScreen(const TerminalScreen&) = delete;
    TerminalScreen& operator=(const TerminalScreen&) = delete;

    void draw(std::string_view frame);

private:
    std::ostream& output_;
};

}  // namespace pulse
