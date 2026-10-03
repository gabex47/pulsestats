#pragma once

#include "pulse/stats.hpp"
#include "pulse/terminal.hpp"

#include <string>

namespace pulse {

std::string render_dashboard(const SystemInfo& info, const SystemStats& stats,
                             TerminalSize size, double interval_seconds);
std::string render_stats(const SystemStats& stats);
std::string help_text();

}  // namespace pulse
