#pragma once

#include <iosfwd>
#include <string_view>

namespace pulse {

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
