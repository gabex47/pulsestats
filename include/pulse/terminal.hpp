#pragma once

#include <cstddef>
#include <iosfwd>
#include <string_view>

namespace pulse {

struct TerminalSize {
    std::size_t columns;
    std::size_t rows;
};

TerminalSize terminal_size();

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
