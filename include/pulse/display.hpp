#pragma once

#include "pulse/stats.hpp"
#include "pulse/terminal.hpp"

#include <string>

namespace pulse {

struct RenderOptions {
    bool color = false;
    bool unicode = true;
    bool live = true;
};

std::string render_dashboard(const SystemInfo& info, const SystemStats& stats,
                             TerminalSize size, double interval_seconds,
                             RenderOptions options = {});
std::string render_stats(const SystemStats& stats);
std::string help_text();

}  // namespace pulse
